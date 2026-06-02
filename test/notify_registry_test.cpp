#include <gtest/gtest.h>

#include "notify/channel_key.h"
#include "notify/registry.h"

namespace {

TEST(ChannelKeyTest, NormalizesEmptyTidScope) {
  EXPECT_EQ(mtdd::notify::ChannelKey("events", ""), "__global__:events");
  EXPECT_EQ(mtdd::notify::ChannelKey("events", "__global__"), "__global__:events");
  EXPECT_EQ(mtdd::notify::ChannelKey("events", "tenant-1"), "tenant-1:events");
}

TEST(NotifyRegistryTest, SubscribeAndUnsubscribe) {
  mtdd::notify::NotifyRegistry registry;
  registry.Subscribe("client-a", "events", "");
  EXPECT_EQ(registry.SubscriptionCountForTest("__global__:events"), 1u);

  registry.Unsubscribe("client-a", "events", "");
  EXPECT_EQ(registry.SubscriptionCountForTest("__global__:events"), 0u);
}

TEST(NotifyRegistryTest, UnsubscribeAllClearsClient) {
  mtdd::notify::NotifyRegistry registry;
  registry.Subscribe("client-a", "events", "tenant-1");
  registry.Subscribe("client-a", "alerts", "tenant-1");
  EXPECT_EQ(registry.SubscriptionCountForTest("tenant-1:events"), 1u);
  EXPECT_EQ(registry.SubscriptionCountForTest("tenant-1:alerts"), 1u);

  registry.UnsubscribeAll("client-a");
  EXPECT_EQ(registry.SubscriptionCountForTest("tenant-1:events"), 0u);
  EXPECT_EQ(registry.SubscriptionCountForTest("tenant-1:alerts"), 0u);
}

TEST(NotifyRegistryTest, PublishWithoutWatchersReturnsZero) {
  mtdd::notify::NotifyRegistry registry;
  registry.Subscribe("client-a", "events", "");
  EXPECT_EQ(registry.Publish("events", "hello", ""), 0);
}

}  // namespace
