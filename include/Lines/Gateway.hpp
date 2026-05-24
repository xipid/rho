#ifndef LINES_GATEWAY_HPP
#define LINES_GATEWAY_HPP

#include <Lines/Router.hpp>
#include <Lines/Reach.hpp>
#include <Util/Client.hpp>
#include <Util/Server.hpp>
#include <Collection/Map.hpp>
#include <Collection/String.hpp>

namespace Lines {

using namespace Rho;
using namespace Xi;

// ---------------------------------------------------------------------------
// Gateway
//
// Auto-Router. Can act as both initiator (client) and receiver (host)
// simultaneously. N:N relationships. No host/client distinction in the code —
// it's just a matter of who initiates.
//
// A Gateway can be hooked to stations where it listens for probes/announces.
// It uses a Router internally for all routing decisions.
// ---------------------------------------------------------------------------
class Gateway {
public:
  Resource::NumericalAddress address;
  Sec::KeyPair keypair;
  Router router;
  Server server;
  Array<Client*> clients;

  // Hooked stations (where this gateway operates)
  Array<Station*> hookedStations;

  // Unauthed forwarding (for constrained IoT devices)
  bool unauthed = false;
  String unauthedMarker;
  Resource::NumericalAddress unauthedAddress;
  u32 unauthedTimeout = 30000; // ms
  Array<TreeRoutingEntry*> unauthedSessions;

  // Reach
  struct ReachServer {
    Resource::NumericalAddress address;
    String publicKey;
  };
  Array<ReachServer> defaultReachServers;
  Array<Resource::NumericalAddress> susReachServers;
  Map<String, Resource::NumericalAddress> reachCache;

  // Callbacks
  Func<RoutingEntry*(Packet&, Tunnel&, Cart&)> onUpgradeCallback;
  Func<void(Packet&, Tunnel&, Cart&)> onReadyCallback;
  Func<void(Cart&)> onAnnounceCallback;
  Func<void(Cart&)> onProbeCallback;
  Func<void(const String&, const Resource::NumericalAddress&)> onReachCompletedCallback;

  // Bound port stations (owned by this gateway)
  struct BoundPort {
    u32 port;
    Station* station;
  };
  Array<BoundPort> boundPorts;

  // Connected tunnel sessions
  struct TunnelSession {
    Tunnel* tunnel = nullptr;
    Station* sessionStation = nullptr;
    Resource::NumericalAddress assignedAddress;
  };
  Array<TunnelSession> sessions;

  // Active reach operations
  struct ActiveReach {
    String address;
    Reach* reach;
  };
  Array<ActiveReach> activeReaches;

  Gateway() {
    keypair = Sec::generateKeyPair();
  }

  ~Gateway() {
    destroy();
  }

  // -----------------------------------------------------------------------
  // hook — Start listening on a station (act as host here).
  // Multiple stations can be hooked.
  // -----------------------------------------------------------------------
  void hook(Station& station) {
    hookedStations.push(&station);

    server.station = &station;

    // Listen for incoming carts for routing
    station.onCart([this, &station](Cart& c) {
      _handleIncomingCart(c, &station);
      server.handleCart(c); // Pass to server for Probes/Upgrades
    });

    // Set up server callbacks
    server.onProbe([this](Cart& c) {
      if (onProbeCallback.isValid()) {
        onProbeCallback(c);
      } else {
        server.announce(c);
      }
    });

    server.onUpgrade([this](Packet pkt, Tunnel& tunnel, Cart cart) {
      _handleUpgrade(pkt, tunnel, cart);
    });
  }

  // -----------------------------------------------------------------------
  // bind — Create a routing entry under our address, return the Station.
  // -----------------------------------------------------------------------
  Station* bind(u32 port) {
    Resource::NumericalAddress portAddr;
    for (usz i = 0; i < address.size(); ++i) portAddr.push(address[i]);
    portAddr.push(port);

    // Check if already bound
    for (usz i = 0; i < boundPorts.size(); ++i) {
      if (boundPorts[i].port == port) return boundPorts[i].station;
    }

    Station* st = new Station();
    st->name = String("Port-") + String((long long)port);
    router.hook(st, portAddr);

    BoundPort bp;
    bp.port = port;
    bp.station = st;
    boundPorts.push(bp);
    return st;
  }

  Station* bind() {
    // Find a random unused port
    Resource::NumericalAddress genAddr = router.generate(address);
    u32 port = (u32)genAddr[genAddr.size() - 1];
    return bind(port);
  }

  // -----------------------------------------------------------------------
  // upgrade — Trigger upgrade on a specific tunnel/cart (initiator side).
  // -----------------------------------------------------------------------
  void upgrade(Packet& pkt, Tunnel& tunnel, Cart& cart) {
    // The client side: don't add routing entry until server sends a packet
    // and onReady completes.
  }

  // -----------------------------------------------------------------------
  // announce / probe — For explicit control
  // -----------------------------------------------------------------------
  void announce(Cart& cart) {
    server.announce(cart);
  }

