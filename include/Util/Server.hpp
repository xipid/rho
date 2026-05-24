#ifndef UTIL_SERVER_HPP
#define UTIL_SERVER_HPP

#include <Rho/Tunnel.hpp>
#include <Rho/Meta.hpp>
#include <Rho/Railway.hpp>
#include <Sec/Crypto.hpp>
#include <Xi/Func.hpp>
#include <Collection/Array.hpp>
#include <Collection/Map.hpp>
#include <Collection/String.hpp>

namespace Rho {

class Server {
public:
  Station* station = nullptr;
  bool acceptsInsecure = false;

  Sec::KeyPair keypair;
  Collection::Array<Tunnel*> clients; // Was "tunnels"

  Func<void(Cart&)> onProbeCallback;
  Func<void(Packet, Tunnel&, Cart)> onPacketCallback;
  Func<void(Packet, Tunnel&, Cart)> onUpgradeCallback; // Was onSwitchCallback
  Func<void(Map<u64, String>, Tunnel&, Cart)> onDisconnectCallback;

  Server() {
    keypair = Sec::generateKeyPair();
  }

  ~Server() {
    destroy();
  }

  // -----------------------------------------------------------------------
  // Hook — connect this server to a station
  // -----------------------------------------------------------------------
  void hook(Station& ourStation) {
    station = &ourStation;
    station->onCart([this](Cart& c) {
      handleCart(c);
    });
  }

  void hook(Station& ourStation, u32 rail) {
    station = &ourStation;
    station->addRailListener(rail, [this](Cart& c) {
      handleCart(c);
    });
  }

  void hook(Station& ourStation, const Resource::NumericalAddress& addr) {
    station = &ourStation;
    station->onCart([this](Cart& c) {
      handleCart(c);
    });
  }

  void hook(Station& ourStation, const String& target) {
    station = &ourStation;
    station->onCart([this](Cart& c) {
      handleCart(c);
    });
  }

  // -----------------------------------------------------------------------
  // Callbacks
  // -----------------------------------------------------------------------
  void onProbe(Func<void(Cart&)> cb) {
    onProbeCallback = Move(cb);
  }

  void onPacket(Func<void(Packet, Tunnel&, Cart)> cb) {
    onPacketCallback = Move(cb);
  }

  void onUpgrade(Func<void(Packet, Tunnel&, Cart)> cb) {
    onUpgradeCallback = Move(cb);
  }

  void onDisconnect(Func<void(Map<u64, String>, Tunnel&, Cart)> cb) {
    onDisconnectCallback = Move(cb);
  }

  // -----------------------------------------------------------------------
  // announce — reply to a probe with our public key
  // -----------------------------------------------------------------------
  void announce(Cart& c) {
    if (!station) return;
    Cart reply;
    reply.isSecure = false;
    reply.rail = c.rail;
    reply.hasRailNotZero = (c.rail != 0);
    reply.hasMeta = true;

    String cmdVal; cmdVal.push(1); // Announce
    reply.meta.put(Meta::Command, cmdVal);
    reply.meta.put(Meta::PublicKey, keypair.publicKey);
    reply.meta.put(Meta::PublicHash, Sec::hash(keypair.publicKey, 8));
    reply.meta.put(Meta::Name, String("Server"));

    // Copy source/target addressing
    if (c.isAddressed) {
      reply.target = c.source;
      reply.isAddressed = true;
    }
    if (c.meta.has(Meta::Source)) {
      reply.meta.put(Meta::Target, *c.meta.get(Meta::Source));
    }
    // Copy SocketPath if any
    if (c.meta.has(Meta::SocketPath)) {
      reply.meta.put(Meta::SocketPath, *c.meta.get(Meta::SocketPath));
    }
    station->push(reply);
  }

  void disconnect(const Map<u64, String>& meta, Tunnel& client) {
    client.disconnect(meta);
    for (usz i = 0; i < clients.size(); i++) {
      if (clients[i] == &client) {
        clients.splice(i, 1);
        break;
      }
    }
    client.destroy();
    delete &client;
  }

  void destroy() {
    for (usz i = 0; i < clients.size(); i++) {
      clients[i]->destroy();
      delete clients[i];
    }
    clients.clear();
    station = nullptr;
  }

  void update() {
    if (station) {
      station->update();
    }
    for (usz i = 0; i < clients.size(); i++) {
      clients[i]->update();
    }
  }

