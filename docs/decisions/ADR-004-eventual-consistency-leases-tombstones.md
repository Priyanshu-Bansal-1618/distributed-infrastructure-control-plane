# ADR-004: Eventual Consistency, Leases, and Tombstones in Service Registry

## Status
Accepted

## Context
A decentralized service registry across dynamic nodes must cope with node crashes, temporary network partitions, and service churn without blocking on multi-datacenter consensus algorithms (like Raft or Paxos) for routine registrations.

## Decision
1. **Local Node Ownership**: Services are registered with their local Node Agent. The local agent assigns the initial `owner_incarnation` and monotonic `record_version`.
2. **Leases**: Every service has a TTL lease. The service or active health check must renew the lease periodically. If a lease expires, the service is marked `UNHEALTHY` / `DRAINING`.
3. **Tombstones with Retention**: When a service is deregistered, a tombstone record with `deleted = true` is broadcasted. Tombstones are retained for 120 seconds before local garbage collection, preventing older synchronization digests from resurrecting dead instances.
4. **Deterministic Merge**: Anti-entropy synchronization applies the record with the higher `owner_incarnation`, breaking ties with `record_version`.

## Consequences:
- High availability for registrations and discoveries during network partitions.
- Eventual convergence across all reachable nodes.
