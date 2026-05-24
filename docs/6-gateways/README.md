# Gateways

**The autonomous system. The node in the tree. The thing that makes the network work.**

A `Gateway` is a self-contained networking entity that owns a Router, manages a Server, maintains client connections, and handles the full lifecycle of sessions. It is the "process" in the network hierarchy — whether that process represents a telecom, a city, a home router, or a single IoT device.

## What It Does

A Gateway:

1. **Owns a Router** — All routing decisions go through this Router's trie.
2. **Runs a Server** — Accepts incoming connections (probes, upgrades, tunnels).
3. **Manages Clients** — Connects to peer gateways and parent gateways.
4. **Handles sessions** — Tracks which tunnels are connected, assigns addresses, cleans up dead connections.
5. **Supports unauthed forwarding** — For constrained IoT devices that can't run a full tunnel.
6. **Integrates Reach** — Resolves human-readable names to NumericalAddresses.

## Quick Example

```cpp
#include <Lines/Gateway.hpp>
#include <Lines/Bind.hpp>

Bind bind("0.0.0.0:9000");

Gateway gw;
gw.address = {3, 1, 1};  // This gateway is at 3.1.1 (a neighborhood)

// Hook the bind as a listening station
gw.hookStation(bind);

// When a new device connects
gw.onUpgrade([&](Packet& pkt, Tunnel& t, Cart& c) -> RoutingEntry* {
    // Assign an address under us
    NumericalAddress childAddr = gw.address;
    childAddr.push(nextDeviceId++);

    // Create a station for this device and hook it into the router
    Station* devStation = new Station();
    gw.router.hook(devStation, childAddr);

    // Return the routing entry so the gateway tracks this session
    return gw.router.entryOf(devStation);
});

// Main loop
while (running) {
    bind.update();
    gw.update();
}
```

## The Gateway as a Tree Node

In the strict tree hierarchy, each Gateway represents one level:

```
Telecom Gateway (address: [3])
├── City Gateway (address: [3, 1])
│   ├── Neighborhood Gateway (address: [3, 1, 1])  ← this example
│   │   ├── Home Gateway (address: [3, 1, 1, 1])
│   │   └── Home Gateway (address: [3, 1, 1, 2])
│   └── Neighborhood Gateway (address: [3, 1, 2])
└── City Gateway (address: [3, 2])
```

Each Gateway:
- Hooks its **children** (the gateways or devices below it)
- Connects to its **parent** (the gateway above it) via a Client

The Gateway's Router contains exactly these entries — nothing more.

## Bound Ports

A Gateway can bind to specific ports within its address space:

```cpp
gw.bindPort(80);   // Creates a station at address [3, 1, 1, ..., 80]
gw.bindPort(443);  // Creates a station at address [3, 1, 1, ..., 443]
```

Bound ports are children in the Router tree. When a cart targets `[3, 1, 1, 80]`, the Router delivers it to the port 80 station.

## Unauthed Forwarding

For constrained IoT devices that can't run a full Tunnel (e.g., a LoRa sensor with 10 KB of RAM), the Gateway supports unauthed forwarding:

```cpp
gw.unauthed = true;
gw.unauthedMarker = "sensor-network";
gw.unauthedAddress = {3, 1, 1, 100};  // Address for unauthed devices
gw.unauthedTimeout = 30000;            // 30 seconds
```

Unauthed devices send carts with a `GatewayMarker` in the metadata. The Gateway creates a temporary session for them without requiring a Tunnel upgrade.
