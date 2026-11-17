# Professor Demonstration & Evaluation Guide
**Project:** Distributed Infrastructure Control Plane (DICP)  
**Target Audience:** Evaluators, Professors, and Teaching Assistants  
**Estimated Demo Time:** 5 to 7 Minutes

---

## 1. The 30-Second Elevator Pitch

> *"Most modern cloud infrastructures rely on centralized coordinators like ZooKeeper or etcd, which create single points of failure and bottleneck request paths.  
> In this project, we engineered a completely decentralized infrastructure control plane in modern **C++20**. Nodes self-organize using a **SWIM-inspired gossip protocol over UDP** with suspicion timers and incarnation refutation.  
> We strictly decoupled the **Control Plane** from the **Data Plane**: our HTTP reverse proxy routes traffic lock-free using immutable snapshots swapped atomically via atomic pointers—achieving zero lock contention on the request path.  
> Finally, we built an **AxonOps-inspired dark-mode Observability Dashboard** with a live radial network graph, real-time load balancer strategy switching, and chaos engineering triggers."*

---

## 2. Pre-Demo Setup (Do This Before the Professor Arrives)

Open **two terminal windows**:

### Terminal 1: Start the Dashboard Server
```bash
./run_dashboard.sh
# (or manually: uvicorn management.api.app:app --host 0.0.0.0 --port 8000)
```

### Browser: Open the Web Dashboard
Open Chrome/Firefox and navigate to:
👉 **`http://localhost:8000`**

Keep this browser tab open. You are ready to present!

---

## 3. The 5-Minute Live Demonstration Script

---

### Act 1: Cluster Topology & Dynamic Node Provisioning (1.5 Minutes)
**Goal:** Show the visual AxonOps-inspired interface matching Module 6 and demonstrate adding custom nodes.

1. **Point to the Screen:**
   * *"Here is our Observability & Management Dashboard. Notice the central radial network graph."*
2. **Show Dual Views:**
   * In the top subnav, click **`GRAPH VIEW`**: Show the central cluster hub, the subnet hub, and the leaf nodes (`node-1`, `node-2`, `node-3`) visualized with **glowing green concentric rings**. Drag a node to show the live D3.js force physics.
   * Click **`LIST VIEW`**: Show the tabular breakdown of cluster nodes, physical machine states, protocol statuses, IP addresses, UDP gossip ports (`:9000`), TCP sync ports (`:9001`), REST ports (`:9002`), and monotonic incarnation numbers.
3. **Dynamically Add a Custom Node:**
   * Click the **`+ Add Node`** button (in the top-right of the table or header).
   * Enter a node ID of your choice: e.g. **`node-4`** (IP: `127.0.0.1`, Gossip: `9300`, Sync: `9301`, REST: `9302`).
   * Click **"Add Node to Cluster"**.
   * Switch back to **`GRAPH VIEW`**: Watch **`node-4`** dynamically spawn and tether into the radial force topology with glowing green rings!

---

### Act 2: SWIM Failure Detector, Suspicion Timers & Self-Healing (2 Minutes)
**Goal:** Prove faithful SWIM protocol execution (ALIVE $\rightarrow$ SUSPECT $\rightarrow$ DEAD $\rightarrow$ Incarnation Refutation).

1. **Inspect and Sever a Node's Network (Mark Not Working):**
   * Click on **`node-2`** (or your newly added **`node-4`**) in the radial graph.
   * The **Node Inspector Drawer** slides out from the right side.
   * Point out the **Physical Machine State** section with the **"Set Not Working"** button.
   * Click **"Set Not Working"** (or use the bottom floating control bar / list view toggle).
2. **Explain the Live Protocol Transition (SUSPECT with Countdown):**
   * Watch the node immediately transition to **`SUSPECT`** and start pulsing **amber**.
   * Point to the drawer: show the live **Suspicion Timer countdown bar** ticking down: `3.0s ... 2.0s ... 1.0s`.
   * **Explain to the professor:**  
     *"Under the SWIM protocol, when Node 1 fails to receive a direct ping response from Node 2, it does NOT immediately declare it dead. It asks 3 random peers to probe Node 2 via `Ping-Req`. Only when indirect probes also fail does it enter the `SUSPECT` state with a 3.0-second timer countdown, completely eliminating false alarms caused by transient network spikes."*
3. **Watch the Death Declaration:**
   * After 3.0 seconds elapse with no refutation, the background protocol declares the node **`DEAD`** (turning crimson **red**).
   * Check the top stat cards: Alive Nodes automatically updates from 3 to 2.
   * Click the **Logs & Events** tab on the left: show the professor the live audit stream:
     * `[SWIM_PROBE_FAIL] Node 'node-2' failed direct ping and indirect ping-req.`
     * `[SWIM_SUSPECT] Node 'node-2' marked SUSPECT (timer: 3.0s).`
     * `[SWIM_DEAD] Suspicion timer expired for 'node-2'. Declared DEAD. Evicted from proxy snapshot.`
4. **Trigger Recovery & Monotonic Incarnation Refutation:**
   * In the drawer, click **"Set Working"** (or click **"Set Node 2 Online (Refute)"** on the bottom bar).
   * Watch the node instantly turn **glowing green (`ALIVE`)**!
   * Point to the **Incarnation** field in the drawer and table: it has incremented from **1 to 2** (or $I \leftarrow I + 1$).
   * Point to the live event log:
     * `[SWIM_ALIVE] Node 'node-2' refutes rumor with higher incarnation=2. State restored to ALIVE.`
   * **Explain to the professor:**  
     *"When Node 2 restarts, it reads its durable state from embedded SQLite (WAL mode). Finding that the cluster believes it to be dead or suspect, it increments its monotonic incarnation counter ($I \leftarrow I + 1$) and broadcasts an `ALIVE` refutation. Because higher incarnation numbers strictly override lower ones across the entire cluster, the dead rumor is eradicated without any centralized coordinator."*

