# Rho

[![Docs](https://img.shields.io/badge/docs-gitbook-blue?style=flat-square)](https://xrho.gitbook.io/rho/docs/1-introduction)
[![Discord](https://img.shields.io/badge/discord-join-5865F2?style=flat-square&logo=discord&logoColor=white)](https://discord.gg/s7Rg4DHuej)
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20Linux%20%7C%20Bare%20Metal-orange?style=flat-square)]()

**Blazing-fast, zero-copy networking primitives that run everywhere — from ESP32 microcontrollers to bare-metal servers.**

Rho is not a single project. It is a **collection of composable networking layers**, each one self-contained, each one purpose-built, and each one designed to be unrolled, inlined, and even synthesized into hardware.

---

## Why Rho?

### For game developers and real-time systems

Drop TCP. Drop QUIC. Rho's `Server` and `Client` give you what the game industry actually needs:

```cpp
#include <Util/Server.hpp>
#include <Lines/Bind.hpp>

Bind bind("0.0.0.0:7777");
Server srv;
srv.hook(bind);

srv.onPacket([](Packet pkt, Tunnel& player, Cart cart) {
    // 60 Hz game state — unreliable, fire-and-forget
    player.push(gameState, 2, false);  // channel 2, not important

    // Kill event — reliable, guaranteed delivery
    player.push(killEvent, 3, true);   // channel 3, important
});
```

- **Unreliable channels** out of the box — no stale retransmissions
- **No head-of-line blocking** — lost packets on channel 2 don't stall channel 3
- **Built-in AEAD encryption** — X25519 key exchange in one call
- **True 0-RTT reconnection** — player switches WiFi → cellular, zero round-trips to resume
- **~700 lines of implementation** — not 200,000 like a QUIC stack
- **Runs on ESP32** — good luck getting QUIC on a microcontroller

### For network architects

Rho was designed from scratch to replace the entire IP/BGP stack with something that actually makes sense:

- **Hierarchical variable-depth addressing** — no more NAT, no more CIDR hacks, no more address exhaustion
- **Strict tree routing** — each node knows only its parent and children, yet packets traverse continents
- **Self-built routing tables** — no BGP advertisements, no convergence delays, no route leaks
- **Government-level manual peering** at the top, automatic child registration below

A device at address `3.1.1.1` in Saudi Arabia sends a packet to `1.1.1.1` in the USA. The packet climbs the tree: `3.1.1` → `3.1` → `3` → fiber → `2` (transit) → fiber → `1` → `1.1` → `1.1.1` → `1.1.1.1`. Each hop is a single table lookup. Each gateway holds maybe 50 entries. Even at 8 billion nodes.

---

## The Layers

Rho is organized into three categories. Each one builds on the previous, but each one is independently useful.

| Layer | What it does | Think of it as |
|-------|-------------|----------------|
| **Rho Railway** | Shared-bus multiplexing | Ethernet + encryption |
| **Rho Tunnel** | Reliable encrypted streams | TCP/QUIC replacement |
| **Rho Lines** | Cross-entity routing and bridging | IP/BGP replacement |

### Rho Railway

Take any shared medium — LoRa, USB, UDP multicast, serial, a Unix socket — and make point-to-point connections possible. `Cart` is the packet. `Station` is the interface. Hook two stations together and they talk.

### Rho Tunnel

Take a Station (unreliable, insecure) and turn it into a fully reliable, encrypted, multiplexed communication channel. Selective ACKs, congestion windows, packet fragmentation, AEAD encryption — all in one class.

### Rho Lines

Take carts across entities that can't physically reach each other. `Router` builds a trie. `Gateway` wires it to `Server`/`Client`. `Reach` resolves names with Byzantine fault tolerance. `Daemon` configures it all from the filesystem.

---

## Quick Start

```bash
git clone https://github.com/xipid/rho.git
cd rho
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Rho is header-only. CMake automatically fetches [Xi](https://github.com/xipid/xic) from GitHub if it's not found locally. For development, clone `xic` next to `rho` and CMake will use the local copy instead.

---

## Platform Support

| Platform | Status |
|----------|--------|
| Linux (x86_64, ARM) | ✅ Full support |
| ESP32 (lwIP) | ✅ Full support |
| Any POSIX system | ✅ Via `Bind` UDP sockets |
| Bare metal | ✅ Via custom `Station` implementations |

---

## Documentation & Community

📖 **[Read the Docs](https://xrho.gitbook.io/rho/docs/1-introduction)** — Architecture deep-dives, API references, and usage guides

💬 **[Join the Discord](https://discord.gg/xxxxxxxxxx)** — Ask questions, share projects, contribute

---

## License

MIT
