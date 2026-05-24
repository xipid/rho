# Tunnel API Reference

## Class: `Tunnel`

**Header:** `#include <Rho/Tunnel.hpp>`

### Lifecycle

```cpp
Tunnel tunnel;

// Hook to a station (required before any I/O)
tunnel.hook(station);
tunnel.hook(station, rail);          // with specific rail
tunnel.hook(station, rail, source);  // with specific rail and source address

// Destroy and free resources
tunnel.destroy();
```

### Sending

```cpp
// Push a packet (returns false if backpressure)
bool ok = tunnel.push(packet);

// Push with explicit parameters
tunnel.push(Packet(payload, channel, important));

// Check if sending is possible
if (tunnel.canPush()) { /* safe to push */ }
```

### Receiving

```cpp
// Set the packet callback
tunnel.onPacket([](Packet pkt) {
    String data = pkt.payload;
    u64 channel = pkt.channel;
    bool wasImportant = pkt.important;
});
```

### Encryption

```cpp
// Generate a key pair
auto keypair = Sec::generateKeyPair();

// Enable X25519 key exchange
tunnel.enableSecureX(theirPublicKey, ourKeypair);

// Enable with a pre-shared key
tunnel.enableSecurity(preSharedKey);

// Check encryption status
bool encrypted = tunnel.isSecure;
```

### Configuration

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `maxInflight` | `usz` | `64` | Max in-flight bundles before backpressure |
| `bMS` | `usz` | `1400` | Max bundle size in bytes |
| `heartbeatIntervalMS` | `u64` | `500` | Heartbeat interval in milliseconds |
| `activeTimeout` | `ActiveTimeout` | `Relaxed` | Connection timeout preset |

### Active Timeout Presets

```cpp
enum ActiveTimeout {
    Relaxed,     // 30 seconds — default, for stable connections
    Active,      // 10 seconds — for responsive failure detection
    Aggressive   //  5 seconds — for real-time apps
};

tunnel.activeTimeout = Tunnel::ActiveTimeout::Active;
```

### State

| Property | Type | Description |
|----------|------|-------------|
| `isSecure` | `bool` | Whether encryption is active |
| `isDestroyed` | `bool` | Whether `destroy()` has been called |
| `isUpgraded` | `bool` | Whether the key exchange completed |
| `sentBundleID` | `u64` | Total bundles sent |
| `receivedBundleID` | `u64` | Highest bundle ID received |

### Tick

```cpp
// Call in your main loop
tunnel.update();
```

`update()` performs:
1. Flushes any pending outgoing bundles
2. Checks heartbeat timers
3. Retransmits unacknowledged bundles if needed
4. Marks the tunnel as dead if the connection has timed out
