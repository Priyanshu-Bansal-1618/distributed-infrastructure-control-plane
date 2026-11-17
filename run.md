# Complete Setup and Execution Guide (`run.md`)

This guide provides step-by-step instructions to set up, build, test, run, and benchmark the **Distributed Infrastructure Control Plane (DICP)**.

---

## 1. Prerequisites & System Requirements

### Hardware Requirements
- **CPU**: 2+ cores (4+ cores recommended for multi-node simulation)
- **RAM**: 2 GB minimum (4+ GB recommended)
- **Disk Space**: 5 GB free disk space
- **Operating System**: Linux (Ubuntu 22.04 / 24.04, Debian 12, RHEL 9) or macOS / WSL2 on Windows

### Software Dependencies
- **C++ Compiler**: GCC 13+ or Clang 16+ supporting **C++20**
- **Build System**: CMake 3.22+ and Ninja build
- **Libraries**:
  - Boost 1.83+ (`libboost-all-dev` or `libboost-dev` + `libboost-system-dev`)
  - Protocol Buffers 3.x (`libprotobuf-dev`, `protobuf-compiler`)
  - SQLite 3 (`libsqlite3-dev`)
  - JSON for Modern C++ (`nlohmann-json3-dev`)
- **Python**: Python 3.10+ (Python 3.12 recommended)
- **Containers (Optional)**: Docker 24.0+ and Docker Compose v2.20+

---

## 2. Dependency Installation

### On Ubuntu / Debian / WSL2

```bash
# Update package lists
sudo apt-get update

# Install build tools and C++20 compiler
sudo apt-get install -y build-essential g++ cmake ninja-build git curl jq

# Install required C++ libraries
sudo apt-get install -y \
    libboost-dev \
    libboost-system-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libsqlite3-dev \
    nlohmann-json3-dev \
    libgtest-dev

# Install Python and virtual environment tools
sudo apt-get install -y python3 python3-pip python3-venv
```

---

## 3. Python Virtual Environment Setup

Create and activate a virtual environment for the Python management API and simulator scripts:

```bash
# Navigate to the project root directory
cd /path/to/project

# Or run the automated universal setup script:
./setup.sh
```

> **Note**: `management/requirements.txt` installs `fastapi`, `uvicorn`, `pydantic`, `httpx`, `pytest`, and `pytest-asyncio`.

---

## 4. Configuration & Customization (What to Add or Change)

Before running, review the configuration parameters you might want to modify depending on your environment:

