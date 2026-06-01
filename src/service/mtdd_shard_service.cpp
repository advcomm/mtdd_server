#include "service/mtdd_shard_service.h"

#include "codec/arrow_ipc_encoder.h"
#include "codec/flex_meta.h"
#include "logging.h"

namespace mtdd::service {
namespace {

std::string ResolveDbName(const mtdd::ConnectRequest& request) {
  if (!request.dbname().empty()) {
    return request.dbname();
  }
  return request.database();
}

}  // namespace

MtddShardServiceImpl::MtddShardServiceImpl(ServerConfig config)
    : config_(std::move(config)), sessions_(config_.max_sessions) {}

pg::ConnectParams MtddShardServiceImpl::BuildConnectParams(const mtdd::ConnectRequest& request) const {
  pg::ConnectParams params;
  params.pg_host = config_.pg_host;
  params.port = request.port() > 0 ? request.port() : 5432;
  params.dbname = ResolveDbName(request);
  params.user = request.user();
  params.password = request.password();
  params.connect_timeout_sec = config_.pg_connect_timeout_sec;
  return params;
}

bool MtddShardServiceImpl::ValidateHostIndex(int32_t host_index, std::string* message) const {
  if (!config_.host_index.has_value()) {
    log::Warn("host_index_validation_skipped", "MTDD_HOST_INDEX unset (dev mode)");
    return true;
  }
  if (host_index != config_.host_index.value()) {
    if (message != nullptr) {
      *message = "host_index mismatch";
    }
    return false;
  }
  return true;
}

bool MtddShardServiceImpl::EnsureArrowFormat(const mtdd::QueryRequest& request, std::string* message) const {
  if (!request.name().empty()) {
    if (message != nullptr) {
      *message = "prepared statements are not supported";
    }
    return false;
  }
  if (!request.values_json().empty()) {
    if (message != nullptr) {
      *message = "values_json is not supported; use params";
    }
    return false;
  }
  const auto fmt = request.response_format();
  if (fmt == mtdd::RESPONSE_FORMAT_JSON) {
    if (message != nullptr) {
      *message = "JSON response format is not supported by this server";
    }
    return false;
  }
  return true;
}

grpc::Status MtddShardServiceImpl::Connect(
    grpc::ServerContext* /*context*/,
    const mtdd::ConnectRequest* request,
    mtdd::ConnectResponse* response) {
  log::ScopedTimer timer("connect");

  std::string message;
  if (!ValidateHostIndex(request->host_index(), &message)) {
    response->set_ok(false);
    response->set_message(message);
    return grpc::Status::OK;
  }

  const auto params = BuildConnectParams(*request);
  if (params.dbname.empty() || params.user.empty()) {
    response->set_ok(false);
    response->set_message("dbname and user are required");
    return grpc::Status::OK;
  }

  pool_.Configure(params, config_.pool_size);
  std::string error;
  if (!pool_.Connect(&error)) {
    response->set_ok(false);
    response->set_message(error);
    return grpc::Status::OK;
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    connect_params_ = params;
    executor_ = std::make_unique<pg::QueryExecutor>(&pool_, &sessions_, params);
  }

  response->set_ok(true);
  response->set_message("connected");
  return grpc::Status::OK;
}

void MtddShardServiceImpl::WriteErrorChunk(
    grpc::ServerWriter<mtdd::ResultChunk>* writer,
    const pg::PgErrorMeta& error) {
  mtdd::ResultChunk chunk;
  chunk.set_kind(mtdd::CHUNK_KIND_ERROR);
  codec::PgError flex_error;
  flex_error.sqlstate = error.sqlstate;
  flex_error.severity = error.severity.empty() ? "ERROR" : error.severity;
  flex_error.message = error.message;
  flex_error.detail = error.detail;
  flex_error.position = error.position;
  chunk.set_flatbuffer_meta(codec::EncodePgError(flex_error));
  writer->Write(chunk);
}

grpc::Status MtddShardServiceImpl::QueryStream(
    grpc::ServerContext* context,
    const mtdd::QueryRequest* request,
    grpc::ServerWriter<mtdd::ResultChunk>* writer) {
  log::ScopedTimer timer("query_stream");

  std::string message;
  if (!ValidateHostIndex(request->host_index(), &message)) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, message);
  }

  if (!EnsureArrowFormat(*request, &message)) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, message);
  }

  std::unique_ptr<pg::QueryExecutor> executor_copy;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!executor_ || !connect_params_.has_value()) {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Connect must be called first");
    }
    executor_copy = std::make_unique<pg::QueryExecutor>(&pool_, &sessions_, *connect_params_);
  }

  std::string exec_error;
  auto execution = executor_copy->Execute(*request, &exec_error);
  if (!exec_error.empty()) {
    pg::PgErrorMeta meta;
    meta.message = exec_error;
    WriteErrorChunk(writer, meta);
    return grpc::Status::OK;
  }

  PGresult* result = execution.result;
  if (result == nullptr) {
    executor_copy->FinishQuery(execution);
    pg::PgErrorMeta meta;
    meta.message = "query returned null";
    WriteErrorChunk(writer, meta);
    return grpc::Status::OK;
  }

  const ExecStatusType status = PQresultStatus(result);
  if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
    WriteErrorChunk(writer, pg::QueryExecutor::ExtractPgError(result));
    executor_copy->FinishQuery(execution);
    return grpc::Status::OK;
  }

  if (context->IsCancelled()) {
    executor_copy->FinishQuery(execution);
    return grpc::Status(grpc::StatusCode::CANCELLED, "cancelled");
  }

  try {
    auto encoded = codec::EncodePgResult(result, config_.arrow_batch_rows);
    executor_copy->FinishQuery(execution);

    if (!encoded.ipc_batches.empty()) {
      mtdd::ResultChunk schema_chunk;
      schema_chunk.set_kind(mtdd::CHUNK_KIND_SCHEMA);
      schema_chunk.set_flatbuffer_meta(codec::EncodeResultSchema(encoded.schema));
      schema_chunk.set_arrow_ipc(encoded.ipc_batches.front());
      if (!writer->Write(schema_chunk)) {
        return grpc::Status(grpc::StatusCode::CANCELLED, "client cancelled stream");
      }

      for (size_t i = 1; i < encoded.ipc_batches.size(); ++i) {
        mtdd::ResultChunk batch_chunk;
        batch_chunk.set_kind(mtdd::CHUNK_KIND_BATCH);
        batch_chunk.set_arrow_ipc(encoded.ipc_batches[i]);
        if (!writer->Write(batch_chunk)) {
          return grpc::Status(grpc::StatusCode::CANCELLED, "client cancelled stream");
        }
      }
    } else {
      mtdd::ResultChunk schema_chunk;
      schema_chunk.set_kind(mtdd::CHUNK_KIND_SCHEMA);
      schema_chunk.set_flatbuffer_meta(codec::EncodeResultSchema(encoded.schema));
      if (!writer->Write(schema_chunk)) {
        return grpc::Status(grpc::StatusCode::CANCELLED, "client cancelled stream");
      }
    }

    mtdd::ResultChunk trailer_chunk;
    trailer_chunk.set_kind(mtdd::CHUNK_KIND_TRAILER);
    trailer_chunk.set_flatbuffer_meta(codec::EncodeResultTrailer(encoded.trailer));
    writer->Write(trailer_chunk);
  } catch (const std::exception& ex) {
    executor_copy->FinishQuery(execution);
    pg::PgErrorMeta meta;
    meta.message = ex.what();
    WriteErrorChunk(writer, meta);
  }

  return grpc::Status::OK;
}

grpc::Status MtddShardServiceImpl::Query(
    grpc::ServerContext* /*context*/,
    const mtdd::QueryRequest* /*request*/,
    mtdd::QueryResponse* response) {
  response->set_ok(false);
  response->set_error("unary Query is not supported; set MTDD_GRPC_RESULT_FORMAT=arrow");
  return grpc::Status::OK;
}

grpc::Status MtddShardServiceImpl::Disconnect(
    grpc::ServerContext* /*context*/,
    const mtdd::DisconnectRequest* request,
    mtdd::DisconnectResponse* response) {
  std::string message;
  if (!ValidateHostIndex(request->host_index(), &message)) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, message);
  }

  sessions_.CloseAll();
  pool_.CloseAll();

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    connect_params_.reset();
    executor_.reset();
  }

  response->set_ok(true);
  return grpc::Status::OK;
}

}  // namespace mtdd::service
