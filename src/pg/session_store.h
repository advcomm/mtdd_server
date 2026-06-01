#pragma once

#include "pg/pq_conn.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mtdd::pg {

class SessionStore {
 public:
  explicit SessionStore(int max_sessions);

  std::shared_ptr<PqConnection> GetOrCreate(
      const std::string& session_id,
      const ConnectParams& params,
      std::string* error_out);

  void CloseAll();
  size_t Size() const;

 private:
  const int max_sessions_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<PqConnection>> sessions_;
};

}  // namespace mtdd::pg
