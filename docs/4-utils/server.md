# Server

## What is Server?

`Server` manages the inbound side of connections. It listens for probes, announces its public key, handles upgrades, and manages multiple concurrent client Tunnels.

## Lifecycle

```
hook(station) → onProbe → announce → onUpgrade → Tunnel created
                                                    ↓
                                              onPacket (per-client)
                                                    ↓
                                              onSwitch (client migrated)
```

## Basic Usage

```cpp
Server server;

// Hook to a station
server.hook(bind);

// When a new client connects
server.onUpgrade([](Packet pkt, Tunnel& client, Cart cart) {
    printf("New client connected!\n");
});

// When any client sends data
server.onPacket([](Packet pkt, Tunnel& client, Cart cart) {
    printf("Client sent: %s\n", pkt.payload.c_str());
    // Echo back
    client.push(Packet("got it", pkt.channel));
});

// Tick
while (running) {
    bind.update();
    server.update();
}
```

## Managing Clients

```cpp
// Access connected clients
for (usz i = 0; i < server.clients.size(); ++i) {
    auto& entry = server.clients[i];
    Tunnel* tunnel = entry.tunnel;

    // Send to this client
    tunnel->push(Packet(data, 1));

    // Check if client is still alive
    if (tunnel->isDestroyed) {
        // Handle disconnect
    }
}
```

## Broadcasting

```cpp
void broadcastGameState(Server& server, const String& state) {
    for (usz i = 0; i < server.clients.size(); ++i) {
        auto& entry = server.clients[i];
        if (entry.tunnel && !entry.tunnel->isDestroyed) {
            // Unreliable broadcast — if a client misses one frame, the next
            // one arrives in 16ms anyway
            entry.tunnel->push(Packet(state, 2, false));
        }
    }
}
```

## 0-RTT Resume (Server Side)

The Server automatically handles 0-RTT reconnection. When a Client sends a Switch cart with the same ephemeral public key:

1. The Server scans `clients` for an entry where `theirEphemeralPublic` matches.
2. If found: silently re-hooks the Tunnel to the new rail/source. The existing encryption state continues.
3. The bundle inside the Switch cart is processed as a normal Tunnel message.
4. **No `onUpgrade` callback fires.** The reconnection is invisible to your application code.

```cpp
// You don't need to write any code for 0-RTT resume.
// It just works. The server automatically recognizes
// returning clients by their ephemeral key.
```

## Configuration

```cpp
// Custom key pair (default: auto-generated)
server.keypair = myKeypair;

// Access the server's public key (for distributing to clients)
String pubKey = server.keypair.publicKey;
```

## Callbacks

```cpp
// New connection
server.onUpgrade([](Packet pkt, Tunnel& client, Cart cart) {
    // pkt = first application packet from the client
    // client = the newly created Tunnel
    // cart = the raw upgrade cart (for metadata inspection)
});

// Data from any client
server.onPacket([](Packet pkt, Tunnel& client, Cart cart) {
    // pkt = application packet
    // client = the sender's Tunnel
});

// Client probed us
server.onProbe([](Cart& probeCart) {
    // Inspect or modify the announce response
});

// Client switched transport (0-RTT resume)
server.onSwitch([](Tunnel& client) {
    // Client migrated to a new transport
    // (only fires if you set this callback)
});
```
