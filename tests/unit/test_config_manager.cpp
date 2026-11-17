#include <gtest/gtest.h>
#include "core/config_manager/config_manager.hpp"

using namespace dicp;

TEST(ConfigManagerTest, SetAndGet) {
    auto queue = std::make_shared<DisseminationQueue>();
    ConfigurationManager mgr(queue);

    bool ok = mgr.Set("timeout.connect", "connect_timeout_ms", "5000", proto::SCOPE_CLUSTER, "", "alice");
    EXPECT_TRUE(ok);

    auto cfg_opt = mgr.Get("timeout.connect");
    ASSERT_TRUE(cfg_opt.has_value());
    EXPECT_EQ(cfg_opt->config_key(), "connect_timeout_ms");
    EXPECT_EQ(cfg_opt->config_value(), "5000");
    EXPECT_EQ(cfg_opt->version(), 1ULL);
    EXPECT_EQ(cfg_opt->author(), "alice");
}

TEST(ConfigManagerTest, MonotonicVersioningAndHistory) {
    auto queue = std::make_shared<DisseminationQueue>();
    ConfigurationManager mgr(queue);

    mgr.Set("rate_limit", "rps", "100", proto::SCOPE_CLUSTER, "", "bob");
    mgr.Set("rate_limit", "rps", "250", proto::SCOPE_CLUSTER, "", "charlie");

    auto cfg = mgr.Get("rate_limit");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->config_value(), "250");
    EXPECT_EQ(cfg->version(), 2ULL);

    auto history = mgr.GetHistory("rate_limit");
    ASSERT_EQ(history.size(), 2ULL);
    EXPECT_EQ(history[0].version, 1ULL);
    EXPECT_EQ(history[0].new_value, "100");
    EXPECT_EQ(history[1].version, 2ULL);
    EXPECT_EQ(history[1].new_value, "250");
}

TEST(ConfigManagerTest, RollbackCreatesNewMonotonicVersion) {
    auto queue = std::make_shared<DisseminationQueue>();
    ConfigurationManager mgr(queue);

    mgr.Set("cache_size", "mb", "512", proto::SCOPE_CLUSTER, "", "dave");  // v1
    mgr.Set("cache_size", "mb", "1024", proto::SCOPE_CLUSTER, "", "dave"); // v2
    mgr.Set("cache_size", "mb", "2048", proto::SCOPE_CLUSTER, "", "dave"); // v3

    EXPECT_EQ(mgr.Get("cache_size")->version(), 3ULL);
    EXPECT_EQ(mgr.Get("cache_size")->config_value(), "2048");

    // Rollback to v1 (512 mb)
    bool rb_ok = mgr.Rollback("cache_size", 1, "operator");
    EXPECT_TRUE(rb_ok);

    auto rolled_back = mgr.Get("cache_size");
    ASSERT_TRUE(rolled_back.has_value());
    EXPECT_EQ(rolled_back->config_value(), "512");
    EXPECT_EQ(rolled_back->version(), 4ULL); // v4 created with v1's value!
}

TEST(ConfigManagerTest, StaleVersionRejectedInAntiEntropy) {
    auto queue = std::make_shared<DisseminationQueue>();
    ConfigurationManager mgr(queue);

    mgr.Set("pool_size", "max_threads", "16", proto::SCOPE_CLUSTER, "", "eve"); // v1
    mgr.Set("pool_size", "max_threads", "32", proto::SCOPE_CLUSTER, "", "eve"); // v2

    proto::ConfigRecord stale;
    stale.set_config_id("pool_size");
    stale.set_config_key("max_threads");
    stale.set_config_value("8");
    stale.set_version(1); // older than current v2

    proto::ConfigUpdate up;
    *up.mutable_record() = stale;

    EXPECT_FALSE(mgr.ApplyConfigUpdate(up)); // Must reject stale version
    EXPECT_EQ(mgr.Get("pool_size")->config_value(), "32");
}
