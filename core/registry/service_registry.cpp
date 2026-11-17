#include "service_registry.hpp"
#include "core/common/logger.hpp"
#include "core/common/types.hpp"

namespace dicp {

ServiceRegistry::ServiceRegistry(const AgentConfig& config,
                                 NodeIdentity& identity,
                                 std::shared_ptr<DisseminationQueue> queue)
    : config_(config),
      identity_(identity),
      queue_(queue) {}

bool ServiceRegistry::Register(proto::ServiceInstanceRecord instance) {
    if (instance.service_name().empty() || instance.instance_id().empty()) {
        DICP_LOG_WARN("ServiceRegistry", "Registration rejected: empty service_name or instance_id");
        return false;
    }

    instance.set_owner_node_id(identity_.NodeId());
    instance.set_owner_incarnation(identity_.Incarnation());
    instance.set_record_version(instance.record_version() + 1);
    instance.set_deleted(false);
    instance.set_deleted_at_unix_sec(0);

    uint64_t now_sec = CurrentTimeSec();
    uint64_t lease_sec = config_.registry.default_lease_seconds;
    if (instance.lease_expiry_unix_sec() > 0 && instance.lease_expiry_unix_sec() > now_sec) {
        // already has expiry
    } else {
        instance.set_lease_expiry_unix_sec(now_sec + lease_sec);
    }

    if (instance.weight() == 0) {
        instance.set_weight(1);
    }

    std::string key = MakeKey(instance.service_name(), instance.instance_id());
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        instances_[key] = instance;
    }

    DICP_LOG_INFO("ServiceRegistry", "Registered service: " + instance.service_name() + 
                  " (id=" + instance.instance_id() + ", port=" + std::to_string(instance.port()) + ")");

    if (queue_) {
        proto::ServiceUpdate up;
        up.set_instance_id(instance.instance_id());
        up.set_service_name(instance.service_name());
        up.set_owner_node_id(identity_.NodeId());
        *up.mutable_instance() = instance;
        up.set_is_tombstone(false);
        queue_->Enqueue(up);
    }

    return true;
}

bool ServiceRegistry::Deregister(const std::string& service_name, const std::string& instance_id) {
    std::string key = MakeKey(service_name, instance_id);
    proto::ServiceInstanceRecord tombstone;

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = instances_.find(key);
        if (it == instances_.end()) return false;

        auto& inst = it->second;
        inst.set_deleted(true);
        inst.set_deleted_at_unix_sec(CurrentTimeSec());
        inst.set_record_version(inst.record_version() + 1);
        inst.set_health(proto::HEALTH_UNHEALTHY);
        tombstone = inst;
    }

    DICP_LOG_INFO("ServiceRegistry", "Deregistered service instance: " + key + " (created tombstone)");

    if (queue_) {
        proto::ServiceUpdate up;
        up.set_instance_id(instance_id);
        up.set_service_name(service_name);
        up.set_owner_node_id(tombstone.owner_node_id());
        *up.mutable_instance() = tombstone;
        up.set_is_tombstone(true);
        queue_->Enqueue(up);
    }

    return true;
}

bool ServiceRegistry::RenewLease(const std::string& service_name, const std::string& instance_id, uint32_t lease_seconds) {
    std::string key = MakeKey(service_name, instance_id);
    proto::ServiceInstanceRecord updated;

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = instances_.find(key);
        if (it == instances_.end() || it->second.deleted()) return false;

        auto& inst = it->second;
        uint32_t dur = (lease_seconds > 0) ? lease_seconds : config_.registry.default_lease_seconds;
        inst.set_lease_expiry_unix_sec(CurrentTimeSec() + dur);
        inst.set_record_version(inst.record_version() + 1);
        updated = inst;
    }

    if (queue_) {
        proto::ServiceUpdate up;
        up.set_instance_id(instance_id);
        up.set_service_name(service_name);
        up.set_owner_node_id(updated.owner_node_id());
        *up.mutable_instance() = updated;
        up.set_is_tombstone(false);
        queue_->Enqueue(up);
    }

    return true;
}

bool ServiceRegistry::UpdateHealth(const std::string& service_name, const std::string& instance_id, proto::HealthStatus health) {
    std::string key = MakeKey(service_name, instance_id);
    proto::ServiceInstanceRecord updated;
    bool changed = false;

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = instances_.find(key);
        if (it == instances_.end() || it->second.deleted()) return false;

        auto& inst = it->second;
        if (inst.health() != health) {
            inst.set_health(health);
            inst.set_record_version(inst.record_version() + 1);
            updated = inst;
            changed = true;
        }
    }

    if (changed && queue_) {
        proto::ServiceUpdate up;
        up.set_instance_id(instance_id);
        up.set_service_name(service_name);
        up.set_owner_node_id(updated.owner_node_id());
        *up.mutable_instance() = updated;
        up.set_is_tombstone(false);
        queue_->Enqueue(up);
    }

    return true;
}

