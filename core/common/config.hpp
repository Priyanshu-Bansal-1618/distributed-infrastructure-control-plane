#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace dicp {

struct NodeConfig {
    std::string id = "node-001";
    std::string bind_address = "0.0.0.0";
    std::string advertised_address = "127.0.0.1";
};

struct NetworkConfig {
    uint16_t gossip_port = 7946;
    uint16_t sync_port = 7947;
    uint16_t management_port = 8080;
};

struct GossipConfig {
    uint32_t interval_ms = 500;
    uint32_t fanout = 3;
    uint32_t max_message_bytes = 1200;
    uint32_t max_pending_updates = 1000;
};

struct FailureDetectionConfig {
    uint32_t probe_interval_ms = 1000;
    uint32_t probe_timeout_ms = 500;
    uint32_t indirect_probe_count = 3;
    uint32_t suspicion_timeout_ms = 5000;
};

struct RegistryConfig {
    uint32_t default_lease_seconds = 30;
    uint32_t tombstone_ttl_seconds = 120;
    uint32_t sync_interval_ms = 5000;
};

struct HealthConfig {
    uint32_t interval_seconds = 5;
    uint32_t failure_threshold = 3;
    uint32_t recovery_threshold = 2;
    uint32_t timeout_ms = 2000;
};

struct StorageConfig {
    std::string database_path = "./data/node.db";
};

struct LoadBalancerConfig {
    std::string listen_address = "0.0.0.0";
    uint16_t listen_port = 8081;
    std::string strategy = "round_robin"; // round_robin, weighted_round_robin, least_conn, consistent_hash
    uint32_t connection_timeout_ms = 3000;
    uint32_t drain_timeout_ms = 10000;
};

struct AgentConfig {
    NodeConfig node;
    NetworkConfig network;
    GossipConfig gossip;
    FailureDetectionConfig failure;
    RegistryConfig registry;
    HealthConfig health;
    StorageConfig storage;
    LoadBalancerConfig load_balancer;
    std::vector<std::string> seeds;

    static AgentConfig LoadFromFile(const std::string& path);
    static AgentConfig ParseFromJson(const std::string& json_str);
    std::string ToJsonString(bool pretty = true) const;
    void Validate() const;
};

} // namespace dicp
