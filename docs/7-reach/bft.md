# Byzantine Fault Tolerance in Reach

## What is BFT?

Byzantine Fault Tolerance is the ability of a system to function correctly even when some participants are malicious, compromised, or faulty. The name comes from the Byzantine Generals Problem: how do multiple generals coordinate an attack when some of them might be traitors?

In the context of name resolution, the "generals" are the Reach servers, and the "traitor" is a compromised server that returns a wrong address — potentially redirecting you to a phishing site or a man-in-the-middle.

## The BFT Threshold

The standard BFT result: a system can tolerate `f` Byzantine faults if it has at least `3f + 1` participants.

| Total servers | Max faulty | Status |
|---------------|-----------|--------|
| 3 | 1 | Minimum viable |
| 4 | 1 | More resilient |
| 5 | 1 | Still f=1 |
| 6 | 2 | Better |
| 7 | 2 | Standard |
| 10 | 3 | High resilience |

Reach enforces this threshold: if more than 1/3 of default servers are marked suspicious, the resolution fails. This prevents an attacker who controls too many servers from achieving consensus on a wrong answer.

## The Algorithm

```
Round 1:
  Pick servers A, B at random (both non-suspicious)
  Probe A and B in parallel
  Wait 200ms

  If A and B agree:
    → Accept the answer
    → Resolution moves to next phase

  If A and B disagree:
    → Mark B as suspicious
    → Pick a new server C
    → Go to Round 2

  If A responds, B times out:
    → Mark B as suspicious
    → Accept A's answer (tentatively)
    → Resolution moves to next phase

  If both time out:
    → Mark both as suspicious
    → Go to Round 2

Round 2:
  If suspicious count > 1/3 of total:
    → Resolution FAILS
  Else:
    Pick servers A, C at random (excluding suspicious)
    Repeat...
```

## What Counts as "Agree"?

Two responses "agree" if their key metadata fields match:

- `NumericalAddress` — Must be identical
- `Address` — Must be identical
- `PublicKey` — Must be identical

Other metadata fields (Name, Service, Version) are not compared — they're informational and don't affect routing.

## The 200ms Timeout

The 200ms timeout is a deliberate tradeoff:

- **Too short** → Legitimate servers on slow links get marked as suspicious
- **Too long** → Resolution takes too long for the user
- **200ms** → Covers most global round-trip times while keeping resolution snappy

A server that responds after 200ms is not immediately discarded — if the BFT phase hasn't advanced, the late response is still processed. But if the BFT phase has moved on (a new round started), the late response counts as a timeout.

## Comparison with DNS Security

| Feature | DNS + DNSSEC | Rho Reach |
|---------|-------------|-----------|
| Trust model | Hierarchical (ICANN → TLD → Registrar) | BFT (no single authority) |
| Forgery protection | DNSSEC signatures (rarely deployed) | BFT consensus + key verification |
| Censorship resistance | None (ISP controls resolver) | Queries go to multiple servers in parallel |
| Single point of failure | Root servers, TLD servers | None (1/3 can fail) |
| Complexity | DNSSEC is notoriously complex | ~350 lines of code |
| Cache poisoning | Ongoing problem | Not possible (BFT detects inconsistency) |
