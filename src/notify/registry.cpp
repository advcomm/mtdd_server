#include "notify/registry.h"

#include <algorithm>

#include "notify/channel_key.h"

namespace mtdd::notify {
namespace {

bool IsValidClientId(const std::string& client_id) {
  return !client_id.empty();
}

bool IsValidChannel(const std::string& channel) {
  return !channel.empty();
}

mtdd::NotifyMessage MakeMessage(const std::string& channel, const std::string& payload) {
  mtdd::NotifyMessage message;
  message.set_channel(channel);
  message.set_payload(payload);
  message.set_process_id(0);
  return message;
}

}  // namespace

void NotifyRegistry::Subscribe(const std::string& client_id, const std::string& channel,
                               const std::string& tid_scope) {
  if (!IsValidClientId(client_id) || !IsValidChannel(channel)) {
    return;
  }

  const std::string key = ChannelKey(channel, tid_scope);
  std::lock_guard<std::mutex> lock(mutex_);
  subscriptions_[key].insert(client_id);
  client_keys_[client_id].insert(key);
}

void NotifyRegistry::RemoveClientFromKey(const std::string& client_id, const std::string& key) {
  auto sub_it = subscriptions_.find(key);
  if (sub_it != subscriptions_.end()) {
    sub_it->second.erase(client_id);
    if (sub_it->second.empty()) {
      subscriptions_.erase(sub_it);
    }
  }

  auto client_it = client_keys_.find(client_id);
  if (client_it != client_keys_.end()) {
    client_it->second.erase(key);
    if (client_it->second.empty()) {
      client_keys_.erase(client_it);
    }
  }
}

void NotifyRegistry::Unsubscribe(const std::string& client_id, const std::string& channel,
                                 const std::string& tid_scope) {
  if (!IsValidClientId(client_id) || !IsValidChannel(channel)) {
    return;
  }

  const std::string key = ChannelKey(channel, tid_scope);
  std::lock_guard<std::mutex> lock(mutex_);
  RemoveClientFromKey(client_id, key);
}

void NotifyRegistry::UnsubscribeAll(const std::string& client_id) {
  if (!IsValidClientId(client_id)) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto client_it = client_keys_.find(client_id);
  if (client_it == client_keys_.end()) {
    return;
  }

  const auto keys = client_it->second;
  for (const auto& key : keys) {
    RemoveClientFromKey(client_id, key);
  }
}

int NotifyRegistry::Publish(const std::string& channel, const std::string& payload,
                            const std::string& tid_scope) {
  if (!IsValidChannel(channel)) {
    return 0;
  }

  const std::string key = ChannelKey(channel, tid_scope);
  std::vector<grpc::ServerWriter<mtdd::NotifyMessage>*> targets;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto sub_it = subscriptions_.find(key);
    if (sub_it == subscriptions_.end()) {
      return 0;
    }
    for (const auto& client_id : sub_it->second) {
      const auto watch_it = watchers_.find(client_id);
      if (watch_it == watchers_.end()) {
        continue;
      }
      for (auto* writer : watch_it->second) {
        targets.push_back(writer);
      }
    }
  }

  const mtdd::NotifyMessage message = MakeMessage(channel, payload);
  int delivered = 0;
  for (auto* writer : targets) {
    if (writer->Write(message)) {
      ++delivered;
    }
  }
  return delivered;
}

void NotifyRegistry::RegisterWatch(const std::string& client_id,
                                   grpc::ServerWriter<mtdd::NotifyMessage>* writer) {
  if (!IsValidClientId(client_id) || writer == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  watchers_[client_id].push_back(writer);
}

void NotifyRegistry::UnregisterWatch(const std::string& client_id,
                                     grpc::ServerWriter<mtdd::NotifyMessage>* writer) {
  if (!IsValidClientId(client_id) || writer == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = watchers_.find(client_id);
  if (it == watchers_.end()) {
    return;
  }

  auto& streams = it->second;
  streams.erase(std::remove(streams.begin(), streams.end(), writer), streams.end());
  if (streams.empty()) {
    watchers_.erase(it);
  }
}

size_t NotifyRegistry::SubscriptionCountForTest(const std::string& channel_key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = subscriptions_.find(channel_key);
  if (it == subscriptions_.end()) {
    return 0;
  }
  return it->second.size();
}

void NotifyRegistry::ClearForTest() {
  std::lock_guard<std::mutex> lock(mutex_);
  subscriptions_.clear();
  client_keys_.clear();
  watchers_.clear();
}

}  // namespace mtdd::notify
