# The `rhod` Binary

## What is rhod?

`rhod` (Rho Daemon) is the standalone binary that runs a Rho gateway. It reads configuration from the filesystem, creates a Gateway with a Bind, and enters the main loop.

## Building

```bash
cd rho
mkdir build && cd build
cmake ..
make rhod
```

The binary is at `build/rhod`.

## Running

```bash
# Start with default config directory
./rhod /etc/rho 0.0.0.0:9000

# Start with custom paths
./rhod /home/user/.rho 192.168.1.100:8080
```

Arguments:
1. **Config directory** — Path to the configuration files (identity, routes, upgrades)
2. **Bind address** — UDP address and port to listen on

## What It Does

On startup, `rhod`:

1. Reads the identity (keypair + address) from `<config>/identity/`
2. Reads the route claims from `<config>/routes/`
3. Reads the upgrade targets from `<config>/upgrades/`
4. Creates a `Bind` on the specified address
5. Creates a `DaemonHost` with the loaded configuration
6. Enters the main loop: `bind.update()` → `host.update()` → `usleep(1000)`

## Process Model

`rhod` is single-threaded. It runs one event loop. All I/O is non-blocking. There are no worker threads, no thread pools, no async runtimes.

This is deliberate. A single `rhod` instance can handle thousands of concurrent tunnels on a single core. If you need more throughput, run multiple `rhod` instances on different ports (or use SO_REUSEPORT).

## Signals

- `SIGTERM` / `SIGINT` — Graceful shutdown. Cleans up all tunnels and exits.
