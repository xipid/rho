# Reliability & Congestion

## The Reliability Model

Rho Tunnel uses a **selective acknowledgment** system with a 64-bit sliding window. This is simpler and faster than TCP's cumulative ACK + SACK extension, and more expressive than QUIC's per-packet ACK frames.

### How It Works

1. The sender assigns each bundle a monotonically increasing **bundle ID**.
2. The receiver tracks the highest received bundle ID and a 64-bit bitmap of recently received bundles.
3. On every bundle, the receiver piggybacks its ACK state: `(highestReceivedID, bitmap)`.
4. The sender scans the bitmap to determine which bundles were received and which need retransmission.

```
Sender:  sends bundles 10, 11, 12, 13, 14
                             ↑ lost

Receiver: highestReceived = 14
          bitmap = ...1101011  (12 missing)

Sender reads bitmap → retransmits bundle 12
```

### Important Packets Only

Only bundles containing `important` packets are tracked for reliability. If a bundle contains only non-important packets, it's sent once and the sender doesn't expect an ACK.

This means the reliability system has zero overhead for unreliable traffic. In a game server sending 60 position updates per second, only the kill events and chat messages consume retransmission state.

## Congestion Control

Rho's congestion control is deliberately minimal. There is no slow start, no congestion avoidance, no fast recovery. There is one number:

```cpp
tunnel.maxInflight = 64;  // default
```

`maxInflight` is the maximum number of bundles that can be "in flight" (sent but not yet acknowledged). If the number of in-flight bundles reaches `maxInflight`, the Tunnel stops sending new bundles until ACKs arrive.

### Why Not Cubic/BBR?

TCP's congestion control algorithms (Reno, Cubic, BBR) are designed for a general-purpose internet where flows compete for shared bottleneck links. They spend thousands of lines of code modeling bandwidth-delay products and probing for available capacity.

Rho's target environments are different:

- **Game servers** — The server knows exactly how much bandwidth each player needs. Congestion control should be a cap, not an algorithm.
- **IoT devices** — A LoRa link has a fixed 250 bps. There is no capacity to probe.
- **Private networks** — The operator controls the links. There are no unknown cross-traffic flows.

If you need sophisticated congestion control, you implement it in your application on top of Rho's `canPush()` backpressure signal:

```cpp
if (tunnel.canPush()) {
    tunnel.push(nextPacket);
} else {
    // Backpressure — either drop or buffer
}
```

## Heartbeats

Tunnels send periodic heartbeats to detect dead connections:

```cpp
tunnel.heartbeatIntervalMS = 500;  // default: 500ms
```

If no data or heartbeat is received within `activeTimeout`, the tunnel is marked as dead:

```cpp
tunnel.activeTimeout = Tunnel::ActiveTimeout::Relaxed;  // 30 seconds
// Other options: Active (10s), Aggressive (5s)
```

Heartbeats also carry ACK information, ensuring that acknowledgments are sent even when the application has no data to send.

## Bundle Sizing

The maximum bundle size controls fragmentation behavior:

```cpp
tunnel.bMS = 1400;  // default: 1400 bytes
```

Packets larger than `bMS` are fragmented into multiple bundles. Each fragment is independently tracked for reliability. The receiver reassembles fragments into the original packet before delivering to the application.

Choose `bMS` based on your transport's MTU:
- **UDP over Ethernet**: 1400 (safe below the 1500 MTU)
- **LoRa**: 200-250
- **USB/Serial**: as large as your buffer allows
