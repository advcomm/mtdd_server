#include "config.h"

#include <cstdlib>
#include <stdexcept>
#include <thread>

#ifndef _WIN32
#include "unix/unix_socket.h"
#endif

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

int ParseNonNegativeInt(const char* name, const char* value, int default_value) {
  if (value == nullptr || *value == '\0') {
    return default_value;
  }
  char* end = nullptr;
  long parsed = std::strtol(value, &end, 10);
  if (end == value || *end != '\0' || parsed < 0) {
    throw std::runtime_error(std::string(name) + " must be a non-negative integer");
  }
  return static_cast<int>(parsed);
}

int ParseOctalMode(const char* name, const char* value, int default_value) {
  if (value == nullptr || *value == '\0') {
    return default_value;
  }
  char* end = nullptr;
  long parsed = std::strtol(value, &end, 8);
  if (end == value || *end != '\0' || parsed < 0 || parsed > 0777) {
    throw std::runtime_error(std::string(name) + " must be a unix socket mode (octal, e.g. 660)");
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

std::string NormalizeUnixPath(std::string path) {
  if (path.empty()) {
    throw std::runtime_error("MTDD_LISTEN unix path must not be empty");
  }
  while (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
    path.erase(0, 1);
  }
  if (path[0] != '/') {
    throw std::runtime_error("MTDD_LISTEN unix path must be absolute");
  }
  return path;
}

void ParseListen(const char* value, ServerConfig& config) {
  if (value == nullptr || *value == '\0') {
#ifdef _WIN32
    config.listen_mode = ListenMode::Tcp;
#else
    config.listen_mode = ListenMode::Unix;
    config.unix_socket_path = "/run/mtdd/grpc.sock";
#endif
    return;
  }

  const std::string listen(value);
  if (listen.rfind("unix:", 0) == 0) {
    config.listen_mode = ListenMode::Unix;
    config.unix_socket_path = NormalizeUnixPath(listen.substr(5));
    return;
  }

  const auto colon = listen.rfind(':');
  if (colon == std::string::npos) {
    throw std::runtime_error("MTDD_LISTEN must be unix:/path or host:port");
  }

  config.listen_mode = ListenMode::Tcp;
  config.listen_address = listen.substr(0, colon);
  config.listen_port = ParsePositiveInt("MTDD_LISTEN port", listen.substr(colon + 1).c_str(), config.listen_port);
}

bool ParseBoolEnv(const char* value, bool default_value) {
  if (value == nullptr || *value == '\0') {
    return default_value;
  }
  const std::string v(value);
  if (v == "1" || v == "true" || v == "TRUE" || v == "yes" || v == "YES") {
    return true;
  }
  if (v == "0" || v == "false" || v == "FALSE" || v == "no" || v == "NO") {
    return false;
  }
  throw std::runtime_error(std::string("invalid boolean env value: ") + v);
}

bool IsProductionEnv(const char* value) {
  if (value == nullptr || *value == '\0') {
    return false;
  }
  const std::string v(value);
  return v == "production" || v == "prod";
}

void RejectServerTlsEnv() {
  static const char* kTlsVars[] = {
      "MTDD_GRPC_TLS",
      "MTDD_GRPC_TLS_CERT_FILE",
      "MTDD_GRPC_TLS_KEY_FILE",
      "MTDD_GRPC_TLS_CLIENT_CA_FILE",
  };

  for (const char* name : kTlsVars) {
    const char* value = std::getenv(name);
    if (value != nullptr && *value != '\0') {
      throw std::runtime_error(
          std::string(name) +
          " is not supported: mtdd_server uses plain gRPC over a unix domain socket; "
          "terminate TLS and compression at nginx");
    }
  }
}

void ValidateProductionConfig(const ServerConfig& config) {
  if (!config.production_mode) {
    return;
  }

  if (!config.host_index.has_value()) {
    throw std::runtime_error("MTDD_HOST_INDEX is required when MTDD_ENV=production");
  }

#ifndef _WIN32
  if (config.listen_mode != ListenMode::Unix) {
    const char* allow = std::getenv("MTDD_ALLOW_TCP_LISTEN");
    if (!ParseBoolEnv(allow, false)) {
      throw std::runtime_error(
          "MTDD_LISTEN must be a unix: path in production (set MTDD_ALLOW_TCP_LISTEN=1 for dev-only TCP)");
    }
  }
#else
  if (config.listen_mode != ListenMode::Tcp) {
    throw std::runtime_error("unix domain sockets are not supported on Windows; use host:port for MTDD_LISTEN");
  }
  if (!IsLoopbackAddress(config.listen_address)) {
    const char* allow = std::getenv("MTDD_ALLOW_PUBLIC_BIND");
    if (!ParseBoolEnv(allow, false)) {
      throw std::runtime_error(
          "MTDD_LISTEN must bind to loopback in production (set MTDD_ALLOW_PUBLIC_BIND=1 to override)");
    }
  }
#endif
}

}  // namespace

bool IsLoopbackAddress(const std::string& address) {
  return address == "127.0.0.1" || address == "::1" || address == "localhost";
}

std::string FormatListenTarget(const ServerConfig& config) {
  if (config.listen_mode == ListenMode::Unix) {
    return "unix:" + config.unix_socket_path;
  }
  return config.listen_address + ":" + std::to_string(config.listen_port);
}

std::string FormatListenLogLabel(const ServerConfig& config) {
  if (config.listen_mode == ListenMode::Unix) {
    return "unix:" + config.unix_socket_path;
  }
  return config.listen_address + ":" + std::to_string(config.listen_port);
}

ServerConfig LoadConfigFromEnv() {
  RejectServerTlsEnv();

  ServerConfig config;
  ParseListen(std::getenv("MTDD_LISTEN"), config);

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

  config.production_mode = IsProductionEnv(std::getenv("MTDD_ENV"));
  config.grpc_reflection = ParseBoolEnv(std::getenv("MTDD_GRPC_REFLECTION"), !config.production_mode);
  config.notify_enabled = ParseBoolEnv(std::getenv("MTDD_NOTIFY_ENABLED"), true);
  config.statement_timeout_ms =
      ParseNonNegativeInt("MTDD_STATEMENT_TIMEOUT_MS", std::getenv("MTDD_STATEMENT_TIMEOUT_MS"), 0);
  config.max_query_text_bytes = ParsePositiveInt(
      "MTDD_MAX_QUERY_TEXT_BYTES", std::getenv("MTDD_MAX_QUERY_TEXT_BYTES"), config.max_query_text_bytes);
  config.max_notify_payload_bytes = ParsePositiveInt(
      "MTDD_MAX_NOTIFY_PAYLOAD_BYTES", std::getenv("MTDD_MAX_NOTIFY_PAYLOAD_BYTES"), config.max_notify_payload_bytes);
  config.max_notify_channel_bytes = ParsePositiveInt(
      "MTDD_MAX_NOTIFY_CHANNEL_BYTES", std::getenv("MTDD_MAX_NOTIFY_CHANNEL_BYTES"), config.max_notify_channel_bytes);
  config.health_probe_interval_sec = ParsePositiveInt(
      "MTDD_HEALTH_PROBE_INTERVAL_SEC", std::getenv("MTDD_HEALTH_PROBE_INTERVAL_SEC"), config.health_probe_interval_sec);
  config.health_require_pg = ParseBoolEnv(std::getenv("MTDD_HEALTH_REQUIRE_PG"), true);
  config.unix_socket_mode =
      ParseOctalMode("MTDD_UNIX_SOCKET_MODE", std::getenv("MTDD_UNIX_SOCKET_MODE"), config.unix_socket_mode);
  config.unix_socket_dir_mode =
      ParseOctalMode("MTDD_UNIX_SOCKET_DIR_MODE", std::getenv("MTDD_UNIX_SOCKET_DIR_MODE"), config.unix_socket_dir_mode);
  config.unix_socket_create_dir =
      ParseBoolEnv(std::getenv("MTDD_UNIX_SOCKET_CREATE_DIR"), config.unix_socket_create_dir);

#ifndef _WIN32
  if (config.listen_mode == ListenMode::Unix) {
    unix_socket::ValidateUnixSocketPath(config.unix_socket_path);
  }
#endif

  ValidateProductionConfig(config);
  return config;
}

}  // namespace mtdd
