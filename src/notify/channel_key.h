#pragma once

#include <string>

namespace mtdd::notify {

constexpr const char* kGlobalTidScope = "__global__";

std::string NormalizeTidScope(const std::string& tid_scope);
std::string ChannelKey(const std::string& channel, const std::string& tid_scope);

}  // namespace mtdd::notify
