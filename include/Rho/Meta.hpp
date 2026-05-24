#ifndef XI_RHO_META_HPP
#define XI_RHO_META_HPP 1

#include <Xi/Primitives.hpp>

namespace Rho {

/**
 * Standardized Metadata Keys used across the Rho networking stack.
 *
 */
enum Meta {
  // This contains public keys, and proofs we own them
  Proofed = 0,

  // A Rho address
  Address = 1,

  // A numerical address, used for routing
  NumericalAddress = 2,

  // Contains a VarLong which includes the target length, then all rest is
  // source
  NumericalAddressTargetSource = 3,

  // An array of Writs, see `Sec/Writ.hpp`
  Writs = 4,

  // A public key
  PublicKey = 5, // If it was 32 it is a public key

  // A name
  Name = 6,

  // A UUID
  UUID = 7,

  // A service, basically a String that names what does this do.
  Service = 8,

  // A VarLong
  Version = 9,

  // Path, see Path.hpp
  Path = 10,

  // A marker for gateway authentication and unauthenticated routing bypass
  GatewayMarker = 11,

  // Hardware/software signal strength value (e.g. RSSI for LoRa)
  SignalStrength = 12,

  // Probe/Announce/Switch command
  Command = 13,

  // Public key hash
  PublicHash = 14,

  // Source path or address
  Source = 15,

  // Target path or address
  Target = 16,

  // Password for gateway authentication
  Password = 17,

  // Username for gateway authentication
  Username = 18,

  // Filesystem socket path for IPC
  SocketPath = 19,
};

} // namespace Rho

#endif // XI_RHO_META_HPP
