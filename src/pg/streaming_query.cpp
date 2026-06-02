#include "pg/streaming_query.h"

#include "codec/query_meta.h"
#include "pg/connection_manager.h"
#include "pg/session_store.h"

#include <atomic>
#include <sstream>
#include <vector>

namespace mtdd::pg {
namespace {

std::atomic<uint64_t> g_cursor_counter{0};

bool IsOkStatus(ExecStatusType status) {
  return status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK || status == PGRES_EMPTY_QUERY;
}

PGresult* Exec(PGconn* conn, const std::string& sql) {
  return PQexec(conn, sql.c_str());
}

void ClearResult(PGresult* result) {
  if (result != nullptr) {
    PQclear(result);
  }
}

struct ParamBuffers {
  std::vector<std::string> storage;
  std::vector<const char*> values;
  std::vector<int> lengths;
  std::vector<int> formats;
  std::vector<Oid> types;
};

ParamBuffers BuildParams(const mtdd::QueryRequest& request) {
  ParamBuffers params;
  params.storage.reserve(static_cast<size_t>(request.params_size()));
  params.values.reserve(request.params_size());
  params.lengths.reserve(request.params_size());
  params.formats.reserve(request.params_size());
  params.types.reserve(request.params_size());

  for (int i = 0; i < request.params_size(); ++i) {
    const auto& param = request.params(i);
    params.storage.push_back(param.value());
    const std::string& stored = params.storage.back();
    if (stored.empty() && param.format() == 0) {
      params.values.push_back(nullptr);
      params.lengths.push_back(0);
    } else {
      params.values.push_back(stored.data());
      params.lengths.push_back(static_cast<int>(stored.size()));
    }
    params.formats.push_back(param.format());
    params.types.push_back(param.oid() == 0 ? 0 : static_cast<Oid>(param.oid()));
  }
  return params;
}

PGresult* ExecParams(
    PGconn* conn,
    const std::string& sql,
    const ParamBuffers& params,
    int result_format) {
  const int param_count = static_cast<int>(params.values.size());
  return PQexecParams(
      conn,
      sql.c_str(),
      param_count,
      param_count > 0 ? params.types.data() : nullptr,
      param_count > 0 ? params.values.data() : nullptr,
      param_count > 0 ? params.lengths.data() : nullptr,
      param_count > 0 ? params.formats.data() : nullptr,
      result_format);
}

}  // namespace

std::string StreamingQuery::NextCursorName() {
  const uint64_t id = g_cursor_counter.fetch_add(1, std::memory_order_relaxed);
  return "mtdd_c_" + std::to_string(id);
}

StreamingQuery::StreamingQuery(
    ConnectionManager* pool,
    SessionStore* sessions,
    const ConnectParams& session_defaults,
    int fetch_size)
    : pool_(pool),
      sessions_(sessions),
      session_defaults_(session_defaults),
      fetch_size_(fetch_size > 0 ? fetch_size : 10000) {}

bool StreamingQuery::AcquireConnection(const mtdd::QueryRequest& request, std::string* error_out) {
  use_session_ = !request.session_id().empty();

  if (use_session_) {
    connection_ = sessions_->GetOrCreate(request.session_id(), session_defaults_, error_out);
    if (!connection_) {
      return false;
    }
    pooled_ = false;
    return true;
  }

  connection_ = pool_->Acquire();
  if (!connection_) {
    if (error_out != nullptr) {
      *error_out = "failed to acquire database connection";
    }
    return false;
  }
  pooled_ = true;
  return true;
}

void StreamingQuery::ReleaseConnection(bool broken) {
  if (!connection_) {
    return;
  }

  if (pooled_) {
    pool_->Release(connection_, broken || !connection_->IsHealthy());
  }
  connection_.reset();
  pooled_ = false;
}

bool StreamingQuery::BeginIfNeeded(std::string* error_out) {
  std::lock_guard<std::mutex> lock(connection_->Mutex());
  const PGTransactionStatusType txn = PQtransactionStatus(connection_->Raw());
  if (txn == PQTRANS_IDLE) {
    PGresult* result = Exec(connection_->Raw(), "BEGIN");
    const ExecStatusType status = PQresultStatus(result);
    const bool ok = status == PGRES_COMMAND_OK;
    if (!ok && error_out != nullptr) {
      *error_out = PQresultErrorMessage(result);
    } else {
      started_local_txn_ = ok;
    }
    ClearResult(result);
    return ok;
  }
  if (txn == PQTRANS_INERROR) {
    if (error_out != nullptr) {
      *error_out = "connection is in failed transaction state";
    }
    return false;
  }
  return true;
}

void StreamingQuery::EndTransaction(bool commit, std::string* error_out) {
  if (!started_local_txn_ || !connection_) {
    return;
  }

  std::lock_guard<std::mutex> lock(connection_->Mutex());
  PGresult* result = Exec(connection_->Raw(), commit ? "COMMIT" : "ROLLBACK");
  if (PQresultStatus(result) != PGRES_COMMAND_OK && error_out != nullptr && error_out->empty()) {
    *error_out = PQresultErrorMessage(result);
  }
  ClearResult(result);
  started_local_txn_ = false;
}

bool StreamingQuery::OpenCursor(const mtdd::QueryRequest& request, std::string* error_out) {
  if (!BeginIfNeeded(error_out)) {
    return false;
  }

  cursor_name_ = NextCursorName();
  std::ostringstream declare;
  declare << "DECLARE " << cursor_name_ << " NO SCROLL CURSOR FOR " << request.text();

  const ParamBuffers params = BuildParams(request);
  const int result_format = request.result_format() != 0 ? 1 : 0;

  std::lock_guard<std::mutex> lock(connection_->Mutex());
  PGresult* result = ExecParams(connection_->Raw(), declare.str(), params, result_format);
  const ExecStatusType status = PQresultStatus(result);
  if (status != PGRES_COMMAND_OK) {
    if (error_out != nullptr) {
      *error_out = PQresultErrorMessage(result);
    }
    ClearResult(result);
    EndTransaction(false, error_out);
    return false;
  }

  ClearResult(result);
  cursor_open_ = true;
  return true;
}

void StreamingQuery::CloseCursor(std::string* error_out) {
  if (!cursor_open_ || !connection_) {
    return;
  }

  std::lock_guard<std::mutex> lock(connection_->Mutex());
  PGresult* result = Exec(connection_->Raw(), "CLOSE " + cursor_name_);
  if (PQresultStatus(result) != PGRES_COMMAND_OK && error_out != nullptr && error_out->empty()) {
    *error_out = PQresultErrorMessage(result);
  }
  ClearResult(result);
  cursor_open_ = false;
}

PGresult* StreamingQuery::ExecuteImmediate(const mtdd::QueryRequest& request, std::string* error_out) {
  const ParamBuffers params = BuildParams(request);
  const int result_format = request.result_format() != 0 ? 1 : 0;

  std::lock_guard<std::mutex> lock(connection_->Mutex());
  PGresult* result = ExecParams(connection_->Raw(), request.text(), params, result_format);
  const ExecStatusType status = result != nullptr ? PQresultStatus(result) : PGRES_FATAL_ERROR;
  if (status == PGRES_FATAL_ERROR || status == PGRES_NONFATAL_ERROR) {
    if (error_out != nullptr) {
      *error_out = result != nullptr ? PQresultErrorMessage(result) : connection_->ErrorMessage();
    }
    ClearResult(result);
    return nullptr;
  }
  if (!IsOkStatus(status)) {
    if (error_out != nullptr) {
      *error_out = PQresultErrorMessage(result);
    }
    ClearResult(result);
    return nullptr;
  }
  return result;
}

PGresult* StreamingQuery::FetchCursor(std::string* error_out) {
  if (!cursor_open_ || !connection_) {
    return nullptr;
  }

  std::ostringstream fetch;
  fetch << "FETCH FORWARD " << fetch_size_ << " FROM " << cursor_name_;

  std::lock_guard<std::mutex> lock(connection_->Mutex());
  PGresult* result = Exec(connection_->Raw(), fetch.str());
  const ExecStatusType status = result != nullptr ? PQresultStatus(result) : PGRES_FATAL_ERROR;
  if (status != PGRES_TUPLES_OK) {
    if (error_out != nullptr) {
      *error_out = result != nullptr ? PQresultErrorMessage(result) : connection_->ErrorMessage();
    }
    ClearResult(result);
    return nullptr;
  }
  return result;
}

bool StreamingQuery::Open(const mtdd::QueryRequest& request, std::string* error_out) {
  request_ = request;
  use_cursor_ = mtdd::codec::IsTupleStreamingSql(request.text());

  if (!AcquireConnection(request, error_out)) {
    return false;
  }

  if (use_cursor_) {
    return OpenCursor(request, error_out);
  }

  immediate_result_ = ExecuteImmediate(request, error_out);
  if (immediate_result_ == nullptr) {
    ReleaseConnection(true);
    return false;
  }

  immediate_done_ = false;
  return true;
}

PGresult* StreamingQuery::Fetch(std::string* error_out) {
  if (!connection_) {
    return nullptr;
  }

  if (use_cursor_) {
    PGresult* result = FetchCursor(error_out);
    return result;
  }

  if (immediate_done_) {
    return nullptr;
  }

  immediate_done_ = true;
  PGresult* result = immediate_result_;
  immediate_result_ = nullptr;
  return result;
}

void StreamingQuery::Close(std::string* error_out) {
  if (immediate_result_ != nullptr) {
    ClearResult(immediate_result_);
    immediate_result_ = nullptr;
  }

  if (use_cursor_) {
    CloseCursor(error_out);
    EndTransaction(true, error_out);
  }

  ReleaseConnection(false);
}

void StreamingQuery::Abort() {
  std::string ignored;
  if (immediate_result_ != nullptr) {
    ClearResult(immediate_result_);
    immediate_result_ = nullptr;
  }
  if (use_cursor_ && cursor_open_) {
    CloseCursor(&ignored);
  }
  if (started_local_txn_) {
    EndTransaction(false, &ignored);
  }
  ReleaseConnection(true);
}

}  // namespace mtdd::pg
