#pragma once

#include <libpq-fe.h>

#include <memory>
#include <string>
#include <vector>

#include "mtdd.pb.h"

namespace mtdd::pg {

struct PgErrorMeta {
  std::string sqlstate;
  std::string severity;
  std::string message;
  std::string detail;
  std::string position;
};

struct QueryExecution {
  PGresult* result = nullptr;
  bool owns_result = true;
  bool connection_broken = false;
};

class ConnectionManager;
class SessionStore;
struct ConnectParams;

class QueryExecutor {
 public:
  QueryExecutor(ConnectionManager* pool, SessionStore* sessions, const ConnectParams& session_defaults);

  QueryExecution Execute(const mtdd::QueryRequest& request, std::string* error_out);

  static PgErrorMeta ExtractPgError(PGresult* result);

 private:
  ConnectionManager* pool_;
  SessionStore* sessions_;
  ConnectParams session_defaults_;
};

}  // namespace mtdd::pg
