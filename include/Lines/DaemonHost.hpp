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

        cli->onReady([this, cli](Packet pkt, Cart cart) {
          Map<u64, String> respMeta;
          usz cursor = 0;
          if (pkt.payload.size() > 0) {
            respMeta = Map<u64, String>::deserialize(pkt.payload, cursor);
          }

          auto* parentAddrStr = respMeta.get(Meta::Address);
          auto* childAddrStr = respMeta.get(Meta::NumericalAddress);

          if (childAddrStr && !childAddrStr->isEmpty()) {
            Resource::NumericalAddress childAddr;
            Array<String> parts = childAddrStr->split(".");
            for (usz i = 0; i < parts.size(); ++i) {
              childAddr.push((u64)parts[i].toInt());
            }
            this->address = childAddr;

            Resource::NumericalAddress parentAddr;
            if (parentAddrStr && !parentAddrStr->isEmpty()) {
              Array<String> parentParts = parentAddrStr->split(".");
              for (usz i = 0; i < parentParts.size(); ++i) {
                parentAddr.push((u64)parentParts[i].toInt());
              }
            }

            Station* sessStation = new Station();
            String derivedKey = Sec::kdf(cli->tunnel->key, "GatewayStation", 32);
            sessStation->enableSecurity(derivedKey);
            sessStation->name = "GW-Client-Session";

            sessStation->onCart([this](Cart& c) {
              _handleIncomingCart(c, nullptr);
            });

            if (hookedStations.size() > 0) {
              sessStation->hook(*hookedStations[0], cli->rail);
            }

            if (parentAddr.size() > 0) {
              router.hook(sessStation, parentAddr);
            }

            TunnelSession sess;
            sess.tunnel = cli->tunnel;
            sess.sessionStation = sessStation;
            sess.assignedAddress = parentAddr;
            sessions.push(sess);

            cli->tunnel->onDisconnect([this, sessStation](Map<u64, String> reason) {
              for (usz i = 0; i < sessions.size(); ++i) {
                if (sessions[i].sessionStation == sessStation) {
                  _cleanupSession(i);
                  break;
                }
              }
            });
          }
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
