#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "pg/pq_conn.h"

namespace grpc {
class Server;
}

namespace mtdd::health {

class PgHealthMonitor {
 public:
  explicit PgHealthMonitor(int probe_interval_sec);
  ~PgHealthMonitor();

  PgHealthMonitor(const PgHealthMonitor&) = delete;
  PgHealthMonitor& operator=(const PgHealthMonitor&) = delete;

  void AttachServer(grpc::Server* server);
  void OnConnectSuccess(const pg::ConnectParams& params, int statement_timeout_ms);
  void OnConnectFailure();
  void SetServingWithoutPg();
  void Stop();

 private:
  void SetServing(bool serving);
  bool ProbeOnce() const;
  void ProbeLoop();

  grpc::Server* server_ = nullptr;
  int probe_interval_sec_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> has_params_{false};
  std::thread probe_thread_;
  mutable std::mutex params_mutex_;
  pg::ConnectParams params_;
  int statement_timeout_ms_ = 0;
};

}  // namespace mtdd::health
