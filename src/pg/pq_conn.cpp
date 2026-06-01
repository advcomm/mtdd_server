#include "pg/pq_conn.h"

#include <sstream>

namespace mtdd::pg {

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

std::unique_ptr<PqConnection> PqConnection::Connect(const ConnectParams& params, std::string* error_out) {
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

  auto conn = std::unique_ptr<PqConnection>(new PqConnection(raw));
  PGresult* probe = PQexec(raw, "SELECT 1");
  if (probe == nullptr || PQresultStatus(probe) != PGRES_TUPLES_OK) {
    if (error_out != nullptr) {
      if (probe != nullptr) {
        const char* err = PQresultErrorMessage(probe);
        *error_out = err != nullptr ? err : conn->ErrorMessage();
      } else {
        *error_out = conn->ErrorMessage();
      }
    }
    PQclear(probe);
    return nullptr;
  }
  PQclear(probe);
  return conn;
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
