#include "sqlite_storage.hpp"
#include "core/common/logger.hpp"
#include <filesystem>
#include <stdexcept>

namespace dicp {

SqliteStorage::SqliteStorage(const std::string& db_path)
    : db_path_(db_path) {
    auto dir = std::filesystem::path(db_path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "Failed to open sqlite db";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("SQLite error: " + err);
    }

    InitializeSchema();
}

SqliteStorage::~SqliteStorage() {
    Close();
}

void SqliteStorage::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void SqliteStorage::InitializeSchema() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;

    const char* schema = R"(
        PRAGMA journal_mode = WAL;
        PRAGMA synchronous = NORMAL;

        CREATE TABLE IF NOT EXISTS nodes (
            node_id TEXT PRIMARY KEY,
            boot_id TEXT NOT NULL,
            hostname TEXT,
            advertised_address TEXT NOT NULL,
            gossip_port INTEGER NOT NULL,
            sync_port INTEGER NOT NULL,
            management_port INTEGER NOT NULL,
            status INTEGER NOT NULL,
            incarnation INTEGER NOT NULL,
            record_version INTEGER NOT NULL,
            last_seen INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS service_instances (
            instance_id TEXT PRIMARY KEY,
            service_name TEXT NOT NULL,
            owner_node_id TEXT NOT NULL,
            address TEXT NOT NULL,
            port INTEGER NOT NULL,
            protocol INTEGER NOT NULL,
            health_status INTEGER NOT NULL,
            owner_incarnation INTEGER NOT NULL,
            record_version INTEGER NOT NULL,
            lease_expiry INTEGER NOT NULL,
            weight INTEGER NOT NULL,
            deleted INTEGER DEFAULT 0,
            deleted_at INTEGER DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_services_name ON service_instances(service_name);
        CREATE INDEX IF NOT EXISTS idx_services_owner ON service_instances(owner_node_id);

        CREATE TABLE IF NOT EXISTS configurations (
            config_id TEXT PRIMARY KEY,
            config_key TEXT NOT NULL,
            config_value TEXT NOT NULL,
            scope INTEGER NOT NULL,
            target TEXT,
            version INTEGER NOT NULL,
            author TEXT,
            updated_at INTEGER NOT NULL
        );
    )";

    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = errmsg ? errmsg : "Unknown error";
        sqlite3_free(errmsg);
        DICP_LOG_ERROR("SqliteStorage", "Failed to initialize schema: " + err);
    } else {
        DICP_LOG_INFO("SqliteStorage", "Initialized SQLite schema at " + db_path_);
    }
}

bool SqliteStorage::SaveNode(const proto::NodeRecord& node) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql = R"(
        INSERT INTO nodes (node_id, boot_id, hostname, advertised_address, gossip_port, sync_port, 
                           management_port, status, incarnation, record_version, last_seen)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(node_id) DO UPDATE SET
            boot_id = excluded.boot_id,
            hostname = excluded.hostname,
            advertised_address = excluded.advertised_address,
            gossip_port = excluded.gossip_port,
            sync_port = excluded.sync_port,
            management_port = excluded.management_port,
            status = excluded.status,
            incarnation = excluded.incarnation,
            record_version = excluded.record_version,
            last_seen = excluded.last_seen;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, node.node_id().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, node.boot_id().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, node.hostname().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, node.advertised_address().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, node.gossip_port());
    sqlite3_bind_int(stmt, 6, node.sync_port());
    sqlite3_bind_int(stmt, 7, node.management_port());
    sqlite3_bind_int(stmt, 8, static_cast<int>(node.status()));
    sqlite3_bind_int64(stmt, 9, node.incarnation());
    sqlite3_bind_int64(stmt, 10, node.record_version());
    sqlite3_bind_int64(stmt, 11, node.last_seen_unix_ms());

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<proto::NodeRecord> SqliteStorage::LoadNodes() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<proto::NodeRecord> list;
    if (!db_) return list;

    const char* sql = "SELECT node_id, boot_id, hostname, advertised_address, gossip_port, sync_port, "
                      "management_port, status, incarnation, record_version, last_seen FROM nodes;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        proto::NodeRecord rec;
        rec.set_node_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        rec.set_boot_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        rec.set_hostname(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        rec.set_advertised_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        rec.set_gossip_port(sqlite3_column_int(stmt, 4));
        rec.set_sync_port(sqlite3_column_int(stmt, 5));
        rec.set_management_port(sqlite3_column_int(stmt, 6));
        rec.set_status(static_cast<proto::NodeStatus>(sqlite3_column_int(stmt, 7)));
        rec.set_incarnation(sqlite3_column_int64(stmt, 8));
        rec.set_record_version(sqlite3_column_int64(stmt, 9));
        rec.set_last_seen_unix_ms(sqlite3_column_int64(stmt, 10));
        list.push_back(rec);
    }

    sqlite3_finalize(stmt);
    return list;
}

