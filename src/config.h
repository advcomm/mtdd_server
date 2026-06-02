#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mtdd {

enum class ListenMode {
  Unix,
  Tcp,
};

struct ServerConfig {
  ListenMode listen_mode = ListenMode::Unix;
  std::string unix_socket_path = "/run/mtdd/grpc.sock";
  std::string listen_address = "127.0.0.1";
  int listen_port = 50051;
  int unix_socket_mode = 0660;
  int unix_socket_dir_mode = 0750;
  bool unix_socket_create_dir = false;
  std::optional<int32_t> host_index;
  std::string pg_host = "127.0.0.1";
  int pool_size = 8;
  int pg_fetch_rows = 10000;
  int pg_wire_batch_rows = 1000;
  int pg_connect_timeout_sec = 5;
  int max_sessions = 512;
  int grpc_max_threads = 0;  // 0 = auto (hardware concurrency)
  bool production_mode = false;
  bool grpc_reflection = false;
  bool notify_enabled = true;
  int statement_timeout_ms = 0;  // 0 = disabled
  int max_query_text_bytes = 1 << 20;  // 1 MiB
  int max_notify_payload_bytes = 65535;
  int max_notify_channel_bytes = 63;
  int health_probe_interval_sec = 30;
  bool health_require_pg = true;
};

ServerConfig LoadConfigFromEnv();

std::string FormatListenTarget(const ServerConfig& config);
std::string FormatListenLogLabel(const ServerConfig& config);

bool IsLoopbackAddress(const std::string& address);

}  // namespace mtdd
