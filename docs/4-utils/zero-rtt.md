# 0-RTT Reconnection

## The Problem

Every transport protocol has a weakness: when the underlying network path changes (WiFi → cellular, IP address change, NAT rebinding), the connection breaks.

| Protocol | What happens on path change |
|----------|---------------------------|
| **TCP** | Connection dies. Full 3-way handshake + TLS renegotiation (2-3 RTT). Application must re-sync state. |
| **QUIC** | Connection migration exists, but requires 1-RTT path validation. Application may see brief stall. |
| **WebSocket** | Connection dies. Full HTTP upgrade + TLS (2-3 RTT). |
| **Rho** | **0-RTT resume. Encryption continues. No handshake. No state re-sync.** |

## How It Works

### Client Side

```cpp
// Step 1: Connection is live, player is in-game
// client.tunnel has: keypair, encryption key, nonces, channels, ACK state

// Step 2: Network drops
client.unhook();
// Tunnel stays alive. Everything is preserved:
// - keypair (ephemeral X25519)
// - encryption key (derived from DH)
// - nonce counter
// - send/receive bundle IDs
// - channel state

// Step 3: New transport available
Bind newBind("0.0.0.0:0");
client.hook(newBind, serverAddress);
// _rehookIfResuming() is called internally:
// - Tunnel is re-hooked to the new Station
// - _needsUpgradeMeta = true (next cart will carry the Switch signal)

// Step 4: Next pushPacket()
client.pushPacket(Packet(inputData, 1, true));
// The cart includes: Meta::Command=Switch, Meta::PublicKey=<same ephemeral>
// The payload is a real Tunnel bundle — encrypted with the existing key
```

### Server Side

```cpp
// Server receives a cart with Command=Switch and a PublicKey

// Step 1: Scan existing clients
for (auto& entry : server.clients) {
    if (entry.theirEphemeralPublic == incomingPublicKey) {
        // Found the existing session!

        // Step 2: Re-hook the Tunnel to the new source/rail
        entry.tunnel->rehook(newStation, newRail, newSource);

        // Step 3: Process the bundle (it's encrypted with the session key)
        entry.tunnel->receive(cart.payload);

        // Step 4: Done. No onUpgrade callback. Connection resumes silently.
        return;
    }
}
// If no match: treat as a normal new connection
```

## Security Model

The security of 0-RTT resume is straightforward:

- The Switch cart contains a Tunnel bundle encrypted with the session key.
- Only the legitimate client possesses this key (derived from the ephemeral X25519 exchange).
- If the bundle decrypts and authenticates correctly (AEAD tag passes), the sender is the original client.
- An attacker who replays an old Switch cart will have a stale bundle ID, which the Tunnel's anti-replay window will reject.

QUIC's connection migration requires an explicit path challenge/response round-trip because QUIC doesn't trust the new path implicitly. Rho trusts the new path immediately because the encryption is the proof.

## When to Use

0-RTT reconnection is most valuable for:

- **Mobile games** — Players move between WiFi and cellular constantly
- **IoT devices** — Sensors on unstable radio links (LoRa, BLE, WiFi mesh)
- **Laptops** — Moving between networks (home → coffee shop → office)
- **VPN-like tunnels** — Persistent encrypted channels that survive network changes

## Limitations

- The 0-RTT resume only works if the server is still running and hasn't evicted the session.
- If the server has restarted, the ephemeral key is gone. A full reconnection is needed.
- The resume trusts the new path immediately, which means a brief window of amplification risk if an attacker spoofs the source address. In practice, the AEAD authentication makes exploitation impractical.
