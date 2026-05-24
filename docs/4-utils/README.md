# Server & Client

**The networking primitives you wish TCP and QUIC gave you.**

`Server` and `Client` are high-level wrappers around Tunnel. They handle the full connection lifecycle — probe, key exchange, encrypted session, and 0-RTT reconnection — so you can focus on your application logic.

## Why This Beats QUIC

| Feature | TCP | QUIC | Rho Server/Client |
|---------|-----|------|-------------------|
| Unreliable channels | ❌ | Extension (draft) | ✅ First-class |
| Channel multiplexing | ❌ | ✅ Streams | ✅ Channels |
| Head-of-line blocking | ❌ Full stream | Per-stream | Per-channel, bypassable |
| Encryption | Bolt-on (TLS) | Built-in | Built-in |
| 0-RTT reconnect | ❌ | 1-RTT (session ticket) | ✅ True 0-RTT |
| Runs on ESP32 | ✅ | ❌ | ✅ |
| Implementation size | ~50K lines | ~200K lines | **~700 lines** |
| Connection setup | 3-way + TLS (2-3 RTT) | 1-RTT | 1-RTT |

Rho's Server/Client is not a "lightweight QUIC." It's a different design that happens to solve the same problems with 1/300th the code.

## Quick Start: Game Server

```cpp
#include <Util/Server.hpp>
#include <Lines/Bind.hpp>

Bind bind("0.0.0.0:7777");
Server server;
server.hook(bind);

// When a player connects
server.onUpgrade([](Packet pkt, Tunnel& player, Cart cart) {
    printf("Player connected!\n");
});

// When a player sends data
server.onPacket([](Packet pkt, Tunnel& player, Cart cart) {
    if (pkt.channel == 1) {
        // Reliable input event
        handleInput(pkt.payload, player);
    }
});

// Main loop
while (running) {
    bind.update();
    server.update();

    // Send game state to all players (unreliable, 60 Hz)
    for (auto& player : server.clients) {
        player.tunnel->push(Packet(gameState, 2, false));
    }

    usleep(16666); // ~60 Hz
}
```

## Quick Start: Game Client

```cpp
#include <Util/Client.hpp>
#include <Lines/Bind.hpp>

Bind bind("0.0.0.0:0");  // Random port

NumericalAddress serverAddr;
serverAddr.push(1);  // Server's address

Client client;
client.hook(bind, serverAddr);

client.onReady([](Packet pkt, Cart cart) {
    printf("Connected to server!\n");
});

client.onPacket([](Packet pkt) {
    if (pkt.channel == 2) {
        // Unreliable game state update
        renderGameState(pkt.payload);
    }
});

// Main loop
while (running) {
    bind.update();
    client.update();

    // Send input (reliable)
    client.pushPacket(Packet(inputData, 1, true));
    usleep(16666);
}
```

## The Handshake

```
Client                              Server
  │                                   │
  │──── Probe ───────────────────────→│  "Are you there?"
  │                                   │
  │←─── Announce ────────────────────│  "Yes, here's my public key"
  │                                   │
  │──── Upgrade ─────────────────────→│  "Here's my key + first payload"
  │     (contains tunnel bundle)      │  (server creates Tunnel)
  │                                   │
  │←──→ Encrypted Tunnel ←──────────→│  Fully encrypted, reliable
  │                                   │
```

Total: **1 round-trip** to a fully encrypted, multiplexed, reliable connection. TCP + TLS takes 3 round-trips. QUIC takes 1 round-trip but with 200x the code.
