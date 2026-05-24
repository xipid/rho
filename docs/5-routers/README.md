# Routers

**The data structure that makes the entire IP/BGP stack unnecessary.**

A `Router` is a trie-based routing engine. It takes a Cart with a target address and delivers it to the correct Station. That's it. No routing protocols, no route advertisements, no convergence algorithms.

## Why It Works

Traditional IP routing requires:

1. **A globally unique address** (IP address)
2. **A routing table** populated by BGP, OSPF, or static routes
3. **Prefix matching** (longest-prefix match on CIDR notation)
4. **Convergence** when links fail (can take minutes)

Rho routing requires:

1. **A hierarchical address** (`3.1.1.1` = telecom 3, city 1, neighborhood 1, device 1)
2. **A trie with ~50 entries** (only your parent and your children)
3. **A tree walk** (forward match for children, ancestor fallback for parent)
4. **Nothing** when links fail (each node's table is static and local)

The address *is* the topology. The trie *is* the routing table. The tree walk *is* the routing algorithm.

## Quick Example

```cpp
#include <Lines/Router.hpp>

Router router;

// Hook children
router.hook(&childStation1, {3, 1, 1, 1});  // device at 3.1.1.1
router.hook(&childStation2, {3, 1, 1, 2});  // device at 3.1.1.2

// Hook parent
router.hook(&parentStation, {3, 1});          // city gateway at 3.1

// Route a cart
Cart cart;
cart.isAddressed = true;
cart.source = {3, 1, 1, 1};
cart.target = {1, 2, 3, 4};  // some address on the other side of the world

router.route(cart);
// → Forward match fails (no '1' at top level)
// → Ancestor fallback finds parentStation at [3, 1]
// → Cart is delivered to parent
```

## Routing Algorithm

The algorithm has two phases:

### Phase 1: Forward Match

Walk the trie following the target address parts. At each level, look for a node whose `addressPart` matches the next part of the target address.

```
Target: [1, 1, 1, 1]
Tree:   3 → 1 → 1 → {1 (device1), 2 (device2)}
                      ↑ match at [3,1,1,1]? No — target starts with 1, tree starts with 3.
→ No forward match.
```

Forward match is used for **downward routing** — routing to children within the local subtree.

### Phase 2: Ancestor Fallback

Walk the source address path in the trie. Find the **shallowest ancestor with a station**. That's the parent gateway — route there.

```
Source: [3, 1, 1, 1]
Tree:   3 (no station) → 1 (station = parentGateway) → 1 → 1 (station = device)
                          ↑ shallowest ancestor with station
→ Route to parent gateway.
```

Ancestor fallback is used for **upward routing** — routing to the parent when the target is outside the local subtree.

### Priority Order

From `router.md`:

```
1.2.3.4 want to send to 5.8.9.9:
1.2.3 < 1.2 < 1 < 5 < 5.8.9 < 5.8.9.9 ( < means less preferred )
```

- **Forward matches always beat ancestor matches.** If a child matches the target, route down.
- **Among ancestors, shallowest wins.** Closer to the root = closer to the other side of the network.
- **Among forward matches, deepest wins.** More specific = better route.

## Fire and Forget

There are no retries in Router. If a routing entry is chosen and the station drops the cart, that cart is gone forever. This is by design.

Reliability is the Tunnel's job. The Router just moves carts. If you need guaranteed delivery, wrap your data in a Tunnel. The Tunnel will retransmit lost packets regardless of how many routers the underlying carts pass through.
