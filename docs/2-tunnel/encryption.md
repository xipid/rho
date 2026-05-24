# Encryption & Key Exchange

## Philosophy

Rho does not use TLS. There are no certificates, no certificate authorities, no ALPN negotiation, no cipher suite negotiation. There is one algorithm, it is always on, and it works.

## The Primitives

| Primitive | Algorithm | Purpose |
|-----------|-----------|---------|
| Key exchange | X25519 | Generate a shared secret from two key pairs |
| Key derivation | HKDF-SHA256 | Derive an encryption key from the shared secret |
| Authenticated encryption | AEAD (ChaCha20-Poly1305 or AES-256-GCM) | Encrypt and authenticate every bundle |
| Hashing | SHA-256 | Public key hashing, data integrity |

## Station-Level Encryption

A Station can encrypt all carts that pass through it:

```cpp
Station station;

// Generate a symmetric key (e.g., from a pre-shared secret)
String key = Sec::hash(sharedSecret, 32);

// Enable encryption on this station
station.enableSecurity(key);
```

Every cart pushed through this station is encrypted with AEAD. Every cart received is decrypted and verified. Invalid MACs are silently dropped.

Station-level encryption protects a physical link — e.g., a LoRa radio channel shared between a gateway and its devices.

## Tunnel-Level Encryption

Tunnels use ephemeral key exchange for forward secrecy:

```cpp
// On the client
auto clientKeypair = Sec::generateKeyPair();
tunnel.enableSecureX(serverPublicKey, clientKeypair);

// On the server
auto serverKeypair = Sec::generateKeyPair();
tunnel.enableSecureX(clientPublicKey, serverKeypair);
```

`enableSecureX` performs:

1. **X25519 Diffie-Hellman** — `sharedSecret = DH(theirPublic, ourPrivate)`
2. **HKDF** — `key = HKDF(sharedSecret, "RhoTunnel", 32)`
3. **AEAD setup** — All subsequent bundles are encrypted with this key

The key is **ephemeral**. Each connection generates a fresh keypair. Compromising one session's key does not compromise past or future sessions.

## Wire Format (Encrypted Bundle)

```
┌──────────┬──────────┬──────────────────────┐
│ Bundle ID│ Nonce(8B)│ AEAD Ciphertext + TAG │
│ (VarLong)│          │    (payload + 8B tag) │
└──────────┴──────────┴──────────────────────┘
```

- **Bundle ID** — VarLong-encoded sequence number
- **Nonce** — 8 bytes, derived from the bundle ID
- **Ciphertext** — The encrypted payload (packets + their metadata)
- **TAG** — 8-byte truncated AEAD authentication tag

The 8-byte tag is a deliberate tradeoff: smaller than the standard 16-byte tag, saving 8 bytes per bundle at the cost of reduced MAC security margin. For a real-time protocol sending thousands of bundles per second, the forgery probability remains negligible.

## Anti-Replay

Tunnels maintain a sliding window bitmap to prevent replay attacks:

```
receivedBundleID    = 1000
window              = 0b...0001111111111111  (bundles 985-999 received)
bitmapLength        = 64
```

A bundle with an ID older than `receivedBundleID - bitmapLength` is silently dropped. This prevents an attacker from recording and replaying old encrypted bundles.
