# Metadata System

## What is Meta?

Every Cart can carry key-value metadata alongside its payload. Metadata is used for signaling between Rho components — upgrade handshakes, routing hints, service discovery, and name resolution.

## The Meta Namespace

```cpp
namespace Meta {
    const u64 Address         = 1;  // Human-readable address (String)
    const u64 NumericalAddress= 2;  // Machine address (dot-separated)
    const u64 PublicKey       = 3;  // X25519 public key
    const u64 Name            = 4;  // Service name
    const u64 Service         = 5;  // Service type identifier
    const u64 Version         = 6;  // Protocol version
    const u64 Path            = 7;  // Resource path
    const u64 Command         = 10; // Control command (Probe=0, Announce=1, Upgrade=2)
    const u64 PublicHash      = 11; // Hash of public key
    const u64 GatewayMarker   = 12; // Gateway identity marker
}
```

## Usage

```cpp
Cart c;
c.hasMeta = true;

// Set metadata
c.meta.put(Meta::Command, String::fromInt(0));  // Probe
c.meta.put(Meta::PublicKey, myPublicKey);
c.meta.put(Meta::Service, "game-server");

// Read metadata
const String* cmd = c.meta.get(Meta::Command);
if (cmd && *cmd == "0") {
    // This is a probe
}
```

## How Components Use Meta

| Component | Reads | Writes |
|-----------|-------|--------|
| **Client** | `Command` (Announce), `PublicKey` | `Command` (Probe, Upgrade), `PublicKey` |
| **Server** | `Command` (Probe, Upgrade), `PublicKey` | `Command` (Announce), `PublicKey`, `PublicHash` |
| **Reach** | `Address`, `NumericalAddress`, `PublicKey` | `Address`, `NumericalAddress` |
| **Gateway** | `GatewayMarker`, `PublicKey` | `GatewayMarker` |
| **Daemon** | `Name`, `Service`, `Version`, `Path` | `Name`, `Service`, `Version`, `Path` |

## The Handshake Flow (Meta in Action)

```
Client                        Server
  │                             │
  │── Probe (Command=0) ──────→│
  │                             │
  │←── Announce (Command=1) ───│
  │     PublicKey=<server_pk>   │
  │     PublicHash=<hash>       │
  │                             │
  │── Upgrade (Command=2) ────→│
  │     PublicKey=<client_pk>   │
  │     payload=<tunnel_bundle> │
  │                             │
  └── Encrypted Tunnel ────────┘
```

Meta makes this handshake zero-config. The Client sends a Probe. The Server responds with its public key. The Client sends an Upgrade with its own public key wrapped in a Tunnel bundle. Both sides derive the shared secret. The Tunnel is live.

## Custom Metadata

You can define your own metadata keys (values 100+):

```cpp
const u64 MyCustomField = 100;

Cart c;
c.hasMeta = true;
c.meta.put(MyCustomField, "my-value");
```

Keys 0-99 are reserved for Rho's internal use. Application-defined keys should start at 100 or higher.
