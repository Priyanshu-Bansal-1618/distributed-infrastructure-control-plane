#include <gtest/gtest.h>
#include "core/agent/node_identity.hpp"

using namespace dicp;

TEST(NodeIdentityTest, Initialization) {
    AgentConfig cfg;
    cfg.node.id = "agent-42";
    cfg.node.advertised_address = "192.168.1.100";
    cfg.network.gossip_port = 7001;

    NodeIdentity id(cfg);
    EXPECT_EQ(id.NodeId(), "agent-42");
    EXPECT_EQ(id.AdvertisedAddress(), "192.168.1.100");
    EXPECT_EQ(id.GossipPort(), static_cast<uint16_t>(7001));
    EXPECT_EQ(id.Incarnation(), 0ULL);
    EXPECT_EQ(id.Status(), proto::NODE_ALIVE);
    EXPECT_FALSE(id.BootId().empty());
}

TEST(NodeIdentityTest, IncarnationRefutation) {
    AgentConfig cfg;
    NodeIdentity id(cfg);

    EXPECT_EQ(id.Incarnation(), 0ULL);
    uint64_t next_inc = id.IncrementIncarnation();
    EXPECT_EQ(next_inc, 1ULL);
    EXPECT_EQ(id.Incarnation(), 1ULL);
    EXPECT_EQ(id.Status(), proto::NODE_ALIVE);

    id.SetIncarnationIfGreater(10);
    EXPECT_EQ(id.Incarnation(), 10ULL);

    // Should not decrease
    id.SetIncarnationIfGreater(5);
    EXPECT_EQ(id.Incarnation(), 10ULL);
}

TEST(NodeIdentityTest, RecordAndMetadataConversion) {
    AgentConfig cfg;
    cfg.node.id = "agent-meta";
    NodeIdentity id(cfg);

    id.SetMetadata("zone", "us-east-1");
    id.SetMetadata("role", "worker");

    EXPECT_EQ(id.GetMetadata("zone"), "us-east-1");
    EXPECT_EQ(id.GetMetadata("role"), "worker");

    auto rec = id.ToRecord();
    EXPECT_EQ(rec.node_id(), "agent-meta");
    EXPECT_EQ(rec.metadata().at("zone"), "us-east-1");
    EXPECT_EQ(rec.metadata().at("role"), "worker");
}
