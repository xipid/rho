# Daemon API Reference

## Class: `Daemon`

**Header:** `#include <Lines/Daemon.hpp>`

The base daemon. Manages a Gateway with filesystem-based identity.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `gt` | `Gateway` | The managed Gateway |
| `identity` | `DaemonIdentity` | Loaded identity (keypair + address) |
| `routes` | `Map<String, NumericalAddress>` | Public key hash → address claims |

### Methods

```cpp
// Load identity from directory
void loadIdentity(const String& path);

// Load route claims from directory
void loadRoutes(const String& path);

// Access the Gateway
Gateway& gateway() { return gt; }
```

---

## Class: `DaemonHost`

**Header:** `#include <Lines/DaemonHost.hpp>`

A Daemon that also manages outbound connections.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `gt` | `Gateway` | The managed Gateway |
| `upgrades` | `Array<UpgradeTarget>` | Peers to connect to |

### Methods

```cpp
// Load identity from directory
void loadIdentity(const String& path);

// Load route claims from directory
void loadRoutes(const String& path);

// Load upgrade targets from directory
void loadUpgrades(const String& path);

// Start: hook to bind, connect to all upgrade targets
void start(Bind& bind);

// Tick: update gateway and all client connections
void update();
```

### UpgradeTarget

```cpp
struct UpgradeTarget {
    String label;     // Human-readable name
    String address;   // "host:port" to connect to
    Client* client;   // The Client managing this connection
};
```

### Full Example

```cpp
#include <Lines/DaemonHost.hpp>
#include <Lines/Bind.hpp>

int main(int argc, char** argv) {
    const char* configDir = argv[1];   // "/etc/rho"
    const char* bindAddr  = argv[2];   // "0.0.0.0:9000"

    Bind bind(bindAddr);

    DaemonHost host;
    host.loadIdentity(String(configDir) + "/identity/");
    host.loadRoutes(String(configDir) + "/routes/");
    host.loadUpgrades(String(configDir) + "/upgrades/");
    host.start(bind);

    while (true) {
        bind.update();
        host.update();
        usleep(1000);
    }

    return 0;
}
```
