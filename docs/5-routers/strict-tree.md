# The Strict Tree

## How the Real Network Works

Rho's routing model is built for a world that looks like this:

```
Telecom 1 (USA)              Telecom 2 (DZ)             Telecom 3 (SA)
├── City 1.1                 ├── City 2.1                ├── City 3.1
│   ├── Neighborhood 1.1.1   │   ├── Neigh 2.1.1         │   ├── Neigh 3.1.1
│   │   ├── Home 1.1.1.1     │   │   ├── Home 2.1.1.1    │   │   ├── Home 3.1.1.1
│   │   │   ├── Dev 1.1.1.1.1│   │   │   └── ...         │   │   │   ├── Dev 3.1.1.1.1
│   │   │   └── Dev 1.1.1.1.2│   │   └── Neigh 2.1.2     │   │   │   └── Dev 3.1.1.1.2
│   │   └── Home 1.1.1.2     │   └── City 2.2            │   │   └── Home 3.1.1.2
│   └── Neighborhood 1.1.2   └── City 2.3                │   └── Neigh 3.1.2
└── City 1.2                                              └── City 3.2
```

Below each device, there can be VMs, containers, processes — each one gets a deeper address:

```
Device 3.1.1.1.1
├── VM 3.1.1.1.1.1
│   ├── Container 3.1.1.1.1.1.1
│   │   └── Process 3.1.1.1.1.1.1.1
│   └── Container 3.1.1.1.1.1.2
└── VM 3.1.1.1.1.2
```

Replicate this structure across 600 telecoms, each with cities, neighborhoods, homes, devices, and virtual machines. That's the global network.

## The Rules

### Rule 1: Each gateway registers only its immediate children and parent

A home gateway at `3.1.1.1` knows:

| Entry | Type | Station |
|-------|------|---------|
| `[3, 1, 1, 1, 1]` | Child | Device 1 |
| `[3, 1, 1, 1, 2]` | Child | Device 2 |
| `[3, 1, 1]` | Parent | Neighborhood gateway |

That's it. Three entries. The home gateway doesn't know about any device in the USA. It doesn't know about the telecom structure. It doesn't know about `2.1.1.1.1`.

### Rule 2: No route synchronization

Gateways never exchange routing tables. Each gateway builds its own trie from the devices and peers directly connected to it. There is no BGP, no OSPF, no LSA flooding.

If a new device connects to a home gateway, the home gateway hooks it. The rest of the network doesn't know and doesn't care.

### Rule 3: Top-level gateways are manually configured

At the telecom level, governments or network operators manually configure the peering:

```cpp
// DZ telecom gateway — manual configuration
dzGateway.router.hook(&fiberToUSA, {1});   // Route prefix 1.x to USA
dzGateway.router.hook(&fiberToSA,  {3});   // Route prefix 3.x to SA
```

These are the only entries in the DZ telecom's router (besides its own children). Two entries to peer with two other countries.

### Rule 4: Routing is always up or down, never sideways

Within the tree, a gateway either:
- **Routes down** to a child (forward match found)
- **Routes up** to its parent (ancestor fallback)

There is no "sibling routing" within the tree. If your target is `1.1.1.1` and you're at `3.1.1`, you go UP to `3.1`, then UP to `3`, then ACROSS to `1` (via a top-level peering fiber), then DOWN to `1.1`, DOWN to `1.1.1`, DOWN to `1.1.1.1`.

The only "sideways" routing happens at the top level, between telecoms.

## Scaling

At 8 billion people with ~3 devices each, the numbers look like this:

| Level | Count | Entries per gateway |
|-------|-------|-------------------|
| Telecoms | ~600 | ~50 (peer fibers) |
| Cities | ~60,000 | ~100 (neighborhoods) |
| Neighborhoods | ~6,000,000 | ~100 (homes) |
| Homes | ~2,000,000,000 | ~5 (devices) |
| Devices | ~6,000,000,000 | ~5 (VMs/containers) |
| VMs/Processes | ~24,000,000,000 | ~5 (processes) |

**Each gateway holds 5 to 100 entries.** The total number of routing entries in the entire global network is proportional to the number of gateways — not the number of nodes. No single node needs to know about the entire network.

Compare this to BGP, where a full routing table has ~1,000,000 prefixes and every edge router carries the full table.

## Example: Cross-Continental Routing

**Source:** Device `3.1.1.1` (Saudi Arabia)
**Target:** Device `1.1.1.1` (USA)

```
Step 1: Home 3.1.1 → target starts with 1, not a child
        → Ancestor fallback → parent station at [3, 1]
        → Routes to Neighborhood 3.1

Step 2: Neighborhood 3.1 → target starts with 1, not a child
        → Ancestor fallback → parent station at [3]
        → Routes to City 3

Step 3: City 3 → target starts with 1, not a child
        → Ancestor fallback → parent station at [3] (SA telecom)
        Wait — actually City 3 is under SA telecom. Parent = SA telecom.
        → Routes to SA Telecom

Step 4: SA Telecom → forward match: entry [1] exists! Station = fiber to DZ
        → Routes to fiber → DZ

Step 5: DZ Telecom → forward match: entry [1] exists! Station = fiber to USA
        → Routes to fiber → USA

Step 6: USA Telecom → forward match: [1, 1] → City 1.1
        → Routes DOWN

Step 7: City 1.1 → forward match: [1, 1, 1] → Neighborhood 1.1.1
        → Routes DOWN

Step 8: Neighborhood 1.1.1 → forward match: [1, 1, 1, 1] → Device
        → DELIVERED
```

Total: 8 hops. Each hop is a single trie lookup. Each gateway's table has <100 entries. No routing protocol was involved.
