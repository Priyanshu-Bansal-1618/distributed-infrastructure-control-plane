#include <gtest/gtest.h>
#include "core/agent/node_agent.hpp"
#include <chrono>
#include <thread>

using namespace dicp;

TEST(FailureDetectorTest, DirectProbeSucceeds) {
    AgentConfig cfg1;
    cfg1.node.id = "probe-node-1";
    cfg1.node.bind_address = "127.0.0.1";
    cfg1.node.advertised_address = "127.0.0.1";
    cfg1.network.gossip_port = 18101;
    cfg1.network.sync_port = 18102;
    cfg1.network.management_port = 18103;
    cfg1.failure.probe_interval_ms = 200;
    cfg1.failure.probe_timeout_ms = 100;

    AgentConfig cfg2;
    cfg2.node.id = "probe-node-2";
    cfg2.node.bind_address = "127.0.0.1";
    cfg2.node.advertised_address = "127.0.0.1";
    cfg2.network.gossip_port = 18201;
    cfg2.network.sync_port = 18202;
    cfg2.network.management_port = 18203;

    auto agent1 = std::make_unique<NodeAgent>(cfg1);
    auto agent2 = std::make_unique<NodeAgent>(cfg2);

    // Register agent2 in agent1's membership manager
    agent1->GetMembershipManager()->AddOrUpdateMember(agent2->Identity().ToRecord());

    agent1->Start();
    agent2->Start();

    // Give it time for a probe cycle to run and succeed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_GT(agent1->GetFailureDetector()->ProbesSent(), 0ULL);
    EXPECT_GT(agent1->GetFailureDetector()->DirectAcks(), 0ULL);
    EXPECT_EQ(agent1->GetFailureDetector()->SuspicionsDeclared(), 0ULL);

    agent1->Stop();
    agent2->Stop();
}

TEST(FailureDetectorTest, UnreachableNodeDeclaredSuspectAndDead) {
    AgentConfig cfg1;
    cfg1.node.id = "detector-node";
    cfg1.node.bind_address = "127.0.0.1";
    cfg1.node.advertised_address = "127.0.0.1";
    cfg1.network.gossip_port = 18301;
    cfg1.network.sync_port = 18302;
    cfg1.network.management_port = 18303;
    cfg1.failure.probe_interval_ms = 100;
    cfg1.failure.probe_timeout_ms = 50;
    cfg1.failure.suspicion_timeout_ms = 200;

    auto agent1 = std::make_unique<NodeAgent>(cfg1);

    // Register a dead/non-existent peer on port 19999
    proto::NodeRecord phantom;
    phantom.set_node_id("phantom-node");
    phantom.set_advertised_address("127.0.0.1");
    phantom.set_gossip_port(19999);
    phantom.set_status(proto::NODE_ALIVE);
    phantom.set_incarnation(1);

    agent1->GetMembershipManager()->AddOrUpdateMember(phantom);
    agent1->Start();

    // Wait for probe to fail, mark SUSPECT, and then suspicion timer to expire and mark DEAD
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    auto status_opt = agent1->GetMembershipManager()->GetMember("phantom-node");
    ASSERT_TRUE(status_opt.has_value());
    EXPECT_EQ(status_opt->status(), proto::NODE_DEAD);

    EXPECT_GT(agent1->GetFailureDetector()->SuspicionsDeclared(), 0ULL);
    EXPECT_GT(agent1->GetFailureDetector()->DeathsDeclared(), 0ULL);

    agent1->Stop();
}
