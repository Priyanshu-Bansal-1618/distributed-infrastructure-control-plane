# DICP Replicated Service Registry Specification

## 1. Data Model

Every service instance registered in DICP is represented as:

```
ServiceInstance {
  instance_id: string          // Globally unique: "service_name-node_id-port"
  service_name: string         // e.g. "payment-service", "order-api"
  owner_node_id: string        // Node Agent managing this instance
  address: string              // IP or resolvable hostname
  port: uint16                 // Listening port
  protocol: HTTP | HTTPS       // Protocol
  service_version: string      // Semantic version: "1.2.0"
  metadata: map<string,string> // Key-value tags (e.g. env=prod, az=us-east-1a)
  health: HealthStatus         // UNKNOWN | HEALTHY | UNHEALTHY | DRAINING
  owner_incarnation: uint64    // Incarnation of owner node at registration
  record_version: uint64       // Monotonic version per instance_id
  lease_expiry: uint64         // Monotonic timestamp (seconds) when lease expires
  deleted: bool                // Tombstone flag for deregistration
  deleted_at: uint64           // Monotonic timestamp when tombstone was created
}
```

---

## 2. Leases & Heartbeats

- **Local Ownership**: Service instances register with their local Node Agent. The local agent becomes the authoritative owner of the instance record.
- **TTL Leases**: Each registration specifies a lease duration (default: 30 seconds).
- **Periodic Renewal**: The owning service or health checker must refresh the lease before expiration.
- **Lease Expiry**: If a lease expires without renewal, the local agent marks the instance `UNHEALTHY` / `DRAINING` and broadcasts an update.
- **Node Demise**: If an owner node transitions to `DEAD` or `LEFT`, all service instances owned by that node are automatically evicted from active routing snapshots across all cluster nodes.

---

## 3. Tombstones & Anti-Entropy Garbage Collection

- **Deregistration**: When a service is explicitly deregistered, it is marked `deleted = true` with a monotonic `record_version + 1` and timestamp `deleted_at`.
- **Tombstone Retention**: Tombstones are retained in the registry for a configurable `tombstone_ttl_seconds` (default: 120 seconds).
- **Convergence**: Retaining tombstones prevents a peer receiving an older push-pull sync from resurrecting a deleted service.
- **Garbage Collection**: After `tombstone_ttl_seconds`, tombstones are purged from memory and the SQLite persistence layer.
