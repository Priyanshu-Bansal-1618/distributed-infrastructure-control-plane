# Distributed Infrastructure Control Plane (DICP) - Master Project Context

> **Authoritative Context Document for Fresh AI Sessions and Developers**  
> Read this file to establish complete context on the architecture, components, data flows, interfaces, and operational procedures of the DICP repository.

---

## 1. Project Overview & Problem Statement

**Distributed Infrastructure Control Plane (DICP)** is an enterprise-grade distributed systems and computer networks project implemented in **modern C++20**, supplemented by a **Python FastAPI** management layer, **Prometheus/Grafana** observability, and an **AxonOps-inspired** real-time web dashboard.

### Core Problem It Solves:
Modern cloud-native applications run microservices across dozens of servers. Traditional centralized architectures (like ZooKeeper or etcd) introduce single points of failure (SPOF) or lock contention when scaling. DICP solves this by providing:
1. **Decentralized Cluster Membership**: Nodes discover peers through randomized UDP gossip with no master node.
2. **SWIM-Inspired Failure Detection**: Combines direct pinging, indirect relay probing (`Ping-Req`), suspicion timers, and monotonic incarnation refutation to eliminate false-positive failure declarations under packet loss.
3. **Control Plane / Data Plane Separation**: The HTTP reverse proxy routes traffic using immutable, versioned backend snapshots swapped atomically in memory (`std::atomic_load`/`std::atomic_store` of `std::shared_ptr<const BackendSnapshot>`). The data plane performs **zero distributed queries and zero lock acquisitions on the request path**.
4. **Replicated Service Registry**: Features TTL heartbeats, automatic lease expirations, and tombstones to prevent resurrection bugs caused by lagging gossip messages.
5. **Durability & Crash Recovery**: Embedded SQLite on each node with Write-Ahead Logging (`WAL`). Restarting nodes load their saved state and bump their incarnation number to refute residual failure claims.

---

## 2. Technology Stack

* **Control Plane & Reverse Proxy**: C++20 (`std::atomic`, `std::shared_mutex`, `std::span`, concepts, RAII).
* **Asynchronous Networking**: Boost.Asio (asynchronous non-blocking UDP/TCP sockets, steady timers) and Boost.Beast (HTTP/1.1 client and reverse proxy).
* **Serialization & Framing**: Protocol Buffers (`proto3`) for internal wire datagrams, `nlohmann/json` for configurations.
* **Local Persistence**: Embedded SQLite 3 with WAL journal mode (`PRAGMA journal_mode = WAL`, `PRAGMA synchronous = NORMAL`).
* **Management & Web UI Layer**: Python 3.12, FastAPI, Pydantic v2, HTTPX, Uvicorn, D3.js v7, Chart.js, Tailwind CSS.
* **Observability**: Native Prometheus `/metrics` exporter, alert rules, and pre-provisioned Grafana dashboards.
* **Build System & Testing**: CMake 3.20+, Ninja, GoogleTest (C++), Pytest (Python).

---

## 3. High-Level Architecture Diagram

```
                    +-----------------------+
                    |  Client Applications  |
                    +-----------+-----------+
                                | HTTP Traffic
                                v
                    +-----------------------+
                    |   HTTP Load Balancer  | <---+ Lock-free read of immutable
                    |       (dicp_lb)       |     | BackendSnapshot pointer
                    +-----------+-----------+     |
                                |                 |
          +---------------------+-----------------+-----+
          |                     |                       |
          v                     v                       v
    +-----------+         +-----------+           +-----------+
    | Backend 1 |         | Backend 2 |           | Backend 3 |
    +-----+-----+         +-----+-----+           +-----+-----+
          ^                     ^                       ^
          | HTTP Health Probes  | HTTP Health Probes    | HTTP Health Probes
          |                     |                       |
    +-----+-----+         +-----+-----+           +-----+-----+
    | NodeAgent | <=====> | NodeAgent | <=======> | NodeAgent |
    |  Node 1   |  Gossip |  Node 2   |   Gossip  |  Node 3   |
    +-----+-----+ (UDP)   +-----+-----+   (UDP)   +-----+-----+
          |                     |                       |
          +---------------------+-----------------------+
                                |
             +------------------+------------------+
             |                                     |
             v                                     v
+------------------------+            +-------------------------+
|  FastAPI Management &  |            |  Prometheus & Grafana   |
| AxonOps Web Dashboard  |            |  Observability Stack    |
| (http://localhost:8000)|            |  (http://localhost:3000)|
+------------------------+            +-------------------------+
```