### 4.1 Node Agent Configuration Files
Located in `deployments/configs/`:
- [`node1.json`](file:///home/pbyanshu/Downloads/CN%20PORJECT/deployments/configs/node1.json): Seed node configuration.
- [`node2.json`](file:///home/pbyanshu/Downloads/CN%20PORJECT/deployments/configs/node2.json): Peer node joining `node-agent-1:9000`.
- [`node3.json`](file:///home/pbyanshu/Downloads/CN%20PORJECT/deployments/configs/node3.json): Peer node joining `node-agent-1:9000` and `node-agent-2:9000`.

**Key parameters to adjust if needed:**
| Parameter | Default | Description | When to Change |
|---|---|---|---|
| `node.bind_address` | `0.0.0.0` | IP to bind sockets to | Set to `127.0.0.1` for local-only testing |
| `node.advertised_address` | `127.0.0.1` / hostname | IP/hostname announced to peers | Change to your machine's LAN or VPC IP if running across multiple physical machines |
| `network.gossip_port` | `9000` | UDP gossip port | Change if port 9000 is already in use |
| `network.sync_port` | `9001` | TCP state sync port | Change if port 9001 is already in use |
| `network.management_port`| `9002` | REST API and `/metrics` port | Change if port 9002 is already in use |
| `storage.database_path` | `/tmp/node.db` | Local SQLite path | Set to a persistent directory (e.g. `./data/node.db`) for production |
| `seeds` | `["127.0.0.1:9000"]` | Initial cluster bootstrap peers | Add other seed IPs when running a multi-machine cluster |

### 4.2 Security Token
- The Management API uses Bearer token authentication.
- Default token: `secret-token-123`
- To customize: Set the environment variable:
  ```bash
  export API_TOKEN="your-secure-custom-token"
  ```

### 4.3 Proxy Environment Note
If your shell or corporate network sets `http_proxy` or `all_proxy`, localhost connections may get intercepted:
```bash
# Ensure local connections bypass any proxy
export NO_PROXY="127.0.0.1,localhost"
```

---

## 5. Building the Project from Source (Method 1)

### Step 5.1: Compile with CMake and Ninja (or use `./setup.sh`)

```bash
# Create and enter the build directory
mkdir -p build && cd build

# Configure CMake
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build all targets (dicp_agent, dicp_lb, and test suites)
ninja
```

---

## 6. Running Automated Tests

Run all tests in one command with the universal test script:
```bash
./run_tests.sh
```

Or run individual suites manually:

### Step 6.1: Run C++ Unit & Integration Tests (11 Test Suites)

```bash
cd build && ctest --output-on-failure
```

Expected output:
```text
100% tests passed out of 11
Total Test time: ~3.5s
```

### Step 6.2: Run Python Management API Tests

```bash
PYTHONPATH=. pytest management/tests/test_management_api.py -v
```

Expected output: `5 passed in ~0.6s`.

---

## 7. Running the Cluster Simulation & Fault Scenarios

```bash
./run_simulator.sh
# (or: python3 simulator/cluster_simulator/simulator.py)
```
```

Expected output:
```text
==================================================
Starting DICP Cluster Simulation & Fault Scenarios
==================================================
[*] Started Node Agent 'node-1' (PID=..., gossip_port=19500, mgmt_port=19502)
[*] Started Node Agent 'node-2' (PID=..., gossip_port=19600, mgmt_port=19602)
[*] Started Node Agent 'node-3' (PID=..., gossip_port=19700, mgmt_port=19702)

--- Scenario 1: Cluster Convergence ---
[+] Node 1 alive members: 3/3
[+] Node 2 alive members: 3/3
[+] Node 3 alive members: 3/3
[SUCCESS] All 3 nodes converged via decentralized gossip dissemination!

--- Scenario 2: Service Registration & Discovery ---
[+] Registered service 'payment' on node-3
[SUCCESS] Service registered on node-3 successfully propagated to node-1!

--- Scenario 3: Sudden Node Crash Detection ---
[*] Stopped Node Agent 'node-3' (Graceful=False)
[SUCCESS] Node 1 detected Node 3 failure (SUSPECT -> DEAD) in 0.65s!

--- Scenario 4: Node Restart Recovery ---
[*] Started Node Agent 'node-3' (PID=..., gossip_port=19700, mgmt_port=19702)
[SUCCESS] Restarted Node 3 successfully reconciled with incarnation=...!

==================================================
ALL CLUSTER FAULT & RECOVERY SCENARIOS PASSED 100%
==================================================
```

---

## 8. Running the Load Balancer & Benchmarks

Run the load balancer benchmark harness to test throughput, latency percentiles (P50, P90, P95, P99), and distribution across all 4 routing strategies, plus zero-downtime failover:

```bash
./run_benchmark.sh
# (or: python3 simulator/traffic_generator/benchmark.py)
```

This will:
1. Start 3 HTTP backends (`be-1` on port 19101, `be-2` on port 19102, `be-3` on port 19103).
2. Start `dicp_lb` on port 18080.
3. Test **Round Robin** (uniform 1:1:1 distribution).
4. Test **Weighted Round Robin** (weights: 5, 3, 2 -> exact 50%, 30%, 20% distribution).
5. Test **Least Connections** (dynamic connection balancing).
6. Test **Consistent Hashing** (virtual nodes sticky routing).
7. Test **Backend Crash Failover** (kills `be-2`, swaps snapshot atomically, sends 300 requests with 0 errors).
8. Save results to `simulator/traffic_generator/benchmark_results.json`.

---

## 9. Running via Docker Compose (Method 2 - Recommended for Full Demo)

Run a complete multi-container cluster including 3 Node Agents, 2 backend demo services, 1 Load Balancer, 1 Management API, Prometheus, and Grafana:

### Step 9.1: Build and Launch Containers

```bash
cd "/home/pbyanshu/Downloads/CN PORJECT/deployments/compose"

# Start the cluster in the background
docker compose up -d
```

### Step 9.2: Verify Running Containers

```bash
docker compose ps
```

You should see 8 containers running:
- `dicp-node-agent-1` (Seed agent, ports: `9002`, `9000/udp`)
- `dicp-node-agent-2` (Peer agent, ports: `9102`, `9100/udp`)
- `dicp-node-agent-3` (Peer agent, ports: `9202`, `9200/udp`)
- `dicp-backend-1` (Order service instance 1)
- `dicp-backend-2` (Order service instance 2)
- `dicp-load-balancer` (HTTP proxy on port `80`)
- `dicp-management-api` (FastAPI on port `8000`)
- `dicp-prometheus` (Prometheus on port `9090`)
- `dicp-grafana` (Grafana on port `3000`)

---

## 10. Interacting with the Running Cluster

### 10.1 Query Cluster Membership
```bash
curl -s http://localhost:8000/api/v1/cluster/members | jq .
```

### 10.2 Register a New Service Instance
```bash
curl -X POST http://localhost:8000/api/v1/cluster/services/register \
  -H "Authorization: Bearer secret-token-123" \
  -H "Content-Type: application/json" \
  -d '{
    "service_name": "order-service",
    "instance_id": "order-service-inst-1",
    "host": "backend-1",
    "port": 8080,
    "weight": 5,
    "health_check_url": "http://backend-1:8080/health"
  }'
```

### 10.3 Discover Service Endpoints
```bash
curl -s http://localhost:8000/api/v1/cluster/services/order-service | jq .
```

### 10.4 Update Distributed Configuration
```bash
curl -X POST http://localhost:8000/api/v1/cluster/config \
  -H "Authorization: Bearer secret-token-123" \
  -H "Content-Type: application/json" \
  -d '{
    "key": "max_pool_size",
    "value": "250",
    "scope": "GLOBAL"
  }'
```

### 10.5 Send Traffic Through the Load Balancer
```bash
# Send request to default service
curl -i http://localhost:80/service/order-service/api/items

# Send sticky-session request using Consistent Hashing key
curl -i -H "X-Routing-Key: user_12345" http://localhost:80/
```

### 10.6 Inspect Load Balancer Active Backends
```bash
curl -s http://localhost:80/admin/backends | jq .
```

---

## 11. Accessing Observability Dashboards

### Prometheus
- **URL**: [http://localhost:9090](http://localhost:9090)
- **Useful queries**:
  - `dicp_cluster_members`: Number of known cluster members
  - `dicp_gossip_rounds_total`: Total gossip rounds executed
  - `dicp_proxy_requests_total`: Total requests handled by load balancer
  - `dicp_suspicion_events_total`: Node suspicion occurrences

### Grafana Dashboards
- **URL**: [http://localhost:3000](http://localhost:3000)
- **Login**:
  - **Username**: `admin`
  - **Password**: `admin`
- Dashboards are pre-provisioned under **Dashboards** $\rightarrow$ **DICP Overview**:
  - Live Node Cluster Health
  - Active Service Registry Counts
  - Gossip Messages per Second
  - Load Balancer Requests & Latency Distribution

---

## 12. Stopping and Cleanup

### To Stop Docker Compose:
```bash
cd "/home/pbyanshu/Downloads/CN PORJECT/deployments/compose"
docker compose down
```

### To Stop Local Background Processes:
If you ran processes locally:
```bash
killall -9 dicp_agent dicp_lb python3 2>/dev/null
```

---

## 13. Troubleshooting & FAQ

1. **Error: `bind: Address already in use`**
   - Check if an earlier run is still holding ports:
     ```bash
     sudo lsof -i :9000,9001,9002,18080,8080,8000
     ```
   - Kill conflicting processes or change the ports in the respective `node.json` file.

2. **Error: `Direct IP access is not allowed` or `HTTP 400 Bad Request`**
   - This occurs if your shell has an active HTTP proxy variable (`http_proxy=...`).
   - Run: `export NO_PROXY="127.0.0.1,localhost"` before running python scripts or curl commands.

3. **Error: `libprotoc.so.32: cannot open shared object file`**
   - Run `export LD_LIBRARY_PATH=/home/pbyanshu/.local/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH` or install `libprotobuf-dev` system-wide.

4. **Tests timing out during lease expiration:**
   - The test `test_service_registry` verifies lease expiration after 2 seconds. Allow `ctest` to finish naturally (~3.5s total execution time).
