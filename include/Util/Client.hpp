#ifndef UTIL_CLIENT_HPP
#define UTIL_CLIENT_HPP

#include <Rho/Tunnel.hpp>
#include <Rho/Meta.hpp>
#include <Rho/Railway.hpp>
#include <Sec/Crypto.hpp>
#include <Xi/Func.hpp>
#include <Collection/Array.hpp>
#include <Collection/Map.hpp>
#include <Collection/String.hpp>

namespace Rho {

class Client {
public:
  Station* station = nullptr;

  Sec::KeyPair keypair;
  Tunnel* tunnel = nullptr;
  u32 rail = 0;
  String password;

  bool hasServerAddr = false;
  Resource::NumericalAddress serverAddr;
  String serverAddrStr;

  String serverEphemeralKey;
  bool readyTriggered = false;
  bool _needsUpgradeMeta = false;
  bool _isResuming = false; // True when re-hooking an already-upgraded tunnel

  Func<void(Cart&)> onAnnounceCallback;
  Func<void(Packet, Cart)> onReadyCallback;
  Func<void(Packet)> onPacketCallback;

  Client() {
    keypair = Sec::generateKeyPair();
  }

  ~Client() {
    destroy();
  }

  // -----------------------------------------------------------------------
  // Hook — connect this client to a station.
  // If the tunnel already exists and is upgraded, this is a 0-RTT resume:
  // the tunnel keeps its key/nonces/state, and the next pushCart() sends
  // an Upgrade cart with the same ephemeral public key so the server
  // can match us to our existing session.
  // -----------------------------------------------------------------------
  void hook(Station& ourStation, u32 r) {
    station = &ourStation;
    rail = r;
    _rehookIfResuming();
    _setupStationListener();
  }

  void hook(Station& ourStation, const Resource::NumericalAddress& addr) {
    station = &ourStation;
    serverAddr = addr;
    hasServerAddr = true;
    _rehookIfResuming();
    _setupStationListener();
  }

  void hook(Station& ourStation, const String& target) {
    station = &ourStation;
    serverAddrStr = target;
    hasServerAddr = false;
    _rehookIfResuming();
    _setupStationListener();
  }

  void hook(Station& ourStation, const char* target) {
    Resource::NumericalAddress addr(target);
    if (addr.size() > 0) {
      hook(ourStation, addr);
    } else {
      hook(ourStation, String(target));
    }
  }

  // -----------------------------------------------------------------------
  // unhook — Detach from the current station without destroying the Tunnel.
  // The Tunnel keeps its key, nonces, keypair — everything alive.
  // A subsequent hook() will trigger 0-RTT resume.
  // -----------------------------------------------------------------------
  void unhook() {
    if (tunnel) {
      tunnel->unhookAll();
    }
    if (station) {
      if (rail != 0) {
        station->removeRailListener(rail);
      } else {
        station->cartListener = Func<void(Cart&)>();
      }
      station = nullptr;
    }
    // Tunnel stays alive — key, nonces, everything preserved.
    // Mark that the next hook() should prepare resume metadata.
    if (tunnel && tunnel->isSecure) {
      _isResuming = true;
    }
  }

  // -----------------------------------------------------------------------
  // Callbacks
  // -----------------------------------------------------------------------
  void onAnnounce(Func<void(Cart&)> cb) {
    onAnnounceCallback = Move(cb);
  }

  void onReady(Func<void(Packet, Cart)> cb) {
    onReadyCallback = Move(cb);
  }

  void onPacket(Func<void(Packet)> cb) {
    onPacketCallback = Move(cb);
  }

  // -----------------------------------------------------------------------
  // probe — send a probe cart to the server
  // -----------------------------------------------------------------------
  void probe() {
    if (!station) return;
    Cart probeCart;
    probeCart.isSecure = false;
    probeCart.rail = rail;
    probeCart.hasRailNotZero = (rail != 0);
    probeCart.hasMeta = true;

    String cmdVal; cmdVal.push(0); // Probe
    probeCart.meta.put(Meta::Command, cmdVal);

    if (hasServerAddr) {
      probeCart.target = serverAddr;
      probeCart.isAddressed = true;
    } else if (!serverAddrStr.isEmpty()) {
      probeCart.meta.put(Meta::Target, serverAddrStr);
    }

    station->push(probeCart);
  }

  // -----------------------------------------------------------------------
  // upgrade — performs the key exchange (was switchConnection/switch_).
  // No args — waits for the next flush triggered by a pushed packet.
  // -----------------------------------------------------------------------
  void upgrade() {
    if (!station) return;
    if (serverEphemeralKey.isEmpty()) return;

    _ensureTunnel();

    tunnel->enableSecureX(serverEphemeralKey, keypair);

    readyTriggered = false;
    _needsUpgradeMeta = true;

    // Send a dummy packet to force metadata flush immediately
    push(Packet(String(), 1));

    tunnel->onPacket([this](Packet p) {
      if (!readyTriggered) {
        readyTriggered = true;
        if (onReadyCallback.isValid()) {
          onReadyCallback(p, Cart());
        }
      }
      if (onPacketCallback.isValid()) {
        onPacketCallback(p);
      }
    });
  }

  // -----------------------------------------------------------------------
  // push — queue a packet to be sent through the tunnel
  // -----------------------------------------------------------------------
  void push(Packet pkt) {
    if (!tunnel) {
      // Create tunnel on first push if needed
      _ensureTunnel();
    }
    if (tunnel) {
      tunnel->push(pkt);
    }
  }

