#include <gtest/gtest.h>
#include "core/storage/sqlite_storage.hpp"
#include <filesystem>

using namespace dicp;

TEST(SqliteStorageTest, SaveAndLoadNodes) {
    std::string test_db = "./data/test_node.db";
    std::filesystem::remove(test_db);

    {
        SqliteStorage storage(test_db);
        proto::NodeRecord node;
        node.set_node_id("storage-node-1");
        node.set_boot_id("boot-uuid-xyz");
        node.set_hostname("host-alpha");
        node.set_advertised_address("10.0.0.5");
        node.set_gossip_port(7946);
        node.set_sync_port(7947);
        node.set_management_port(8080);
        node.set_status(proto::NODE_ALIVE);
        node.set_incarnation(7);
        node.set_record_version(12);
        node.set_last_seen_unix_ms(123456789);

        EXPECT_TRUE(storage.SaveNode(node));
    }

    // Reopen database from disk and load
    {
        SqliteStorage storage(test_db);
        auto nodes = storage.LoadNodes();
        ASSERT_EQ(nodes.size(), 1ULL);
        EXPECT_EQ(nodes[0].node_id(), "storage-node-1");
        EXPECT_EQ(nodes[0].boot_id(), "boot-uuid-xyz");
        EXPECT_EQ(nodes[0].hostname(), "host-alpha");
        EXPECT_EQ(nodes[0].incarnation(), 7ULL);
        EXPECT_EQ(nodes[0].status(), proto::NODE_ALIVE);
    }

    std::filesystem::remove(test_db);
}

TEST(SqliteStorageTest, SaveAndLoadServiceInstances) {
    std::string test_db = "./data/test_services.db";
    std::filesystem::remove(test_db);

    {
        SqliteStorage storage(test_db);
        proto::ServiceInstanceRecord inst;
        inst.set_instance_id("inst-db-1");
        inst.set_service_name("billing");
        inst.set_owner_node_id("node-1");
        inst.set_address("10.0.0.10");
        inst.set_port(8085);
        inst.set_protocol(proto::PROTOCOL_HTTP);
        inst.set_health(proto::HEALTH_HEALTHY);
        inst.set_owner_incarnation(2);
        inst.set_record_version(5);
        inst.set_lease_expiry_unix_sec(999999);
        inst.set_weight(10);

        EXPECT_TRUE(storage.SaveServiceInstance(inst));
    }

    {
        SqliteStorage storage(test_db);
        auto instances = storage.LoadServiceInstances();
        ASSERT_EQ(instances.size(), 1ULL);
        EXPECT_EQ(instances[0].instance_id(), "inst-db-1");
        EXPECT_EQ(instances[0].service_name(), "billing");
        EXPECT_EQ(instances[0].port(), 8085U);
        EXPECT_EQ(instances[0].weight(), 10U);
    }

    std::filesystem::remove(test_db);
}
