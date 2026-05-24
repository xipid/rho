# Philosophy

## Stupid Simple

Rho's internal motto is borrowed from engineering: **Keep It Stupid Simple.**

Every protocol in the traditional networking stack was designed by a committee, for a committee. Rho is designed for one programmer, reading one file, understanding everything in one sitting.

The entire Router is one file. The entire Tunnel is one file. The entire Server is one file. No source files — just headers. No build steps beyond `#include`. You read the code. You understand the code. You own the code.

## Fire and Forget

Rho does not retry. The Router does not care. If a routing entry is chosen and the station drops the cart, that cart is gone forever. This is a feature.

Reliability is the Tunnel's job, not the Router's job. Encryption is the Station's job, not the application's job. Each layer does one thing. If you want reliability, you add a Tunnel. If you don't, you don't. There is no implicit cost for features you aren't using.

## The Code is the Spec

There is no RFC. There is no specification document that diverges from the implementation. The `.hpp` files are the specification. The comments in those files are the spec's prose.

This means:

- **No ambiguity.** If you wonder how the routing algorithm works, you read `Router.hpp`. It is 460 lines. The algorithm is at line 190.
- **No implementation drift.** The spec can't be "ahead" of the code or "behind" the code. They are the same artifact.
- **No compliance testing.** You don't test against a spec. You test against the behavior of the code.

## Unrollable

Every function in Rho is written so that a compiler — or a hardware engineer — can unroll it into a flat pipeline. No recursion that can't be bounded. No allocations that can't be pre-sized. No control flow that can't be predicted.

This matters because:

- **On a CPU**, the compiler can inline and vectorize the hot paths. `_findBestMatch` is a bounded tree walk. `Cart::toString()` is a linear serialization.
- **On an FPGA/ASIC**, the same algorithm can be implemented as a combinational circuit or a fixed-stage pipeline. The Router's trie walk maps directly to a TCAM lookup.
- **On an ESP32**, there are no hidden allocations that would fragment the 520 KB heap.

## Hierarchical, Not Flat

IP addresses are flat. `192.168.1.1` doesn't tell you where the device is in the network hierarchy. CIDR notation is a hack to add hierarchy after the fact.

Rho addresses are hierarchical by design. `3.1.1.1` means: telecom 3, city 1, neighborhood 1, device 1. The address *is* the topology. The routing algorithm *is* a tree walk. Aggregation is free — you don't need to calculate prefix lengths.

## Self-Built, Not Synced

BGP works by having routers advertise their reachable prefixes to neighbors, who advertise to their neighbors, who advertise to their neighbors. This is slow, fragile, and vulnerable to route leaks.

Rho routing tables are self-built. Each gateway hooks its immediate children and its immediate parent. That's it. No advertisements. No convergence. No route leaks. The tree structure is implicit in the address hierarchy and the physical topology.

A gateway at `3.1` knows about `3.1.1`, `3.1.2`, `3.1.3` (its children) and `3` (its parent). It does not know about `1.2.3.4` on the other side of the planet. It doesn't need to.

## Encryption is Not Optional

In the traditional stack, encryption is a separate layer (TLS, IPsec) that you bolt on and hope you configured correctly.

In Rho, encryption is built into the Station and the Tunnel. A single function call — `enableSecurity(key)` on a Station, or `enableSecureX(theirPublic, ourKeypair)` on a Tunnel — and every byte is authenticated and encrypted. No certificates. No certificate authorities. No ALPN negotiation. Just X25519 + HKDF + AEAD.
