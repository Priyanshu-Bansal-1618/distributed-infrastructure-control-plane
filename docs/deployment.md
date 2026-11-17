# Distributed Infrastructure Control Plane - Deployment Guide

## 1. Prerequisites

* **Operating System**: Linux (Ubuntu 22.04+, Debian 12+, RHEL 9+)
* **Container Runtime**: Docker 24.0+ & Docker Compose v2.20+
* **System Requirements**:
  * Minimum: 2 CPU cores, 2 GB RAM, 10 GB disk
  * Recommended: 4+ CPU cores, 8 GB RAM, NVMe storage

---

## 2. Docker Compose Multi-Node Deployment

DICP includes a turn-key multi-node cluster deployment with 3 Node Agents, 2 backend demo services, 1 HTTP load balancer, 1 management API, Prometheus, and Grafana.

### 2.1 Starting the Cluster

```bash
cd deployments/compose
docker compose up -d
```

### 2.2 Verifying Cluster Health

```bash
# Check status of all containers
docker compose ps

# Query cluster members via Management API
curl -s http://localhost:8000/api/v1/cluster/members | jq .

# Check Load Balancer active backends
curl -s http://localhost/admin/backends | jq .
```

### 2.3 Accessing Observability Dashboards

* **Grafana Dashboard**: [http://localhost:3000](http://localhost:3000) (Username: `admin` / Password: `admin`)
  * Pre-provisioned dashboards include Cluster Topology, Suspicion Rates, Gossip Throughput, and Load Balancer Routing.
* **Prometheus Metrics**: [http://localhost:9090](http://localhost:9090)

---

## 3. Bare-Metal & Systemd Service Deployment

### 3.1 Compiling from Source

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get update && sudo apt-get install -y \
    build-essential g++ cmake ninja-build \
    libboost-all-dev libprotobuf-dev protobuf-compiler \
    libsqlite3-dev nlohmann-json3-dev

# Build binaries
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja dicp_agent dicp_lb
sudo cp dicp_agent dicp_lb /usr/local/bin/
```

### 3.2 Systemd Unit File: `dicp-agent.service`

Create `/etc/systemd/system/dicp-agent.service`:

```ini
[Unit]
Description=DICP Node Agent
After=network.target

[Service]
Type=simple
User=dicp
Group=dicp
ExecStart=/usr/local/bin/dicp_agent /etc/dicp/config.json
Restart=always
RestartSec=3
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

Enable and start the service:
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now dicp-agent
```

---

## 4. Configuration Tuning Guide

| Parameter | Recommended Dev | Recommended Prod | Purpose |
|---|---|---|---|
| `gossip.interval_ms` | `100` | `200` | Interval between gossip dissemination rounds |
| `gossip.fanout` | `3` | `3 - 5` | Number of random peers contacted per round |
| `failure_detection.probe_interval_ms` | `150` | `500` | Frequency of SWIM direct probes |
| `failure_detection.probe_timeout_ms` | `80` | `200` | Direct ping timeout before ping-req |
| `failure_detection.suspicion_timeout_ms` | `300` | `2000` | Grace period before suspect node marked DEAD |
| `storage.database_path` | `/tmp/node.db` | `/var/lib/dicp/node.db` | Path to persistent SQLite database |
