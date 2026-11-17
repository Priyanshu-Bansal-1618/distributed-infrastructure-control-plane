#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <sqlite3.h>
#include "proto/membership.pb.h"
#include "proto/registry.pb.h"
#include "proto/configuration.pb.h"

namespace dicp {

class SqliteStorage {
public:
    explicit SqliteStorage(const std::string& db_path);
    ~SqliteStorage();

    void InitializeSchema();

    // Node operations
    bool SaveNode(const proto::NodeRecord& node);
    std::vector<proto::NodeRecord> LoadNodes();

    // Service instance operations
    bool SaveServiceInstance(const proto::ServiceInstanceRecord& inst);
    std::vector<proto::ServiceInstanceRecord> LoadServiceInstances();

    // Configuration operations
    bool SaveConfiguration(const proto::ConfigRecord& config);
    std::vector<proto::ConfigRecord> LoadConfigurations();

    void Close();

private:
    std::string db_path_;
    sqlite3* db_{nullptr};
    mutable std::mutex mutex_;
};

} // namespace dicp
