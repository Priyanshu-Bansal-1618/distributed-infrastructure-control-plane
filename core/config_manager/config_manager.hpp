#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <optional>
#include "core/gossip/dissemination_queue.hpp"
#include "proto/configuration.pb.h"

namespace dicp {

struct ConfigHistoryEntry {
    uint64_t version;
    std::string previous_value;
    std::string new_value;
    std::string author;
    uint64_t updated_at_unix_sec;
};

class ConfigurationManager {
public:
    explicit ConfigurationManager(std::shared_ptr<DisseminationQueue> queue);

    bool Set(const std::string& config_id,
             const std::string& key,
             const std::string& value,
             proto::ConfigScope scope,
             const std::string& target,
             const std::string& author);

    std::optional<proto::ConfigRecord> Get(const std::string& config_id) const;
    std::vector<proto::ConfigRecord> GetAll() const;
    std::vector<ConfigHistoryEntry> GetHistory(const std::string& config_id) const;

    bool Rollback(const std::string& config_id, uint64_t target_version, const std::string& author);

    bool ApplyConfigUpdate(const proto::ConfigUpdate& update);

private:
    std::shared_ptr<DisseminationQueue> queue_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, proto::ConfigRecord> configs_;
    std::unordered_map<std::string, std::vector<ConfigHistoryEntry>> history_;
};

} // namespace dicp
