#include <gtest/gtest.h>
#include "core/common/config.hpp"

using namespace dicp;

TEST(ConfigTest, DefaultValuesAreValid) {
    AgentConfig cfg;
    EXPECT_NO_THROW(cfg.Validate());
    EXPECT_EQ(cfg.node.id, "node-001");
    EXPECT_EQ(cfg.network.gossip_port, static_cast<uint16_t>(7946));
    EXPECT_EQ(cfg.gossip.max_message_bytes, static_cast<uint32_t>(1200));
}

TEST(ConfigTest, ParseFromJson) {
    std::string json_str = R"({
        "node": {
            "id": "node-test",
            "bind_address": "127.0.0.1",
            "advertised_address": "127.0.0.1"
        },
        "network": {
            "gossip_port": 8888,
            "sync_port": 8889,
            "management_port": 8890
        },
        "gossip": {
            "max_message_bytes": 1000
        },
        "seeds": ["127.0.0.1:7946", "127.0.0.1:7947"]
    })";

    AgentConfig cfg = AgentConfig::ParseFromJson(json_str);
    EXPECT_EQ(cfg.node.id, "node-test");
    EXPECT_EQ(cfg.network.gossip_port, static_cast<uint16_t>(8888));
    EXPECT_EQ(cfg.network.sync_port, static_cast<uint16_t>(8889));
    EXPECT_EQ(cfg.gossip.max_message_bytes, static_cast<uint32_t>(1000));
    ASSERT_EQ(cfg.seeds.size(), static_cast<size_t>(2));
    EXPECT_EQ(cfg.seeds[0], "127.0.0.1:7946");
}

TEST(ConfigTest, ValidationFailsOnEmptyNodeId) {
    AgentConfig cfg;
    cfg.node.id = "";
    EXPECT_THROW(cfg.Validate(), std::invalid_argument);
}

TEST(ConfigTest, ValidationFailsOnZeroPort) {
    AgentConfig cfg;
    cfg.network.gossip_port = 0;
    EXPECT_THROW(cfg.Validate(), std::invalid_argument);
}
