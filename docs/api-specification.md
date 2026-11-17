# DICP API Specification

## 1. Node Agent Internal HTTP API (Port 8080)

Exposed by each C++ Node Agent for local interactions and proxy snapshot queries.

### Endpoints:
- `GET /api/v1/agent/self`: Information about this node (id, incarnation, status, endpoints).
- `GET /api/v1/agent/members`: Current cluster membership list.
- `GET /api/v1/agent/services`: List of all registered services and instances.
- `GET /api/v1/agent/services/{service_name}/snapshot`: Active, healthy instance snapshot formatted for the Load Balancer.
- `POST /api/v1/agent/services/register`: Register a local service instance.
  ```json
  {
    "instance_id": "payment-node1-9001",
    "service_name": "payment",
    "address": "10.0.0.1",
    "port": 9001,
    "protocol": "HTTP",
    "health_check_url": "http://10.0.0.1:9001/health",
    "lease_seconds": 30,
    "weight": 10
  }
  ```
- `POST /api/v1/agent/services/deregister`: Explicitly deregister an instance.
  ```json
  {
    "service_name": "payment",
    "instance_id": "payment-node1-9001"
  }
  ```
- `POST /api/v1/agent/services/heartbeat`: Renew instance lease.
  ```json
  {
    "service_name": "payment",
    "instance_id": "payment-node1-9001"
  }
  ```
- `POST /api/v1/agent/drain`: Request graceful draining and departure of this node.
- `GET /metrics`: Prometheus metrics scrape endpoint.

---

## 2. Load Balancer Management API (Port 8081 / Admin)

- `GET /admin/backends`: Current active snapshot of eligible backends.
- `PUT /admin/strategy`: Change routing strategy dynamically (`round_robin`, `weighted_round_robin`, `least_connections`, `consistent_hash`).
- `GET /metrics`: Load balancer Prometheus metrics.

---

## 3. Python Management API (FastAPI, Port 8000)

Administrative API providing an aggregated cluster view and configuration workflows.
- `GET /api/v1/cluster`: Overview of cluster health, active nodes, total services.
- `GET /api/v1/nodes`: Known cluster nodes and their states.
- `GET /api/v1/nodes/{node_id}`: Detailed node status.
- `POST /api/v1/nodes/{node_id}/drain`: Initiate graceful node drain.
- `GET /api/v1/services`: List of registered services and counts.
- `GET /api/v1/services/{name}/instances`: Query instances for service `{name}`.
- `POST /api/v1/services/register`: Proxied registration to target node agent.
- `DELETE /api/v1/services/{name}/instances/{instance_id}`: Deregister an instance.
- `GET /api/v1/config`: Get cluster configurations.
- `POST /api/v1/config`: Create a configuration key.
- `PUT /api/v1/config/{config_id}`: Publish a new configuration version.
- `GET /api/v1/config/{config_id}/history`: Revision history for audit.
- `POST /api/v1/config/{config_id}/rollback`: Rollback to previous version.
- `GET /api/v1/loadbalancer/backends`: Load balancer view of backends.
- `GET /api/v1/loadbalancer/stats`: Upstream request metrics.
- `PUT /api/v1/loadbalancer/strategy`: Update active load balancing strategy.
