# Packets & Channels

## The Packet

A `Packet` is the application-level unit of data in a Tunnel. It carries a payload, a channel identifier, and flags that control delivery behavior.

```cpp
struct Packet {
    String payload;
    u64 channel = 1;       // Which logical stream this belongs to
    bool important = true;  // If true, guaranteed delivery. If false, fire-and-forget.
    bool bypassHOL = false; // If true, deliver immediately even if earlier packets are missing

    Packet() {}
    Packet(const String& data, u64 ch = 1, bool imp = true);
};
```

## Channels

Channels are independent logical streams multiplexed over a single Tunnel. Each channel has its own ordering.

- **Channel 0** — Reserved for control messages (heartbeats, disconnect signals, ACKs). Never use this for application data.
- **Channels 1+** — Application data. Use as many as you need.

```cpp
// Game server example: three channels

// Channel 1: Chat messages (reliable, ordered)
tunnel.push(Packet(chatMessage, 1, true));

// Channel 2: Player positions (unreliable, latest-wins)
tunnel.push(Packet(positionUpdate, 2, false));

// Channel 3: Kill events (reliable, ordered)
tunnel.push(Packet(killEvent, 3, true));
```

A lost packet on channel 2 does not block delivery on channels 1 or 3. This is the fundamental advantage over TCP, where a single lost segment blocks the entire stream.

## Important vs. Not Important

The `important` flag controls whether the Tunnel guarantees delivery:

| `important` | Behavior |
|-------------|----------|
| `true` (default) | The packet is tracked. If the receiver doesn't ACK it, the Tunnel retransmits. Packets are delivered in order within the channel. |
| `false` | The packet is sent once and forgotten. If it's lost, it's gone. The application is expected to send fresh data that supersedes the old. |

This is the feature that makes Rho suitable for real-time applications. In TCP, every byte is `important`. In QUIC, unreliable datagrams are an extension. In Rho, it's a boolean.

```cpp
// Sensor reading — if this one is lost, the next one arrives in 100ms anyway
tunnel.push(Packet(sensorData, 5, false));
```

## Head-of-Line Bypass

Even within a reliable channel, you sometimes want a packet delivered immediately:

```cpp
Packet urgentPkt(criticalAlert, 3, true);
urgentPkt.bypassHOL = true;
tunnel.push(urgentPkt);
```

With `bypassHOL = true`, the packet is delivered to the application as soon as it arrives, even if earlier packets in the same channel haven't been received yet. The earlier packets will still be retransmitted and delivered later.

## Fragment Status

Packets larger than the Tunnel's bundle maximum size (`bMS`, default 1400 bytes) are automatically fragmented:

```cpp
// This 50 KB packet will be split into ~36 fragments automatically
String largePayload(50000, 'A');
tunnel.push(Packet(largePayload, 1));
```

The receiver reassembles fragments transparently. You never see fragment boundaries in your `onPacket` callback.
