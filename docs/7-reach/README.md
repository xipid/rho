# Reach

**Decentralized name resolution with Byzantine fault tolerance.**

Reach is Rho's answer to DNS. It resolves human-readable names to NumericalAddresses without relying on a single trusted authority. Instead, it queries multiple servers in parallel and uses a BFT consensus mechanism to detect liars.

## The Problem with DNS

DNS is a single point of failure, a single point of censorship, and a single point of surveillance. It's also centralized — ICANN controls the root, TLD operators control the zones, and your ISP's recursive resolver sees every query.

Rho's Reach takes a different approach:

- **No single authority.** Resolution queries go to multiple servers in parallel.
- **Byzantine fault tolerance.** If one server lies, the BFT mechanism detects the inconsistency and marks it as suspicious.
- **No caching hierarchy.** Each Gateway maintains its own cache.
- **Recursive resolution.** If a server says "the answer is at this other address," Reach follows the chain.

## Quick Example

```cpp
Gateway gw;

// Add default Reach servers (like DNS root servers)
gw.addDefaultReachServer({10, 1}, reachServer1PublicKey);
gw.addDefaultReachServer({10, 2}, reachServer2PublicKey);
gw.addDefaultReachServer({10, 3}, reachServer3PublicKey);

// Resolve a name (async)
NumericalAddress addr = gw.reach("game.example.com");
// First call: returns {} (resolution in progress)
// After resolution: returns [5, 8, 9, 42] (cached)

// Get notified when resolution completes
gw.onReachCompleted([](const String& name, const NumericalAddress& addr) {
    printf("Resolved '%s' to address\n", name.c_str());
});

// Tick (processes Reach operations)
while (running) {
    gw.update();
}
```

## How It Works

### Phase 1: BFT Probing

1. Pick 2 random non-suspicious default servers.
2. Probe both in parallel (send a query cart with the name to resolve).
3. Wait up to 200ms for responses.

### Phase 2: Consensus

- **Both respond with the same metadata** → Agreement. Move to Phase 3.
- **Responses differ** → One is lying. Mark the "loser" as suspicious. Pick a new server. Repeat.
- **One times out** → Mark the non-responder as suspicious. Use the response from the one that did respond.
- **Both time out** → Mark both as suspicious. Try again.

If more than 1/3 of default servers are suspicious, the resolution fails. This is the standard BFT threshold — with `f` faulty servers, you need at least `3f + 1` total servers to tolerate them.

### Phase 3: Process Response

The response metadata can contain:

| Field | Meaning | Action |
|-------|---------|--------|
| `NumericalAddress` only | The final answer | Resolution complete |
| `Address` + `NumericalAddress` | Redirect | Recurse: resolve the Address using the NumericalAddress as a new default server |
| `PublicKey` | Identity verification | On the next hop, verify the server's public key matches |

### Phase 4: Recursive Resolution

If the response contains an `Address` (a further name to resolve) and a `NumericalAddress` (the server to ask), Reach creates a **child Reach** instance and continues the resolution chain.

This is similar to DNS's iterative resolution, but with BFT at every step.

## Suspicious Server Tracking

The `susDefaults` list tracks servers that have given inconsistent answers or timed out:

```cpp
Reach reach;
reach.addDefault(server1Addr, server1Key);
reach.addDefault(server2Addr, server2Key);
reach.addDefault(server3Addr, server3Key);

// After resolution, if server2 was caught lying:
// reach.susDefaults = [server2Addr]
// Future resolutions skip server2
```

Suspicious servers are also propagated to the Gateway level:

```cpp
// After a reach completes, the Gateway collects sus servers
for (auto& sus : completedReach->susDefaults) {
    gw.susReachServers.push(sus);
}
```

## Security Model

Reach's BFT works under the assumption that at most 1/3 of the default servers are Byzantine (malicious or faulty). With 3 default servers, 1 can be faulty. With 6, 2 can be faulty.

The `PublicKey` field provides an additional layer: even if a server is compromised, the attacker can't redirect to a fake server unless they also compromise the target's private key.

```
Query: "game.example.com"
Response: NumericalAddress=[5,8,9,42], PublicKey=<game_server_pk>

→ When connecting to [5,8,9,42], the Client verifies that the server's
  ephemeral key exchange produces a valid session with <game_server_pk>.
  A MITM who redirected to a fake address can't produce this proof.
```
