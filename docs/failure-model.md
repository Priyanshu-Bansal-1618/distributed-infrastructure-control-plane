# DICP Failure Model & Consistency Specification

## 1. Failure Assumptions

DICP operates under an asynchronous network model with crash-recovery processes:
- **Crash-Recovery**: Nodes may crash at any time and may subsequently restart with their persistent state preserved in SQLite.
- **Unreliable Network**: Packets on UDP may be lost, duplicated, delayed, or delivered out of order.
- **Network Partitions**: The network may partition into two or more disjoint components.
- **Clock Skew**: System wall-clock time can drift arbitrarily across nodes. DICP relies strictly on monotonic logical clocks, incarnation counters, and monotonic duration timers—never wall-clock timestamps for distributed correctness.
- **Byzantine Failures**: Nodes are presumed non-Byzantine (crash-fault-tolerant, not arbitrary malicious behavior). Message payloads are validated with schemas and lengths.

---

## 2. Partition Behavior & Recovery

- **During Partition**:
  - Each partition continues operating independently.
  - Nodes in the opposing partition will fail probes, enter `SUSPECT`, and eventually be marked `DEAD` locally.
  - Services registered in the opposite partition will be excluded from the local load balancer snapshot.
- **Healing**:
  - When network connectivity is restored, nodes resume exchanging gossip packets.
  - If a node marked `DEAD` is encountered sending packets with an equal or higher incarnation, anti-entropy TCP sync triggers reconciliation.
  - If a previously partitioned node receives a `DEAD` or `SUSPECT` notice for itself, it increments its incarnation and broadcasts an `ALIVE` refutation.
  - The replicated service registry merges records deterministically: highest `owner_incarnation`, then highest `record_version`.