  void handleCart(Cart& c) {
    if (c.isSecure) {
      // Try to route to existing tunnel by address or rail
      for (usz i = 0; i < clients.size(); i++) {
        if (c.isAddressed && clients[i]->hasHookedTarget && clients[i]->hookedTarget == c.source) {
          clients[i]->receive(c.payload);
          return;
        }
        if (c.meta.has(Meta::Source) && clients[i]->hasHookedTargetStr && clients[i]->hookedTargetStr == *c.meta.get(Meta::Source)) {
          clients[i]->receive(c.payload);
          return;
        }
      }
    }

    const String* cmdPtr = c.meta.get(Meta::Command);
    int cmd = cmdPtr && !cmdPtr->isEmpty() ? (*cmdPtr)[0] : -1;

    if (cmd == 0) { // Probe
      if (onProbeCallback.isValid()) {
        onProbeCallback(c);
      } else {
        announce(c);
      }
    } else if (cmd == 2) { // Upgrade (was Switch)
      _handleUpgrade(c);
    } else {
      // Fallback for regular unencrypted carts
      for (usz i = 0; i < clients.size(); i++) {
        if (c.isAddressed && clients[i]->hasHookedTarget && clients[i]->hookedTarget == c.source) {
          clients[i]->receive(c);
          return;
        }
        if (c.meta.has(Meta::Source) && clients[i]->hasHookedTargetStr && clients[i]->hookedTargetStr == *c.meta.get(Meta::Source)) {
          clients[i]->receive(c);
          return;
        }
      }
    }
  }

private:
  void _handleUpgrade(Cart& c) {
    const String* clientEphemeralPtr = c.meta.get(Meta::PublicKey);
    if (!clientEphemeralPtr || clientEphemeralPtr->isEmpty()) {
      if (!acceptsInsecure) return;
    }

    // -----------------------------------------------------------------
    // 0-RTT Resume: Check if we already have a tunnel with this
    // ephemeral public key. If so, silently re-hook and resume.
    // The client has unhook()'d and re-hook()'d — it sends an Upgrade
    // cart with the same ephemeral key and a real tunnel bundle inside.
    // We just re-hook the existing tunnel and process the bundle.
    // No onUpgradeCallback — this is a silent, seamless resume.
    // -----------------------------------------------------------------
    if (clientEphemeralPtr && !clientEphemeralPtr->isEmpty()) {
      for (usz i = 0; i < clients.size(); i++) {
        if (clients[i]->theirEphemeralPublic == *clientEphemeralPtr && clients[i]->isSecure) {
          Tunnel* resuming = clients[i];

          // Re-hook the tunnel to the new rail/source
          resuming->unhookAll();
          if (c.isAddressed) {
            resuming->hook(*station, c.rail, c.source);
          } else if (c.meta.has(Meta::Source)) {
            resuming->hook(*station, c.rail, *c.meta.get(Meta::Source));
          } else {
            resuming->hook(*station, c.rail);
          }

          // Process the bundle — continues the existing encrypted stream
          resuming->receive(c.payload);

          // Do NOT call onUpgradeCallback — silent resume
          return;
        }
      }
    }

    // -----------------------------------------------------------------
    // New connection: full key exchange and upgrade
    // -----------------------------------------------------------------
    Tunnel* clientTunnel = nullptr;

    // Check if we already have a tunnel matched by rail or source
    for (usz i = 0; i < clients.size(); i++) {
      if (c.rail != 0 && clients[i]->hookedRail == c.rail) {
        clientTunnel = clients[i];
        break;
      }
      if (c.isAddressed && clients[i]->hasHookedTarget && clients[i]->hookedTarget == c.source) {
        clientTunnel = clients[i];
        break;
      }
      if (c.meta.has(Meta::Source) && clients[i]->hasHookedTargetStr && clients[i]->hookedTargetStr == *c.meta.get(Meta::Source)) {
        clientTunnel = clients[i];
        break;
      }
    }

    bool isNew = false;
    if (!clientTunnel) {
      clientTunnel = new Tunnel();
      clientTunnel->ephemeralKeypair = keypair;
      clients.push(clientTunnel);
      isNew = true;
    }

    if (clientEphemeralPtr && !clientEphemeralPtr->isEmpty()) {
      clientTunnel->enableSecureX(*clientEphemeralPtr, keypair);
    } else if (acceptsInsecure) {
      clientTunnel->enableWindowing();
    } else {
      if (isNew) {
        clients.pop();
        delete clientTunnel;
      }
      return;
    }

    Packet firstPacket;
    bool gotPacket = false;
    clientTunnel->onPacket([&](Packet p) {
      firstPacket = p;
      gotPacket = true;
    });

    if (clientTunnel->receive(c.payload)) {
      // Hook the tunnel — copy rail, copy source/target info
      if (c.isAddressed) {
        clientTunnel->hook(*station, c.rail, c.source);
      } else if (c.meta.has(Meta::Source)) {
        clientTunnel->hook(*station, c.rail, *c.meta.get(Meta::Source));
      } else {
        clientTunnel->hook(*station, c.rail);
      }

      if (onUpgradeCallback.isValid()) {
        onUpgradeCallback(firstPacket, *clientTunnel, c);
      } else if (onPacketCallback.isValid() && gotPacket) {
        onPacketCallback(firstPacket, *clientTunnel, c);
      }

      clientTunnel->onPacket([this, clientTunnel](Packet p) {
        if (onPacketCallback.isValid()) {
          onPacketCallback(p, *clientTunnel, Cart());
        }
      });

      clientTunnel->onDisconnect([this, clientTunnel](Map<u64, String> reason) {
        if (onDisconnectCallback.isValid()) {
          onDisconnectCallback(reason, *clientTunnel, Cart());
        }
        for (usz i = 0; i < clients.size(); i++) {
          if (clients[i] == clientTunnel) {
            clients.splice(i, 1);
            break;
          }
        }
        clientTunnel->destroy();
        delete clientTunnel;
      });
    } else {
      if (isNew) {
        clients.pop();
        delete clientTunnel;
      }
    }
  }
};

} // namespace Rho

#endif // UTIL_SERVER_HPP
