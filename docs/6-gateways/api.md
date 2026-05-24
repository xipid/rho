# Gateway API Reference

## Class: `Gateway`

**Header:** `#include <Lines/Gateway.hpp>`

### Construction

```cpp
Gateway gw;
// keypair is auto-generated
// router is empty
// server is unhooked
```

### Configuration

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `address` | `NumericalAddress` | `{}` | This gateway's address in the tree |
| `keypair` | `Sec::KeyPair` | Auto-generated | X25519 keypair for encryption |
| `unauthed` | `bool` | `false` | Enable unauthed forwarding for IoT |
| `unauthedMarker` | `String` | `""` | Marker for unauthed identification |
| `unauthedAddress` | `NumericalAddress` | `{}` | Address prefix for unauthed devices |
| `unauthedTimeout` | `u32` | `30000` | Unauthed session timeout (ms) |

### Subcomponents

| Property | Type | Description |
|----------|------|-------------|
| `router` | `Router` | The routing trie |
| `server` | `Server` | The connection acceptor |
| `clients` | `Array<Client*>` | Outbound connections to peers/parent |
| `sessions` | `Array<TunnelSession>` | Active client sessions |
| `activeReaches` | `Array<ActiveReach>` | In-progress name resolutions |

### Station Management

```cpp
// Hook a station (listen for incoming connections)
void hookStation(Station& station);

// Bind a port (create a child address)
void bindPort(u32 port);
```

### Reach (Name Resolution)

```cpp
// Resolve a name (returns cached result or starts async resolution)
NumericalAddress reach(const String& address);

// Add a default Reach server
void addDefaultReachServer(const NumericalAddress& addr, const String& publicKey);
```

### Callbacks

```cpp
// New client connected
gw.onUpgrade([](Packet& pkt, Tunnel& client, Cart& c) -> RoutingEntry* {
    // Return a routing entry to track the session
    // Return nullptr to reject the connection
});

// Data from a client
gw.onReady([](Packet& pkt, Tunnel& client, Cart& c) {
    // Handle application data
});

// Client probed us
gw.onAnnounce([](Cart& c) {
    // Inspect or modify the announce response
});

// Name resolution completed
gw.onReachCompleted([](const String& name, const NumericalAddress& addr) {
    // Use the resolved address
});
```

### Lifecycle

```cpp
// Tick all components (Server, Clients, Reaches, session cleanup)
void update();

// Clean up all resources
void destroy();
```
