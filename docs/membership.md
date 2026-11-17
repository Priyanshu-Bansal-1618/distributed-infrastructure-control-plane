# DICP Cluster Membership & Failure Detection Specification

## 1. Overview

The DICP membership subsystem is based on the **SWIM (Structured Weakly-Consistent Infection-Style Process Group Membership Protocol)** failure detector with the **Suspicion Mechanism** and **Monotonic Incarnation Numbers**.

---

## 2. Node Lifecycle & States

```
                [BOOT]
                  │
                  ▼
              STARTING
                  │ (Initialize storage, network, load state)
                  ▼
               JOINING
                  │ (Bootstrap via seed nodes)
                  ▼
          ┌───► ALIVE ◄───┐
          │       │       │ (Valid Refutation: Incarnation + 1)
          │       ▼       │
          │    SUSPECT ───┘
          │       │
          │       │ (Suspicion Timeout expires)
          │       ▼
          │     DEAD ───► (Tombstone GC after 24h)
          │
          └───── LEFT (Explicit graceful leave message)
```

### State Definitions:
- **ALIVE**: Node is responsive and actively participating in the cluster.
- **SUSPECT**: Node failed direct and indirect probes. A suspicion timer is running.
- **DEAD**: Suspicion timer expired without refutation. Node is considered failed.
- **LEFT**: Node explicitly departed via graceful shutdown (`drain` / `leave`).

---

## 3. Failure Detection Flow

In each protocol period ($T = \text{probe\_interval\_ms}$, default $1000\,\text{ms}$):
1. **Direct Probe**:
   - Prober selects a random peer from its known membership list.
   - Prober sends `PING(probe_id, sender_incarnation)`.
   - Prober sets a direct timeout timer ($T_{\text{direct}} = \text{probe\_timeout\_ms}$, default $500\,\text{ms}$).
2. **Indirect Probe**:
   - If `ACK(probe_id)` is received within $T_{\text{direct}}$, probe succeeds.
   - If direct timer fires without ACK:
     - Prober selects $k = \text{indirect\_probe\_count}$ (default 3) random peers (excluding target and self).
     - Prober sends `PING_REQ(target_node_id, probe_id)` to each intermediary.
     - Intermediaries send direct `PING` to target; if they receive `ACK`, they relay `ACK_RELAY(target_node_id, probe_id)` back to prober.
3. **Suspicion Declaration**:
   - If all indirect probes time out, target is marked `SUSPECT`.
   - Prober creates a `NodeUpdate(target, incarnation, SUSPECT)` and enqueues it into the gossip dissemination queue.
   - Suspicion timer ($T_{\text{suspect}} = \text{suspicion\_timeout\_ms}$, default $5000\,\text{ms}$) starts on observing nodes.
4. **Refutation**:
   - If the suspected node receives a `SUSPECT` update naming itself with incarnation $I$, it refutes the claim by:
     - Incrementing its incarnation: $I_{\text{new}} = I + 1$.
     - Broadcasting `NodeUpdate(self, I_new, ALIVE)`.
5. **Death Declaration**:
   - If $T_{\text{suspect}}$ elapses and no refutation with higher incarnation is received, the node transitions to `DEAD`.

---

## 4. Deterministic Precedence Rules

When an incoming `NodeUpdate(id, inc_in, status_in)` is evaluated against local `NodeRecord(id, inc_loc, status_loc)`:

| Comparison | Condition | Action |
|---|---|---|
| `inc_in > inc_loc` | Any status | Accept update. Update local record to `(inc_in, status_in)`. |
| `inc_in < inc_loc` | Any status | Reject update. Piggyback local record in gossip to correct sender. |
| `inc_in == inc_loc` | `status_loc == ALIVE` and `status_in == SUSPECT` | Accept update. Transition to `SUSPECT`. Start suspicion timer. |
| `inc_in == inc_loc` | `status_loc == SUSPECT` and `status_in == DEAD` | Accept update. Transition to `DEAD`. Cancel suspicion timer. |
| `inc_in == inc_loc` | `status_loc == SUSPECT` and `status_in == ALIVE` | Ignore update (`SUSPECT` overrides equal incarnation `ALIVE`). |
| `inc_in == inc_loc` | `status_loc == DEAD` and `status_in == ALIVE/SUSPECT` | Ignore update (`DEAD` is absorbing for equal incarnation). |
| `inc_in == inc_loc` | `status_in == LEFT` | Accept update. Transition to `LEFT`. |
| Any | `id == self.id` and `status_in == SUSPECT` | Refute! Increment incarnation: $inc_{\text{self}} = \max(inc_{\text{self}}, inc_{\text{in}}) + 1$. Broadcast `ALIVE`. |
