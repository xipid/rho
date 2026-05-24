# Railway API Reference

## Class: `Cart`

**Header:** `#include <Rho/Railway.hpp>`

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `isSecure` | `bool` | `false` | Whether this cart is AEAD-encrypted |
| `isAddressed` | `bool` | `false` | Whether this cart has source/target addresses |
| `hasMeta` | `bool` | `false` | Whether this cart has metadata |
| `hasPayload` | `bool` | `false` | Whether this cart has a payload |
| `source` | `NumericalAddress` | `{}` | Source address |
| `target` | `NumericalAddress` | `{}` | Target address |
| `payload` | `String` | `""` | Cart payload data |
| `meta` | `Map<u64, String>` | `{}` | Key-value metadata |
| `rail` | `u64` | `0` | Which rail to push on (set by sender) |
| `nonce` | `u64` | `0` | AEAD nonce (set by encryption layer) |
| `mac` | `String` | `""` | AEAD authentication tag |

### Methods

```cpp
// Serialize to wire format
String toString();

// Deserialize from wire format
void fromString(const String& data);
void fromString(const String& data, usz& cursor);
```

---

## Class: `Station`

**Header:** `#include <Rho/Railway.hpp>`

### Lifecycle

```cpp
Station station;
station.name = "my-station";  // Optional, for debugging

// Hook to another station (bidirectional)
u64 rail = station.hook(other);

// Unhook a rail
station.unhook(rail);
```

### I/O

```cpp
// Send a cart to all hooked stations
station.push(cart);

// Deliver a cart to this station's listeners
station.receive(cart);
```

### Callbacks

```cpp
// Called when this station receives a cart
station.onCart([](Cart& c) { /* ... */ });

// Called when this station pushes a cart
station.onCartPushed([](Cart& c) { /* ... */ });
```

### Link Encryption

```cpp
// Enable AEAD encryption on this station
station.enableSecurity(key);

// Disable encryption
station.disableSecurity();
```

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `name` | `String` | Station name (for debugging) |
| `lastRecvUS` | `u64` | Timestamp of last received cart (microseconds) |
| `lastSentUS` | `u64` | Timestamp of last sent cart (microseconds) |

---

## Class: `Bind`

**Header:** `#include <Lines/Bind.hpp>`

A Station subclass that wraps a UDP socket. Creates rails automatically for each remote address.

```cpp
// Create a UDP listener
Bind bind("0.0.0.0:9000");

// Pump the socket (call in your main loop)
bind.update();

// Send to a specific remote address
bind.sendTo("192.168.1.100:8080", data);
```

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `port` | `u16` | Listening port |
| `socket` | `SockBind` | Underlying UDP socket |
