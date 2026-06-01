#pragma once

#include <libpq-fe.h>

#include <memory>
#include <mutex>
#include <string>

namespace mtdd::pg {

struct ConnectParams {
  std::string pg_host;
  int port = 5432;
  std::string dbname;
  std::string user;
  std::string password;
  int connect_timeout_sec = 5;
};

class PqConnection {
 public:
  static std::unique_ptr<PqConnection> Connect(const ConnectParams& params, std::string* error_out);
  static std::unique_ptr<PqConnection> Adopt(PGconn* raw);

  ~PqConnection();

  PqConnection(const PqConnection&) = delete;
  PqConnection& operator=(const PqConnection&) = delete;

  PGconn* Raw() { return conn_; }
  std::mutex& Mutex() { return mutex_; }

  bool IsHealthy() const;
  std::string ErrorMessage() const;

 private:
  explicit PqConnection(PGconn* conn);

  PGconn* conn_ = nullptr;
  mutable std::mutex mutex_;
};

std::string BuildConninfo(const ConnectParams& params);

}  // namespace mtdd::pg