  void probe(Cart& cart) {
    // Send a probe through all hooked stations
    for (usz i = 0; i < hookedStations.size(); ++i) {
      hookedStations[i]->push(cart);
    }
  }

  // -----------------------------------------------------------------------
  // connectUnauthed — For constrained IoT devices (client side)
  // -----------------------------------------------------------------------
  void connectUnauthed(const String& marker) {
    if (hookedStations.size() == 0) return;

    Cart c;
    c.isSecure = false;
    c.hasMeta = true;
    c.meta.put(Meta::GatewayMarker, marker);
    hookedStations[0]->push(c);
  }

  // -----------------------------------------------------------------------
  // reach — Non-blocking address resolution.
  // Returns a NumericalAddress if cached, empty otherwise (triggers new Reach).
  // -----------------------------------------------------------------------
  Resource::NumericalAddress reach(const String& addr) {
    auto* cached = reachCache.get(addr);
    if (cached && cached->size() > 0) {
      return *cached;
    }

    // Check if we already have an active reach for this address
    for (usz i = 0; i < activeReaches.size(); ++i) {
      if (activeReaches[i].address == addr) {
        return Resource::NumericalAddress(); // Already resolving
      }
    }

    // Start a new Reach
    Reach* rch = new Reach();
    for (usz i = 0; i < defaultReachServers.size(); ++i) {
      // Skip sus servers
      bool isSus = false;
      for (usz j = 0; j < susReachServers.size(); ++j) {
        if (susReachServers[j] == defaultReachServers[i].address) {
          isSus = true;
          break;
        }
      }
      if (!isSus) {
        rch->addDefault(defaultReachServers[i].address, defaultReachServers[i].publicKey);
      }
    }
    if (hookedStations.size() > 0) {
      rch->start(*hookedStations[0], addr);
    }
    ActiveReach ar;
    ar.address = addr;
    ar.reach = rch;
    activeReaches.push(ar);
    return Resource::NumericalAddress();
  }

  // -----------------------------------------------------------------------
  // Callback setters
  // -----------------------------------------------------------------------
  void onUpgrade(Func<RoutingEntry*(Packet&, Tunnel&, Cart&)> cb) {
    onUpgradeCallback = Move(cb);
  }

  void onReady(Func<void(Packet&, Tunnel&, Cart&)> cb) {
    onReadyCallback = Move(cb);
  }

  void onAnnounce(Func<void(Cart&)> cb) {
    onAnnounceCallback = Move(cb);
  }

  void onProbe(Func<void(Cart&)> cb) {
    onProbeCallback = Move(cb);
  }

  void onReachCompleted(Func<void(const String&, const Resource::NumericalAddress&)> cb) {
    onReachCompletedCallback = Move(cb);
  }

  // -----------------------------------------------------------------------
  // update — Tick all components
  // -----------------------------------------------------------------------
  void update() {
    server.update();
    for (usz i = 0; i < clients.size(); ++i) {
      clients[i]->update();
    }

    // Tick active reaches
    for (usz i = 0; i < activeReaches.size(); ++i) {
      activeReaches[i].reach->update();
      if (activeReaches[i].reach->done) {
        if (activeReaches[i].reach->success) {
          // Cache the result
          reachCache.put(activeReaches[i].address, activeReaches[i].reach->finalAddress);

          // Collect sus servers
          for (usz j = 0; j < activeReaches[i].reach->susDefaults.size(); ++j) {
            susReachServers.push(activeReaches[i].reach->susDefaults[j]);
          }

          // Fire callback
          if (onReachCompletedCallback.isValid()) {
            onReachCompletedCallback(activeReaches[i].address, activeReaches[i].reach->finalAddress);
          }
        }
        delete activeReaches[i].reach;
        activeReaches.splice(i, 1);
        --i;
      }
    }

    // Clean up dead tunnel sessions
    u64 now = Xi::millis();
    for (usz i = 0; i < sessions.size(); ++i) {
      if (sessions[i].tunnel && sessions[i].tunnel->isDestroyed) {
        _cleanupSession(i);
        --i;
      }
    }

    // Unauthed timeout cleanup
    if (unauthed) {
      for (usz i = 0; i < sessions.size(); ++i) {
        if (sessions[i].sessionStation) {
          u64 lastActive = sessions[i].sessionStation->lastRecvUS > sessions[i].sessionStation->lastSentUS
                           ? sessions[i].sessionStation->lastRecvUS
                           : sessions[i].sessionStation->lastSentUS;
          if (lastActive > 0 && (now - lastActive / 1000) > unauthedTimeout) {
            _cleanupSession(i);
            --i;
          }
        }
      }
    }
  }

