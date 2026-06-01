#pragma once

#include "pg/pq_conn.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace mtdd::pg {

class ConnectionManager {
 public:
  void Configure(const ConnectParams& params, int pool_size);

  bool IsConfigured() const;

  // Establishes credentials and warms the pool with one connection.
  bool Connect(std::string* error_out);

  std::shared_ptr<PqConnection> Acquire();
  void Release(std::shared_ptr<PqConnection> conn, bool broken);

  void CloseAll();

 private:
  std::shared_ptr<PqConnection> CreateConnection();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  ConnectParams params_;
  int pool_size_ = 8;
  bool configured_ = false;
  std::deque<std::shared_ptr<PqConnection>> idle_;
  int in_use_ = 0;
};

}  // namespace mtdd::pg
