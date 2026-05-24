# Game Networking with Rho

This guide shows how to build the networking layer for an AAA multiplayer game using Rho's Server and Client.

## Architecture

```
┌─────────────────────────────────────────┐
│              Game Server                 │
│  ┌────────┐  ┌──────────────────────┐   │
│  │  Bind  │──│      Server          │   │
│  │ :7777  │  │  ┌─────────────────┐ │   │
│  └────────┘  │  │ Tunnel (P1)     │ │   │
│              │  │ Tunnel (P2)     │ │   │
│              │  │ Tunnel (P3)     │ │   │
│              │  │ ...             │ │   │
│              │  └─────────────────┘ │   │
│              └──────────────────────┘   │
└─────────────────────────────────────────┘
         ↕ UDP            ↕ UDP

┌──────────────┐   ┌──────────────┐
│ Game Client 1│   │ Game Client 2│
│ ┌────┐┌─────┐│   │ ┌────┐┌─────┐│
│ │Bind││Client││   │ │Bind││Client││
│ └────┘└─────┘│   │ └────┘└─────┘│
└──────────────┘   └──────────────┘
```

## Channel Layout

Design your channels based on data characteristics:

| Channel | Direction | Reliability | Data | Tick Rate |
|---------|-----------|-------------|------|-----------|
| 1 | Client → Server | ✅ Reliable | Input events | Every frame |
| 2 | Server → Client | ❌ Unreliable | World state snapshot | 60 Hz |
| 3 | Bidirectional | ✅ Reliable | Kill events, scoring | On event |
| 4 | Bidirectional | ✅ Reliable | Chat messages | On event |
| 5 | Server → Client | ❌ Unreliable | Audio voice data | 50 Hz |

```cpp
enum Channel : u64 {
    Input      = 1,
    WorldState = 2,
    Events     = 3,
    Chat       = 4,
    Voice      = 5
};
```

## Server Implementation

```cpp
#include <Util/Server.hpp>
#include <Lines/Bind.hpp>

struct Player {
    Tunnel* tunnel;
    String name;
    Vec3 position;
    u64 lastInputSeq;
};

Array<Player> players;

Bind bind("0.0.0.0:7777");
Server server;
server.hook(bind);

server.onUpgrade([&](Packet pkt, Tunnel& t, Cart c) {
    Player p;
    p.tunnel = &t;
    p.name = pkt.payload;  // First packet = player name
    p.lastInputSeq = 0;
    players.push(p);
    printf("Player '%s' joined (%zu total)\n", p.name.c_str(), players.size());
});

server.onPacket([&](Packet pkt, Tunnel& t, Cart c) {
    switch (pkt.channel) {
        case Channel::Input: {
            // Deserialize input, apply to game state
            PlayerInput input;
            input.fromBytes(pkt.payload);
            applyInput(findPlayer(t), input);
            break;
        }
        case Channel::Chat: {
            // Broadcast to all players
            for (auto& p : players) {
                p.tunnel->push(Packet(pkt.payload, Channel::Chat, true));
            }
            break;
        }
    }
});

// Main game loop
while (running) {
    bind.update();
    server.update();

    // Game tick
    simulateWorld();

    // Broadcast world state (unreliable — players see the latest state, not stale retransmissions)
    String snapshot = serializeWorldState();
    for (auto& p : players) {
        if (!p.tunnel->isDestroyed) {
            p.tunnel->push(Packet(snapshot, Channel::WorldState, false));
        }
    }

    // Clean up disconnected players
    for (usz i = 0; i < players.size(); ++i) {
        if (players[i].tunnel->isDestroyed) {
            printf("Player '%s' disconnected\n", players[i].name.c_str());
            players.splice(i, 1);
            --i;
        }
    }

    usleep(16666); // ~60 Hz
}
```

## Client Implementation

```cpp
#include <Util/Client.hpp>
#include <Lines/Bind.hpp>

Bind bind("0.0.0.0:0");
Client client;
client.hook(bind, serverAddress);

client.onReady([&](Packet pkt, Cart cart) {
    printf("Connected to server!\n");
    isConnected = true;
});

client.onPacket([&](Packet pkt) {
    switch (pkt.channel) {
        case Channel::WorldState:
            // Latest world state — render it
            updateWorldState(pkt.payload);
            break;
        case Channel::Events:
            // Kill event, scoring, etc.
            handleGameEvent(pkt.payload);
            break;
        case Channel::Chat:
            showChatMessage(pkt.payload);
            break;
    }
});

// Main game loop
while (running) {
    bind.update();
    client.update();

    if (isConnected) {
        // Send input (reliable — every input matters)
        PlayerInput input = captureInput();
        client.pushPacket(Packet(input.toBytes(), Channel::Input, true));
    }

    renderFrame();
    usleep(16666);
}
```

## Handling Network Switches

When the player's WiFi drops and they switch to cellular:

```cpp
void onNetworkChange(const char* newInterface) {
    // 1. Unhook — Tunnel stays alive
    client.unhook();

    // 2. Create new transport
    Bind newBind(newInterface);

    // 3. Re-hook — next packet triggers 0-RTT resume
    client.hook(newBind, serverAddress);

    // Player never sees a disconnect. The game state streams
    // continuously. The server doesn't fire onUpgrade. The
    // encryption key doesn't change.
}
```

The player's ping might spike for one frame. That's it. No loading screen, no "reconnecting..." dialog, no state re-sync.

## Performance Characteristics

With Rho's Server/Client on a modern Linux server:

- **Concurrent connections**: Limited by file descriptors and memory, not protocol overhead
- **Per-packet overhead**: ~40 bytes (UDP header + Cart header + Tunnel bundle header + AEAD tag)
- **Latency**: 1 RTT to connect, 0 RTT to resume, sub-ms processing per packet
- **CPU per connection**: Minimal — one `update()` tick processes all pending I/O
- **Memory per connection**: ~2 KB (Tunnel state + send/receive buffers)
