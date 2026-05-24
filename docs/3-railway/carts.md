# Carts

## What is a Cart?

A Cart is Rho's universal packet format. Every piece of data that moves through a Rho network — from a LoRa radio payload to a cross-continent routed message — is a Cart.

## Structure

```cpp
struct Cart {
    // Header flags
    bool isSecure    = false;  // Encrypted with AEAD
    bool isAddressed = false;  // Has source/target addresses
    bool hasMeta     = false;  // Has key-value metadata
    bool hasPayload  = false;  // Has a payload

    // Addressing
    Resource::NumericalAddress source;  // Where this cart came from
    Resource::NumericalAddress target;  // Where this cart is going

    // Payload
    String payload;

    // Metadata
    Map<u64, String> meta;

    // Security
    u64 nonce = 0;
    String mac;
};
```

## NumericalAddress

Addresses in Rho are **variable-depth arrays of u64 values**:

```cpp
NumericalAddress addr;
addr.push(3);    // Telecom 3
addr.push(1);    // City 1
addr.push(1);    // Neighborhood 1
addr.push(1);    // Device 1
// addr = [3, 1, 1, 1]
```

Each number is called a **port**. The address is hierarchical — `[3, 1, 1, 1]` encodes the full path from the root of the network to the specific device. Deeper addresses are more specific. Shallower addresses match broader subtrees.

Unlike IP addresses (fixed 32 or 128 bits), NumericalAddresses have no fixed length. A top-level telecom might have address `[3]`. A VM inside a container inside a device might have address `[3, 1, 1, 1, 2, 5, 1]`. The address depth matches the actual network depth.

## Wire Format

```
┌────────┬────────┬────────┬──────────┬───────────┬────────────┐
│ Header │ Target │ Source │ Metadata │  Payload  │ Nonce+MAC  │
│  (1B)  │(VarLen)│(VarLen)│ (VarLen) │ (VarLen)  │ (optional) │
└────────┴────────┴────────┴──────────┴───────────┴────────────┘
```

The **header byte** is a bitfield:

| Bit | Meaning |
|-----|---------|
| 0 | `isSecure` — Cart is AEAD-encrypted |
| 1 | `isAddressed` — Cart has addresses |
| 2 | `hasMeta` — Cart has metadata |
| 3 | `hasPayload` — Cart has payload |

All variable-length fields use VarLong encoding for their sizes.

## Creating Carts

```cpp
// Minimal cart (just a payload)
Cart c;
c.payload = "hello";

// Addressed cart
Cart c;
c.isAddressed = true;
c.source = {3, 1, 1, 1};  // from 3.1.1.1
c.target = {1, 1, 1, 1};  // to 1.1.1.1
c.payload = data;

// Cart with metadata
Cart c;
c.hasMeta = true;
c.meta.put(Meta::Service, "game-server");
c.meta.put(Meta::Version, "1.0");
c.payload = data;
```

## Serialization

```cpp
// Serialize to bytes
String wire = cart.toString();

// Deserialize from bytes
Cart incoming;
incoming.fromString(wire);
```

Serialization is zero-copy where possible. The `toString()` method writes directly into a pre-sized buffer. The `fromString()` method reads in place without intermediate allocations.
