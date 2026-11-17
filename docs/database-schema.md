# DICP Local SQLite Persistence Schema

## 1. Design & Threading Model

Every Node Agent manages its own local SQLite database file (e.g. `./data/node.db`).
- **Isolation**: SQLite is strictly node-local. It is never accessed directly over the network and never shared between processes.
- **Asynchronous Execution**: SQLite operations are dispatched to a dedicated background storage thread via a thread-safe message queue, ensuring the Boost.Asio I/O event loop is never blocked by disk I/O.
- **WAL Mode**: SQLite is configured in Write-Ahead Logging (`PRAGMA journal_mode=WAL;`) for optimal concurrent read performance.

---

## 2. DDL Schema

```sql
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;

-- Cluster Node state
CREATE TABLE IF NOT EXISTS nodes (
    node_id TEXT PRIMARY KEY,
    boot_id TEXT NOT NULL,
    hostname TEXT,
    advertised_address TEXT NOT NULL,
    gossip_port INTEGER NOT NULL,
    sync_port INTEGER NOT NULL,
    management_port INTEGER NOT NULL,
    status TEXT NOT NULL,
    incarnation INTEGER NOT NULL,
    record_version INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

-- Replicated Service Instances
CREATE TABLE IF NOT EXISTS service_instances (
    instance_id TEXT PRIMARY KEY,
    service_name TEXT NOT NULL,
    owner_node_id TEXT NOT NULL,
    address TEXT NOT NULL,
    port INTEGER NOT NULL,
    protocol TEXT NOT NULL,
    health_status TEXT NOT NULL,
    owner_incarnation INTEGER NOT NULL,
    record_version INTEGER NOT NULL,
    lease_expiry INTEGER NOT NULL,
    metadata TEXT,
    deleted INTEGER DEFAULT 0,
    deleted_at INTEGER DEFAULT 0,
    updated_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_services_name ON service_instances(service_name);
CREATE INDEX IF NOT EXISTS idx_services_owner ON service_instances(owner_node_id);
CREATE INDEX IF NOT EXISTS idx_services_lease ON service_instances(lease_expiry);

-- Versioned Configurations
CREATE TABLE IF NOT EXISTS configurations (
    config_id TEXT PRIMARY KEY,
    config_key TEXT NOT NULL,
    config_value TEXT NOT NULL,
    scope TEXT NOT NULL,
    target TEXT,
    version INTEGER NOT NULL,
    author TEXT,
    updated_at INTEGER NOT NULL
);

-- Configuration Audit History
CREATE TABLE IF NOT EXISTS configuration_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    config_id TEXT NOT NULL,
    version INTEGER NOT NULL,
    previous_value TEXT,
    new_value TEXT,
    author TEXT,
    updated_at INTEGER NOT NULL
);
```
