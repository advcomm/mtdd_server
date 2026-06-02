#include <gtest/gtest.h>

#include <cstdlib>

#include "config.h"

TEST(ConfigTest, ParsesProductionMode) {
  setenv("MTDD_ENV", "production", 1);
  setenv("MTDD_HOST_INDEX", "2", 1);
  setenv("MTDD_LISTEN", "127.0.0.1:50051", 1);
  const mtdd::ServerConfig config = mtdd::LoadConfigFromEnv();
  EXPECT_TRUE(config.production_mode);
  EXPECT_TRUE(config.host_index.has_value());
  EXPECT_EQ(config.host_index.value(), 2);
  unsetenv("MTDD_ENV");
  unsetenv("MTDD_HOST_INDEX");
  unsetenv("MTDD_LISTEN");
}

TEST(ConfigTest, ProductionRequiresHostIndex) {
  setenv("MTDD_ENV", "production", 1);
  unsetenv("MTDD_HOST_INDEX");
  setenv("MTDD_LISTEN", "127.0.0.1:50051", 1);
  EXPECT_THROW(mtdd::LoadConfigFromEnv(), std::runtime_error);
  unsetenv("MTDD_ENV");
  unsetenv("MTDD_LISTEN");
}

TEST(ConfigTest, IsLoopbackAddress) {
  EXPECT_TRUE(mtdd::IsLoopbackAddress("127.0.0.1"));
  EXPECT_TRUE(mtdd::IsLoopbackAddress("::1"));
  EXPECT_FALSE(mtdd::IsLoopbackAddress("0.0.0.0"));
}