bool ServiceRegistry::ApplyServiceUpdate(const proto::ServiceUpdate& update) {
    const auto& incoming = update.instance();
    std::string key = MakeKey(update.service_name(), update.instance_id());

    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = instances_.find(key);

    if (it == instances_.end()) {
        instances_[key] = incoming;
        DICP_LOG_INFO("ServiceRegistry", "Learned new service instance from peer: " + key +
                      " (deleted=" + std::to_string(incoming.deleted()) + ")");
        return true;
    }

    auto& existing = it->second;

    // Rule 1: Higher owner incarnation wins
    if (incoming.owner_incarnation() > existing.owner_incarnation()) {
        existing = incoming;
        return true;
    }

    // Rule 2: Lower owner incarnation loses
    if (incoming.owner_incarnation() < existing.owner_incarnation()) {
        return false;
    }

    // Rule 3: Equal owner incarnation -> higher record_version wins
    if (incoming.record_version() > existing.record_version()) {
        existing = incoming;
        return true;
    }

    // Rule 4: Equal owner incarnation & equal record_version -> tombstone wins
    if (incoming.record_version() == existing.record_version()) {
        if (!existing.deleted() && incoming.deleted()) {
            existing = incoming;
            return true;
        }
    }

    return false;
}

std::vector<proto::ServiceInstanceRecord> ServiceRegistry::Discover(const std::string& service_name, bool healthy_only) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<proto::ServiceInstanceRecord> results;
    uint64_t now_sec = CurrentTimeSec();

    for (const auto& [key, inst] : instances_) {
        if (inst.service_name() != service_name) continue;
        if (inst.deleted()) continue;

        // Check lease expiration
        if (inst.lease_expiry_unix_sec() > 0 && now_sec >= inst.lease_expiry_unix_sec()) {
            continue; // Expired
        }

        if (healthy_only && inst.health() != proto::HEALTH_HEALTHY) {
            continue; // Not healthy
        }

        results.push_back(inst);
    }
    return results;
}

std::vector<proto::ServiceInstanceRecord> ServiceRegistry::GetAllInstances(bool include_deleted) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<proto::ServiceInstanceRecord> results;
    for (const auto& [key, inst] : instances_) {
        if (!include_deleted && inst.deleted()) continue;
        results.push_back(inst);
    }
    return results;
}

std::optional<proto::ServiceInstanceRecord> ServiceRegistry::GetInstance(const std::string& service_name, const std::string& instance_id) const {
    std::string key = MakeKey(service_name, instance_id);
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = instances_.find(key);
    if (it != instances_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<std::string> ServiceRegistry::GetServiceNames() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::unordered_map<std::string, bool> names;
    for (const auto& [key, inst] : instances_) {
        if (!inst.deleted()) {
            names[inst.service_name()] = true;
        }
    }
    std::vector<std::string> list;
    for (const auto& [n, _] : names) {
        list.push_back(n);
    }
    return list;
}

void ServiceRegistry::CheckLeaseExpirations() {
    uint64_t now_sec = CurrentTimeSec();
    std::vector<proto::ServiceUpdate> updates_to_enqueue;

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto& [key, inst] : instances_) {
            if (inst.owner_node_id() == identity_.NodeId() && !inst.deleted()) {
                if (inst.lease_expiry_unix_sec() > 0 && now_sec >= inst.lease_expiry_unix_sec()) {
                    if (inst.health() != proto::HEALTH_UNHEALTHY) {
                        inst.set_health(proto::HEALTH_UNHEALTHY);
                        inst.set_record_version(inst.record_version() + 1);

                        DICP_LOG_WARN("ServiceRegistry", "Lease expired for instance " + key + ". Marked UNHEALTHY.");

                        proto::ServiceUpdate up;
                        up.set_instance_id(inst.instance_id());
                        up.set_service_name(inst.service_name());
                        up.set_owner_node_id(identity_.NodeId());
                        *up.mutable_instance() = inst;
                        up.set_is_tombstone(false);
                        updates_to_enqueue.push_back(up);
                    }
                }
            }
        }
    }

    if (queue_) {
        for (const auto& up : updates_to_enqueue) {
            queue_->Enqueue(up);
        }
    }
}

void ServiceRegistry::PurgeExpiredTombstones() {
    uint64_t now_sec = CurrentTimeSec();
    uint64_t ttl = config_.registry.tombstone_ttl_seconds;

    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto it = instances_.begin(); it != instances_.end();) {
        if (it->second.deleted()) {
            if (now_sec - it->second.deleted_at_unix_sec() > ttl) {
                DICP_LOG_DEBUG("ServiceRegistry", "Purged expired tombstone: " + it->first);
                it = instances_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void ServiceRegistry::OnNodeFailedOrLeft(const std::string& node_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto& [key, inst] : instances_) {
        if (inst.owner_node_id() == node_id && !inst.deleted()) {
            inst.set_health(proto::HEALTH_UNHEALTHY);
            DICP_LOG_WARN("ServiceRegistry", "Evicted instance " + key + 
                          " from active routing due to owner node failure (" + node_id + ")");
        }
    }
}

size_t ServiceRegistry::ActiveInstanceCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [key, inst] : instances_) {
        if (!inst.deleted() && inst.health() == proto::HEALTH_HEALTHY) {
            count++;
        }
    }
    return count;
}

} // namespace dicp
