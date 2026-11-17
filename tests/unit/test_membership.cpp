#include <gtest/gtest.h>
#include "core/membership/membership_manager.hpp"
#include "core/agent/node_identity.hpp"

using namespace dicp;

TEST(MembershipTest, InitializeWithSelf) {
    AgentConfig cfg;
    cfg.node.id = "self-node";
    NodeIdentity id(cfg);
    MembershipManager mgr(id);

    EXPECT_EQ(mgr.MemberCount(), 1ULL);
    EXPECT_EQ(mgr.AliveCount(), 1ULL);

    auto self_rec = mgr.GetMember("self-node");
    ASSERT_TRUE(self_rec.has_value());
    EXPECT_EQ(self_rec->node_id(), "self-node");
    EXPECT_EQ(self_rec->status(), proto::NODE_ALIVE);
}

TEST(MembershipTest, NewMemberDiscovery) {
    AgentConfig cfg;
    cfg.node.id = "local-node";
    NodeIdentity id(cfg);
    MembershipManager mgr(id);

    proto::NodeUpdate update;
    update.set_node_id("peer-1");
    update.set_incarnation(0);
    update.set_status(proto::NODE_ALIVE);
    update.set_advertised_address("127.0.0.1");
    update.set_gossip_port(8001);

    auto [changed, refutation] = mgr.ApplyUpdate(update);
    EXPECT_TRUE(changed);
    EXPECT_FALSE(refutation.has_value());
    EXPECT_EQ(mgr.MemberCount(), 2ULL);
    EXPECT_EQ(mgr.AliveCount(), 2ULL);
}

TEST(MembershipTest, EqualIncarnationPrecedence) {
    AgentConfig cfg;
    cfg.node.id = "local-node";
    NodeIdentity id(cfg);
    MembershipManager mgr(id);

    proto::NodeUpdate alive_up;
    alive_up.set_node_id("peer-1");
    alive_up.set_incarnation(5);
    alive_up.set_status(proto::NODE_ALIVE);
    mgr.ApplyUpdate(alive_up);

    // Update with equal incarnation and SUSPECT status: SUSPECT beats ALIVE
    proto::NodeUpdate suspect_up;
    suspect_up.set_node_id("peer-1");
    suspect_up.set_incarnation(5);
    suspect_up.set_status(proto::NODE_SUSPECT);
    auto [changed1, ref1] = mgr.ApplyUpdate(suspect_up);
    EXPECT_TRUE(changed1);
    EXPECT_EQ(mgr.GetMember("peer-1")->status(), proto::NODE_SUSPECT);

    // Update with equal incarnation and ALIVE status: ALIVE should be ignored because SUSPECT is higher
    auto [changed2, ref2] = mgr.ApplyUpdate(alive_up);
    EXPECT_FALSE(changed2);
    EXPECT_EQ(mgr.GetMember("peer-1")->status(), proto::NODE_SUSPECT);

    // Update with equal incarnation and DEAD status: DEAD beats SUSPECT
    proto::NodeUpdate dead_up;
    dead_up.set_node_id("peer-1");
    dead_up.set_incarnation(5);
    dead_up.set_status(proto::NODE_DEAD);
    auto [changed3, ref3] = mgr.ApplyUpdate(dead_up);
    EXPECT_TRUE(changed3);
    EXPECT_EQ(mgr.GetMember("peer-1")->status(), proto::NODE_DEAD);

    // Update with HIGHER incarnation ALIVE (e.g. 6): Higher incarnation wins unconditionally!
    proto::NodeUpdate resurrected;
    resurrected.set_node_id("peer-1");
    resurrected.set_incarnation(6);
    resurrected.set_status(proto::NODE_ALIVE);
    auto [changed4, ref4] = mgr.ApplyUpdate(resurrected);
    EXPECT_TRUE(changed4);
    EXPECT_EQ(mgr.GetMember("peer-1")->status(), proto::NODE_ALIVE);
    EXPECT_EQ(mgr.GetMember("peer-1")->incarnation(), 6ULL);
}

TEST(MembershipTest, SelfSuspicionTriggersRefutation) {
    AgentConfig cfg;
    cfg.node.id = "local-node";
    NodeIdentity id(cfg);
    MembershipManager mgr(id);

    EXPECT_EQ(id.Incarnation(), 0ULL);

    // Incoming message claiming local-node is SUSPECT at incarnation 0
    proto::NodeUpdate false_claim;
    false_claim.set_node_id("local-node");
    false_claim.set_incarnation(0);
    false_claim.set_status(proto::NODE_SUSPECT);

    auto [changed, refutation] = mgr.ApplyUpdate(false_claim);
    EXPECT_TRUE(changed);
    ASSERT_TRUE(refutation.has_value());
    EXPECT_EQ(refutation->node_id(), "local-node");
    EXPECT_EQ(refutation->status(), proto::NODE_ALIVE);
    EXPECT_EQ(refutation->incarnation(), 1ULL); // Incarnation was incremented to refute
    EXPECT_EQ(id.Incarnation(), 1ULL);
}
