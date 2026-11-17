#pragma once

#include <string>
#include <map>
#include <atomic>
#include <mutex>
#include "core/common/config.hpp"
#include "proto/membership.pb.h"
#include "proto/common.pb.h"

namespace dicp {

class NodeIdentity {
public:
    explicit NodeIdentity(const AgentConfig& config);

    const std::string& NodeId() const { return node_id_; }
    const std::string& BootId() const { return boot_id_; }
    const std::string& Hostname() const { return hostname_; }
    const std::string& AdvertisedAddress() const { return advertised_address_; }
    uint16_t GossipPort() const { return gossip_port_; }
    uint16_t SyncPort() const { return sync_port_; }
    uint16_t ManagementPort() const { return management_port_; }

    uint64_t Incarnation() const { return incarnation_.load(); }
    uint64_t RecordVersion() const { return record_version_.load(); }
    proto::NodeStatus Status() const { return status_.load(); }

    void SetStatus(proto::NodeStatus status);
    uint64_t IncrementIncarnation();
    uint64_t SetIncarnationIfGreater(uint64_t new_inc);

    void SetMetadata(const std::string& key, const std::string& value);
    std::string GetMetadata(const std::string& key) const;

    proto::NodeRecord ToRecord() const;
    proto::NodeUpdate ToUpdate() const;

private:
    std::string node_id_;
    std::string boot_id_;
    std::string hostname_;
    std::string advertised_address_;
    uint16_t gossip_port_;
    uint16_t sync_port_;
    uint16_t management_port_;

    std::atomic<uint64_t> incarnation_{0};
    std::atomic<uint64_t> record_version_{0};
    std::atomic<proto::NodeStatus> status_{proto::NODE_STATUS_UNKNOWN};

    mutable std::mutex metadata_mutex_;
    std::map<std::string, std::string> metadata_;
};

} // namespace dicp
