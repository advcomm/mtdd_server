#include "config.h"

#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace mtdd {
namespace {

int ParsePositiveInt(const char* name, const char* value, int default_value) {
  if (value == nullptr || *value == '\0') {
    return default_value;
  }
  char* end = nullptr;
  long parsed = std::strtol(value, &end, 10);
  if (end == value || *end != '\0' || parsed <= 0) {
    throw std::runtime_error(std::string(name) + " must be a positive integer");
  }
  return static_cast<int>(parsed);
}

std::optional<int32_t> ParseOptionalHostIndex(const char* value) {
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  char* end = nullptr;
  long parsed = std::strtol(value, &end, 10);
  if (end == value || *end != '\0' || parsed < 0) {
    throw std::runtime_error("MTDD_HOST_INDEX must be a non-negative integer");
  }
  return static_cast<int32_t>(parsed);
}

void ParseListen(const char* value, std::string& address, int& port) {
  if (value == nullptr || *value == '\0') {
    return;
  }
  const std::string listen(value);
  const auto colon = listen.rfind(':');
  if (colon == std::string::npos) {
    throw std::runtime_error("MTDD_LISTEN must be host:port");
  }
  address = listen.substr(0, colon);
  port = ParsePositiveInt("MTDD_LISTEN port", listen.substr(colon + 1).c_str(), port);
}

}  // namespace

ServerConfig LoadConfigFromEnv() {
  ServerConfig config;
  ParseListen(std::getenv("MTDD_LISTEN"), config.listen_address, config.listen_port);

  if (const char* pg_host = std::getenv("MTDD_PG_HOST"); pg_host != nullptr && *pg_host != '\0') {
    config.pg_host = pg_host;
  }

  config.host_index = ParseOptionalHostIndex(std::getenv("MTDD_HOST_INDEX"));
  config.pool_size = ParsePositiveInt("MTDD_POOL_SIZE", std::getenv("MTDD_POOL_SIZE"), config.pool_size);
  config.arrow_batch_rows =
      ParsePositiveInt("MTDD_ARROW_BATCH_ROWS", std::getenv("MTDD_ARROW_BATCH_ROWS"), config.arrow_batch_rows);
  config.pg_connect_timeout_sec = ParsePositiveInt(
      "MTDD_PG_CONNECT_TIMEOUT_SEC", std::getenv("MTDD_PG_CONNECT_TIMEOUT_SEC"), config.pg_connect_timeout_sec);
  config.max_sessions =
      ParsePositiveInt("MTDD_MAX_SESSIONS", std::getenv("MTDD_MAX_SESSIONS"), config.max_sessions);

  const int hw = static_cast<int>(std::thread::hardware_concurrency());
  config.grpc_max_threads =
      ParsePositiveInt("MTDD_GRPC_MAX_THREADS", std::getenv("MTDD_GRPC_MAX_THREADS"), hw > 0 ? hw : 4);

  return config;
}

}  // namespace mtdd
