# Resolution Algorithm

## Resolution Flow

```
start("game.example.com")
        │
        ▼
┌── BFT Phase ──┐
│ Probe 2 servers│
│ Compare answers│
│ Mark sus if    │
│ mismatch       │
└───────┬────────┘
        │
        ▼
┌── Process Response ─────────────────────────────┐
│                                                  │
│  Has NumericalAddress only?                      │
│  └── YES → DONE. Final address found.           │
│                                                  │
│  Has Address + NumericalAddress?                 │
│  └── YES → RECURSE. Create child Reach          │
│            with NumericalAddress as new default. │
│                                                  │
│  Has PublicKey only?                             │
│  └── YES → UPGRADE. Connect to a default server,│
│            verify its key, get the real answer.  │
│                                                  │
│  None of the above?                              │
│  └── FAIL. Not enough information.               │
└──────────────────────────────────────────────────┘
```

## Recursive Resolution

When a Reach server responds with both an `Address` (a further name) and a `NumericalAddress` (where to ask next), the resolution chain continues:

```
Query: "game.example.com"
  → Server A responds: Address="game.cdn.net", NumericalAddress=[10, 5]
     → Child Reach queries [10, 5] for "game.cdn.net"
        → Server at [10, 5] responds: NumericalAddress=[5, 8, 9, 42]
           → DONE. Final address = [5, 8, 9, 42]
```

This is similar to DNS CNAME chains, but at every step the BFT mechanism ensures the answer is consistent.

## Metadata Accumulation

As the resolution chain progresses, metadata is accumulated:

```cpp
Reach reach;
reach.start(station, "game.example.com");

// After resolution, reach.meta contains merged metadata from all steps:
// - PublicKey from the final server
// - Service type from intermediate servers
// - Version info
// etc.
```

Later metadata overwrites earlier metadata for the same key. This allows intermediate servers to provide provisional answers that the final server can override.

## Upgrade Phase

When the BFT phase returns a `PublicKey` without a final `NumericalAddress`, the Reach enters an Upgrade phase:

1. Pick a non-suspicious default server.
2. Connect via Client (probe → announce → upgrade).
3. Verify the server's public key matches the required one.
4. Process the post-upgrade response metadata.
5. Continue resolution based on the response.

This handles cases where the name resolution requires an authenticated connection — for example, when the Reach server needs to verify the requester's identity before providing the answer.

## Error Handling

Resolution fails when:

- More than 1/3 of default servers are suspicious
- No default servers are available
- The recursive chain reaches a dead end (no NumericalAddress, no Address, no PublicKey)
- The Upgrade phase fails (key mismatch, timeout)

```cpp
Reach reach;
reach.start(station, "nonexistent.example.com");

// After ticking...
if (reach.done && !reach.success) {
    // Resolution failed
    printf("Could not resolve address\n");
}
```

There are no retries. If the resolution fails, the application decides whether to try again with different defaults.
