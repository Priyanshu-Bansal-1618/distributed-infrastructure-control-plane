#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <optional>
#include "core/common/config.hpp"
#include "core/agent/node_identity.hpp"
#include "core/gossip/dissemination_queue.hpp"
#include "proto/registry.pb.h"
#include "proto/common.pb.h"

namespace dicp {

class ServiceRegistry {
public:
    ServiceRegistry(const AgentConfig& config,
                    NodeIdentity& identity,
                    std::shared_ptr<DisseminationQueue> queue);

    // Local service operations
    bool Register(proto::ServiceInstanceRecord instance);
    bool Deregister(const std::string& service_name, const std::string& instance_id);
    bool RenewLease(const std::string& service_name, const std::string& instance_id, uint32_t lease_seconds);
    bool UpdateHealth(const std::string& service_name, const std::string& instance_id, proto::HealthStatus health);

    // Anti-entropy / Gossip incoming update merge
    bool ApplyServiceUpdate(const proto::ServiceUpdate& update);

    // Queries
    std::vector<proto::ServiceInstanceRecord> Discover(const std::string& service_name, bool healthy_only = true) const;
    std::vector<proto::ServiceInstanceRecord> GetAllInstances(bool include_deleted = false) const;
    std::optional<proto::ServiceInstanceRecord> GetInstance(const std::string& service_name, const std::string& instance_id) const;
    std::vector<std::string> GetServiceNames() const;

    // Background housekeeping
    void CheckLeaseExpirations();
    void PurgeExpiredTombstones();

    // Node failure eviction
    void OnNodeFailedOrLeft(const std::string& node_id);

    size_t ActiveInstanceCount() const;

private:
    std::string MakeKey(const std::string& service_name, const std::string& instance_id) const {
        return service_name + "/" + instance_id;
    }

    const AgentConfig& config_;
    NodeIdentity& identity_;
    std::shared_ptr<DisseminationQueue> queue_;

    mutable std::shared_mutex mutex_;
    // Key: service_name/instance_id
    std::unordered_map<std::string, proto::ServiceInstanceRecord> instances_;
};

} // namespace dicp
