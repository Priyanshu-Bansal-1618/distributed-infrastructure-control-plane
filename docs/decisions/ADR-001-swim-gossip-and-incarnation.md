# ADR-001: SWIM Gossip Protocol with Suspicion and Incarnation Numbers

## Status
Accepted

## Context
A distributed control plane requires robust, decentralized failure detection and membership convergence without single points of failure. Naive heartbeating scales as $O(N^2)$ network traffic. Simple timeouts can falsely suspect healthy nodes during transient network latency or packet loss.

## Decision
We implement a SWIM-inspired failure detector with two key extensions:
1. **Indirect Probing (`PING_REQ`)**: When a direct `PING` fails, the node requests $k$ random intermediaries to probe the target, preventing false suspicion caused by localized path degradation.
2. **Suspicion Mechanism & Incarnations**: Instead of immediately marking an unresponsive node `DEAD`, it transitions to `SUSPECT` with a configurable timeout. A suspected node can refute false accusations by broadcasting an `ALIVE` message with an incremented monotonic incarnation counter ($I + 1$).
3. **Deterministic Precedence**: Update conflicts are resolved deterministically using incarnation comparison, followed by status precedence (`DEAD` > `SUSPECT` > `ALIVE` at equal incarnation), preventing timestamp synchronization issues.

## Consequences
- False positive failure declarations are minimized under high load and lossy networks.
- Membership traffic scales at $O(1)$ per node per cycle, with dissemination reaching $O(\log N)$ convergence.
