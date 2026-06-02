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

void ValidateProductionConfig(const ServerConfig& config) {
  if (!config.production_mode) {
    return;
  }

  if (!config.host_index.has_value()) {
    throw std::runtime_error("MTDD_HOST_INDEX is required when MTDD_ENV=production");
  }

  if (!IsLoopbackAddress(config.listen_address)) {
    const char* allow = std::getenv("MTDD_ALLOW_PUBLIC_BIND");
    if (!ParseBoolEnv(allow, false)) {
      throw std::runtime_error(
          "MTDD_LISTEN must bind to loopback in production (set MTDD_ALLOW_PUBLIC_BIND=1 to override)");
    }
  }
}

void LoadGrpcTlsConfig(GrpcTlsConfig& tls) {
  tls.enabled = ParseBoolEnv(std::getenv("MTDD_GRPC_TLS"), false);

  if (const char* cert = std::getenv("MTDD_GRPC_TLS_CERT_FILE"); cert != nullptr) {
    tls.cert_file = cert;
  }
  if (const char* key = std::getenv("MTDD_GRPC_TLS_KEY_FILE"); key != nullptr) {
    tls.key_file = key;
  }
  if (const char* client_ca = std::getenv("MTDD_GRPC_TLS_CLIENT_CA_FILE"); client_ca != nullptr) {
    tls.client_ca_file = client_ca;
  }

  if (tls.enabled || !tls.cert_file.empty() || !tls.key_file.empty()) {
    tls.enabled = true;
    if (tls.cert_file.empty() || tls.key_file.empty()) {
      throw std::runtime_error(
          "MTDD_GRPC_TLS_CERT_FILE and MTDD_GRPC_TLS_KEY_FILE are required when MTDD_GRPC_TLS is enabled");
    }
  }
}

}  // namespace

bool IsLoopbackAddress(const std::string& address) {
  return address == "127.0.0.1" || address == "::1" || address == "localhost";
}

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
  LoadGrpcTlsConfig(config.grpc_tls);

  ValidateProductionConfig(config);
  return config;
}

}  // namespace mtdd
