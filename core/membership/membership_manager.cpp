#include "membership_manager.hpp"
#include "core/common/logger.hpp"
#include "core/common/types.hpp"
#include <random>
#include <algorithm>

namespace dicp {

MembershipManager::MembershipManager(NodeIdentity& local_identity)
    : local_identity_(local_identity) {
    AddOrUpdateMember(local_identity.ToRecord());
}

void MembershipManager::AddOrUpdateMember(const proto::NodeRecord& record) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    members_[record.node_id()] = record;
}

std::optional<proto::NodeRecord> MembershipManager::GetMember(const std::string& node_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = members_.find(node_id);
    if (it != members_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<proto::NodeRecord> MembershipManager::GetAllMembers() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<proto::NodeRecord> list;
    list.reserve(members_.size());
    for (const auto& [id, record] : members_) {
        list.push_back(record);
    }
    return list;
}

std::vector<proto::NodeRecord> MembershipManager::GetAliveMembers(bool exclude_self) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<proto::NodeRecord> list;
    for (const auto& [id, record] : members_) {
        if (exclude_self && id == local_identity_.NodeId()) continue;
        if (record.status() == proto::NODE_ALIVE) {
            list.push_back(record);
        }
    }
    return list;
}

std::vector<proto::NodeRecord> MembershipManager::GetRandomPeers(size_t count, const std::string& exclude_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<proto::NodeRecord> candidates;
    for (const auto& [id, record] : members_) {
        if (id == local_identity_.NodeId() || id == exclude_id) continue;
        if (record.status() == proto::NODE_ALIVE || record.status() == proto::NODE_SUSPECT) {
            candidates.push_back(record);
        }
    }

    if (candidates.empty()) return {};

    static thread_local std::mt19937 g(std::random_device{}());
    std::shuffle(candidates.begin(), candidates.end(), g);

    if (candidates.size() > count) {
        candidates.resize(count);
    }
    return candidates;
}

size_t MembershipManager::MemberCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return members_.size();
}

size_t MembershipManager::AliveCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [id, record] : members_) {
        if (record.status() == proto::NODE_ALIVE) {
            count++;
        }
    }
    return count;
}

std::pair<bool, std::optional<proto::NodeUpdate>> MembershipManager::ApplyUpdate(const proto::NodeUpdate& update) {
    const std::string& target_id = update.node_id();

    // Check if this update claims something about our local node
    if (target_id == local_identity_.NodeId()) {
        if (update.status() == proto::NODE_SUSPECT || update.status() == proto::NODE_DEAD) {
            // Suspicion / Death refuted!
            uint64_t target_inc = std::max(local_identity_.Incarnation(), update.incarnation()) + 1;
            local_identity_.SetIncarnationIfGreater(target_inc);
            local_identity_.SetStatus(proto::NODE_ALIVE);

            DICP_LOG_INFO("MembershipManager", "Refuting false " + 
                          std::string(update.status() == proto::NODE_SUSPECT ? "SUSPECT" : "DEAD") +
                          " claim against local node. New incarnation=" + std::to_string(target_inc));

            auto local_update = local_identity_.ToUpdate();
            AddOrUpdateMember(local_identity_.ToRecord());
            return {true, local_update};
        }
        return {false, std::nullopt};
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = members_.find(target_id);

    if (it == members_.end()) {
        // Discovered brand new node
        proto::NodeRecord new_rec;
        new_rec.set_node_id(target_id);
        new_rec.set_advertised_address(update.advertised_address());
        new_rec.set_gossip_port(update.gossip_port());
        new_rec.set_sync_port(update.sync_port());
        new_rec.set_management_port(update.management_port());
        new_rec.set_incarnation(update.incarnation());
        new_rec.set_status(update.status());
        new_rec.set_record_version(update.record_version());
        new_rec.set_last_seen_unix_ms(CurrentTimeMs());

        members_[target_id] = new_rec;
        DICP_LOG_INFO("MembershipManager", "Discovered new member: " + target_id + 
                      " status=" + std::to_string(update.status()) + " inc=" + std::to_string(update.incarnation()));
        return {true, std::nullopt};
    }

    auto& existing = it->second;

    // Rule 1: Higher incarnation always wins
    if (update.incarnation() > existing.incarnation()) {
        existing.set_incarnation(update.incarnation());
        existing.set_status(update.status());
        existing.set_record_version(update.record_version());
        if (!update.advertised_address().empty()) {
            existing.set_advertised_address(update.advertised_address());
        }
        if (update.gossip_port() > 0) existing.set_gossip_port(update.gossip_port());
        if (update.sync_port() > 0) existing.set_sync_port(update.sync_port());
        if (update.management_port() > 0) existing.set_management_port(update.management_port());
        existing.set_last_seen_unix_ms(CurrentTimeMs());

        DICP_LOG_INFO("MembershipManager", "Updated member " + target_id + 
                      " to higher incarnation=" + std::to_string(update.incarnation()) +
                      " status=" + std::to_string(update.status()));
        return {true, std::nullopt};
    }

    // Rule 2: Older incarnation is rejected
    if (update.incarnation() < existing.incarnation()) {
        return {false, std::nullopt};
    }

    // Rule 3: Equal incarnation conflict resolution
    // Precedence: DEAD > SUSPECT > ALIVE; LEFT is absorbing
    bool state_changed = false;

    if (update.status() == proto::NODE_LEFT && existing.status() != proto::NODE_LEFT) {
        existing.set_status(proto::NODE_LEFT);
        state_changed = true;
    } else if (existing.status() == proto::NODE_ALIVE && update.status() == proto::NODE_SUSPECT) {
        existing.set_status(proto::NODE_SUSPECT);
        state_changed = true;
    } else if (existing.status() == proto::NODE_SUSPECT && update.status() == proto::NODE_DEAD) {
        existing.set_status(proto::NODE_DEAD);
        state_changed = true;
    } else if (existing.status() == proto::NODE_ALIVE && update.status() == proto::NODE_DEAD) {
        existing.set_status(proto::NODE_DEAD);
        state_changed = true;
    }

    if (state_changed) {
        existing.set_record_version(std::max(existing.record_version(), update.record_version()) + 1);
        existing.set_last_seen_unix_ms(CurrentTimeMs());
        DICP_LOG_INFO("MembershipManager", "Member " + target_id + 
                      " transitioned to status=" + std::to_string(existing.status()) + 
                      " (equal inc=" + std::to_string(existing.incarnation()) + ")");
    }

    return {state_changed, std::nullopt};
}

void MembershipManager::MarkDeadIfSuspect(const std::string& node_id, uint64_t expected_incarnation) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = members_.find(node_id);
    if (it != members_.end()) {
        auto& record = it->second;
        if (record.incarnation() == expected_incarnation && record.status() == proto::NODE_SUSPECT) {
            record.set_status(proto::NODE_DEAD);
            record.set_record_version(record.record_version() + 1);
            record.set_last_seen_unix_ms(CurrentTimeMs());
            DICP_LOG_WARN("MembershipManager", "Suspicion timeout expired for node " + node_id +
                          ". Marked DEAD at incarnation=" + std::to_string(expected_incarnation));
        }
    }
}

} // namespace dicp
