# Daemons

**Filesystem-based configuration for Rho gateways.**

A Daemon is a pre-configured Gateway that reads its identity, routes, and peer connections from the filesystem. It's the glue between Rho's networking primitives and the operating system.

## What It Does

A Daemon:

1. **Reads identity** from the filesystem (keypair, address, name)
2. **Reads routes** (address claims for connecting clients)
3. **Reads upgrade targets** (peers to connect to on startup)
4. **Creates a Gateway** with all of this configuration
5. **Runs the main loop** — `bind.update()`, `gateway.update()`, repeat

## Philosophy

Daemons follow the Unix philosophy: configuration is files, identity is a keypair on disk, routing is a directory listing. No databases, no config file parsers, no YAML. Just files.

```
/etc/rho/
├── identity/
│   ├── public.key     # X25519 public key
│   ├── private.key    # X25519 private key
│   └── address        # NumericalAddress (e.g., "3.1.1")
├── routes/
│   ├── <public_key_hash_1>    # Contains address to assign (e.g., "3.1.1.1")
│   └── <public_key_hash_2>    # Contains address to assign (e.g., "3.1.1.2")
└── upgrades/
    ├── parent           # Contains "addr:port" of parent gateway
    └── peer-dz          # Contains "addr:port" of DZ peer
```

## Two Levels

### Daemon (Lines/Daemon.hpp)

The base daemon. Manages a Gateway with filesystem-based identity:

```cpp
Daemon daemon;
daemon.loadIdentity("/etc/rho/identity/");
daemon.loadRoutes("/etc/rho/routes/");

// daemon.gt is the Gateway
daemon.gt.hookStation(bind);

while (running) {
    bind.update();
    daemon.gt.update();
}
```

### DaemonHost (Lines/DaemonHost.hpp)

A Daemon that also manages outbound connections (upgrades to peers and parents):

```cpp
DaemonHost host;
host.loadIdentity("/etc/rho/identity/");
host.loadRoutes("/etc/rho/routes/");
host.loadUpgrades("/etc/rho/upgrades/");

// host connects to all upgrade targets on startup
host.start(bind);

while (running) {
    bind.update();
    host.update();
}
```

## Use Cases

### Home Router

A home router daemon manages a household's devices:

```
/etc/rho/
├── identity/
│   ├── address = "3.1.1.1"
│   └── ...keys...
├── routes/
│   ├── abc123 = "3.1.1.1.1"    # Phone
│   ├── def456 = "3.1.1.1.2"    # Laptop
│   └── ghi789 = "3.1.1.1.3"    # Smart TV
└── upgrades/
    └── parent = "192.168.1.1:9000"  # ISP neighborhood gateway
```

### Telecom Gateway

A telecom gateway peers with other telecoms:

```
/etc/rho/
├── identity/
│   ├── address = "3"
│   └── ...keys...
├── routes/
│   ├── city1_pk = "3.1"
│   ├── city2_pk = "3.2"
│   └── city3_pk = "3.3"
└── upgrades/
    ├── dz_peer = "10.0.0.1:9000"
    └── eu_peer = "10.0.0.2:9000"
```
