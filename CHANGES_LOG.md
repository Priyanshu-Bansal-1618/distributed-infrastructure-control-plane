# Session Changes Log & Modification Record
**Project:** Distributed Infrastructure Control Plane (DICP)  
**Date:** 17 November 2026  
**Status:** All tests passing (11/11 C++ GoogleTest, 4/4 Python Pytest).

This document records all modifications made to the codebase to implement the **Module 6 Observability & Management Dashboard**, resolve environment hardcodings, and ensure cross-platform compatibility.

---

## 1. Summary of Changes

| File | Status | Nature of Change |
|---|---|---|
| `management/dashboard/index.html` | **Created & Enhanced** | Full-featured dark-mode Single Page Application (SPA) dashboard inspired by AxonOps (`Screenshot_2026-11-17_00-23-09.png`). Features D3.js radial topology, live node inspection drawer, physical "Working / Not Working" machine toggles, live 3.0s SWIM suspicion countdown progress bar, dynamic Add Node modal, metrics charts, load balancer strategy switcher, traffic generator, and chaos engineering triggers. |
| `management/services/cluster_service.py` | **Modified** | Added dynamic custom node addition (`add_node`) and removal (`remove_node`), physical state toggling (`set_node_working`), asynchronous background SWIM protocol loop (`_swim_protocol_loop`) with 3.0s suspicion timer and refutation with monotonic incarnation increment ($I \leftarrow I + 1$), topology data generation (`get_topology_data`), live event stream buffering (`get_events`), load balancer client integration (`get_lb_status`, `set_lb_strategy`), and traffic burst generator (`send_test_traffic`). |
| `management/api/app.py` | **Modified** | Mounted HTML routes (`GET /` and `GET /dashboard`), added REST endpoints for `/api/v1/nodes/add`, `/api/v1/nodes/{node_id}/delete`, `/api/v1/nodes/{node_id}/toggle_working`, `/api/v1/topology`, `/api/v1/events`, `/api/v1/loadbalancer/*`, and `/api/v1/operations/chaos`. Fixed environment variable name aliases (`DICP_API_TOKEN` / `API_TOKEN`, `AGENT_API_URL` / `AGENT_URL`). |
| `CMakeLists.txt` | **Modified** | Replaced rigid hardcoded paths (`/home/pbyanshu/...`) with standard CMake dynamic discovery (`find_package(Protobuf)`, `find_package(GTest)`), maintaining conditional fallback for local user paths without breaking the current environment. |
| `simulator/cluster_simulator/simulator.py` | **Modified** | Replaced hardcoded relative binary paths (`./build/...`) with dynamic script-relative path resolution (`os.path.join(ROOT_DIR, "build", ...)`). Replaced hardcoded POSIX `/tmp/` with portable `tempfile.gettempdir()`. |

---

## 2. Detailed File Modifications

### A. [`management/dashboard/index.html`](file:///home/pbyanshu/Downloads/CN%20PORJECT/management/dashboard/index.html) *(New File)*
* **Purpose:** Implements the complete graphical interface requested for course project Module 6.
* **Key Features:**
  1. **Radial Topology Graph (`GRAPH VIEW`)**: Uses D3.js v7 force simulation. Renders a central cluster hub branching out to subnets and glowing concentric rings for leaf nodes. Links animate pulses representing UDP gossip datagrams. Nodes change color dynamically (Green = `ALIVE`, Amber = `SUSPECT`, Red = `DEAD`, Gray = `LEFT`).
  2. **Table View (`LIST VIEW`)**: Displays tabular data of node IDs, statuses, monotonic incarnation counters, IP addresses, UDP gossip ports, TCP sync ports, and REST ports.
  3. **Node Inspector Drawer**: Slide-over panel triggered by clicking any node in the graph, showing hardware/network details and action buttons (`Crash Node`, `Recover Node`).
  4. **Telemetry Dashboard**: Displays live stat cards for Active Nodes, Healthy Services, Proxy Throughput (rps), and P99 Latency (ms), along with Chart.js charts for backend request distribution and gossip/SWIM rates.
  5. **Service Registry Tab**: Shows registered microservices and their health probe status, plus an interactive modal form to register custom services.
  6. **Load Balancer Tab**: Allows hot-switching routing algorithms (`round_robin`, `weighted_round_robin`, `least_connections`, `consistent_hash`) and includes a "Send Request Burst" tool with animated distribution progress bars.
  7. **Logs & Events Tab**: Live streaming log console showing color-coded audit events.
  8. **Chaos & Operations Tab**: One-click scenario triggers for demonstration.

---

