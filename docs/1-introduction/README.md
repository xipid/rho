# What is Rho?

Rho is a collection of composable networking primitives. Not a framework, not a library with opinions — a set of building blocks that each solve one problem completely, and compose into something much larger.

You can use Rho to build a game server that handles 10,000 concurrent players at 60 Hz. Or you can use it to replace the entire TCP/IP networking stack of a country. Same code, same primitives, different composition.

## The Problem with Existing Stacks

TCP was designed in 1981. IP was designed in 1983. BGP was designed in 1989. We have been bolting extensions onto these protocols for over 40 years:

- **NAT** was a hack to extend IPv4 addresses. It broke end-to-end connectivity.
- **TLS** was bolted on top of TCP because TCP has no encryption. It adds round-trips.
- **QUIC** was built to fix TCP's head-of-line blocking. It's 200,000+ lines of code.
- **BGP** route leaks have caused global outages. The protocol has no built-in security.

Each layer was designed in isolation, by different people, at different times, with different assumptions. The result is a stack of historical accidents.

## The Rho Approach

Rho starts from zero. No backward compatibility. No legacy considerations. Just the question: *if you were designing networking today, knowing what we know, what would it look like?*

The answer has three layers:

1. **Railway** — A shared medium becomes point-to-point connections. Any bus (LoRa, USB, UDP, serial) becomes addressable. Optional per-link encryption. No IP needed.

2. **Tunnel** — An unreliable connection becomes reliable, encrypted, multiplexed, and congestion-controlled. No TCP needed. No TLS needed. No QUIC needed.

3. **Lines** — Entities that can't physically reach each other can communicate. Hierarchical addressing eliminates NAT. Tree routing eliminates BGP. Each node holds ~50 entries regardless of network size.

## Who is This For?

- **Game developers** who need sub-millisecond unreliable channels alongside reliable event streams, with encryption, without the complexity of QUIC or the latency of TCP.
- **IoT engineers** who need secure communication on ESP32 microcontrollers where a TLS stack won't fit.
- **Network architects** who want to explore what networking looks like without the legacy of IP and BGP.
- **Systems programmers** who want networking primitives they can reason about — code they can read in an afternoon, unroll, and even synthesize into hardware.

## Design Constraints

Every line of Rho was written under these constraints:

- **No dynamic memory allocation in the hot path.** Buffers are pre-allocated. Carts are stack-allocated.
- **No virtual dispatch.** Callbacks are function pointers and closures, not vtables.
- **No exceptions.** Error handling is via return values and state flags.
- **No threads.** Rho is single-threaded by design. You call `update()` in your loop.
- **No dependencies beyond Xi.** Xi is a lightweight C++ framework (~2000 lines) providing `Array`, `Map`, `String`, `Func`, and cryptographic primitives.
- **Runs on ESP32.** If it doesn't compile on a microcontroller with 520 KB of RAM, it doesn't ship.