bool SqliteStorage::SaveServiceInstance(const proto::ServiceInstanceRecord& inst) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql = R"(
        INSERT INTO service_instances (instance_id, service_name, owner_node_id, address, port, protocol,
                                       health_status, owner_incarnation, record_version, lease_expiry, weight, deleted, deleted_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(instance_id) DO UPDATE SET
            service_name = excluded.service_name,
            owner_node_id = excluded.owner_node_id,
            address = excluded.address,
            port = excluded.port,
            protocol = excluded.protocol,
            health_status = excluded.health_status,
            owner_incarnation = excluded.owner_incarnation,
            record_version = excluded.record_version,
            lease_expiry = excluded.lease_expiry,
            weight = excluded.weight,
            deleted = excluded.deleted,
            deleted_at = excluded.deleted_at;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, inst.instance_id().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, inst.service_name().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, inst.owner_node_id().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, inst.address().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, inst.port());
    sqlite3_bind_int(stmt, 6, static_cast<int>(inst.protocol()));
    sqlite3_bind_int(stmt, 7, static_cast<int>(inst.health()));
    sqlite3_bind_int64(stmt, 8, inst.owner_incarnation());
    sqlite3_bind_int64(stmt, 9, inst.record_version());
    sqlite3_bind_int64(stmt, 10, inst.lease_expiry_unix_sec());
    sqlite3_bind_int(stmt, 11, inst.weight());
    sqlite3_bind_int(stmt, 12, inst.deleted() ? 1 : 0);
    sqlite3_bind_int64(stmt, 13, inst.deleted_at_unix_sec());

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<proto::ServiceInstanceRecord> SqliteStorage::LoadServiceInstances() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<proto::ServiceInstanceRecord> list;
    if (!db_) return list;

    const char* sql = "SELECT instance_id, service_name, owner_node_id, address, port, protocol, "
                      "health_status, owner_incarnation, record_version, lease_expiry, weight, deleted, deleted_at FROM service_instances;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        proto::ServiceInstanceRecord inst;
        inst.set_instance_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        inst.set_service_name(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        inst.set_owner_node_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        inst.set_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        inst.set_port(sqlite3_column_int(stmt, 4));
        inst.set_protocol(static_cast<proto::ServiceProtocol>(sqlite3_column_int(stmt, 5)));
        inst.set_health(static_cast<proto::HealthStatus>(sqlite3_column_int(stmt, 6)));
        inst.set_owner_incarnation(sqlite3_column_int64(stmt, 7));
        inst.set_record_version(sqlite3_column_int64(stmt, 8));
        inst.set_lease_expiry_unix_sec(sqlite3_column_int64(stmt, 9));
        inst.set_weight(sqlite3_column_int(stmt, 10));
        inst.set_deleted(sqlite3_column_int(stmt, 11) != 0);
        inst.set_deleted_at_unix_sec(sqlite3_column_int64(stmt, 12));
        list.push_back(inst);
    }

    sqlite3_finalize(stmt);
    return list;
}

bool SqliteStorage::SaveConfiguration(const proto::ConfigRecord& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql = R"(
        INSERT INTO configurations (config_id, config_key, config_value, scope, target, version, author, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(config_id) DO UPDATE SET
            config_key = excluded.config_key,
            config_value = excluded.config_value,
            scope = excluded.scope,
            target = excluded.target,
            version = excluded.version,
            author = excluded.author,
            updated_at = excluded.updated_at;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, config.config_id().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, config.config_key().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, config.config_value().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, static_cast<int>(config.scope()));
    sqlite3_bind_text(stmt, 5, config.target().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, config.version());
    sqlite3_bind_text(stmt, 7, config.author().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 8, config.updated_at_unix_sec());

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<proto::ConfigRecord> SqliteStorage::LoadConfigurations() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<proto::ConfigRecord> list;
    if (!db_) return list;

    const char* sql = "SELECT config_id, config_key, config_value, scope, target, version, author, updated_at FROM configurations;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        proto::ConfigRecord rec;
        rec.set_config_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        rec.set_config_key(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        rec.set_config_value(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        rec.set_scope(static_cast<proto::ConfigScope>(sqlite3_column_int(stmt, 3)));
        rec.set_target(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
        rec.set_version(sqlite3_column_int64(stmt, 5));
        rec.set_author(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
        rec.set_updated_at_unix_sec(sqlite3_column_int64(stmt, 7));
        list.push_back(rec);
    }

    sqlite3_finalize(stmt);
    return list;
}

} // namespace dicp