---

## 4. Subsystems Deep-Dive

### 4.1 Binary Framing & UDP Transport (`core/networking/`)
* **Binary Frame Header (12 bytes)**:
  * Magic Bytes (`0x44494350` = `"DICP"`)
  * Protocol Version (`uint16_t`, current = 1)
  * Message Type (`uint16_t`)
  * Payload Length (`uint32_t`)
* **MTU Enforcement**: Datagrams exceeding 1200 bytes are rejected to prevent IP fragmentation over WAN.
* **TCP Framed Transport**: Length-prefixed framing for anti-entropy bulk state synchronization (`MSG_SYNC_REQUEST` / `MSG_SYNC_RESPONSE`).

### 4.2 Membership & SWIM Failure Detector (`core/membership/`, `core/gossip/`)
* **State Machine & Update Precedence**:
  1. Higher incarnation counter always supersedes lower incarnation.
  2. Equal incarnation resolves deterministically: $\text{DEAD} > \text{SUSPECT} > \text{ALIVE}$; $\text{LEFT}$ is absorbing.
* **SWIM Failure Detector**:
  1. **Direct Probe**: Sends `MSG_PING` to a randomly selected peer.
  2. **Indirect Probe (`Ping-Req`)**: If direct probe fails within `probe_timeout_ms`, selects $k=3$ random peers and sends `MSG_PING_REQ`.
  3. **Suspicion Timer**: If indirect probes fail, transitions target to `SUSPECT` and arms a suspicion timer.
  4. **Incarnation Refutation**: If a suspected node receives a rumor about itself, it increments its local incarnation ($I_{new} = I_{old} + 1$) and broadcasts an `ALIVE` message to refute the claim.
  5. **Death Declaration**: If suspicion expires without refutation, node transitions to `DEAD`.
* **Dissemination Engine**: Bounded priority queue with fanout tracking ($f = 3$) and an LRU deduplication cache (5000 entries) preventing broadcast storms.

### 4.3 Service Registry & Leases (`core/registry/`)
* **Heartbeat Leases**: Services register with a TTL lease duration (default 10s–30s).
* **Tombstones**: Soft-deletion markers preserved across the cluster for 60s–120s to prevent resurrected stale registrations before garbage collection.

### 4.4 Active Health Monitoring (`core/health/`)
* Boost.Beast asynchronous HTTP client probing `/health` endpoints.
* **Hysteresis Filtering**:
  * Failure threshold: $N$ consecutive probe failures required before transition to `UNHEALTHY`.
  * Recovery threshold: $M$ consecutive probe successes required before transition to `HEALTHY`.

### 4.5 Data Plane HTTP Load Balancer (`core/load_balancer/`)
* **Lock-Free Read Path**: Client worker threads call `std::atomic_load(&snapshot_)`. In-flight requests safely reference the snapshot via `std::shared_ptr`, while updates use `std::atomic_store`.
* **4 Routing Algorithms**:
  1. `round_robin`: Atomic index rotation.
  2. `weighted_round_robin`: Smooth deficit round-robin honoring configured weights.
  3. `least_connections`: Routes to the instance with the minimum active TCP streams (`active_connections.load()`).
  4. `consistent_hash`: FNV-1a hash ring with 100 virtual nodes per physical backend, supporting sticky sessions via `X-Routing-Key` header.
* **Admin Endpoints**:
  * `GET /admin/backends`: Returns current snapshot and backend metrics.
  * `POST /admin/strategy`: Dynamically switches load balancing algorithms without process restart.
  * `POST /admin/snapshot`: Dynamically ingests updated routing topology.
  * `GET /metrics`: Exposes Prometheus counters.

### 4.6 Persistence & Crash Recovery (`core/storage/`)
* Embedded SQLite database per node with `PRAGMA journal_mode = WAL`.
* Pre-compiled prepared statements prevent SQL injection.
* On reboot, reads previous state, recovers previous incarnation number $I$, increments it to $I + 1$, and broadcasts an `ALIVE` refutation.

### 4.7 Observability & Management Dashboard (`management/dashboard/`)
* **Interactive Web Dashboard**: Styled after AxonOps (`Screenshot_2026-11-17_00-23-09.png`).
* **Dynamic Node Management**:
  * Users can dynamically add custom nodes (`+ Add Node` modal) with custom IDs, IP addresses, UDP gossip ports, TCP sync ports, and REST management ports.
  * Nodes can be removed (`DELETE /api/v1/nodes/{id}/delete`), transitioning them to the absorbing `LEFT` state.
