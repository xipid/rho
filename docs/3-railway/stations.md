# Stations

## What is a Station?

A Station is Rho's universal I/O port. It's the point where carts enter and leave your code. Stations are composable bricks — you can hook them together, chain them, fork them, or wrap them with encryption.

## Hooks

Two stations hooked together can send carts to each other:

```cpp
Station a, b;
a.hook(b);   // Bidirectional — a can send to b, b can send to a

// Now:
a.push(cart);   // → b receives the cart
b.push(cart);   // → a receives the cart
```

A single station can be hooked to multiple others:

```cpp
Station hub, spoke1, spoke2, spoke3;
hub.hook(spoke1);
hub.hook(spoke2);
hub.hook(spoke3);
```

When `hub.push(cart)` is called, the cart goes to all hooked stations. This is how broadcast works.

## Rails

Rails are sub-channels within a hook. They let a single station multiplex between different peers:

```cpp
// Hook with explicit rails
Station server;
Station clientA, clientB;

u64 railA = server.hook(clientA);  // railA = 0
u64 railB = server.hook(clientB);  // railB = 1

// Send to a specific client
Cart toA;
toA.rail = railA;
server.push(toA);  // → only clientA receives it
```

Rails are how `Bind` (the UDP transport) multiplexes multiple remote peers over a single socket. Each remote address gets its own rail.

## Event Callbacks

```cpp
Station s;

// Called when a cart is received
s.onCart([](Cart& c) {
    printf("Got: %s\n", c.payload.c_str());
});

// Called when a cart is pushed out
s.onCartPushed([](Cart& c) {
    printf("Sent: %s\n", c.payload.c_str());
});
```

## Per-Station Encryption

```cpp
// Enable link-level encryption
String key = Sec::hash(preSharedSecret, 32);
station.enableSecurity(key);

// All carts through this station are now encrypted
// Invalid MACs are silently dropped
```

This is separate from Tunnel-level encryption. Station encryption protects the physical link. Tunnel encryption protects the end-to-end session. You can have both.

## Practical Patterns

### Gateway forwarding

```cpp
Station inbound, outbound;

// Forward all inbound carts to outbound
inbound.onCart([&](Cart& c) {
    outbound.push(c);
});
```

### Bind (UDP transport)

`Bind` is a Station subclass that wraps a UDP socket:

```cpp
#include <Lines/Bind.hpp>

Bind bind("0.0.0.0:9000");

// Bind creates rails automatically for each remote address
// bind.update() pumps the socket
while (running) {
    bind.update();
    usleep(1000);
}
```

### Custom transport

You can implement any transport by subclassing Station:

```cpp
class LoRaStation : public Station {
    void push(Cart& c) override {
        String wire = c.toString();
        lora_send(wire.data(), wire.size());
    }
    void poll() {
        if (lora_available()) {
            Cart c;
            c.fromString(lora_read());
            receive(c);  // deliver to listeners
        }
    }
};
```
