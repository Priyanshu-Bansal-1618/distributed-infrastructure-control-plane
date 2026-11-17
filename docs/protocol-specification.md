# Distributed Infrastructure Control Plane - Protocol Specification

## 1. Overview

Communication between Node Agents in the DICP cluster is divided into two channels:
1. **Unreliable Datagram Transport (UDP)**: Used for time-sensitive, low-overhead failure detection probes (PING, ACK, PING_REQ, ACK_RELAY) and bounded opportunistic gossip dissemination updates.
2. **Reliable Framed Stream Transport (TCP)**: Used for state synchronization (anti-entropy full push-pull reconciliation) and large data transfer that exceeds UDP datagram limits.

---

## 2. Wire Format & Framing

### 2.1 UDP Datagrams
All UDP datagrams are capped at a configurable MTU-safe ceiling (`max_gossip_message_bytes = 1200` by default) to prevent IP fragmentation.

Each datagram has the following structure:
```
+--------------------------------------------------------------+
| Magic Bytes (4B) : 0x44 0x49 0x43 0x50 ("DICP")             |
+--------------------------------------------------------------+
| Protocol Version (2B) : uint16_t (e.g., 0x0001)             |
+--------------------------------------------------------------+
| Message Type (2B) : uint16_t                                 |
+--------------------------------------------------------------+
| Payload Length (4B) : uint32_t                               |
+--------------------------------------------------------------+
| Serialized Protobuf Payload (Variable length <= 1200 bytes)  |
+--------------------------------------------------------------+
```

### 2.2 TCP Framing
TCP connections use length-prefixed framing:
```
+--------------------------------------------------------------+
| Magic Bytes (4B) : 0x44 0x49 0x43 0x50 ("DICP")             |
+--------------------------------------------------------------+
| Payload Length (4B) : uint32_t (Big-Endian / Network Order)  |
+--------------------------------------------------------------+
| Serialized Protobuf Payload (Length bytes)                   |
+--------------------------------------------------------------+
```

---

## 3. Message Types

| Type ID | Enum Name | Description | Transport |
|---|---|---|---|
| `0x0001` | `MSG_PING` | Direct SWIM probe from Node A to Node B | UDP |
| `0x0002` | `MSG_ACK` | Direct probe acknowledgment from Node B to Node A | UDP |
| `0x0003` | `MSG_PING_REQ` | Indirect probe request from Node A to intermediary Node C | UDP |
| `0x0004` | `MSG_ACK_RELAY` | Indirect probe acknowledgment relayed from Node C to Node A | UDP |
| `0x0005` | `MSG_GOSSIP_BATCH` | Piggybacked/standalone dissemination batch of updates | UDP |
| `0x0010` | `MSG_SYNC_REQUEST` | Anti-entropy state digest request | TCP |
| `0x0011` | `MSG_SYNC_RESPONSE` | Anti-entropy state payload response | TCP |

---

## 4. Replay Protection & Duplicate Detection

- **Sender Sequence Number**: Every message carries a monotonic `uint64_t sender_sequence` per sending node.
- **Message Identifier**: Unique string UUID (`message_id`) or hash `(sender_node_id, sender_sequence)`.
- **LRU Sliding Window**: Nodes maintain an LRU cache of recently seen message identifiers (default capacity: 10,000 entries) to suppress duplicate broadcasts.
- **Maximum Age Window**: Messages with logical timestamps or sequence numbers outside acceptable bounds are discarded.