* **Physical State vs Protocol State**:
  * **Physical State**: Working (machine responds to UDP/TCP probes) or Not Working (severed network, process frozen).
  * **Protocol State**: Automatically derived by the background SWIM loop (`ALIVE` $\rightarrow$ `SUSPECT` $\rightarrow$ `DEAD`).
* **Graph View**: D3.js force-directed radial cluster graph showing center hub, subnets, and glowing concentric circles for nodes. Links animate moving UDP gossip datagrams. Nodes pulse amber during `SUSPECT` and turn red when `DEAD`.
* **List View**: Detailed tabular view of membership records, physical states, protocol statuses, incarnations, and network ports, with interactive action buttons to toggle working state or delete nodes.
* **Node Inspector Drawer**: Slide-over panel with node hardware/network details, physical state toggle switch, and a **live 3.0-second suspicion countdown progress bar**.
* **Live Controls**: Dropdown strategy switcher, test traffic generator with live animated distribution bars, and chaos engineering triggers.

---

## 5. Repository File Structure

```
distributed-infrastructure-control-plane/
├── CMakeLists.txt              # C++20 build configuration (dynamic Protobuf & GTest)
├── CMakePresets.json           # Standard CMake build & test presets
├── README.md                   # System documentation & quickstart
├── CHANGES_LOG.md              # Detailed log of all code modifications
├── PROJECT_CONTEXT.md          # Master context document (this file)
├── PROFESSOR_DEMO_GUIDE.md     # Step-by-step presentation & demo guide
│
├── proto/                      # Protocol Buffer definitions
│   ├── common.proto            # Shared enums (NodeStatus, HealthStatus)
│   ├── gossip.proto            # Framing, Ping, Ack, PingReq, GossipBatch
│   ├── membership.proto        # NodeRecord, NodeUpdate
│   ├── registry.proto          # ServiceInstanceRecord, ServiceUpdate
│   └── configuration.proto     # ConfigRecord, ConfigUpdate
│
├── core/                       # C++20 Core Subsystems
│   ├── common/                 # Config parser, UUIDv4, Logger, Types
│   ├── networking/             # MessageFrame (0x44494350), UdpTransport, TcpTransport
│   ├── agent/                  # NodeIdentity, NodeAgent orchestrator, AgentHttpServer, main.cpp
│   ├── membership/             # MembershipManager, SWIM FailureDetector
│   ├── gossip/                 # DisseminationQueue, GossipEngine
│   ├── registry/               # ServiceRegistry (Leases, Tombstones)
│   ├── health/                 # HealthMonitor (Beast async client, hysteresis)
│   ├── load_balancer/          # BackendSnapshot, Strategies (RR, WRR, LC, CH), HttpProxy, main.cpp
│   ├── config_manager/         # ConfigurationManager (scopes, monotonic rollback)
│   └── storage/                # SqliteStorage (WAL mode, crash recovery)
│
├── management/                 # Python Management Layer & Web Dashboard
│   ├── api/app.py              # FastAPI server serving API and Web Dashboard
│   ├── dashboard/index.html    # AxonOps-inspired dark-mode SPA dashboard (D3.js radial graph)
│   ├── services/cluster_service.py # Cluster aggregation, topology generator, LB client, chaos
│   ├── clients/agent_client.py # HTTPX client for Node Agent REST
│   ├── models/schemas.py       # Pydantic v2 schemas
│   └── tests/                  # Pytest automated test suite
│
├── monitoring/                 # Prometheus scrape configs, alerts, Grafana dashboards
├── deployments/                # Multi-node Docker Compose, Dockerfiles, node configs
├── simulator/                  # Fault simulation scripts & benchmarking harness
└── tests/unit/                 # 11 GoogleTest C++ unit & integration test suites
```

---

## 6. How to Build & Run Tests

```bash
# 1. Setup & Build on any machine:
./setup.sh

# 2. Run all test suites (C++ 11 suites + Python 5 suites):
./run_tests.sh
```

---

## 7. How to Launch the System & Web UI

```bash
# Launch the Management API & Observability Dashboard
./run_dashboard.sh

# Open in browser:
# http://localhost:8000/
# http://localhost:8000/docs (Swagger UI)
```
