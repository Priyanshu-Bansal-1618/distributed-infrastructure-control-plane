# Distributed Infrastructure Control Plane - Security Specification & Threat Model

## 1. Threat Model & Boundaries

DICP is designed for cluster orchestration inside private subnets, cloud VPCs, or container networks. The security design accounts for the following threat vectors:

| Threat Category | Potential Attack Vector | DICP Defense Mechanism |
|---|---|---|
| **Eavesdropping / Sniffing** | Unencrypted gossip or sync payloads | Isolated overlay networks, planned mTLS on TCP sync channels |
| **Message Tampering** | Bit corruption or MITM payload modification | Binary framing with Magic Bytes (`0x44494350`), payload length validation, and frame checksum |
| **Replay Attacks** | Capturing and replaying outdated death or state updates | Monotonic sequence counters, incarnation numbers, and message deduplication cache |
| **State Poisoning** | Unauthorized service registration or malicious config updates | Bearer token authentication on HTTP endpoints, parameter validation, strict versioning |
| **SQL Injection** | Malicious inputs in node IDs or service names | 100% parameterized SQLite prepared statements (`sqlite3_bind_*`) |
| **Resource Exhaustion** | Flooding UDP gossip or HTTP reverse proxy | Bounded dissemination queues, connection timeouts, and MTU limits (1200 bytes) |

---

## 2. Authentication & Authorization

### 2.1 Management API Security
* The Python FastAPI management API requires an HTTP `Authorization: Bearer <token>` header for all mutating and administrative operations (`/api/v1/cluster/services/register`, `/api/v1/cluster/config`).
* Configurable token via the `API_TOKEN` environment variable.
* Constant-time string comparison (`secrets.compare_digest`) prevents timing attacks.

### 2.2 Network Segmentation
* **Control Plane UDP Gossip (`:9000/udp`)**: Dedicated to node-to-node failure detection and gossip updates. Closed to external traffic.
* **Control Plane TCP Sync (`:9001/tcp`)**: Used exclusively for bulk anti-entropy state synchronization between nodes.
* **Node Agent Internal REST (`:9002/tcp`)**: Local/internal REST API used by management services and Prometheus metrics scraping.
* **Data Plane Reverse Proxy (`:80/tcp`)**: Public/ingress HTTP load balancer port. Strictly proxies request traffic to registered backend instances.

---

## 3. Input Validation & Memory Safety

1. **Protobuf Deserialization**:
   All network envelopes received via UDP and TCP are validated against Protocol Buffer schemas before unpacking. Malformed or oversized payloads (>1200 bytes on UDP) are dropped immediately.
2. **C++20 Type & Memory Safety**:
   Memory management follows RAII idioms (`std::shared_ptr`, `std::unique_ptr`, `std::vector`). Raw pointer arithmetic on incoming buffers is strictly prohibited.
3. **Prepared Statements**:
   All database interactions utilize SQLite prepared statements with parameter binding:
   ```cpp
   sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO nodes VALUES (?, ?, ...)", -1, &stmt, nullptr);
   sqlite3_bind_text(stmt, 1, node.node_id().c_str(), -1, SQLITE_TRANSIENT);
   ```

---

## 4. Production Mutual TLS (mTLS) Roadmap

For enterprise zero-trust deployments spanning untrusted WANs or multi-cloud topologies:
1. **X.509 PKI**: Internal Certificate Authority (CA) issues certificates to each node with Subject Alternative Names (SAN) matching its node ID.
2. **TLS 1.3 for TCP Sync & HTTP**: Enforces mutual certificate verification (`SSL_VERIFY_PEER`).
3. **Authenticated Gossip**: Symmetric HMAC-SHA256 signing for UDP datagrams using pre-shared cluster keys or rotating ephemeral secrets.
