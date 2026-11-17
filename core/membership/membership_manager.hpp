#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include "core/agent/node_identity.hpp"
#include "proto/membership.pb.h"
#include "proto/common.pb.h"

namespace dicp {

class MembershipManager {
public:
    explicit MembershipManager(NodeIdentity& local_identity);

    // Applies an incoming update according to deterministic precedence rules.
    // Returns: pair<bool state_changed, optional<NodeUpdate> refutation>
    std::pair<bool, std::optional<proto::NodeUpdate>> ApplyUpdate(const proto::NodeUpdate& update);

    void AddOrUpdateMember(const proto::NodeRecord& record);
    std::optional<proto::NodeRecord> GetMember(const std::string& node_id) const;
    std::vector<proto::NodeRecord> GetAllMembers() const;
    std::vector<proto::NodeRecord> GetAliveMembers(bool exclude_self = true) const;
    std::vector<proto::NodeRecord> GetRandomPeers(size_t count, const std::string& exclude_id = "") const;

    size_t MemberCount() const;
    size_t AliveCount() const;

    void MarkDeadIfSuspect(const std::string& node_id, uint64_t expected_incarnation);

private:
    NodeIdentity& local_identity_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, proto::NodeRecord> members_;
};

} // namespace dicp
