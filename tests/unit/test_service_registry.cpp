#include <gtest/gtest.h>
#include "core/registry/service_registry.hpp"
#include "core/agent/node_identity.hpp"
#include <thread>
#include <chrono>

using namespace dicp;

TEST(ServiceRegistryTest, RegisterAndDiscover) {
    AgentConfig cfg;
    cfg.node.id = "agent-reg-1";
    NodeIdentity id(cfg);
    auto queue = std::make_shared<DisseminationQueue>();
    ServiceRegistry reg(cfg, id, queue);

    proto::ServiceInstanceRecord inst;
    inst.set_instance_id("inst-payment-1");
    inst.set_service_name("payment");
    inst.set_address("127.0.0.1");
    inst.set_port(9001);
    inst.set_health(proto::HEALTH_HEALTHY);
    inst.set_weight(5);

    bool ok = reg.Register(inst);
    EXPECT_TRUE(ok);

    auto discovered = reg.Discover("payment", true);
    ASSERT_EQ(discovered.size(), 1ULL);
    EXPECT_EQ(discovered[0].instance_id(), "inst-payment-1");
    EXPECT_EQ(discovered[0].port(), 9001U);
    EXPECT_EQ(discovered[0].weight(), 5U);
    EXPECT_EQ(discovered[0].owner_node_id(), "agent-reg-1");
}

TEST(ServiceRegistryTest, DeregisterCreatesTombstone) {
    AgentConfig cfg;
    NodeIdentity id(cfg);
    auto queue = std::make_shared<DisseminationQueue>();
    ServiceRegistry reg(cfg, id, queue);

    proto::ServiceInstanceRecord inst;
    inst.set_instance_id("inst-orders-1");
    inst.set_service_name("orders");
    inst.set_address("127.0.0.1");
    inst.set_port(9002);
    inst.set_health(proto::HEALTH_HEALTHY);
    reg.Register(inst);

    EXPECT_EQ(reg.Discover("orders", true).size(), 1ULL);

    bool dereg = reg.Deregister("orders", "inst-orders-1");
    EXPECT_TRUE(dereg);

    // Should no longer appear in discover
    EXPECT_EQ(reg.Discover("orders", false).size(), 0ULL);

    // But should be in tombstone list
    auto all = reg.GetAllInstances(true);
    ASSERT_EQ(all.size(), 1ULL);
    EXPECT_TRUE(all[0].deleted());
}

TEST(ServiceRegistryTest, ConflictResolutionPrecedence) {
    AgentConfig cfg;
    NodeIdentity id(cfg);
    auto queue = std::make_shared<DisseminationQueue>();
    ServiceRegistry reg(cfg, id, queue);

    proto::ServiceInstanceRecord inst1;
    inst1.set_instance_id("inst-auth-1");
    inst1.set_service_name("auth");
    inst1.set_address("127.0.0.1");
    inst1.set_port(9003);
    inst1.set_owner_node_id("other-node");
    inst1.set_owner_incarnation(1);
    inst1.set_record_version(1);
    inst1.set_health(proto::HEALTH_HEALTHY);

    proto::ServiceUpdate up1;
    up1.set_instance_id("inst-auth-1");
    up1.set_service_name("auth");
    *up1.mutable_instance() = inst1;

    EXPECT_TRUE(reg.ApplyServiceUpdate(up1));
    EXPECT_EQ(reg.Discover("auth", true).size(), 1ULL);

    // Lower owner incarnation is rejected
    proto::ServiceUpdate up_older = up1;
    up_older.mutable_instance()->set_owner_incarnation(0);
    EXPECT_FALSE(reg.ApplyServiceUpdate(up_older));

    // Equal owner incarnation with higher record_version is accepted
    proto::ServiceUpdate up_newer = up1;
    up_newer.mutable_instance()->set_record_version(2);
    up_newer.mutable_instance()->set_port(9004);
    EXPECT_TRUE(reg.ApplyServiceUpdate(up_newer));
    EXPECT_EQ(reg.Discover("auth", true)[0].port(), 9004U);
}

TEST(ServiceRegistryTest, LeaseExpirationMarksUnhealthy) {
    AgentConfig cfg;
    cfg.registry.default_lease_seconds = 1; // 1 second lease
    NodeIdentity id(cfg);
    auto queue = std::make_shared<DisseminationQueue>();
    ServiceRegistry reg(cfg, id, queue);

    proto::ServiceInstanceRecord inst;
    inst.set_instance_id("inst-expiring-1");
    inst.set_service_name("ephemeral");
    inst.set_address("127.0.0.1");
    inst.set_port(9005);
    inst.set_health(proto::HEALTH_HEALTHY);
    reg.Register(inst);

    EXPECT_EQ(reg.Discover("ephemeral", true).size(), 1ULL);

    // Wait for lease to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    reg.CheckLeaseExpirations();

    // Now it should be excluded from active healthy discovery
    EXPECT_EQ(reg.Discover("ephemeral", true).size(), 0ULL);
}
