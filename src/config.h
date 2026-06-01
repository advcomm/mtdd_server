#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mtdd {

struct ServerConfig {
  std::string listen_address = "127.0.0.1";
  int listen_port = 50051;
  std::optional<int32_t> host_index;
  std::string pg_host = "127.0.0.1";
  int pool_size = 8;
  int arrow_batch_rows = 10000;
  int pg_connect_timeout_sec = 5;
  int max_sessions = 512;
  int grpc_max_threads = 0;  // 0 = auto (hardware concurrency)
};

ServerConfig LoadConfigFromEnv();

}  // namespace mtdd
