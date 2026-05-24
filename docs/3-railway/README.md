# Rho Railway

**Take any shared medium — LoRa, USB, UDP, serial — and make point-to-point connections possible.**

Railway is the foundation of Rho. It defines two primitives: the **Cart** (the universal packet) and the **Station** (the universal I/O port). Everything else in Rho builds on these two abstractions.

## Why "Railway"?

Think of a physical railway network:

- **Carts** carry cargo along tracks. They have a source, a destination, and a payload.
- **Stations** are where carts are loaded, unloaded, and redirected. They connect to other stations via tracks (hooks).

Rho's Railway works the same way. A `Cart` is a self-contained message with addresses, metadata, and payload. A `Station` is a programmable I/O port that can receive, send, and forward carts.

## Core Idea

Most networking libraries assume a point-to-point channel already exists (a TCP connection, a UDP socket pair). Railway assumes nothing. It takes a **shared bus** — a medium where multiple entities coexist — and provides addressing, multiplexing, and optional encryption on top.

This makes Railway the right abstraction for:

- **LoRa radio** — Multiple devices share a frequency band
- **USB bus** — Multiple endpoints on a shared bus
- **UDP multicast** — Multiple receivers on a multicast group
- **Serial UART** — Two devices on a wire
- **Shared memory** — Multiple processes on the same machine

## Quick Example

```cpp
#include <Rho/Railway.hpp>

using namespace Rho;

// Create two stations
Station alice;
Station bob;

// Hook them together (bidirectional)
alice.hook(bob);

// Alice listens for carts
alice.onCart([](Cart& c) {
    printf("Alice received: %s\n", c.payload.c_str());
});

// Bob sends a cart
Cart greeting;
greeting.payload = "Hello from Bob";
bob.push(greeting);
// → Alice received: Hello from Bob
```

That's it. Two stations, one hook, carts flow. No sockets, no ports, no IP addresses. The abstraction works at the lowest level and scales to the highest.