  void destroy() {
    for (usz i = 0; i < activeReaches.size(); ++i) {
      activeReaches[i].reach->destroy();
      delete activeReaches[i].reach;
    }
    activeReaches.clear();

    for (usz i = 0; i < sessions.size(); ++i) {
      if (sessions[i].sessionStation) {
        router.unhook(sessions[i].sessionStation);
        delete sessions[i].sessionStation;
      }
    }
    sessions.clear();

    for (usz i = 0; i < boundPorts.size(); ++i) {
      router.unhook(boundPorts[i].station);
      delete boundPorts[i].station;
    }
    boundPorts.clear();

    for (usz i = 0; i < clients.size(); ++i) {
      clients[i]->destroy();
      delete clients[i];
    }
    clients.clear();

    server.destroy();
    router.destroy();
  }

private:
  void _handleIncomingCart(Cart& c, Station* originStation) {
    // Unauthed forwarding check
    if (unauthed && !c.isSecure && c.hasMeta) {
      const String* cartMarker = c.meta.get(Meta::GatewayMarker);
      if (cartMarker && !cartMarker->isEmpty() && *cartMarker == unauthedMarker) {
        _handleUnauthedCart(c);
        return;
      }
    }

    // If addressed, try routing
    if (c.isAddressed && c.target.size() > 0) {
      router.route(c);
    }
  }

  void _handleUnauthedCart(Cart& c) {
    // Create a temporary station for this unauthed session
    Station* st = new Station();
    st->name = "Unauthed";

    // Hook the station to push back through our hooked stations
    if (hookedStations.size() > 0) {
      st->onCartPushed([this](Cart& c2) {
        if (hookedStations.size() > 0) {
          hookedStations[0]->push(c2);
        }
      });
    }

    Resource::NumericalAddress addr;
    if (unauthedAddress.size() > 0) {
      addr = router.generate(unauthedAddress);
    } else {
      addr = router.generate(address);
    }
    router.hook(st, addr);

    TunnelSession sess;
    sess.tunnel = nullptr;
    sess.sessionStation = st;
    sess.assignedAddress = addr;
    sessions.push(sess);

    // Forward the cart
    st->receive(c);
  }

  void _handleUpgrade(Packet& pkt, Tunnel& tunnel, Cart& cart) {
    RoutingEntry* result = nullptr;

    if (onUpgradeCallback.isValid()) {
      result = onUpgradeCallback(pkt, tunnel, cart);
    }

    if (!result) {
      // Default: auto-accept, generate an address under ours
      Station* sessStation = new Station();
      String derivedKey = Sec::kdf(tunnel.key, "GatewayStation", 32);
      sessStation->enableSecurity(derivedKey);
      sessStation->name = "GW-Session";

      // Hook session station to push through our hooked station
      if (hookedStations.size() > 0) {
        sessStation->hook(*hookedStations[0], cart.rail);
      }

      Resource::NumericalAddress childAddr = router.generate(address);
      router.hook(sessStation, childAddr);

      TunnelSession sess;
      sess.tunnel = &tunnel;
      sess.sessionStation = sessStation;
      sess.assignedAddress = childAddr;
      sessions.push(sess);

      // Send our address and child address to the client
      Packet addrPkt;
      addrPkt.channel = 1;
      Map<u64, String> addrMeta;

      // Serialize our address
      String addrStr;
      for (usz i = 0; i < address.size(); ++i) {
        if (i > 0) addrStr += ".";
        addrStr += String((long long)address[i]);
      }
      addrMeta.put(Meta::Address, addrStr);

      // Serialize child address
      String childAddrStr;
      for (usz i = 0; i < childAddr.size(); ++i) {
        if (i > 0) childAddrStr += ".";
        childAddrStr += String((long long)childAddr[i]);
      }
      addrMeta.put(Meta::NumericalAddress, childAddrStr);

      addrPkt.payload = addrMeta.serialize();
      tunnel.push(addrPkt);

      // Set up disconnect cleanup
      tunnel.onDisconnect([this, sessStation](Map<u64, String> reason) {
        for (usz i = 0; i < sessions.size(); ++i) {
          if (sessions[i].sessionStation == sessStation) {
            _cleanupSession(i);
            break;
          }
        }
      });

    } else {
      // User-provided routing entry
      router.hook(result->station, result->address);

      TunnelSession sess;
      sess.tunnel = &tunnel;
      sess.sessionStation = result->station;
      sess.assignedAddress = result->address;
      sessions.push(sess);

      tunnel.onDisconnect([this, station = result->station](Map<u64, String> reason) {
        for (usz i = 0; i < sessions.size(); ++i) {
          if (sessions[i].sessionStation == station) {
            _cleanupSession(i);
            break;
          }
        }
      });
    }

    if (onReadyCallback.isValid()) {
      onReadyCallback(pkt, tunnel, cart);
    }
  }

  void _cleanupSession(usz idx) {
    if (idx >= sessions.size()) return;
    auto& sess = sessions[idx];
    if (sess.sessionStation) {
      router.unhook(sess.sessionStation);
      if (sess.assignedAddress.size() > 0) {
        router.unhookAll(sess.assignedAddress);
      }
      delete sess.sessionStation;
    }
    sessions.splice(idx, 1);
  }
};

} // namespace Lines

#endif // LINES_GATEWAY_HPP
