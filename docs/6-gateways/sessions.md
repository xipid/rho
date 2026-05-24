# Sessions & Security

## Session Lifecycle

When a client connects to a Gateway, a session is created:

```
Client Probe → Server Announce → Client Upgrade → Session Created
                                                         │
                                                   TunnelSession {
                                                       tunnel: Tunnel*
                                                       sessionStation: Station*
                                                       assignedAddress: NumericalAddress
                                                   }
```

The `sessionStation` is hooked into the Router at the `assignedAddress`. All carts routed to that address are delivered through the Tunnel to the remote client.

## Address Assignment

When a device connects, the Gateway's `onUpgrade` callback decides what address to assign:

```cpp
gw.onUpgrade([&](Packet& pkt, Tunnel& t, Cart& c) -> RoutingEntry* {
    // Option 1: Sequential assignment
    NumericalAddress addr = gw.address;
    addr.push(nextId++);

    // Option 2: Identity-based (from claims/routes)
    const String* pk = c.meta.get(Meta::PublicKey);
    NumericalAddress addr = lookupClaim(*pk);

    // Option 3: Fixed assignment
    NumericalAddress addr = {3, 1, 1, 42};

    Station* s = new Station();
    gw.router.hook(s, addr);
    return gw.router.entryOf(s);
});
```

## Encryption

All sessions are encrypted by default. The Gateway's `keypair` is generated on construction:

```cpp
Gateway gw;
// gw.keypair is auto-generated (X25519)

// Or set a specific keypair
gw.keypair = myKeypair;

// The server inside the gateway uses this keypair for all upgrades
```

The Server announces the Gateway's public key during the Probe→Announce handshake. Clients derive the shared secret via X25519 DH. All subsequent communication through the Tunnel is AEAD-encrypted.

## Session Cleanup

The Gateway's `update()` method automatically cleans up dead sessions:

```cpp
gw.update();
// Internally:
// 1. Ticks the Server and all Clients
// 2. Checks each session's Tunnel for isDestroyed
// 3. If destroyed: unhooks the session station from the Router, frees memory
// 4. Checks unauthed session timeouts
```

Dead sessions are removed from the Router immediately. The address becomes available for reassignment.

## Reach Cache

When a Gateway resolves a name via `Reach`, the result is cached:

```cpp
// First call: starts async resolution, returns empty address
NumericalAddress addr = gw.reach("game.example.com");
// addr = {} (resolving...)

// After resolution completes (in update()):
NumericalAddress addr = gw.reach("game.example.com");
// addr = [5, 8, 9, 42] (cached)
```

The cache is populated when the `Reach` instance completes. Subsequent `reach()` calls return the cached result immediately.

The `onReachCompleted` callback fires when a resolution finishes:

```cpp
gw.onReachCompleted([](const String& name, const NumericalAddress& addr) {
    printf("Resolved '%s' to %s\n", name.c_str(), addrStr(addr).c_str());
});
```
