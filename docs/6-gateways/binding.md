# Binding Ports

## What is Port Binding?

In the traditional stack, a server "binds" to a port (e.g., `0.0.0.0:80`). In Rho, a Gateway can bind to ports within its address space. Each port becomes a child address in the Router trie.

```cpp
Gateway gw;
gw.address = {3, 1, 1, 1};  // Device at 3.1.1.1

gw.bindPort(80);   // Reachable at [3, 1, 1, 1, 80]
gw.bindPort(443);  // Reachable at [3, 1, 1, 1, 443]
gw.bindPort(7777); // Reachable at [3, 1, 1, 1, 7777]
```

## How It Works

`bindPort()` creates a Station and hooks it into the Router:

```cpp
void bindPort(u32 port) {
    Station* station = new Station();
    NumericalAddress portAddr = address;
    portAddr.push(port);
    router.hook(station, portAddr);

    BoundPort bp;
    bp.port = port;
    bp.station = station;
    boundPorts.push(bp);
}
```

When a cart targets `[3, 1, 1, 1, 80]`, the Router's forward match walks:
- `3` → `1` → `1` → `1` → `80` → station found. Delivered.

## Listening on a Port

```cpp
// Bind the port
Station* httpPort = gw.bindPort(80);

// Listen for carts on this port
httpPort->onCart([](Cart& c) {
    printf("HTTP request: %s\n", c.payload.c_str());
    // Handle the request...
});
```

## Port Addressing

In Rho's address model, ports are just deeper address components. There's no semantic difference between a "port" and any other address level. The concept is a convention, not a protocol feature.

A "port" at `[3, 1, 1, 1, 80]` is the same thing as a "device" at `[3, 1, 1, 1]` — it's just one level deeper in the tree. You could have ports within ports:

```cpp
// App at [3, 1, 1, 1, 80, 1]  — app 1 on port 80
// App at [3, 1, 1, 1, 80, 2]  — app 2 on port 80
```

This makes the addressing system naturally extensible without any protocol changes.
