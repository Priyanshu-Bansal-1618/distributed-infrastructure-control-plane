#include "node_identity.hpp"
#include "core/common/uuid.hpp"
#include "core/common/types.hpp"
#include "core/common/logger.hpp"
#include <unistd.h>

namespace dicp {

NodeIdentity::NodeIdentity(const AgentConfig& config)
    : node_id_(config.node.id),
      boot_id_(GenerateUUID()),
      advertised_address_(config.node.advertised_address),
      gossip_port_(config.network.gossip_port),
      sync_port_(config.network.sync_port),
      management_port_(config.network.management_port),
      incarnation_(0),
      record_version_(1),
      status_(proto::NODE_ALIVE) {
    char host_buf[256];
    if (gethostname(host_buf, sizeof(host_buf)) == 0) {
        hostname_ = host_buf;
    } else {
        hostname_ = "unknown-host";
    }
}

void NodeIdentity::SetStatus(proto::NodeStatus status) {
    status_.store(status);
    record_version_++;
}

uint64_t NodeIdentity::IncrementIncarnation() {
    uint64_t next_inc = incarnation_.fetch_add(1) + 1;
    record_version_++;
    status_.store(proto::NODE_ALIVE);
    DICP_LOG_INFO("NodeIdentity", "Incarnation incremented to " + std::to_string(next_inc) + 
                  " (Refutation) for node " + node_id_);
    return next_inc;
}

uint64_t NodeIdentity::SetIncarnationIfGreater(uint64_t new_inc) {
    uint64_t curr = incarnation_.load();
    while (curr < new_inc) {
        if (incarnation_.compare_exchange_weak(curr, new_inc)) {
            record_version_++;
            return new_inc;
        }
    }
    return curr;
}

void NodeIdentity::SetMetadata(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    metadata_[key] = value;
    record_version_++;
}

std::string NodeIdentity::GetMetadata(const std::string& key) const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    auto it = metadata_.find(key);
    return (it != metadata_.end()) ? it->second : "";
}

proto::NodeRecord NodeIdentity::ToRecord() const {
    proto::NodeRecord rec;
    rec.set_node_id(node_id_);
    rec.set_boot_id(boot_id_);
    rec.set_hostname(hostname_);
    rec.set_advertised_address(advertised_address_);
    rec.set_gossip_port(gossip_port_);
    rec.set_sync_port(sync_port_);
    rec.set_management_port(management_port_);
    rec.set_incarnation(incarnation_.load());
    rec.set_status(status_.load());
    rec.set_record_version(record_version_.load());
    rec.set_last_seen_unix_ms(CurrentTimeMs());

    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        for (const auto& [k, v] : metadata_) {
            (*rec.mutable_metadata())[k] = v;
        }
    }
    return rec;
}

proto::NodeUpdate NodeIdentity::ToUpdate() const {
    proto::NodeUpdate up;
    up.set_node_id(node_id_);
    up.set_incarnation(incarnation_.load());
    up.set_status(status_.load());
    up.set_record_version(record_version_.load());
    up.set_advertised_address(advertised_address_);
    up.set_gossip_port(gossip_port_);
    up.set_sync_port(sync_port_);
    up.set_management_port(management_port_);
    return up;
}

} // namespace dicp
