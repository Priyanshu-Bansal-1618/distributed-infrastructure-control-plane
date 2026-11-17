# Distributed Infrastructure Control Plane (DICP) - Architecture Document

## 1. Executive Summary

The **Distributed Infrastructure Control Plane (DICP)** is an enterprise-grade, decentralized infrastructure control plane built in modern **C++20**. It integrates decentralized cluster membership, SWIM-inspired failure detection, a replicated service registry, health-aware request routing, and distributed configuration management.

A core tenet of DICP is the **strict separation of the Control Plane and the Data Plane**:
- **Control Plane**: Operates peer-to-peer across independent Node Agents using UDP gossip and TCP state synchronization. It manages node lifecycle, detects failures, checks health, manages leases, and computes routing topologies.
- **Data Plane**: An asynchronous, high-throughput HTTP reverse proxy that routes traffic based on atomically updated, immutable backend snapshots. It performs **zero** synchronous lookups against the distributed registry or gossip layer in the request path.

```
                              CLIENT APPLICATIONS
                                      │
                                      ▼
                      HTTP LOAD BALANCER / DATA PLANE
                                      │
               Backend Selection (Atomic Snapshot / Lock-Free Read)
                                      │
             ┌────────────────────────┼────────────────────────┐
             ▼                        ▼                        ▼
      Service Backend A        Service Backend B        Service Backend C
             ▲                        ▲                        ▲
             │          Versioned Backend Snapshots            │
             └────────────────────────┬────────────────────────┘
                                      │
                        DISTRIBUTED CONTROL PLANE
         ┌────────────────────────────┼────────────────────────────┐
         ▼                            ▼                            ▼
   NODE AGENT A                 NODE AGENT B                 NODE AGENT C
   - Gossip Engine              - Gossip Engine              - Gossip Engine
   - SWIM Failure Detector      - SWIM Failure Detector      - SWIM Failure Detector
   - Membership State Machine   - Membership State Machine   - Membership State Machine
   - Service Registry           - Service Registry           - Service Registry
   - Active Health Monitor      - Active Health Monitor      - Active Health Monitor
   - Config Manager             - Config Manager             - Config Manager
   - Local SQLite Storage       - Local SQLite Storage       - Local SQLite Storage
   - Internal Management HTTP   - Internal Management HTTP   - Internal Management HTTP
         ▲                            ▲                            ▲
         └─────────── UDP Gossip / TCP State Sync ─────────────────┘
                                      │
                                      ▼
                 MANAGEMENT API (Python / FastAPI)
                                      │
                                      ▼
                   OBSERVABILITY (Prometheus & Grafana)
```

---

## 2. Core Architectural Principles & Subsystems

### 2.1 Decoupled Subsystems

The Node Agent is decomposed into decoupled, single-responsibility modules:
1. **Networking (`dicp::net`)**:
   - Asynchronous UDP transport for gossip datagrams and failure probes.
   - Framed asynchronous TCP transport for bulk anti-entropy state synchronization.
   - HTTP server (Boost.Beast) for internal management and health checking.
2. **Gossip Engine (`dicp::gossip`)**:
   - Periodic randomized peer selection with configurable fanout ($k$).
   - Piggybacked dissemination queues with bounded buffer limits and priority ordering.
   - Application-layer duplicate suppression via message sequence tracking and LRU caches.
3. **SWIM Failure Detector (`dicp::failure`)**:
   - Periodic direct ping/ack probes.
   - Indirect ping-req relaying through $k$ intermediary peers upon direct timeout.
   - Suspicion mechanism with monotonically increasing incarnation refutations to eliminate false positives in lossy networks.
4. **Membership Manager (`dicp::membership`)**:
   - Maintains the cluster membership list with deterministic state transitions: `STARTING -> JOINING -> ALIVE <-> SUSPECT -> DEAD` and `LEFT`.
   - Incarnation precedence resolution: higher incarnation always supersedes lower incarnation; for equal incarnation, `SUSPECT` and `DEAD` take precedence over `ALIVE` to guarantee convergence.
5. **Service Registry (`dicp::registry`)**:
   - Multi-tenant service registration, discovery, and heartbeat renewals.
   - Lease management with monotonic deadline tracking and automatic expiration.
   - Replicated state with tombstone propagation and bounded tombstone garbage collection.
6. **Health Monitor (`dicp::health`)**:
   - Active HTTP health checks with configurable polling intervals, timeouts, and hysteresis thresholds (consecutive failure/success counts).
   - Strict separation between **Node Health** (cluster participation) and **Service Health** (endpoint readiness).
7. **Load Balancer / Data Plane (`dicp::lb`)**:
   - HTTP reverse proxy with asynchronous connection pooling and client/upstream forwarding.
   - Immutable snapshot publication: reader threads access backend vectors via `std::shared_ptr` / atomic snapshot pointer swaps without taking locks.
   - Pluggable routing strategies: Round Robin, Weighted Round Robin, Least Connections, and Consistent Hashing with virtual nodes.
   - Connection draining and circuit breaking.
8. **Configuration Manager (`dicp::config`)**:
   - Versioned key-value configuration with hierarchical scopes (cluster, service, node).
   - Audit trail, rollback capability via new monotonic versions, and staged rollout support.
9. **Persistence (`dicp::storage`)**:
   - Thread-isolated SQLite database per node for crash-recovery durability.
   - Asynchronous worker queue keeping disk I/O off the Boost.Asio network event loop.

---

## 3. Communication Contracts & Protocols

| Channel | Transport | Port (Default) | Serialization | Purpose |
|---|---|---|---|---|
| Gossip & Probes | UDP | `7946` | Protocol Buffers | Ping, Ack, PingReq, AckRelay, Dissemination Batches |
| State Sync | TCP | `7947` | Length-Prefixed Protobuf | Full anti-entropy sync, push-pull digest exchange |
| Node Agent API | HTTP/1.1 | `8080` | JSON / REST | Service registration, agent health, snapshot export |
| Load Balancer | HTTP/1.1 | `8081` | HTTP/1.1 Proxy | Inbound client requests forwarded to upstream services |
| Management API | HTTP/1.1 | `8000` | JSON / REST | Administrative control, multi-agent aggregation |
| Prometheus Scrape | HTTP/1.1 | `/metrics` | Prometheus Exposition | Agent and Load Balancer telemetry |

---

## 4. Conflict Resolution & Consistency Model

1. **Eventually Consistent Membership**:
   - Conflict resolution is governed strictly by tuples: `(incarnation, status_precedence, record_version)`.
   - Wall-clock timestamps are **never** used for state arbitration.
2. **Deterministic Precedence**:
   - If `update.incarnation > local.incarnation`: update accepted unconditionally.
   - If `update.incarnation == local.incarnation`:
     - If `local.status == ALIVE` and `update.status == SUSPECT`: update accepted (`SUSPECT` wins).
     - If `local.status == SUSPECT` and `update.status == DEAD`: update accepted (`DEAD` wins).
     - If `update.status == LEFT`: update accepted (`LEFT` is terminal).
     - If `update.node_id == self.node_id` and `update.status == SUSPECT`: local node increments its incarnation number by 1 and broadcasts an `ALIVE` update (refutation).
3. **Registry Leases & Tombstones**:
   - Services registered on a node are owned by that node. If the owner node dies or leaves, its registered service instances are automatically marked dead/draining.
   - Service instances carry a monotonic `record_version` and an explicit TTL lease.
   - Deregistered services generate tombstones with a retention period (e.g. 5x gossip interval) to ensure anti-entropy propagation before local deletion.