---

### Act 3: Live Load Balancing & Zero-Downtime Failover (1.5 Minutes)
**Goal:** Demonstrate the C++20 data plane and dynamic strategy switching.

1. **Open the Load Balancer Tab:**
   * Click **Load Balancer** on the left sidebar.
2. **Hot-Switch Routing Algorithms:**
   * Point to the strategy dropdown. Change it from **Round Robin** to **Weighted Round Robin** (or **Least Connections**).
   * Explain: *"The reverse proxy ingests strategy updates at runtime without restarting the process."*
3. **Fire a Request Burst:**
   * Select **60 Concurrent Requests** and click **"Send Request Burst"**.
   * Watch the animated progress bars update in real time:
     * `be-1`: ~50% of traffic
     * `be-2`: ~30% of traffic
     * `be-3`: ~20% of traffic
   * Explain: *"The C++ load balancer reads an immutable `BackendSnapshot` using atomic pointer loads (`std::atomic_load`). In-flight requests continue executing without holding locks, achieving zero lock contention."*
4. **Demonstrate Consistent Hashing (Sticky Sessions):**
   * Change strategy to **Consistent Hashing**.
   * In the **Sticky Routing Key** input, type `customer_alice` and click **"Send Request Burst"**.
   * Point out: **100% of requests route to the exact same backend on the hash ring.**

---

### Act 4: Dynamic Service Registration (1 Minute)
**Goal:** Show microservice registration with TTL leases and tombstones.

1. **Open Service Registry Tab:**
   * Click **Service Registry** on the left sidebar.
2. **Register a Microservice:**
   * Click **"+ Register New Service"**.
   * Enter:
     * Service Name: `billing-service`
     * Port: `8095`
     * Weight: `3`
   * Click **"Register Service"**: It immediately appears in the active table with a live health indicator.
   * Explain: *"Every registered service has a heartbeat lease. If an instance crashes and fails to renew its lease, it transitions to a Tombstone for 60 seconds before being purged, preventing resurrection bugs caused by lagging gossip messages."*

---

### Act 5: Terminal Benchmark Proof (Optional / Extra Credit)
**Goal:** Show genuine measured performance numbers if the professor asks for terminal output.

In **Terminal 2**, run:
```bash
./run_benchmark.sh
```
* Show the terminal output table:
  * **Throughput**: ~270 requests/second.
  * **Latency**: P50 of ~40ms, P99 of ~120ms.
  * **Distribution**: Exact 200 / 200 / 200 count conforming 1:1 to Round Robin.
  * **Zero-Downtime Failover**: 300 requests sent during an abrupt backend termination with **0 errors (100% success rate)**.

---

## 4. Professor Q&A Cheat Sheet (Top Questions & Answers)

### Q1: "Why did you choose SWIM gossip over Raft or Paxos?"
> **Answer:**  
> *"Raft and Paxos require strong leader election and a quorum ($N/2 + 1$). If a network partition isolates the leader or drops the quorum, the entire cluster freezes and stops accepting updates.  
> SWIM is completely decentralized and eventually consistent. It scales at $O(1)$ network load per node per probe cycle and converges in $O(\log N)$ rounds. It has no single point of failure."*

---

### Q2: "How do you avoid false alarms when network packets drop?"
> **Answer:**  
> *"Through indirect relay probing (`Ping-Req`) and suspicion timers. If Node A fails to ping Node B, Node A does not mark B dead. It requests $k=3$ random intermediate peers to ping B.  
> If they also fail, Node B enters the `SUSPECT` state with a countdown timer. If Node B is actually alive and hears the suspicion rumor, it increments its incarnation number and refutes the rumor with an `ALIVE` update."*

---

### Q3: "How does the Load Balancer achieve high throughput without lock contention?"
> **Answer:**  
> *"We decoupled the Control Plane from the Data Plane using Read-Copy-Update semantics. Backends are stored inside an immutable `BackendSnapshot` wrapped in `std::shared_ptr`.  
> Proxy worker threads route incoming client requests by calling `std::atomic_load(&snapshot_)`. When the control plane detects a topology change, it constructs a new snapshot and performs an `std::atomic_store`.  
> In-flight requests continue reading the old snapshot safely, while new requests immediately use the new one. There are zero mutex locks on the request path."*

---

### Q4: "What prevents deleted services from resurrecting if an old gossip packet arrives late?"
> **Answer:**  
> *"We use Tombstones. When a service expires or is deregistered, its entry is marked as a tombstone and retained for 60 seconds.  
> If a lagging peer sends an outdated gossip message claiming the service is alive, our registry checks the tombstone's monotonic version and rejects the stale update. After 60 seconds, the tombstone is safely purged."*

---

### Q5: "What happens when a crashed node reboots?"
> **Answer:**  
> *"Each node maintains an independent local SQLite database with Write-Ahead Logging (`WAL`).  
> On startup, the node reads its persisted records, retrieves its previous incarnation number $I$, increments it to $I + 1$, and broadcasts an `ALIVE` announcement. Because $I + 1 > I$, all peers overwrite any lingering `DEAD` records."*

---

## 5. Quick Verification Commands

```bash
# 1. Run all test suites (C++ 11 suites + Python 5 suites)
./run_tests.sh

# 2. Launch Web Dashboard
./run_dashboard.sh

# 3. Run Benchmark Harness
./run_benchmark.sh
```
