#pragma once

#include <libpq-fe.h>

#include <atomic>
#include <memory>
#include <string>

#include "mtdd.pb.h"
#include "pg/query_executor.h"

namespace mtdd::pg {

class ConnectionManager;
class SessionStore;

// Streams query results via PostgreSQL cursor FETCH or a single immediate execution.
class StreamingQuery {
 public:
  StreamingQuery(ConnectionManager* pool, SessionStore* sessions, const ConnectParams& session_defaults,
                 int fetch_size);

  bool Open(const mtdd::QueryRequest& request, std::string* error_out);
  PGresult* Fetch(std::string* error_out);
  void Close(std::string* error_out);
  void Abort();

 private:
  bool AcquireConnection(const mtdd::QueryRequest& request, std::string* error_out);
  void ReleaseConnection(bool broken);
  bool BeginIfNeeded(std::string* error_out);
  void EndTransaction(bool commit, std::string* error_out);
  bool OpenCursor(const mtdd::QueryRequest& request, std::string* error_out);
  void CloseCursor(std::string* error_out);
  PGresult* ExecuteImmediate(const mtdd::QueryRequest& request, std::string* error_out);
  PGresult* FetchCursor(std::string* error_out);

  static std::string NextCursorName();

  ConnectionManager* pool_;
  SessionStore* sessions_;
  ConnectParams session_defaults_;
  int fetch_size_ = 10000;

  std::shared_ptr<PqConnection> connection_;
  bool pooled_ = false;
  bool use_session_ = false;
  bool use_cursor_ = false;
  bool started_local_txn_ = false;
  bool cursor_open_ = false;
  bool immediate_done_ = false;
  PGresult* immediate_result_ = nullptr;
  std::string cursor_name_;
  mtdd::QueryRequest request_;
};

}  // namespace mtdd::pg
