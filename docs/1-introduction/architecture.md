# Architecture

## Layer Diagram

```
┌─────────────────────────────────────────────────────┐
│                    Your Application                  │
├─────────────────────────────────────────────────────┤
│  Server / Client          │  Gateway / Daemon        │
│  (connection lifecycle)   │  (auto-routing, config)  │
├───────────────────────────┴─────────────────────────┤
│                    Tunnel                            │
│  (reliability, encryption, multiplexing, congestion) │
├─────────────────────────────────────────────────────┤
│                    Station                           │
│  (bidirectional hooks, rail multiplexing, per-link   │
│   encryption, event bus)                             │
├─────────────────────────────────────────────────────┤
│                    Cart                              │
│  (the universal packet: header, addresses, payload,  │
│   metadata, nonce+MAC)                               │
├─────────────────────────────────────────────────────┤
│              Physical Transport                      │
│  UDP / LoRa / USB / Serial / Unix Socket / Multicast │
└─────────────────────────────────────────────────────┘
```

## Category Breakdown

### Rho Railway (`Rho/`)

The foundation. Two things:

- **Cart** — The universal packet format. Variable-length addresses, optional AEAD encryption, metadata key-value pairs, payload. The wire format is compact: a single header byte followed by optional fields.
- **Station** — The universal I/O port. Bidirectional hooks, rail-based multiplexing, per-station encryption, event-driven callbacks. Stations are pluggable bricks — hook two together and they communicate.

### Rho Tunnel (`Rho/`)

Sits on top of a Station. Turns an unreliable, insecure datagram link into a reliable, encrypted, multiplexed channel.

- **Packets** — Application-level messages with channels, importance flags, and HOL bypass.
- **Bundles** — Wire-level frames containing one or more packets, with bundle IDs for selective ACK.
- **Windowed reliability** — 64-bit receive bitmap for selective ACK/NACK.
- **Congestion control** — `maxInflight` cap on in-flight bundles.
- **Fragmentation** — Packets larger than the bundle MTU are transparently fragmented.
- **AEAD encryption** — X25519 key exchange, HKDF key derivation, per-bundle nonce.

### Rho Lines (`Lines/`)

Everything that makes communication possible between entities that can't physically reach each other.

- **Router** — Trie-based routing engine. Forward match for downward routing, ancestor fallback for upward routing to parent.
- **Meterer** — Inline traffic shaper. Byte and cart quotas per second.
- **Gateway** — Autonomous system. Owns a Router and a Server. Manages sessions, handles upgrade lifecycle, supports unauthed forwarding for IoT devices.
- **Reach** — Decentralized name resolution with Byzantine fault tolerance.
- **Daemon** — Filesystem-based configuration. The `rhod` binary that ties everything together.

### Utils (`Util/`)

High-level connection lifecycle management.

- **Client** — Probe → Announce → Upgrade handshake. 0-RTT reconnection via `unhook()`/`hook()`.
- **Server** — Accepts probes, announces public keys, handles upgrades. Silent 0-RTT resume by matching ephemeral keys.

## File Map

```
include/
├── Rho/
│   ├── Railway.hpp      Cart + Station
│   ├── Tunnel.hpp       Reliable encrypted transport
│   └── Meta.hpp         Metadata key constants
├── Lines/
│   ├── Router.hpp       Trie-based routing engine
│   ├── Meterer.hpp      Traffic shaping
│   ├── Bind.hpp         UDP + Unix socket transport
│   ├── Gateway.hpp      Auto-routing autonomous system
│   ├── Reach.hpp        BFT name resolution
│   ├── Daemon.hpp       Filesystem config API
│   └── DaemonHost.hpp   Daemon with identity management
└── Util/
    ├── Client.hpp       Connection initiator
    └── Server.hpp       Connection acceptor
```

## Dependencies

Rho depends on **Xi** (`xic`), a lightweight C++ framework providing:

| Component | What it provides |
|-----------|-----------------|
| `Collection/Array.hpp` | Dynamic array (like `std::vector` but allocation-aware) |
| `Collection/Map.hpp` | Hash map |
| `Collection/String.hpp` | Byte string with builder pattern |
| `Sec/Crypto.hpp` | X25519, AEAD, HKDF, SHA-256, key generation |
| `Resource/Path.hpp` | `NumericalAddress` (extends `Array<u64>`) |
| `Resource/SockBind.hpp` | Non-blocking UDP socket wrapper |
| `Xi/Func.hpp` | Lightweight callable wrapper |
| `Xi/Xi.hpp` | `millis()`, `micros()`, type aliases |

Xi compiles on Linux and ESP32. It has no dependencies beyond the C standard library and platform headers.
