#include "pg/connection_manager.h"

namespace mtdd::pg {

void ConnectionManager::Configure(const ConnectParams& params, int pool_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  params_ = params;
  pool_size_ = pool_size > 0 ? pool_size : 1;
  configured_ = true;
}

bool ConnectionManager::IsConfigured() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return configured_;
}

bool ConnectionManager::Connect(std::string* error_out) {
  std::string error;
  auto owned = PqConnection::Connect(params_, &error);
  if (!owned) {
    if (error_out != nullptr) {
      *error_out = error;
    }
    return false;
  }
  auto conn = std::shared_ptr<PqConnection>(owned.release());
  std::lock_guard<std::mutex> lock(mutex_);
  idle_.push_back(std::move(conn));
  return true;
}

std::shared_ptr<PqConnection> ConnectionManager::CreateConnection() {
  std::string error;
  auto conn = PqConnection::Connect(params_, &error);
  if (!conn) {
    return nullptr;
  }
  return std::shared_ptr<PqConnection>(conn.release());
}

std::shared_ptr<PqConnection> ConnectionManager::Acquire() {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [&]() { return !idle_.empty() || in_use_ < pool_size_; });

  if (!idle_.empty()) {
    auto conn = idle_.front();
    idle_.pop_front();
    ++in_use_;
    return conn;
  }

  ++in_use_;
  lock.unlock();
  auto conn = CreateConnection();
  if (!conn) {
    std::lock_guard<std::mutex> relock(mutex_);
    --in_use_;
    cv_.notify_one();
  }
  return conn;
}

void ConnectionManager::Release(std::shared_ptr<PqConnection> conn, bool broken) {
  if (!conn) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  --in_use_;

  if (broken || !conn->IsHealthy()) {
    cv_.notify_one();
    return;
  }

  idle_.push_back(std::move(conn));
  cv_.notify_one();
}

void ConnectionManager::CloseAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  idle_.clear();
  configured_ = false;
}

}  // namespace mtdd::pg
