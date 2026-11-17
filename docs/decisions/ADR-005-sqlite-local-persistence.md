# ADR-005: SQLite for Local Node-Scoped Durability

## Status
Accepted

## Context
When a Node Agent restarts after a crash or planned maintenance, it must recover its node identity, incarnation counter, and registered services to prevent duplicate node collisions or stale cluster state.

## Decision
Each Node Agent maintains a private SQLite database (`node.db`).
- **No Shared Cluster Database**: SQLite is strictly node-local.
- **Off-Event-Loop Asynchronous Worker**: All database reads/writes are queued to a dedicated storage thread using a thread-safe task queue.
- **WAL Mode**: Write-Ahead Logging enables concurrent readers without database locking.

## Consequences:
- Fast, ACID-compliant local storage without external database daemon dependencies.
- Zero latency interference with high-frequency network packets.