  void push(const String& s, u64 channel = 1) {
    push(Packet(s, channel));
  }

  // -----------------------------------------------------------------------
  // pushCart — immediately push a cart through the station
  // -----------------------------------------------------------------------
  void pushCart() {
    if (!station || !tunnel) return;
    String out = tunnel->flush();
    if (out.length() > 0) {
      Cart c;
      c.rail = rail;
      c.payload = out;
      c.isSecure = false;
      if (_needsUpgradeMeta) {
        String cmdVal; cmdVal.push(2); // Upgrade command
        c.meta.put(Meta::Command, cmdVal);
        c.meta.put(Meta::PublicKey, keypair.publicKey);
        if (!password.isEmpty()) {
          c.meta.put(Meta::Password, password);
        }
        c.hasMeta = true;
        _needsUpgradeMeta = false;
      }
      _setCartTarget(c);
      station->push(c);
    }
  }

  void pushCart(Cart& cart) {
    if (!station || !tunnel) return;
    String out = tunnel->flush();
    if (out.length() > 0) {
      cart.payload = out; // Overwrite payload with the bundle
      cart.rail = rail;
      cart.isSecure = false;
      if (_needsUpgradeMeta) {
        String cmdVal; cmdVal.push(2); // Upgrade command
        cart.meta.put(Meta::Command, cmdVal);
        cart.meta.put(Meta::PublicKey, keypair.publicKey);
        if (!password.isEmpty()) {
          cart.meta.put(Meta::Password, password);
        }
        cart.hasMeta = true;
        _needsUpgradeMeta = false;
      }
      station->push(cart);
    }
  }

  // -----------------------------------------------------------------------
  // Lifecycle
  // -----------------------------------------------------------------------
  void destroy() {
    if (tunnel) {
      tunnel->destroy();
      delete tunnel;
      tunnel = nullptr;
    }
    station = nullptr;
    _isResuming = false;
  }

  void update() {
    if (station) {
      station->update();
    }
    if (_needsUpgradeMeta && tunnel && tunnel->readyToSend()) {
      pushCart();
    }
    if (tunnel) {
      tunnel->update();
    }
  }

  void handleAnnounce(Cart& c) {
    if (c.isSecure) return;
    const String* cmdPtr = c.meta.get(Meta::Command);
    int cmd = cmdPtr && !cmdPtr->isEmpty() ? (*cmdPtr)[0] : -1;

    if (cmd == 1) { // Announce
      const String* srvPubKey = c.meta.get(Meta::PublicKey);
      if (srvPubKey) {
        serverEphemeralKey = *srvPubKey;
      }
      if (onAnnounceCallback.isValid()) {
        onAnnounceCallback(c);
      }
    }
  }

private:
  void _ensureTunnel() {
    if (tunnel) return;
    tunnel = new Tunnel();
    tunnel->ephemeralKeypair = keypair;

    if (rail == 0 && station) {
      rail = station->generateRail();
      if (rail == 0) {
        rail = 12345 + (Xi::millis() % 50000);
      }
    }

    if (station) {
      if (hasServerAddr) {
        tunnel->hook(*station, rail, serverAddr);
      } else if (!serverAddrStr.isEmpty()) {
        tunnel->hook(*station, rail, serverAddrStr);
      } else {
        tunnel->hook(*station, rail);
      }
    }

    tunnel->onPacket([this](Packet p) {
      if (onPacketCallback.isValid()) {
        onPacketCallback(p);
      }
    });
  }

  /// When re-hooking after unhook(), re-attach the existing tunnel to the new
  /// station and prepare the upgrade meta so the server can identify us.
  void _rehookIfResuming() {
    if (!_isResuming || !tunnel) return;

    if (rail == 0 && station) {
      rail = station->generateRail();
      if (rail == 0) {
        rail = 12345 + (Xi::millis() % 50000);
      }
    }

    // Re-hook the tunnel to the new station — same key, nonces, everything
    if (station) {
      if (hasServerAddr) {
        tunnel->hook(*station, rail, serverAddr);
      } else if (!serverAddrStr.isEmpty()) {
        tunnel->hook(*station, rail, serverAddrStr);
      } else {
        tunnel->hook(*station, rail);
      }
    }

    // Prepare upgrade meta: next pushCart() will send Meta::Command=2 + same PublicKey
    // so the server can match us to the existing session. The tunnel bundle
    // inside the cart continues the existing encrypted stream — 0-RTT.
    _needsUpgradeMeta = true;
    readyTriggered = true; // Don't re-fire onReady for a resume
    _isResuming = false;
  }

  void _setCartTarget(Cart& c) {
    if (hasServerAddr) {
      c.target = serverAddr;
      c.isAddressed = true;
    } else if (!serverAddrStr.isEmpty()) {
      c.meta.put(Meta::Target, serverAddrStr);
      c.hasMeta = true;
    }
  }

  void _setupStationListener() {
    if (!station) return;
    if (rail != 0) {
      station->addRailListener(rail, [this](Cart& c) {
        handleAnnounce(c);
      });
    } else {
      station->onCart([this](Cart& c) {
        handleAnnounce(c);
      });
    }
  }
};

} // namespace Rho

#endif // UTIL_CLIENT_HPP
