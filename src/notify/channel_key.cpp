#include "notify/channel_key.h"

namespace mtdd::notify {

std::string NormalizeTidScope(const std::string& tid_scope) {
  if (tid_scope.empty()) {
    return kGlobalTidScope;
  }
  return tid_scope;
}

std::string ChannelKey(const std::string& channel, const std::string& tid_scope) {
  return NormalizeTidScope(tid_scope) + ":" + channel;
}

}  // namespace mtdd::notify
