#include "config.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace dicp {

using json = nlohmann::json;

AgentConfig AgentConfig::ParseFromJson(const std::string& json_str) {
    AgentConfig cfg;
    json j = json::parse(json_str);

    if (j.contains("node")) {
        const auto& n = j["node"];
        if (n.contains("id")) cfg.node.id = n["id"].get<std::string>();
        if (n.contains("bind_address")) cfg.node.bind_address = n["bind_address"].get<std::string>();
        if (n.contains("advertised_address")) cfg.node.advertised_address = n["advertised_address"].get<std::string>();
    }

    if (j.contains("network")) {
        const auto& net = j["network"];
        if (net.contains("gossip_port")) cfg.network.gossip_port = net["gossip_port"].get<uint16_t>();
        if (net.contains("sync_port")) cfg.network.sync_port = net["sync_port"].get<uint16_t>();
        if (net.contains("management_port")) cfg.network.management_port = net["management_port"].get<uint16_t>();
    }

    if (j.contains("gossip")) {
        const auto& g = j["gossip"];
        if (g.contains("interval_ms")) cfg.gossip.interval_ms = g["interval_ms"].get<uint32_t>();
        if (g.contains("fanout")) cfg.gossip.fanout = g["fanout"].get<uint32_t>();
        if (g.contains("max_message_bytes")) cfg.gossip.max_message_bytes = g["max_message_bytes"].get<uint32_t>();
        if (g.contains("max_pending_updates")) cfg.gossip.max_pending_updates = g["max_pending_updates"].get<uint32_t>();
    }

    if (j.contains("failure_detection")) {
        const auto& f = j["failure_detection"];
        if (f.contains("probe_interval_ms")) cfg.failure.probe_interval_ms = f["probe_interval_ms"].get<uint32_t>();
        if (f.contains("probe_timeout_ms")) cfg.failure.probe_timeout_ms = f["probe_timeout_ms"].get<uint32_t>();
        if (f.contains("indirect_probe_count")) cfg.failure.indirect_probe_count = f["indirect_probe_count"].get<uint32_t>();
        if (f.contains("suspicion_timeout_ms")) cfg.failure.suspicion_timeout_ms = f["suspicion_timeout_ms"].get<uint32_t>();
    }

    if (j.contains("registry")) {
        const auto& r = j["registry"];
        if (r.contains("default_lease_seconds")) cfg.registry.default_lease_seconds = r["default_lease_seconds"].get<uint32_t>();
        if (r.contains("tombstone_ttl_seconds")) cfg.registry.tombstone_ttl_seconds = r["tombstone_ttl_seconds"].get<uint32_t>();
        if (r.contains("sync_interval_ms")) cfg.registry.sync_interval_ms = r["sync_interval_ms"].get<uint32_t>();
    }

    if (j.contains("health")) {
        const auto& h = j["health"];
        if (h.contains("interval_seconds")) cfg.health.interval_seconds = h["interval_seconds"].get<uint32_t>();
        if (h.contains("failure_threshold")) cfg.health.failure_threshold = h["failure_threshold"].get<uint32_t>();
        if (h.contains("recovery_threshold")) cfg.health.recovery_threshold = h["recovery_threshold"].get<uint32_t>();
        if (h.contains("timeout_ms")) cfg.health.timeout_ms = h["timeout_ms"].get<uint32_t>();
    }

    if (j.contains("storage")) {
        const auto& s = j["storage"];
        if (s.contains("database_path")) cfg.storage.database_path = s["database_path"].get<std::string>();
    }

    if (j.contains("load_balancer")) {
        const auto& lb = j["load_balancer"];
        if (lb.contains("listen_address")) cfg.load_balancer.listen_address = lb["listen_address"].get<std::string>();
        if (lb.contains("listen_port")) cfg.load_balancer.listen_port = lb["listen_port"].get<uint16_t>();
        if (lb.contains("strategy")) cfg.load_balancer.strategy = lb["strategy"].get<std::string>();
        if (lb.contains("connection_timeout_ms")) cfg.load_balancer.connection_timeout_ms = lb["connection_timeout_ms"].get<uint32_t>();
        if (lb.contains("drain_timeout_ms")) cfg.load_balancer.drain_timeout_ms = lb["drain_timeout_ms"].get<uint32_t>();
    }

    if (j.contains("seeds") && j["seeds"].is_array()) {
        cfg.seeds.clear();
        for (const auto& item : j["seeds"]) {
            cfg.seeds.push_back(item.get<std::string>());
        }
    }

    cfg.Validate();
    return cfg;
}

AgentConfig AgentConfig::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open configuration file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return ParseFromJson(buffer.str());
}

std::string AgentConfig::ToJsonString(bool pretty) const {
    json j;
    j["node"]["id"] = node.id;
    j["node"]["bind_address"] = node.bind_address;
    j["node"]["advertised_address"] = node.advertised_address;

    j["network"]["gossip_port"] = network.gossip_port;
    j["network"]["sync_port"] = network.sync_port;
    j["network"]["management_port"] = network.management_port;

    j["gossip"]["interval_ms"] = gossip.interval_ms;
    j["gossip"]["fanout"] = gossip.fanout;
    j["gossip"]["max_message_bytes"] = gossip.max_message_bytes;
    j["gossip"]["max_pending_updates"] = gossip.max_pending_updates;

    j["failure_detection"]["probe_interval_ms"] = failure.probe_interval_ms;
    j["failure_detection"]["probe_timeout_ms"] = failure.probe_timeout_ms;
    j["failure_detection"]["indirect_probe_count"] = failure.indirect_probe_count;
    j["failure_detection"]["suspicion_timeout_ms"] = failure.suspicion_timeout_ms;

    j["registry"]["default_lease_seconds"] = registry.default_lease_seconds;
    j["registry"]["tombstone_ttl_seconds"] = registry.tombstone_ttl_seconds;
    j["registry"]["sync_interval_ms"] = registry.sync_interval_ms;

    j["health"]["interval_seconds"] = health.interval_seconds;
    j["health"]["failure_threshold"] = health.failure_threshold;
    j["health"]["recovery_threshold"] = health.recovery_threshold;
    j["health"]["timeout_ms"] = health.timeout_ms;

    j["storage"]["database_path"] = storage.database_path;

    j["load_balancer"]["listen_address"] = load_balancer.listen_address;
    j["load_balancer"]["listen_port"] = load_balancer.listen_port;
    j["load_balancer"]["strategy"] = load_balancer.strategy;
    j["load_balancer"]["connection_timeout_ms"] = load_balancer.connection_timeout_ms;
    j["load_balancer"]["drain_timeout_ms"] = load_balancer.drain_timeout_ms;

    j["seeds"] = seeds;

    return pretty ? j.dump(2) : j.dump();
}

void AgentConfig::Validate() const {
    if (node.id.empty()) {
        throw std::invalid_argument("node.id must not be empty");
    }
    if (network.gossip_port == 0) {
        throw std::invalid_argument("network.gossip_port must be > 0");
    }
    if (gossip.max_message_bytes > 65507 || gossip.max_message_bytes < 64) {
        throw std::invalid_argument("gossip.max_message_bytes must be between 64 and 65507");
    }
    if (failure.probe_timeout_ms >= failure.probe_interval_ms && failure.probe_timeout_ms > 0) {
        // Warning or validation
    }
    if (registry.default_lease_seconds == 0) {
        throw std::invalid_argument("registry.default_lease_seconds must be > 0");
    }
}

} // namespace dicp