### B. [`management/services/cluster_service.py`](file:///home/pbyanshu/Downloads/CN%20PORJECT/management/services/cluster_service.py) *(Modified)*
* **Before:** Simple fallback returning static node count 0 on failure, hardcoded to port 8080.
* **After:**
  * **Asynchronous SWIM Protocol Loop (`_swim_protocol_loop`)**: Runs every 1.0s in background. Continuously inspects node physical states (`working: True/False`).
  * **Failure Detection Lifecycle**:
    * When a node is marked `working: False` (or drops offline), probe failures immediately trigger transition to `SUSPECT` with a 3.0s armed timer (`suspicion_timer_expires`).
    * Emits live events: `[SWIM_PROBE_FAIL]` and `[SWIM_SUSPECT]`.
    * If unrefuted after 3.0s, automatically transitions to `DEAD` (status 3), emitting `[SWIM_DEAD]` and evicting all services hosted by this node from the active load balancer snapshot.
    * When marked `working: True` (node restart), the node reads its local state, detects the stale rumor, increments its monotonic incarnation counter ($I \leftarrow I + 1$), broadcasts `[SWIM_ALIVE]`, refutes the suspicion/death rumor, and restores its services.
  * **Dynamic Node Management**:
    * `add_node(node_id, address, gossip_port, sync_port, mgmt_port)`: Adds user-defined custom nodes dynamically to the cluster.
    * `remove_node(node_id)`: Marks node as `LEFT` (status 4) and departs cleanly.
    * `set_node_working(node_id, working: bool)`: Toggles physical responsiveness.
  * Added auto-discovery across candidate ports (`9002`, `9102`, `9202`, `19502`, `19602`, `8080`).
  * Implemented `get_topology_data()` with real-time countdown calculation (`suspicion_time_left`).
  * Added `get_lb_status()` and `set_lb_strategy()` to query and control the reverse proxy (`dicp_lb`).
  * Added `send_test_traffic()` to send real or simulated request bursts and record backend distribution ratios.
  * Maintained an in-memory event buffer (`_events`) for the live audit log.

---

### C. [`management/api/app.py`](file:///home/pbyanshu/Downloads/CN%20PORJECT/management/api/app.py) *(Modified)*
* **Before:** Existed as an unmounted REST API requiring Bearer tokens for all mutations, missing dashboard routes.
* **After:**
  * Added HTML response routes:
    ```python
    @app.get("/", response_class=HTMLResponse)
    @app.get("/dashboard", response_class=HTMLResponse)
    async def serve_dashboard(): ...
    ```
  * Added REST endpoints for the dashboard:
    * `GET /api/v1/topology`
    * `GET /api/v1/events`
    * `GET /api/v1/loadbalancer`
    * `POST /api/v1/loadbalancer/strategy`
    * `POST /api/v1/loadbalancer/traffic`
    * `POST /api/v1/operations/chaos`
    * `POST /api/v1/ui/services/register`
  * Fixed environment variable fallback:
    ```python
    API_SECRET_TOKEN = os.environ.get("DICP_API_TOKEN") or os.environ.get("API_TOKEN", "secret-token-123")
    ```

---

### D. [`CMakeLists.txt`](file:///home/pbyanshu/Downloads/CN%20PORJECT/CMakeLists.txt) *(Modified)*
* **Before:**
  ```cmake
  list(APPEND CMAKE_PREFIX_PATH "/home/pbyanshu/.local/usr" "/home/pbyanshu/.local")
  include_directories("/home/pbyanshu/.local/usr/include")
  link_directories("/home/pbyanshu/.local/usr/lib/x86_64-linux-gnu")
  set(Protobuf_PROTOC_EXECUTABLE "/home/pbyanshu/.local/bin/protoc")
  find_package(Protobuf REQUIRED)
  ```
* **After:**
  Wrapped user paths inside `if(EXISTS ...)` checks, and added standard CMake fallback:
  ```cmake
  if(EXISTS "/home/pbyanshu/.local/usr")
      list(APPEND CMAKE_PREFIX_PATH "/home/pbyanshu/.local/usr" "/home/pbyanshu/.local")
      include_directories("/home/pbyanshu/.local/usr/include")
      link_directories("/home/pbyanshu/.local/usr/lib/x86_64-linux-gnu")
  endif()

  if(EXISTS "/home/pbyanshu/.local/bin/protoc")
      set(Protobuf_PROTOC_EXECUTABLE "/home/pbyanshu/.local/bin/protoc")
  endif()
  find_package(Protobuf REQUIRED)
  if(NOT Protobuf_PROTOC_EXECUTABLE)
      find_program(Protobuf_PROTOC_EXECUTABLE protoc REQUIRED)
  endif()
  ```

---

### E. [`simulator/cluster_simulator/simulator.py`](file:///home/pbyanshu/Downloads/CN%20PORJECT/simulator/cluster_simulator/simulator.py) *(Modified)*
* **Before:**
  ```python
  BIN_AGENT = os.path.abspath("./build/dicp_agent")
  BIN_LB = os.path.abspath("./build/dicp_lb")
  config_path = f"/tmp/{node_id}_config.json"
  db_path = f"/tmp/{node_id}.db"
  ```
* **After:**
  ```python
  ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
  BIN_AGENT = os.path.join(ROOT_DIR, "build", "dicp_agent")
  BIN_LB = os.path.join(ROOT_DIR, "build", "dicp_lb")
  
  import tempfile
  temp_dir = tempfile.gettempdir()
  config_path = os.path.join(temp_dir, f"{node_id}_config.json")
  db_path = os.path.join(temp_dir, f"{node_id}.db")
  ```

---

## 3. Verification & Test Integrity

After all changes were made, the entire test suite was re-executed:
1. **C++ Unit Tests (`ctest --output-on-failure`)**: **11/11 Passed (100%)**
2. **Python Management API Tests (`pytest`)**: **4/4 Passed (100%)**
3. **Dashboard Endpoints Verification (`TestClient`)**: **100% Operational**
