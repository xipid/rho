# Rho Tunnel

**Take a datagram — insecure, unreliable — and turn it into fully secure, reliable, multiplexed communication.**

The Tunnel is Rho's answer to TCP, TLS, and QUIC combined. But unlike those protocols, it's 700 lines of code, runs on ESP32, and gives you first-class unreliable channels for real-time applications.

## What It Does

A Tunnel sits on top of a Station (which itself sits on any transport — UDP, LoRa, USB, serial). It provides:

- **Reliability** — Selective ACK with a 64-bit receive bitmap. Lost packets are retransmitted.
- **Multiplexing** — Multiple channels over a single connection. Channel 0 is reserved for control.
- **Encryption** — X25519 key exchange → HKDF → AEAD. One function call.
- **Congestion control** — A `maxInflight` cap on in-flight bundles. Simple, predictable, no surprise throttling.
- **Fragmentation** — Packets larger than the bundle MTU (~1400 bytes) are transparently split and reassembled.
- **Head-of-line bypass** — Mark a packet as `bypassHOL` and it's delivered immediately, even if earlier packets are missing.

## Quick Example

```cpp
#include <Rho/Tunnel.hpp>
#include <Lines/Bind.hpp>

// Create a UDP transport
Bind bind("0.0.0.0:9000");

// Create a tunnel and hook it to the bind
Tunnel tunnel;
tunnel.hook(bind);

// Enable encryption
auto keypair = Sec::generateKeyPair();
tunnel.enableSecureX(theirPublicKey, keypair);

// Send a reliable packet
tunnel.push(Packet("hello", 1, true));   // channel 1, important

// Send an unreliable packet (game state, sensor reading, etc.)
tunnel.push(Packet(gameState, 2, false)); // channel 2, not important

// In your main loop
while (running) {
    bind.update();
    tunnel.update(); // Flushes outgoing, processes heartbeats
    usleep(1000);    // 1 kHz tick
}
```

## When to Use Tunnel

Use Tunnel directly when you want full control over the connection lifecycle. For most applications, prefer the higher-level `Server` and `Client` (which manage Tunnels for you).

| Use Case | Use Tunnel directly? | Use Server/Client? |
|----------|---------------------|---------------------|
| Custom protocol | ✅ | — |
| Game server | — | ✅ |
| IoT sensor | ✅ | — |
| Web API | — | ✅ |
| Point-to-point link | ✅ | — |
