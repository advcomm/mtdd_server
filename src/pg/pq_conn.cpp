#include "pg/pq_conn.h"

#include <sstream>

namespace mtdd::pg {
namespace {

bool ApplyStatementTimeout(PGconn* raw, int statement_timeout_ms, std::string* error_out) {
  if (statement_timeout_ms <= 0 || raw == nullptr) {
    return true;
  }

  const std::string sql = "SET statement_timeout = " + std::to_string(statement_timeout_ms);
  PGresult* result = PQexec(raw, sql.c_str());
  if (result == nullptr) {
    if (error_out != nullptr) {
      *error_out = "failed to set statement_timeout";
    }
    return false;
  }

  const bool ok = PQresultStatus(result) == PGRES_COMMAND_OK;
  if (!ok && error_out != nullptr) {
    const char* err = PQresultErrorMessage(result);
    *error_out = err != nullptr ? err : "failed to set statement_timeout";
  }
  PQclear(result);
  return ok;
}

}  // namespace

std::string BuildConninfo(const ConnectParams& params) {
  std::ostringstream out;
  out << "host=" << params.pg_host << " "
      << "port=" << params.port << " "
      << "dbname=" << params.dbname << " "
      << "user=" << params.user << " "
      << "password=" << params.password << " "
      << "application_name=mtdd_server "
      << "connect_timeout=" << params.connect_timeout_sec;
  return out.str();
}

PqConnection::PqConnection(PGconn* conn) : conn_(conn) {}

PqConnection::~PqConnection() {
  if (conn_ != nullptr) {
    PQfinish(conn_);
  }
}

std::unique_ptr<PqConnection> PqConnection::Connect(const ConnectParams& params, std::string* error_out,
                                                    int statement_timeout_ms) {
  const std::string conninfo = BuildConninfo(params);
  PGconn* raw = PQconnectdb(conninfo.c_str());
  if (raw == nullptr) {
    if (error_out != nullptr) {
      *error_out = "PQconnectdb returned null";
    }
    return nullptr;
  }

  if (PQstatus(raw) != CONNECTION_OK) {
    if (error_out != nullptr) {
      *error_out = PQerrorMessage(raw);
    }
    PQfinish(raw);
    return nullptr;
  }

  PGresult* probe = PQexec(raw, "SELECT 1");
  if (probe == nullptr || PQresultStatus(probe) != PGRES_TUPLES_OK) {
    if (error_out != nullptr) {
      if (probe != nullptr) {
        const char* err = PQresultErrorMessage(probe);
        *error_out = err != nullptr ? err : PQerrorMessage(raw);
      } else {
        *error_out = PQerrorMessage(raw);
      }
    }
    PQclear(probe);
    PQfinish(raw);
    return nullptr;
  }
  PQclear(probe);

  const int timeout_ms = statement_timeout_ms > 0 ? statement_timeout_ms : params.statement_timeout_ms;
  if (!ApplyStatementTimeout(raw, timeout_ms, error_out)) {
    PQfinish(raw);
    return nullptr;
  }

  return std::unique_ptr<PqConnection>(new PqConnection(raw));
}

std::unique_ptr<PqConnection> PqConnection::Adopt(PGconn* raw) {
  return std::unique_ptr<PqConnection>(new PqConnection(raw));
}

bool PqConnection::IsHealthy() const {
  return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
}

std::string PqConnection::ErrorMessage() const {
  if (conn_ == nullptr) {
    return "connection is null";
  }
  const char* msg = PQerrorMessage(conn_);
  return msg != nullptr ? std::string(msg) : std::string();
}

}  // namespace mtdd::pg
