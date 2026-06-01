#include "pg/query_executor.h"

#include "pg/connection_manager.h"
#include "pg/session_store.h"

#include <cstring>
#include <vector>

namespace mtdd::pg {
namespace {

std::string FieldOrEmpty(PGresult* result, int field) {
  const char* value = PQresultErrorField(result, field);
  return value != nullptr ? std::string(value) : std::string();
}

}  // namespace

QueryExecutor::QueryExecutor(
    ConnectionManager* pool,
    SessionStore* sessions,
    const ConnectParams& session_defaults)
    : pool_(pool), sessions_(sessions), session_defaults_(session_defaults) {}

PgErrorMeta QueryExecutor::ExtractPgError(PGresult* result) {
  PgErrorMeta meta;
  if (result == nullptr) {
    meta.message = "query returned null result";
    return meta;
  }
  meta.sqlstate = FieldOrEmpty(result, PG_DIAG_SQLSTATE);
  meta.severity = FieldOrEmpty(result, PG_DIAG_SEVERITY);
  meta.message = FieldOrEmpty(result, PG_DIAG_MESSAGE_PRIMARY);
  if (meta.message.empty()) {
    meta.message = "unknown PostgreSQL error";
  }
  meta.detail = FieldOrEmpty(result, PG_DIAG_MESSAGE_DETAIL);
  meta.position = FieldOrEmpty(result, PG_DIAG_STATEMENT_POSITION);
  return meta;
}

void QueryExecutor::FinishQuery(QueryExecution& execution) {
  if (execution.result != nullptr) {
    PQclear(execution.result);
    execution.result = nullptr;
  }

  if (execution.pooled && execution.connection) {
    pool_->Release(execution.connection, execution.connection_broken);
    execution.connection.reset();
    execution.pooled = false;
  }
}

QueryExecution QueryExecutor::Execute(const mtdd::QueryRequest& request, std::string* error_out) {
  QueryExecution execution;
  const bool use_session = !request.session_id().empty();

  std::shared_ptr<PqConnection> conn;
  if (use_session) {
    conn = sessions_->GetOrCreate(request.session_id(), session_defaults_, error_out);
    if (!conn) {
      return execution;
    }
    execution.connection = conn;
    execution.pooled = false;
  } else {
    conn = pool_->Acquire();
    if (!conn) {
      if (error_out != nullptr) {
        *error_out = "failed to acquire database connection";
      }
      return execution;
    }
    execution.connection = conn;
    execution.pooled = true;
  }

  std::vector<std::string> param_storage;
  param_storage.reserve(static_cast<size_t>(request.params_size()));

  const int param_count = request.params_size();
  std::vector<const char*> values;
  std::vector<int> lengths;
  std::vector<int> formats;
  std::vector<Oid> types;
  values.reserve(param_count);
  lengths.reserve(param_count);
  formats.reserve(param_count);
  types.reserve(param_count);

  for (int i = 0; i < param_count; ++i) {
    const auto& param = request.params(i);
    param_storage.push_back(param.value());
    const std::string& stored = param_storage.back();
    if (stored.empty() && param.format() == 0) {
      values.push_back(nullptr);
      lengths.push_back(0);
    } else {
      values.push_back(stored.data());
      lengths.push_back(static_cast<int>(stored.size()));
    }
    formats.push_back(param.format());
    types.push_back(param.oid() == 0 ? 0 : static_cast<Oid>(param.oid()));
  }

  const int result_format = request.result_format() != 0 ? 1 : 0;

  {
    std::lock_guard<std::mutex> lock(conn->Mutex());

    PGresult* result = PQexecParams(
        conn->Raw(),
        request.text().c_str(),
        param_count,
        types.empty() ? nullptr : types.data(),
        values.empty() ? nullptr : values.data(),
        lengths.empty() ? nullptr : lengths.data(),
        formats.empty() ? nullptr : formats.data(),
        result_format);

    const ExecStatusType status = result != nullptr ? PQresultStatus(result) : PGRES_FATAL_ERROR;
    const bool ok = status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK;

    execution.connection_broken = !conn->IsHealthy();

    if (!ok) {
      execution.result = result;
      if (!use_session) {
        FinishQuery(execution);
      }
      return execution;
    }

    execution.result = result;
  }

  return execution;
}

}  // namespace mtdd::pg
