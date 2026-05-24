# Reach API Reference

## Class: `Reach`

**Header:** `#include <Lines/Reach.hpp>`

### Construction

```cpp
Reach reach;
```

### Configuration

```cpp
// Add default servers (Reach will query these)
reach.addDefault(serverAddress, serverPublicKey);

// BFT timeout (default: 200ms)
reach.bftTimeoutUS = 200000;
```

### Starting Resolution

```cpp
// Start resolving an address
reach.start(station, "game.example.com");
```

### Ticking

```cpp
// Must be called in your main loop
reach.update();
```

### State

| Property | Type | Description |
|----------|------|-------------|
| `done` | `bool` | Resolution has completed (success or failure) |
| `success` | `bool` | Resolution succeeded |
| `finalAddress` | `NumericalAddress` | The resolved address (valid when success=true) |
| `finalPublicKey` | `String` | The target's public key (if provided) |
| `meta` | `Map<u64, String>` | Accumulated metadata from the resolution chain |
| `susDefaults` | `Array<NumericalAddress>` | Servers marked as suspicious |

### Cleanup

```cpp
// Stop and free all resources
reach.destroy();
```

### Full Example

```cpp
Bind bind("0.0.0.0:0");
Station station;
station.hook(bind);

Reach reach;
reach.addDefault({10, 1}, server1Key);
reach.addDefault({10, 2}, server2Key);
reach.addDefault({10, 3}, server3Key);

reach.start(station, "game.example.com");

while (!reach.done) {
    bind.update();
    reach.update();
    usleep(1000);
}

if (reach.success) {
    printf("Resolved to: ");
    for (usz i = 0; i < reach.finalAddress.size(); ++i) {
        printf("%lu.", reach.finalAddress[i]);
    }
    printf("\n");
} else {
    printf("Resolution failed. %zu servers marked suspicious.\n",
           reach.susDefaults.size());
}
```
