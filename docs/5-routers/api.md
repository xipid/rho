# Router API Reference

## Class: `Router`

**Header:** `#include <Lines/Router.hpp>`

### Hook / Unhook

```cpp
// Register a station at an address
void hook(Station* station, const NumericalAddress& address, i32 weight = 0);

// Register a station under a parent address (child registration)
void hookUnder(Station* station, const NumericalAddress& parentAddress);

// Remove a station from the trie
void unhook(Station* station);
```

### Routing

```cpp
// Route a cart to the best matching station
// Cart must have isAddressed = true and a non-empty target
void route(Cart& cart);
```

### Address Lookup

```cpp
// Find the address assigned to a station
NumericalAddress addressOf(Station* station);
```

### Destruction

```cpp
// Remove all entries and clean up
void destroy();
```

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `entries` | `Array<TreeRoutingEntry>` | Top-level trie nodes |

### TreeRoutingEntry

```cpp
struct TreeRoutingEntry {
    u64 addressPart;                    // Address component at this trie depth
    i32 weight = 0;                     // Priority weight for tie-breaking
    Station* station = nullptr;          // Destination station (null = intermediate node)
    TreeRoutingEntry* parent = nullptr;  // Back-pointer to parent node
    Array<TreeRoutingEntry> children;    // Child nodes
};
```

### Usage Examples

```cpp
Router router;

// Create a simple local network
Station device1, device2, parentGateway;

router.hook(&device1, {3, 1, 1, 1});      // Child device
router.hook(&device2, {3, 1, 1, 2});      // Child device
router.hook(&parentGateway, {3, 1});        // Parent gateway

// Route a local packet (forward match)
Cart local;
local.isAddressed = true;
local.source = {3, 1, 1, 1};
local.target = {3, 1, 1, 2};
router.route(local);  // → device2

// Route an external packet (ancestor fallback)
Cart external;
external.isAddressed = true;
external.source = {3, 1, 1, 1};
external.target = {1, 2, 3, 4};
router.route(external);  // → parentGateway

// Look up a station's address
NumericalAddress addr = router.addressOf(&device1);
// addr = [3, 1, 1, 1]

// Clean up
router.unhook(&device1);
router.destroy();
```
