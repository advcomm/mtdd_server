#include <gtest/gtest.h>

#include <cstdlib>

#include "config.h"

TEST(ConfigTest, ParsesProductionMode) {
  setenv("MTDD_ENV", "production", 1);
  setenv("MTDD_HOST_INDEX", "2", 1);
  setenv("MTDD_LISTEN", "unix:/run/mtdd/grpc.sock", 1);
  const mtdd::ServerConfig config = mtdd::LoadConfigFromEnv();
  EXPECT_TRUE(config.production_mode);
  EXPECT_TRUE(config.host_index.has_value());
  EXPECT_EQ(config.host_index.value(), 2);
  EXPECT_EQ(config.listen_mode, mtdd::ListenMode::Unix);
  EXPECT_EQ(config.unix_socket_path, "/run/mtdd/grpc.sock");
  unsetenv("MTDD_ENV");
  unsetenv("MTDD_HOST_INDEX");
  unsetenv("MTDD_LISTEN");
}

TEST(ConfigTest, ProductionRequiresHostIndex) {
  setenv("MTDD_ENV", "production", 1);
  unsetenv("MTDD_HOST_INDEX");
  setenv("MTDD_LISTEN", "unix:/run/mtdd/grpc.sock", 1);
  EXPECT_THROW(mtdd::LoadConfigFromEnv(), std::runtime_error);
  unsetenv("MTDD_ENV");
  unsetenv("MTDD_LISTEN");
}

TEST(ConfigTest, ProductionRequiresUnixListen) {
  setenv("MTDD_ENV", "production", 1);
  setenv("MTDD_HOST_INDEX", "0", 1);
  setenv("MTDD_LISTEN", "127.0.0.1:50051", 1);
  unsetenv("MTDD_ALLOW_TCP_LISTEN");
  EXPECT_THROW(mtdd::LoadConfigFromEnv(), std::runtime_error);
  unsetenv("MTDD_ENV");
  unsetenv("MTDD_HOST_INDEX");
  unsetenv("MTDD_LISTEN");
}

TEST(ConfigTest, RejectsServerTlsEnv) {
  setenv("MTDD_GRPC_TLS", "1", 1);
  setenv("MTDD_GRPC_TLS_CERT_FILE", "/tmp/cert.pem", 1);
  setenv("MTDD_GRPC_TLS_KEY_FILE", "/tmp/key.pem", 1);
  EXPECT_THROW(mtdd::LoadConfigFromEnv(), std::runtime_error);
  unsetenv("MTDD_GRPC_TLS");
  unsetenv("MTDD_GRPC_TLS_CERT_FILE");
  unsetenv("MTDD_GRPC_TLS_KEY_FILE");
}

TEST(ConfigTest, ParsesUnixListenPath) {
  setenv("MTDD_LISTEN", "unix:///run/mtdd/grpc.sock", 1);
  const mtdd::ServerConfig config = mtdd::LoadConfigFromEnv();
  EXPECT_EQ(config.listen_mode, mtdd::ListenMode::Unix);
  EXPECT_EQ(config.unix_socket_path, "/run/mtdd/grpc.sock");
  EXPECT_EQ(mtdd::FormatListenTarget(config), "unix:/run/mtdd/grpc.sock");
  unsetenv("MTDD_LISTEN");
}

#ifndef _WIN32
TEST(ConfigTest, RejectsInvalidUnixListenPath) {
  setenv("MTDD_LISTEN", "unix:/run/mtdd/", 1);
  EXPECT_THROW(mtdd::LoadConfigFromEnv(), std::runtime_error);
  unsetenv("MTDD_LISTEN");
}
#endif

TEST(ConfigTest, IsLoopbackAddress) {
  EXPECT_TRUE(mtdd::IsLoopbackAddress("127.0.0.1"));
  EXPECT_TRUE(mtdd::IsLoopbackAddress("::1"));
  EXPECT_FALSE(mtdd::IsLoopbackAddress("0.0.0.0"));
}
