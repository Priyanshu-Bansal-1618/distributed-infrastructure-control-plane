# Distributed Infrastructure Control Plane - System Design Document

## 1. System Overview

The **Distributed Infrastructure Control Plane (DICP)** is a production-grade, decentralized platform engineered in C++20 for cluster membership, failure detection, service discovery, distributed configuration, and health-aware request routing.

```
                    +-----------------------+
                    |  Client Applications  |
                    +-----------+-----------+
                                | HTTP Requests
                                v
                    +-----------------------+
                    |   HTTP Load Balancer  | <---+ Consumes versioned
                    |       (dicp_lb)       |     | backend snapshots
                    +-----------+-----------+     |
                                |                 |
          +---------------------+-----------------+-----+
          |                     |                       |
          v                     v                       v
    +-----------+         +-----------+           +-----------+
    | Backend A |         | Backend B |           | Backend C |
    +-----+-----+         +-----+-----+           +-----+-----+
          ^                     ^                       ^
          | HTTP Health Probes  | HTTP Health Probes    | HTTP Health Probes
          |                     |                       |
    +-----+-----+         +-----+-----+           +-----+-----+
    | NodeAgent | <=====> | NodeAgent | <=======> | NodeAgent |
    |  Node A   |  Gossip |  Node B   |   Gossip  |  Node C   |
    +-----+-----+ (UDP)   +-----+-----+   (UDP)   +-----+-----+
          |                     |                       |
          +---------------------+-----------------------+
                                | REST Scrape
                                v
                    +-----------------------+
                    | Prometheus & Grafana  |
                    +-----------------------+
```

---

## 2. Architectural Pillars

### 2.1 Control Plane vs Data Plane Decoupling
* **Control Plane (`dicp_agent`)**: Responsible for decentralized cluster state, SWIM gossip dissemination, active health checks, service leases, and configuration versioning.
* **Data Plane (`dicp_lb`)**: A high-performance, asynchronous HTTP reverse proxy built on Boost.Beast. The data plane **never** participates in gossip or performs distributed registry lookups on the request path. Instead, it routes requests by consuming immutable, versioned `BackendSnapshot` objects swapped atomically via `std::shared_ptr`.

### 2.2 Decentralized Consensus-Free State Dissemination
* No single point of failure (SPOF) or authoritative leader.
* Peer-to-peer state exchange via UDP gossip with bounded MTU (1200 bytes) and SHA-256 binary framing.
* Fast bootstrap join protocol with 1 RTT convergence.

---

## 3. Subsystem Breakdown

### 3.1 SWIM-Inspired Failure Detector
* **Direct Probing**: Every period $T$, a node selects a random peer and sends a `MSG_PING`. An `ACK` received within timeout confirms node aliveness.
* **Indirect Probing (`Ping-Req`)**: If direct probe times out, $k$ intermediate nodes are asked to probe the target. If any relay receives an `ACK`, the node remains `ALIVE`.
* **Suspicion Mechanism**: If indirect probes fail, the target transitions to `SUSPECT` with a configurable timeout.
* **Incarnation Refutation**: If a node discovers it is suspected or declared dead, it refutes the claim by monotonically incrementing its incarnation number ($I \leftarrow I + 1$) and broadcasting an `ALIVE` update.
* **State Precedence**:
  $$\text{Higher Incarnation Wins}$$
  $$\text{Equal Incarnation: DEAD} > \text{SUSPECT} > \text{ALIVE}$$
  $$\text{LEFT is absorbing (graceful departure)}$$

### 3.2 Distributed Service Registry
* **Service Model**: Services register with `instance_id`, `service_name`, `host`, `port`, `weight`, and `lease_duration_sec`.
* **Heartbeat & Leases**: Instances must renew leases periodically. Unrenewed instances expire and transition to tombstones.
* **Tombstones**: Soft-deletion markers preserved for $T_{\text{tombstone}}$ (default 60s) to prevent resurrected stale registrations before garbage collection.

### 3.3 Active HTTP Health Monitoring
* Decouples node health from service health (a node may be healthy while a service running on it has crashed).
* Independent hysteresis configuration:
  * Failure threshold: consecutive failures required to declare `UNHEALTHY`.
  * Recovery threshold: consecutive successes required to declare `HEALTHY`.

### 3.4 Load Balancing Strategies
Implemented as strategy pattern interfaces operating on `BackendSnapshot`:
1. **Round Robin**: Atomic index rotation across healthy backends.
2. **Weighted Round Robin**: Smooth deficit round-robin distribution honoring backend capacity weights.
3. **Least Connections**: Dynamically selects the backend with the minimum active TCP connections (`active_connections.load()`).
4. **Consistent Hashing**: High-distribution MurmurHash/std::hash ring with 100 virtual nodes per physical backend. Enables sticky routing based on `X-Routing-Key` or URL path with minimal key churn on backend topology changes.

### 3.5 Versioned Configuration Management
* Hierarchical scoping: `GLOBAL`, `DATACENTER`, `NODE`, and `SERVICE`.
* Monotonic versioning with strict conflict resolution (stale versions rejected).
* Rollback mechanism generates a new monotonic version with the historical payload.

### 3.6 Local Persistence (SQLite)
* Local SQLite database per node with WAL (`Write-Ahead Logging`) enabled.
* Stores `nodes`, `service_instances`, and `configurations`.
* Crash recovery: On startup, the node reads persisted state, recovers its previous incarnation number, and increments it to refute any residual suspicions.
