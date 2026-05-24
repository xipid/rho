# Tree Routing

## The Algorithm in Detail

The Router maintains a trie where each node represents one part of a NumericalAddress. For example, address `[3, 1, 1, 1]` creates a path: `3` → `1` → `1` → `1` in the trie.

Each node can optionally have a **Station** — the physical I/O port where carts are delivered.

### Data Structure

```cpp
struct TreeRoutingEntry {
    u64 addressPart;                    // This node's address component
    i32 weight = 0;                     // Priority weight
    Station* station = nullptr;          // Where to deliver (null = intermediate node)
    TreeRoutingEntry* parent = nullptr;  // Back-pointer for traversal
    Array<TreeRoutingEntry> children;    // Subtree
};
```

### Populating the Trie

```cpp
Router router;

// hook(station, address) creates the path in the trie
router.hook(&deviceStation, {3, 1, 1, 1});
// Creates: entries[3].children[1].children[1].children[1].station = &deviceStation

router.hook(&parentStation, {3, 1});
// Creates: entries[3].children[1].station = &parentStation
// (The path [3] and [3, 1] already exist from the first hook)
```

### Forward Match (`_findBestMatch`)

```cpp
void _findBestMatch(Array<TreeRoutingEntry>& level,
                    const NumericalAddress& target,
                    usz depth, i32 accumWeight,
                    TreeRoutingEntry*& best, i32& bestScore, usz& bestDepth);
```

Walk the trie following `target[depth]` at each level. Track the deepest node with a station. This finds the most specific route toward the target.

**Example:** Target `[3, 1, 1, 2]`

```
entries:
  3 (no station)
    1 (station = parent)     ← match at depth 2
      1 (no station)
        1 (station = dev1)   ← would match [3,1,1,1], not [3,1,1,2]
        2 (station = dev2)   ← match at depth 4 ✓ BEST
```

Result: `dev2` at depth 4.

### Ancestor Fallback (`_findBestViaSource`)

When forward match fails (the target's top-level address part doesn't exist in the trie), the algorithm falls back to routing "up" to the parent.

1. Build the **source path**: walk the trie following the source address.
2. Find the **shallowest ancestor with a station**: iterate from root toward the source.
3. Route there.

**Example:** Source `[3, 1, 1, 1]`, Target `[1, 2, 3, 4]`

```
Source path in trie:
  3 (no station)
  3.1 (station = parent)  ← SHALLOWEST with station
  3.1.1 (no station)
  3.1.1.1 (station = self)
```

Result: route to `parent` at `[3, 1]`.

### Why Shallowest?

From `router.md`:

```
1.2.3 < 1.2 < 1 (shallowest is most preferred for upward routing)
```

The shallowest ancestor is the closest to the network root. In the strict tree hierarchy, that's always the immediate parent — because the Gateway only hooks its immediate parent and children. The parent is the only non-child station in the trie.

### Weights

Nodes have optional weights for tie-breaking:

```cpp
router.hook(&fiberA, {1}, 100);  // weight 100
router.hook(&fiberB, {1}, 50);   // weight 50

// Both match target [1, ...]. fiberA wins (higher weight).
```

Weights accumulate along the path. A deeper match with a lower per-node weight can beat a shallower match with a higher per-node weight, because depth is the primary comparator and weight is the secondary.
