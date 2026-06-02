#pragma once

#include <grpcpp/grpcpp.h>
#include <mtdd.grpc.pb.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mtdd::notify {

class NotifyRegistry {
 public:
  void Subscribe(const std::string& client_id, const std::string& channel, const std::string& tid_scope);
  void Unsubscribe(const std::string& client_id, const std::string& channel, const std::string& tid_scope);
  void UnsubscribeAll(const std::string& client_id);

  // Returns number of watch streams that accepted the write.
  int Publish(const std::string& channel, const std::string& payload, const std::string& tid_scope);

  void RegisterWatch(const std::string& client_id, grpc::ServerWriter<mtdd::NotifyMessage>* writer);
  void UnregisterWatch(const std::string& client_id, grpc::ServerWriter<mtdd::NotifyMessage>* writer);

  size_t SubscriptionCountForTest(const std::string& channel_key) const;
  void ClearForTest();

 private:
  void RemoveClientFromKey(const std::string& client_id, const std::string& key);

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::unordered_set<std::string>> subscriptions_;
  std::unordered_map<std::string, std::unordered_set<std::string>> client_keys_;
  std::unordered_map<std::string, std::vector<grpc::ServerWriter<mtdd::NotifyMessage>*>> watchers_;
};

}  // namespace mtdd::notify
