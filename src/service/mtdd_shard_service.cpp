#include "service/mtdd_shard_service.h"

#include "codec/flex_meta.h"
#include "codec/query_meta.h"
#include "codec/raw_batch_encoder.h"
#include "logging.h"
#include "pg/streaming_query.h"

namespace mtdd::service {
namespace {

std::string ResolveDbName(const mtdd::ConnectRequest& request) {
  if (!request.dbname().empty()) {
    return request.dbname();
  }
  return request.database();
}

bool WriteResultChunk(
    grpc::ServerWriter<mtdd::ResultChunk>* writer,
    mtdd::ChunkKind kind,
    const std::string& flatbuffer_meta,
    const std::string& payload) {
  if (payload.empty() && flatbuffer_meta.empty()) {
    return true;
  }

  mtdd::ResultChunk chunk;
  chunk.set_kind(kind);
  if (!flatbuffer_meta.empty()) {
    chunk.set_flatbuffer_meta(flatbuffer_meta);
  }
  if (!payload.empty()) {
    chunk.set_payload(payload);
  }
  return writer->Write(chunk);
}

bool SendBatchChunk(
    grpc::ServerContext* context,
    grpc::ServerWriter<mtdd::ResultChunk>* writer,
    const codec::ResultSchema& schema_meta,
    bool* batch_sent,
    const std::string& payload) {
  if (payload.empty()) {
    return true;
  }
  if (context->IsCancelled()) {
    return false;
  }

  const mtdd::ChunkKind kind = *batch_sent ? mtdd::CHUNK_KIND_BATCH : mtdd::CHUNK_KIND_SCHEMA;
  const std::string meta = *batch_sent ? std::string() : codec::EncodeResultSchema(schema_meta);
  if (!WriteResultChunk(writer, kind, meta, payload)) {
    return false;
  }
  *batch_sent = true;
  return true;
}

}  // namespace

MtddShardServiceImpl::MtddShardServiceImpl(ServerConfig config, health::PgHealthMonitor* health_monitor)
    : config_(std::move(config)), health_monitor_(health_monitor), sessions_(config_.max_sessions) {}

pg::ConnectParams MtddShardServiceImpl::BuildConnectParams(const mtdd::ConnectRequest& request) const {
  pg::ConnectParams params;
  params.pg_host = config_.pg_host;
  params.port = request.port() > 0 ? request.port() : 5432;
  params.dbname = ResolveDbName(request);
  params.user = request.user();
  params.password = request.password();
  params.connect_timeout_sec = config_.pg_connect_timeout_sec;
  params.statement_timeout_ms = config_.statement_timeout_ms;
  return params;
}

bool MtddShardServiceImpl::ValidateHostIndex(int32_t host_index, std::string* message) const {
  if (!config_.host_index.has_value()) {
    if (config_.production_mode) {
      if (message != nullptr) {
        *message = "MTDD_HOST_INDEX is required in production";
      }
      return false;
    }
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

bool MtddShardServiceImpl::EnsureQueryRequest(const mtdd::QueryRequest& request, std::string* message) const {
  if (!request.name().empty()) {
    if (message != nullptr) {
      *message = "prepared statements are not supported";
    }
    return false;
  }
  if (request.result_format() != 1) {
    if (message != nullptr) {
      *message = "result_format must be 1 (libpq binary)";
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
    if (health_monitor_ != nullptr) {
      health_monitor_->OnConnectFailure();
    }
    return grpc::Status::OK;
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    connect_params_ = params;
    executor_ = std::make_unique<pg::QueryExecutor>(&pool_, &sessions_, params);
  }

  if (health_monitor_ != nullptr) {
    health_monitor_->OnConnectSuccess(params, config_.statement_timeout_ms);
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

  if (!EnsureQueryRequest(*request, &message)) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, message);
  }

  if (static_cast<int>(request->text().size()) > config_.max_query_text_bytes) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "query text exceeds MTDD_MAX_QUERY_TEXT_BYTES");
  }

  pg::ConnectionManager* pool = nullptr;
  pg::SessionStore* sessions = nullptr;
  pg::ConnectParams connect_params;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!executor_ || !connect_params_.has_value()) {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Connect must be called first");
    }
    pool = &pool_;
    sessions = &sessions_;
    connect_params = connect_params_.value();
  }

  const int fetch_rows = config_.pg_fetch_rows > 0 ? config_.pg_fetch_rows : 10000;
  const int wire_batch_rows = config_.pg_wire_batch_rows > 0 ? config_.pg_wire_batch_rows : 1000;

  pg::StreamingQuery stream(pool, sessions, connect_params, fetch_rows);
  std::string exec_error;
  if (!stream.Open(*request, &exec_error)) {
    pg::PgErrorMeta meta;
    meta.message = exec_error;
    WriteErrorChunk(writer, meta);
    return grpc::Status::OK;
  }

  codec::ResultSchema schema_meta;
  bool schema_open = false;
  bool batch_sent = false;
  int64_t total_rows = 0;
  PGresult* last_result = nullptr;

  try {
    while (true) {
      if (context->IsCancelled()) {
        stream.Abort();
        return grpc::Status(grpc::StatusCode::CANCELLED, "cancelled");
      }

      PGresult* fetch = stream.Fetch(&exec_error);
      if (fetch == nullptr) {
        if (!exec_error.empty()) {
          pg::PgErrorMeta meta;
          meta.message = exec_error;
          WriteErrorChunk(writer, meta);
          stream.Abort();
          return grpc::Status::OK;
        }
        break;
      }

      if (last_result != nullptr) {
        PQclear(last_result);
      }
      last_result = fetch;

      const ExecStatusType status = PQresultStatus(fetch);
      if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        WriteErrorChunk(writer, pg::QueryExecutor::ExtractPgError(fetch));
        stream.Abort();
        return grpc::Status::OK;
      }

      const int num_fields = PQnfields(fetch);
      const int num_rows = PQntuples(fetch);

      if (num_fields == 0) {
        const auto trailer = codec::BuildCommandTrailer(fetch);
        if (!WriteResultChunk(
                writer,
                mtdd::CHUNK_KIND_TRAILER,
                codec::EncodeResultTrailer(trailer),
                {})) {
          stream.Abort();
          return grpc::Status(grpc::StatusCode::CANCELLED, "client cancelled stream");
        }
        PQclear(last_result);
        last_result = nullptr;
        stream.Close(&exec_error);
        return grpc::Status::OK;
      }

      if (num_rows == 0 && schema_open) {
        break;
      }

      if (!schema_open) {
        schema_meta = codec::BuildSchemaMeta(fetch);
        schema_open = true;
      }

      if (num_rows == 0) {
        continue;
      }

      for (int row_begin = 0; row_begin < num_rows; row_begin += wire_batch_rows) {
        const int row_end = std::min(row_begin + wire_batch_rows, num_rows);
        const std::string payload = codec::EncodeRawPgBatch(fetch, row_begin, row_end);
        total_rows += row_end - row_begin;

        if (!SendBatchChunk(context, writer, schema_meta, &batch_sent, payload)) {
          stream.Abort();
          return grpc::Status(
              grpc::StatusCode::CANCELLED,
              context->IsCancelled() ? "cancelled" : "client cancelled stream");
        }
      }
    }

    if (schema_open && !batch_sent) {
      const std::string empty_payload = codec::EncodeRawPgBatch(last_result, 0, 0);
      if (!SendBatchChunk(context, writer, schema_meta, &batch_sent, empty_payload)) {
        stream.Abort();
        return grpc::Status(
            grpc::StatusCode::CANCELLED,
            context->IsCancelled() ? "cancelled" : "client cancelled stream");
      }
    }

    codec::ResultTrailer trailer;
    if (schema_open) {
      trailer.command_tag = schema_meta.command + " " + std::to_string(total_rows);
      trailer.row_count = total_rows;
    } else if (last_result != nullptr) {
      trailer = codec::BuildCommandTrailer(last_result);
    } else {
      trailer.command_tag = "SELECT 0";
      trailer.row_count = 0;
    }
    trailer.oid = 0;

    if (!WriteResultChunk(writer, mtdd::CHUNK_KIND_TRAILER, codec::EncodeResultTrailer(trailer), {})) {
      stream.Abort();
      return grpc::Status(grpc::StatusCode::CANCELLED, "client cancelled stream");
    }

    if (last_result != nullptr) {
      PQclear(last_result);
      last_result = nullptr;
    }
    stream.Close(&exec_error);
    return grpc::Status::OK;
  } catch (const std::exception& ex) {
    if (last_result != nullptr) {
      PQclear(last_result);
    }
    stream.Abort();
    pg::PgErrorMeta meta;
    meta.message = ex.what();
    WriteErrorChunk(writer, meta);
    return grpc::Status::OK;
  }
}

grpc::Status MtddShardServiceImpl::Disconnect(
    grpc::ServerContext* /*context*/,
    const mtdd::DisconnectRequest* request,
    mtdd::DisconnectResponse* response) {
  std::string message;
  if (!ValidateHostIndex(request->host_index(), &message)) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, message);
  }

  log::Info("disconnect", "acknowledged (shared pool unchanged)");
  response->set_ok(true);
  return grpc::Status::OK;
}

}  // namespace mtdd::service
