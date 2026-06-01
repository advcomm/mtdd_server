#include "pg/session_store.h"

#include <stdexcept>

namespace mtdd::pg {

SessionStore::SessionStore(int max_sessions) : max_sessions_(max_sessions > 0 ? max_sessions : 1) {}

std::shared_ptr<PqConnection> SessionStore::GetOrCreate(
    const std::string& session_id,
    const ConnectParams& params,
    std::string* error_out) {
  if (session_id.empty()) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    if (it->second->IsHealthy()) {
      return it->second;
    }
    sessions_.erase(it);
  }

  if (static_cast<int>(sessions_.size()) >= max_sessions_) {
    if (error_out != nullptr) {
      *error_out = "session limit exceeded";
    }
    return nullptr;
  }

  std::string error;
  auto owned = PqConnection::Connect(params, &error);
  if (!owned) {
    if (error_out != nullptr) {
      *error_out = error;
    }
    return nullptr;
  }

  auto shared = std::shared_ptr<PqConnection>(owned.release());
  sessions_[session_id] = shared;
  return shared;
}

void SessionStore::CloseAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.clear();
}

size_t SessionStore::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.size();
}

}  // namespace mtdd::pg
