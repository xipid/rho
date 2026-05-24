# Client

## What is Client?

`Client` manages the outbound side of a connection. It handles probing, key exchange, and Tunnel creation automatically. After `hook()`, you call `probe()` and wait for `onReady` — then you have a fully encrypted, multiplexed channel.

## Lifecycle

```
hook() → probe() → onAnnounce → upgrade() → onReady → pushPacket()
                                                  ↓
                                              unhook() → hook() → 0-RTT resume
```

## Basic Usage

```cpp
Client client;

// 1. Hook to a station with the server's address
client.hook(bind, serverAddress);

// 2. Set callbacks
client.onAnnounce([&](Cart& c) {
    // Server announced — auto-upgrade
    client.upgrade();
});

client.onReady([](Packet pkt, Cart cart) {
    printf("Connection ready!\n");
});

client.onPacket([](Packet pkt) {
    printf("Received on channel %lu: %s\n", pkt.channel, pkt.payload.c_str());
});

// 3. Start probing
client.probe();

// 4. Tick
while (running) {
    bind.update();
    client.update();
}
```

## Sending Data

```cpp
// After onReady fires:

// Reliable packet
client.pushPacket(Packet(data, 1, true));

// Unreliable packet
client.pushPacket(Packet(data, 2, false));

// Through the underlying cart (with metadata)
Cart cart;
cart.hasMeta = true;
cart.meta.put(100, "custom-value");
client.pushCart(cart);
```

## Configuration

```cpp
// Auto-upgrade on announce (skip manual upgrade() call)
client.autoUpgrade = true;

// Auto-probe on hook
client.autoProbe = true;
```

## 0-RTT Reconnection

When a player's network changes (WiFi → cellular, IP address change), the standard approach is to disconnect and reconnect — which means a full handshake, state re-sync, and visible interruption.

Rho's Client supports **true 0-RTT reconnection**: the Tunnel's encryption state survives the transport change.

```cpp
// Player's WiFi drops — switch to cellular
client.unhook();  // Detaches from station, but Tunnel stays alive

// Create new transport (new Bind on cellular)
Bind cellularBind("0.0.0.0:0");

// Re-hook with the same server address
client.hook(cellularBind, serverAddress);

// Next pushPacket() automatically sends a Switch cart with the
// same ephemeral key. The server recognizes it and silently
// re-hooks the existing Tunnel. Zero round-trips. Zero key
// renegotiation. The encrypted stream continues.
```

### How It Works Internally

1. `unhook()` sets `_isResuming = true`. The Tunnel's keypair, encryption key, nonces, and all state are preserved.
2. `hook()` detects `_isResuming` and calls `_rehookIfResuming()`.
3. `_rehookIfResuming()` re-hooks the Tunnel to the new Station/rail/target.
4. On the next `pushCart()`, the Client piggybacks a `Meta::Command = Switch` with `Meta::PublicKey = <same ephemeral key>`.
5. The Server receives this, finds an existing client entry with matching `theirEphemeralPublic`, and silently re-hooks the Tunnel to the new source/rail. No `onUpgrade` callback fires.
6. The bundle inside the Switch cart is a real Tunnel bundle — it's processed as normal. The connection resumes mid-stream.

### Why This Beats QUIC's Connection Migration

QUIC connection migration requires a 1-RTT path validation before the new path is trusted. Rho's 0-RTT resume trusts the new path immediately because the bundle is encrypted with the session key — if it decrypts correctly, the sender is authentic.
