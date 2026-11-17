#include "config_manager.hpp"
#include "core/common/types.hpp"
#include "core/common/logger.hpp"

namespace dicp {

ConfigurationManager::ConfigurationManager(std::shared_ptr<DisseminationQueue> queue)
    : queue_(queue) {}

bool ConfigurationManager::Set(const std::string& config_id,
                               const std::string& key,
                               const std::string& value,
                               proto::ConfigScope scope,
                               const std::string& target,
                               const std::string& author) {
    if (config_id.empty() || key.empty()) return false;

    proto::ConfigRecord record;
    record.set_config_id(config_id);
    record.set_config_key(key);
    record.set_config_value(value);
    record.set_scope(scope);
    record.set_target(target);
    record.set_author(author);
    record.set_updated_at_unix_sec(CurrentTimeSec());

    std::string prev_val;
    uint64_t new_version = 1;

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = configs_.find(config_id);
        if (it != configs_.end()) {
            prev_val = it->second.config_value();
            new_version = it->second.version() + 1;
        }

        record.set_version(new_version);
        configs_[config_id] = record;

        history_[config_id].push_back({
            new_version,
            prev_val,
            value,
            author,
            record.updated_at_unix_sec()
        });
    }

    DICP_LOG_INFO("ConfigManager", "Published config " + config_id + 
                  " version=" + std::to_string(new_version) + " author=" + author);

    if (queue_) {
        proto::ConfigUpdate up;
        *up.mutable_record() = record;
        queue_->Enqueue(up);
    }

    return true;
}

std::optional<proto::ConfigRecord> ConfigurationManager::Get(const std::string& config_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = configs_.find(config_id);
    if (it != configs_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<proto::ConfigRecord> ConfigurationManager::GetAll() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<proto::ConfigRecord> list;
    for (const auto& [id, record] : configs_) {
        list.push_back(record);
    }
    return list;
}

std::vector<ConfigHistoryEntry> ConfigurationManager::GetHistory(const std::string& config_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = history_.find(config_id);
    if (it != history_.end()) {
        return it->second;
    }
    return {};
}

bool ConfigurationManager::Rollback(const std::string& config_id, uint64_t target_version, const std::string& author) {
    std::string rollback_value;
    std::string key;
    proto::ConfigScope scope = proto::SCOPE_CLUSTER;
    std::string target;

    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = history_.find(config_id);
        if (it == history_.end()) return false;

        bool found = false;
        for (const auto& entry : it->second) {
            if (entry.version == target_version) {
                rollback_value = entry.new_value;
                found = true;
                break;
            }
        }
        if (!found) return false;

        auto curr = configs_.find(config_id);
        if (curr != configs_.end()) {
            key = curr->second.config_key();
            scope = curr->second.scope();
            target = curr->second.target();
        }
    }

    // Rollback creates a NEW monotonic version with the historical value
    return Set(config_id, key, rollback_value, scope, target, author + " (Rollback to v" + std::to_string(target_version) + ")");
}

bool ConfigurationManager::ApplyConfigUpdate(const proto::ConfigUpdate& update) {
    const auto& incoming = update.record();
    const std::string& config_id = incoming.config_id();

    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = configs_.find(config_id);

    if (it == configs_.end()) {
        configs_[config_id] = incoming;
        history_[config_id].push_back({
            incoming.version(),
            "",
            incoming.config_value(),
            incoming.author(),
            incoming.updated_at_unix_sec()
        });
        DICP_LOG_INFO("ConfigManager", "Applied new configuration from peer: " + config_id +
                      " version=" + std::to_string(incoming.version()));
        return true;
    }

    // Monotonic version conflict resolution
    if (incoming.version() > it->second.version()) {
        std::string prev = it->second.config_value();
        it->second = incoming;
        history_[config_id].push_back({
            incoming.version(),
            prev,
            incoming.config_value(),
            incoming.author(),
            incoming.updated_at_unix_sec()
        });
        DICP_LOG_INFO("ConfigManager", "Updated configuration " + config_id + 
                      " to newer version=" + std::to_string(incoming.version()));
        return true;
    }

    return false; // Stale version rejected
}

} // namespace dicp
