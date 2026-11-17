#include <gtest/gtest.h>
#include "core/agent/node_agent.hpp"
#include <chrono>
#include <thread>

using namespace dicp;

TEST(ClusterConvergenceTest, MultiNodeGossipAndFailureDetection) {
    // Node 1 (Seed)
    AgentConfig cfg1;
    cfg1.node.id = "node-1";
    cfg1.node.bind_address = "127.0.0.1";
    cfg1.node.advertised_address = "127.0.0.1";
    cfg1.network.gossip_port = 19100;
    cfg1.network.sync_port = 19101;
    cfg1.network.management_port = 19102;
    cfg1.gossip.interval_ms = 100;
    cfg1.failure.probe_interval_ms = 200;
    cfg1.failure.probe_timeout_ms = 100;
    cfg1.failure.suspicion_timeout_ms = 300;

    // Node 2 (Connects to Node 1)
    AgentConfig cfg2;
    cfg2.node.id = "node-2";
    cfg2.node.bind_address = "127.0.0.1";
    cfg2.node.advertised_address = "127.0.0.1";
    cfg2.network.gossip_port = 19200;
    cfg2.network.sync_port = 19201;
    cfg2.network.management_port = 19202;
    cfg2.gossip.interval_ms = 100;
    cfg2.failure.probe_interval_ms = 200;
    cfg2.failure.probe_timeout_ms = 100;
    cfg2.failure.suspicion_timeout_ms = 300;
    cfg2.seeds = {"127.0.0.1:19100"};

    // Node 3 (Connects to Node 2 only!)
    AgentConfig cfg3;
    cfg3.node.id = "node-3";
    cfg3.node.bind_address = "127.0.0.1";
    cfg3.node.advertised_address = "127.0.0.1";
    cfg3.network.gossip_port = 19300;
    cfg3.network.sync_port = 19301;
    cfg3.network.management_port = 19302;
    cfg3.gossip.interval_ms = 100;
    cfg3.failure.probe_interval_ms = 200;
    cfg3.failure.probe_timeout_ms = 100;
    cfg3.failure.suspicion_timeout_ms = 300;
    cfg3.seeds = {"127.0.0.1:19200"};

    auto agent1 = std::make_unique<NodeAgent>(cfg1);
    auto agent2 = std::make_unique<NodeAgent>(cfg2);
    auto agent3 = std::make_unique<NodeAgent>(cfg3);

    agent1->Start();
    agent2->Start();
    agent3->Start();

    // Wait for gossip dissemination to converge across all 3 nodes
    // Node 1 should discover Node 3 transitively through Node 2
    bool converged = false;
    for (int i = 0; i < 30; ++i) {
        if (agent1->GetMembershipManager()->AliveCount() == 3 &&
            agent2->GetMembershipManager()->AliveCount() == 3 &&
            agent3->GetMembershipManager()->AliveCount() == 3) {
            converged = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(converged);
    EXPECT_EQ(agent1->GetMembershipManager()->AliveCount(), 3ULL);
    EXPECT_EQ(agent2->GetMembershipManager()->AliveCount(), 3ULL);
    EXPECT_EQ(agent3->GetMembershipManager()->AliveCount(), 3ULL);

    // Now test graceful leave of Node 3
    agent3->Stop();

    // Wait for Node 1 and Node 2 to receive graceful departure (status == NODE_LEFT)
    bool detected_left = false;
    for (int i = 0; i < 30; ++i) {
        auto m1 = agent1->GetMembershipManager()->GetMember("node-3");
        auto m2 = agent2->GetMembershipManager()->GetMember("node-3");
        if (m1 && m1->status() == proto::NODE_LEFT &&
            m2 && m2->status() == proto::NODE_LEFT) {
            detected_left = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(detected_left);

    agent1->Stop();
    agent2->Stop();
}
