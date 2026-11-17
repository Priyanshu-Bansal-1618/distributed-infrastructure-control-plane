#include <gtest/gtest.h>
#include "core/agent/node_agent.hpp"
#include "core/common/uuid.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>

using namespace dicp;

TEST(UdpTransportTest, TwoAgentsPingAckExchange) {
    AgentConfig cfg1;
    cfg1.node.id = "agent-1";
    cfg1.node.bind_address = "127.0.0.1";
    cfg1.node.advertised_address = "127.0.0.1";
    cfg1.network.gossip_port = 17946;
    cfg1.network.sync_port = 17947;
    cfg1.network.management_port = 18080;

    AgentConfig cfg2;
    cfg2.node.id = "agent-2";
    cfg2.node.bind_address = "127.0.0.1";
    cfg2.node.advertised_address = "127.0.0.1";
    cfg2.network.gossip_port = 27946;
    cfg2.network.sync_port = 27947;
    cfg2.network.management_port = 28080;

    auto agent1 = std::make_unique<NodeAgent>(cfg1);
    auto agent2 = std::make_unique<NodeAgent>(cfg2);

    agent1->Start();
    agent2->Start();

    std::mutex mtx;
    std::condition_variable cv;
    bool ack_received = false;
    std::string received_probe_id;
    std::string received_responder_id;

    // Attach custom message handler to agent1's UDP transport to intercept the ACK
    agent1->GetUdpTransport()->SetMessageHandler(
        [&](const proto::Envelope& env, const boost::asio::ip::udp::endpoint&) {
            if (env.type() == proto::MSG_ACK) {
                std::lock_guard<std::mutex> lock(mtx);
                ack_received = true;
                received_probe_id = env.ack().probe_id();
                received_responder_id = env.ack().responder_node_id();
                cv.notify_one();
            }
        });

    // Agent 1 sends PING to Agent 2
    proto::Envelope ping_env;
    ping_env.set_protocol_version(1);
    ping_env.set_sender_node_id("agent-1");
    ping_env.set_message_id(GenerateUUID());
    ping_env.set_sender_sequence(1);
    ping_env.set_type(proto::MSG_PING);

    std::string expected_probe_id = "probe-test-999";
    auto* ping = ping_env.mutable_ping();
    ping->set_probe_id(expected_probe_id);
    ping->set_target_node_id("agent-2");
    ping->set_sender_incarnation(0);

    bool sent = agent1->GetUdpTransport()->SendTo(ping_env, "127.0.0.1", 27946);
    EXPECT_TRUE(sent);

    // Wait for ACK
    {
        std::unique_lock<std::mutex> lock(mtx);
        bool success = cv.wait_for(lock, std::chrono::seconds(3), [&]() { return ack_received; });
        EXPECT_TRUE(success);
    }

    EXPECT_EQ(received_probe_id, expected_probe_id);
    EXPECT_EQ(received_responder_id, "agent-2");

    // Clean shutdown test
    agent1->Stop();
    agent2->Stop();

    EXPECT_FALSE(agent1->IsRunning());
    EXPECT_FALSE(agent2->IsRunning());
}
