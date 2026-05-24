# Configuration

## Directory Structure

```
<config_root>/
├── identity/
│   ├── public.key       # X25519 public key (binary or base64)
│   ├── private.key      # X25519 private key (binary or base64)
│   └── address          # NumericalAddress as dot-separated integers
├── routes/
│   ├── <key_hash_1>     # File content = address to assign
│   ├── <key_hash_2>     # File content = address to assign
│   └── ...
└── upgrades/
    ├── <peer_name_1>    # File content = "host:port" to connect to
    ├── <peer_name_2>    # File content = "host:port" to connect to
    └── ...
```

## Identity

### `identity/address`

The gateway's NumericalAddress as a dot-separated string:

```
3.1.1
```

This sets `gateway.address = [3, 1, 1]`.

### `identity/public.key` and `identity/private.key`

The X25519 keypair. Generated once with:

```cpp
auto kp = Sec::generateKeyPair();
writeFile("identity/public.key", kp.publicKey);
writeFile("identity/private.key", kp.privateKey);
```

If these files don't exist, the Daemon generates a new keypair on first run and writes it.

## Routes (Claims)

Each file in `routes/` maps a client's public key hash to the address they should be assigned when they connect.

**Filename:** SHA-256 hash of the client's public key (hex-encoded)
**Content:** The NumericalAddress to assign (dot-separated)

```bash
# Example: client with key hash abc123def456... gets address 3.1.1.1
echo "3.1.1.1" > routes/abc123def456789...
```

When a client connects and upgrades, the DaemonHost:
1. Reads the client's public key from the Upgrade cart metadata
2. Hashes it with SHA-256
3. Looks for a matching file in `routes/`
4. If found, assigns that address to the client's session

This is the "claims" system — governments and operators decide who gets what address by placing files.

## Upgrades (Peers)

Each file in `upgrades/` specifies a peer to connect to on startup.

**Filename:** A human-readable label (for logging)
**Content:** The peer's bind address as `"host:port"`

```bash
# Connect to parent gateway
echo "10.0.0.1:9000" > upgrades/parent

# Connect to peer telecom
echo "10.0.0.2:9000" > upgrades/dz-peer
```

On startup, the DaemonHost creates a Client for each file in `upgrades/`, hooks it to the Bind, and probes the target address. Once upgraded, the peer's Tunnel is hooked into the Router.

## Hot Reloading

The Daemon watches the configuration directory for changes. When a new route file appears, the Daemon picks it up on the next tick. When a file is deleted, the corresponding claim is removed.

This allows live reconfiguration without restarting `rhod`:

```bash
# Add a new device claim while rhod is running
echo "3.1.1.42" > /etc/rho/routes/new_device_key_hash

# Remove a claim
rm /etc/rho/routes/old_device_key_hash
```

## Generating Configuration

```bash
# Create config structure
mkdir -p /etc/rho/{identity,routes,upgrades}

# Generate keypair
rho-keygen > /etc/rho/identity/public.key 2> /etc/rho/identity/private.key

# Set address
echo "3.1.1" > /etc/rho/identity/address

# Add route claims
echo "3.1.1.1" > /etc/rho/routes/$(sha256sum client1.pub | cut -d' ' -f1)

# Add parent connection
echo "192.168.1.1:9000" > /etc/rho/upgrades/parent
```
