# Distributed Infrastructure Control Plane (DICP)

[![Platform: Linux Only](https://img.shields.io/badge/Platform-Linux%20Only%20(or%20WSL2)-red.svg)](#platform-support-notice)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.22%2B-064F8C.svg)](https://cmake.org/)
[![Boost.Asio](https://img.shields.io/badge/Boost.Asio-1.81%2B-orange.svg)](https://www.boost.org/)
[![Protocol Buffers](https://img.shields.io/badge/Protobuf-3.x-green.svg)](https://protobuf.dev/)
[![FastAPI](https://img.shields.io/badge/FastAPI-0.110%2B-009688.svg)](https://fastapi.tiangolo.com/)
[![Tests](https://img.shields.io/badge/Tests-100%25%20Passing-brightgreen.svg)](#automated-testing)

A decentralized, production-grade distributed infrastructure control plane engineered in modern **C++20** with a **Python FastAPI / Tailwind / D3.js** management plane. 

DICP eliminates the single points of failure (SPOF) and scaling bottlenecks inherent in centralized coordination systems (like ZooKeeper or Consul master nodes). It combines **SWIM-based peer-to-peer gossip membership**, **dynamic decentralized service discovery**, **lock-free Layer-7 HTTP reverse proxying**, and **real-time observability** across heterogeneous multi-datacenter clusters.

---

> [!IMPORTANT]
> ### Platform Support Notice
> **DICP is officially supported on Linux only** (Ubuntu 22.04 / 24.04, Debian 12, Fedora, Arch, RHEL 9).
>
> **Windows Users**: Native Windows builds (MSVC / Win32 sockets) are **not supported** due to reliance on POSIX asynchronous socket primitives, Linux `epoll` via Boost.Asio, and POSIX shell automation. 
> 
> To run DICP on Windows, use **WSL2 (Windows Subsystem for Linux)** with Ubuntu:
> ```powershell
> # In Windows PowerShell (Run as Administrator):
> wsl --install -d Ubuntu-22.04
> ```
> Once installed, open your WSL Ubuntu terminal and follow the Linux setup instructions below. Everything will run natively and flawlessly inside WSL2.

---

## Table of Contents
1. [Architecture Overview](#1-architecture-overview)
2. [Core Subsystems & Technical Innovations](#2-core-subsystems--technical-innovations)
3. [Prerequisites & System Dependencies](#3-prerequisites--system-dependencies)
4. [Quick Start (One-Command Setup)](#4-quick-start-one-command-setup)
5. [Running the Observability Dashboard](#5-running-the-observability-dashboard)
6. [Testing the Live SWIM Failure Detector](#6-testing-the-live-swim-failure-detector)
7. [Running Tests & Benchmarks](#7-running-tests--benchmarks)
8. [Manual Terminal Execution (Without Scripts)](#8-manual-terminal-execution-without-scripts)
9. [REST API Quick Reference](#9-rest-api-quick-reference)
10. [Repository Directory Structure](#10-repository-directory-structure)
11. [Troubleshooting & FAQ](#11-troubleshooting--faq)

---

## 1. Architecture Overview

DICP strictly decouples the **Control Plane** (state dissemination, failure detection, service registration) from the **Data Plane** (high-throughput request routing and proxying):

```
                       +-----------------------+
                       |  Client Applications  |
                       +-----------+-----------+
                                   | HTTP Requests (Port 8080)
                                   v
                       +-----------------------+
                       |  Layer-7 HTTP Proxy   | <---+ Consumes lock-free
                       |       (dicp_lb)       |     | atomic BackendSnapshot
                       +-----------+-----------+     |
                                   |                 |
             +---------------------+-----------------+-----+
             |                     |                       |
             v                     v                       v
       +-----------+         +-----------+           +-----------+
       | Backend A |         | Backend B |           | Backend C |
       +-----+-----+         +-----+-----+           +-----+-----+
             ^                     ^                       ^
             | Health Checks       | Health Checks         | Health Checks
             |                     |                       |
       +-----+-----+         +-----+-----+           +-----+-----+
       | NodeAgent | <=====> | NodeAgent | <=======> | NodeAgent |
       |  Node 1   |  Gossip |  Node 2   |   Gossip  |  Node 3   |
       +-----+-----+  (UDP)  +-----+-----+   (UDP)   +-----+-----+
             |                     |                       |
             +---------------------+-----------------------+
                                   | REST API / Polling (Port 8000)
                                   v
                       +-----------------------+
                       | Observability UI &    |
                       | FastAPI Control Plane |
                       +-----------------------+
```

### Core Architecture Invariants
1. **Zero Queries on the Request Path**: The HTTP reverse proxy never queries the service registry or executes gossip during client requests. It routes using immutable, versioned `BackendSnapshot` objects swapped atomically via `std::atomic<std::shared_ptr<const BackendSnapshot>>`.
2. **Decentralized Membership**: No master leader election. Nodes discover and track each other via randomized UDP gossip envelopes clamped to 1200 bytes (preventing MTU fragmentation).
3. **SWIM Failure Detector**: Direct UDP probes backed by $k=3$ indirect proxy probes (`Ping-Req`), configurable suspicion timeouts, and monotonically increasing incarnation numbers ($I$).
4. **Crash-Recovery Durability**: Local state persists to an embedded SQLite database with Write-Ahead Logging (`WAL`). Restarted nodes load their persisted incarnation number and bump it ($I \leftarrow I + 1$) to refute stale suspicion claims.

---

## 2. Core Subsystems & Technical Innovations

### 2.1 Custom Binary Wire Protocol
* Implemented in [`core/networking/message_frame.cpp`](file:///home/pbyanshu/Downloads/CN%20PORJECT/core/networking/message_frame.cpp).
* Uses a compact 12-byte binary header:
  ```
  [ Magic: 0x44494350 ("DICP") (4B) ] [ Version (2B) ] [ MsgType (2B) ] [ Payload Length (4B) ]
  ```
* Wraps Protocol Buffers (`proto::Envelope`) over non-blocking UDP sockets.

### 2.2 SWIM Failure Detection & Relaying
* Implemented in [`core/membership/failure_detector.cpp`](file:///home/pbyanshu/Downloads/CN%20PORJECT/core/membership/failure_detector.cpp).
* **Direct Probe**: Periodically pings a random cluster peer.
* **Indirect Relay Probes (Proxying)**: If direct ping times out, selects $k=3$ random peers to ping the target on its behalf.
* **Suspicion Mechanism**: If indirect probes fail, transitions target to `SUSPECT` (grace period, e.g., 3s).
* **Refutation Protocol**: If a node hears it is suspected, it increments its incarnation number ($I \leftarrow I + 1$) and broadcasts an `ALIVE` update, refuting false rumours.

### 2.3 Epidemic Gossip Dissemination
* Implemented in [`core/gossip/dissemination_queue.cpp`](file:///home/pbyanshu/Downloads/CN%20PORJECT/core/gossip/dissemination_queue.cpp).
* Updates are piggybacked on outgoing UDP ping/ack packets.
* Retransmission is bounded by $\lambda \cdot \lceil\ln(N + 1)\rceil$, achieving mathematically guaranteed $O(\log N)$ epidemic convergence without flooding the network.

### 2.4 Decentralized Service Registry
* Implemented in [`core/registry/service_registry.cpp`](file:///home/pbyanshu/Downloads/CN%20PORJECT/core/registry/service_registry.cpp).
* Microservices register with host, port, weight, and TTL leases.
* **SWIM Auto-Eviction**: When SWIM detects a host node has died, the registry automatically purges or marks unhealthy all services on that host, immediately updating the load balancer snapshot.

### 2.5 Layer-7 HTTP Reverse Proxy & 4 Load Balancing Strategies
* Implemented in [`core/load_balancer/http_proxy.cpp`](file:///home/pbyanshu/Downloads/CN%20PORJECT/core/load_balancer/http_proxy.cpp) using Boost.Asio & Boost.Beast.
* Inspects `X-Service-Name` header or URL path prefix (`/service/<name>/...`) and `X-Routing-Key`.
* **Pluggable Strategies**:
  1. **Round Robin**: Atomic index modulo healthy backend count.
  2. **Weighted Round Robin**: Capacity-based proportional distribution.
  3. **Least Connections**: Routes to backend with the lowest `active_connections.load()`.
  4. **Consistent Hashing**: 64-bit FNV-1a hash ring with 100 virtual nodes per backend for session stickiness.

---

## 3. Prerequisites & System Dependencies

### Ubuntu / Debian / WSL2 Installation
Run the following command to install all build tools, C++20 compilers, and system libraries:

```bash
sudo apt-get update && sudo apt-get install -y \
    build-essential \
    g++ \
    cmake \
    ninja-build \
    git \
    curl \
    jq \
    libboost-dev \
    libboost-system-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libsqlite3-dev \
    nlohmann-json3-dev \
    libgtest-dev \
    python3 \
    python3-pip \
    python3-venv
```

---

## 4. Quick Start (One-Command Setup)

We provide portable runner scripts that automate virtual environment creation, dependencies, compilation, and testing.

### Step 1: Run the Automated Setup
```bash
./setup.sh
```
This script will:
1. Create and activate a Python virtual environment (`./venv`).
2. Install Python requirements (`fastapi`, `uvicorn`, `httpx`, `pydantic`, `pytest`).
3. Compile all C++20 binaries (`dicp_agent`, `dicp_lb`, and 11 test suites) into `./build` using Ninja/Make.

---

## 5. Running the Observability Dashboard

Launch the web management control plane and real-time topology dashboard:

```bash
./run_dashboard.sh
```

Now open your browser:
* **Web UI Dashboard**: [http://localhost:8000](http://localhost:8000)
* **Interactive API Swagger Docs**: [http://localhost:8000/docs](http://localhost:8000/docs)

### Dashboard Visual Features:
* **WAN Federation & Multi-Datacenter Topology**: Interactive D3.js force-directed canvas displaying the central `DICP WAN FEDERATION` hub connected to regional datacenters (`Data Center A`, `Data Center B`).
* **Unambiguous Role Colors**:
  - **Server** `[SRV]`: Sapphire Blue (`#2563eb`)
  - **Worker** `[WRK]`: Emerald Green (`#059669`)
  - **Gateway** `[GW]`: Crisp Cyan / Teal (`#0891b2`)
  - **Database** `[DB]`: Amethyst Purple (`#9333ea`)
  - **Cache** `[CACHE]`: Vivid Pink (`#ec4899`)
* **Real-Time Telemetry**: Live Chart.js graphs tracking proxy throughput (RPS), P95/P99 latency, and gossip probe rates.
* **Dynamic Node Management**: Click **Add Node** in the top bar to dynamically add custom machines to any datacenter with customized roles and services.

---

## 6. Testing the Live SWIM Failure Detector

You can watch the distributed SWIM failure detection algorithm operate in real-time on the UI:

1. Open the dashboard at [http://localhost:8000](http://localhost:8000).
2. Click on any node in the topology graph to open its details drawer (or switch to the **List View** tab).
3. Toggle the node status from **Working (Online)** to **Not Working (Offline)**.
4. **Observe the Protocol in Action**:
   - Direct ping to the node fails.
   - The cluster attempts indirect relay probes via $k=3$ proxies.
   - When all proxies fail, the node ring turns **Pulsing Neon Amber** with countdown: `(SUSPECT 2.5s)`.
   - If left offline, the suspicion timer expires and the node turns **Crimson Red**: `(DEAD)`.
   - If set back to **Online**, the node recovers its incarnation, broadcasts an authoritative refutation, and immediately returns to **Green (ALIVE)**.

---

## 7. Running Tests & Benchmarks

### 7.1 Run All Tests (C++ and Python)
```bash
./run_tests.sh
```
Executes all **11 C++20 GoogleTest test suites** and **5 Python FastAPI test suites** (100% pass rate):
- `test_config`: Configuration parsing and validation.
- `test_message_frame`: Binary framing, magic byte validation, MTU clamping.
- `test_node_identity`: UUID generation and monotonic incarnation refutation.
- `test_udp_transport`: Non-blocking async UDP transmission and packet handling.
- `test_membership`: Deterministic update precedence and state transitions.
- `test_failure_detector`: Direct probes, indirect proxy probing, suspicion timeouts.
- `test_cluster_convergence`: Multi-node peer discovery and convergence.
- `test_service_registry`: Service leases, renewal heartbeats, tombstones, and purges.
- `test_load_balancer`: Round Robin, Weighted, Least Conn, Consistent Hash routing.
- `test_config_manager`: Dynamic reloadable configuration parameters.
- `test_sqlite_storage`: ACID transactions, table schemas, and crash recovery.

### 7.2 Run the Multi-Node Cluster Fault Simulator
```bash
./run_simulator.sh
```
Spawns a multi-process cluster demonstrating gossip convergence, service registration, sudden node crash detection, and automated recovery.

### 7.3 Run the Load Balancer Benchmark
```bash
./run_benchmark.sh
```
Sends thousands of concurrent HTTP requests across all 4 load balancing algorithms, measuring RPS throughput, P50/P95/P99 latencies, and zero-error failover during backend crashes.

---

## 8. Manual Terminal Execution (Without Scripts)

If you prefer building and running manually via terminal:

### 1. Build C++ Binaries Manually
```bash
mkdir -p build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
ctest --output-on-failure
cd ..
```

### 2. Run Python Management API Manually
```bash
source venv/bin/activate
uvicorn management.api.app:app --host 0.0.0.0 --port 8000
```

### 3. Run Node Agent Daemons
```bash
# Seed Node (Node 1)
./build/core/agent/dicp_agent --config deployments/configs/node1.json

# Peer Node (Node 2)
./build/core/agent/dicp_agent --config deployments/configs/node2.json
```

---

## 9. REST API Quick Reference

The FastAPI service exposes administrative controls authenticated via Bearer token (default: `secret-token-123`):

```bash
# 1. Fetch live multi-datacenter topology
curl -s http://localhost:8000/api/v1/topology | jq .

# 2. Add a new machine node dynamically
curl -X POST http://localhost:8000/api/v1/nodes/add \
  -H "Authorization: Bearer secret-token-123" \
  -H "Content-Type: application/json" \
  -d '{
    "node_id": "node-7",
    "node_type": "database",
    "datacenter": "Data Center B",
    "initial_service": "redis-cluster",
    "address": "127.0.0.1",
    "gossip_port": 9700,
    "sync_port": 9701,
    "management_port": 9702
  }'

# 3. Simulate failure (Set node offline)
curl -X POST http://localhost:8000/api/v1/nodes/node-2/status \
  -H "Authorization: Bearer secret-token-123" \
  -H "Content-Type: application/json" \
  -d '{"working": false}'

# 4. Switch Load Balancer routing strategy
curl -X POST http://localhost:8000/api/v1/loadbalancer/strategy \
  -H "Authorization: Bearer secret-token-123" \
  -H "Content-Type: application/json" \
  -d '{"strategy": "least_conn"}'
```

---

## 10. Repository Directory Structure

```
distributed-infrastructure-control-plane/
|-- setup.sh                    # Universal 1-click build & dependency installer
|-- run_dashboard.sh            # Universal runner for Web Dashboard (port 8000)
|-- run_tests.sh                # Universal runner for all C++ and Python tests
|-- run_simulator.sh            # Universal runner for multi-node fault simulation
|-- run_benchmark.sh            # Universal runner for load balancer benchmarks
|-- CMakeLists.txt              # C++20 root CMake configuration
|-- proto/                      # Protocol Buffer message definitions
|   |-- common.proto            # Shared enums (NodeStatus, HealthStatus)
|   |-- gossip.proto            # Envelope, Ping, Ack, PingReq, GossipBatch
|   |-- membership.proto        # NodeRecord, NodeUpdate
|   |-- registry.proto          # ServiceInstanceRecord, ServiceUpdate
|   `-- configuration.proto     # ConfigRecord, ConfigUpdate
|-- core/                       # Core C++20 Subsystems
|   |-- networking/             # MessageFrame (12B header), UDP & TCP Transports
|   |-- membership/             # MembershipManager, FailureDetector (SWIM)
|   |-- gossip/                 # DisseminationQueue, GossipEngine (Piggybacking)
|   |-- registry/               # ServiceRegistry (Leases, Tombstones, Auto-Eviction)
|   |-- load_balancer/          # HttpProxy (Boost.Beast L7), BackendSnapshot, Strategies
|   |-- storage/                # SqliteStorage (ACID WAL mode, Incarnation persistence)
|   |-- config_manager/         # Dynamic hot-reloadable configuration
|   `-- agent/                  # NodeAgent daemon and local REST server
|-- management/                 # Management Control Plane & UI
|   |-- api/app.py              # FastAPI endpoints & token authentication
|   |-- dashboard/index.html    # Full-featured Tailwind + D3.js real-time UI
|   |-- services/cluster_service.py # Cluster & multi-datacenter simulation service
|   |-- requirements.txt        # Python dependencies
|   `-- tests/                  # Pytest test suite
|-- simulator/                  # Multi-node fault simulation & benchmarks
|-- deployments/                # Multi-node configs and Docker Compose setups
`-- tests/                      # C++ unit & integration tests (11 suites)
```

---

## 11. Troubleshooting & FAQ

#### Q1: Can I run this directly on Windows Command Prompt or PowerShell?
**No.** DICP uses Linux socket models and POSIX shell scripts. On Windows, install **WSL2** (`wsl --install -d Ubuntu-22.04`). Inside the WSL Ubuntu terminal, run `./setup.sh` and `./run_dashboard.sh`. You can then open `http://localhost:8000` directly in your Windows web browser!

#### Q2: `curl` or UI fails to connect to `127.0.0.1:8000` when behind a corporate proxy?
If you have `http_proxy` or `https_proxy` environment variables set in your shell, bypass localhost:
```bash
export NO_PROXY="127.0.0.1,localhost"
curl --noproxy '*' http://127.0.0.1:8000/api/v1/topology
```

#### Q3: "Address already in use" on port 8000?
You can start the dashboard on a custom port using the `PORT` environment variable:
```bash
PORT=8080 ./run_dashboard.sh
```

---

## 12. License

This project is licensed under the Apache 2.0 License.
