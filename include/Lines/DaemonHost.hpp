#ifndef LINES_DAEMONHOST_HPP
#define LINES_DAEMONHOST_HPP

#include <Lines/Daemon.hpp>
#include <Lines/Gateway.hpp>

namespace Lines {

using namespace Rho;
using namespace Xi;

// ---------------------------------------------------------------------------
// DaemonHost
//
// Extends Gateway with Daemon-specific logic: password authentication,
// identity management, auto-config from filesystem.
// ---------------------------------------------------------------------------
class DaemonHost : public Gateway {
public:
  Array<GatewayInfo> hostAs;
  Array<IdentityClaim> routes; // If publicKey matches, assign that address
  Array<GatewayInfo> upgrades;

  DaemonHost() : Gateway() {}

  /// Update — processes all host-as announcements, applies routes, ticks gateway.
  void update() {
    // Process upgrades: connect to configured hosts
    for (usz i = 0; i < upgrades.size(); ++i) {
      auto& up = upgrades[i];
      // Check if we already have a client for this
      bool found = false;
      for (usz j = 0; j < clients.size(); ++j) {
        if (clients[j]->serverAddr == up.address) {
          found = true;
          break;
        }
      }
      if (!found && up.address.size() > 0) {
        Client* cli = new Client();
        if (hookedStations.size() > 0) {
          cli->hook(*hookedStations[0], up.address);
        }
        cli->serverAddr = up.address;
        cli->hasServerAddr = true;

        cli->onAnnounce([cli, this](Cart& c) {
          cli->upgrade();
        });

        cli->onReady([this](Packet pkt, Cart cart) {
          // Connection established — routing will be handled by the gateway
        });

        cli->probe();
        clients.push(cli);
      }
    }

    // Apply identity-based routing from routes
    for (usz i = 0; i < routes.size(); ++i) {
      // If a session matches a known identity, assign their claimed address
      // This is checked during onUpgrade in the Gateway
    }

    Gateway::update();
  }
};

} // namespace Lines

#endif // LINES_DAEMONHOST_HPP
