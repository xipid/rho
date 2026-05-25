#ifndef RHO_RAILWAY_HPP
#define RHO_RAILWAY_HPP

#include <Collection/Array.hpp>
#include <Sec/Crypto.hpp>
#include <Xi/Func.hpp>
#include <Collection/Map.hpp>
#include <Collection/String.hpp>
#include <Resource/Path.hpp>
#include <cstdio>

using namespace Xi;
using namespace Sec;

namespace Rho {
class Station;

struct Cart {
  bool isSecure = true;
  bool isStructureFixed = false;
  bool hasMeta = false;
  bool hasRailNotZero = false;
  bool isAddressed = false;

  u32 rail = 0;
  u64 nonce = 0;

  Resource::NumericalAddress source;
  Resource::NumericalAddress target;

  Map<u64, String> meta;
  String payload;
  String mac;

  Cart() {}

  Cart(String raw) {
    if (raw.size() == 0) return;
    usz cursor = 0;
    u8 header = raw[cursor++];
    
    isSecure = (header & 1) != 0;
    isStructureFixed = (header & 2) != 0;
    hasMeta = (header & 4) != 0;
    hasRailNotZero = (header & 8) != 0;
    isAddressed = (header & 16) != 0;

    if (hasRailNotZero) {
      if (isStructureFixed) {
        if (cursor + 4 <= raw.size()) {
          rail = raw.peekI32(cursor);
          cursor += 4;
        }
      } else {
        auto res = raw.peekVarLong(cursor);
        if (!res.error) {
          rail = (u32)res.value;
          cursor += res.bytes;
        }
      }
    } else {
      rail = 0;
    }

    if (isSecure) {
      if (isStructureFixed) {
        if (cursor + 8 <= raw.size()) {
          nonce = (u64)raw.peekI64(cursor);
          cursor += 8;
        }
      } else {
        auto res = raw.peekVarLong(cursor);
        if (!res.error) {
          nonce = (u64)res.value;
          cursor += res.bytes;
        }
      }
      
      if (cursor + 8 <= raw.size()) {
        mac = raw.substring(cursor, cursor + 8);
        cursor += 8;
      }
    }

    if (cursor < raw.size()) {
      payload = raw.substring(cursor, raw.size());
    }

    if (!isSecure) {
      decodeInner(payload);
    }
  }

  void decodeInner(const String& inner) {
    usz cursor = 0;
    if (isAddressed) {
      if (isStructureFixed) {
        auto srcRes = Array<u64>::deserialize(inner, cursor);
        source = Resource::NumericalAddress();
        for (usz i = 0; i < srcRes.size(); i++) source.push(srcRes[i]);
        
        auto tgtRes = Array<u64>::deserialize(inner, cursor);
        target = Resource::NumericalAddress();
        for (usz i = 0; i < tgtRes.size(); i++) target.push(tgtRes[i]);
      } else {
        auto srcRes = Array<u64>::deserialize(inner, cursor);
        source = Resource::NumericalAddress();
        for (usz i = 0; i < srcRes.size(); i++) source.push(srcRes[i]);
        
        auto tgtRes = Array<u64>::deserialize(inner, cursor);
        target = Resource::NumericalAddress();
        for (usz i = 0; i < tgtRes.size(); i++) target.push(tgtRes[i]);
      }
    }

    // Now read payload length
    usz pLen = 0;
    if (isStructureFixed) {
      if (cursor + 4 <= inner.size()) {
        pLen = inner.peekI32(cursor);
        cursor += 4;
      }
    } else {
      auto res = inner.peekVarLong(cursor);
      if (!res.error) {
        pLen = (usz)res.value;
        cursor += res.bytes;
      }
    }

    String extractedPayload;
    if (cursor + pLen <= inner.size()) {
      extractedPayload = inner.substring(cursor, cursor + pLen);
      cursor += pLen;
    }
    
    if (hasMeta && cursor < inner.size()) {
      meta = Map<u64, String>::deserialize(inner, cursor);
    }

    // Replace payload with the actual extracted payload
    payload = extractedPayload;
  }

  String encodeInner() const {
    String inner;
    if (isAddressed) {
      inner += source.serialize(); // Array<u64> serialize
      inner += target.serialize();
    }
    
    if (isStructureFixed) {
      inner.pushI32(payload.size());
    } else {
      inner.pushVarLong((long long)payload.size());
    }
    inner += payload;

    if (hasMeta) {
      inner += meta.serialize();
    }
    return inner;
  }

  String toString() const {
    String raw;
    u8 header = 0;
    if (isSecure) header |= 1;
    if (isStructureFixed) header |= 2;
    if (hasMeta) header |= 4;
    if (rail != 0 || hasRailNotZero) header |= 8;
    if (isAddressed) header |= 16;
    raw.push(header);

    if (rail != 0 || hasRailNotZero) {
      if (isStructureFixed) raw.pushI32(rail);
      else raw.pushVarLong(rail);
    }

    if (isSecure) {
      if (isStructureFixed) raw.pushI64(nonce);
      else raw.pushVarLong((long long)nonce);
      raw += mac;
    }

    if (isSecure) {
      // payload contains the cipherText
      raw += payload;
    } else {
      raw += encodeInner();
    }

    return raw;
  }
};

class Station {
public:
  virtual ~Station() = default;
  String name = "Station";
  bool isSecure = false;
  String key;
  u64 lastSentNonce = 0;
  u64 lastSentUS = 0;
  u64 lastRecvUS = 0;
  u64 window = 0;
  usz bitmapLength = 64;

  Array<u32> unusedRailTracker;

  Func<void(Cart&)> cartListener;
  Func<void(Cart&)> cartPushListener;
  
  Map<u32, Array<Func<void(Cart&)>>> railListeners;

  Station() {
    unusedRailTracker.push(0);
  }

  void enableSecurity(const String& k, usz bitmapLen = 64) {
    this->key = k;
    this->bitmapLength = bitmapLen;
    this->isSecure = true;
    this->lastSentNonce = 0;
    this->window = 0;
  }

  u32 generateRail() {
    if (unusedRailTracker.size() > 0) {
      return unusedRailTracker[0];
    }
    return 0; // fallback
  }

  void onCart(Func<void(Cart&)> cb) { cartListener = Move(cb); }
  void onCartPushed(Func<void(Cart&)> cb) { cartPushListener = Move(cb); }

  void addRailListener(u32 rail, Func<void(Cart&)> cb) {
    auto* arr = railListeners.get(rail);
    if (!arr) {
      Array<Func<void(Cart&)>> newArr;
      newArr.push(Move(cb));
      railListeners.put(rail, Move(newArr));
    } else {
      arr->push(Move(cb));
    }
  }

  void removeRailListener(u32 rail) {
    railListeners.remove(rail);
  }

  /// Standard hook: bidirectional link between this station and another.
  void hook(Station& anotherStation) {
    anotherStation.onCart([this](Cart& c) { this->receive(c); });
    this->onCartPushed([&anotherStation](Cart& c) { anotherStation.push(c); });
  }

  /// Rail-specific hook: only listens on and pushes to a specific rail.
  void hook(Station& anotherStation, u32 rail) {
    anotherStation.addRailListener(rail, [this](Cart& c) { this->receive(c); });
    this->onCartPushed([&anotherStation, rail](Cart& c) {
      if (c.rail == rail || rail == 0) {
        anotherStation.push(c);
      }
    });
  }

  void unhookAll() {
    // Unhooks are a bit complex with lambdas. In this design, just clearing listeners.
    cartListener = Func<void(Cart&)>();
    cartPushListener = Func<void(Cart&)>();
    railListeners.clear();
  }

  virtual void update() {}
  virtual void push(Cart& c) {
    if (isSecure && !c.isSecure) {
      c.hasRailNotZero = (c.rail != 0);
      String inner = c.encodeInner();
      
      c.isSecure = true;
      c.nonce = ++lastSentNonce;
      
      Sec::AEADOptions opt;
      opt.text = inner;
      opt.ad = String();
      opt.ad.push((char)((c.isSecure ? 1 : 0) | (c.isStructureFixed ? 2 : 0) | (c.hasMeta ? 4 : 0) | ((c.rail != 0 || c.hasRailNotZero) ? 8 : 0) | (c.isAddressed ? 16 : 0)));
      if (c.rail != 0 || c.hasRailNotZero) {
        if (c.isStructureFixed) opt.ad.pushI32(c.rail);
        else opt.ad.pushVarLong(c.rail);
      }
      
      opt.tagLength = 8;
      if (Sec::aeadSeal(key, c.nonce, opt)) {
        c.mac = opt.tag;
        c.payload = opt.text; // Store cipherText in payload
      } else {
        return;
      }
    }
    
    lastSentUS = Xi::micros();
    if (cartPushListener.isValid()) {
      cartPushListener(c);
    }
  }

  virtual void receive(Cart& c) {
    if (c.isSecure && isSecure) {
      Sec::AEADOptions opt;
      opt.text = c.payload;
      opt.tag = c.mac;
      opt.tagLength = 8;
      opt.ad = String();
      opt.ad.push((char)((c.isSecure ? 1 : 0) | (c.isStructureFixed ? 2 : 0) | (c.hasMeta ? 4 : 0) | ((c.rail != 0 || c.hasRailNotZero) ? 8 : 0) | (c.isAddressed ? 16 : 0)));
      if (c.rail != 0 || c.hasRailNotZero) {
        if (c.isStructureFixed) opt.ad.pushI32(c.rail);
        else opt.ad.pushVarLong(c.rail);
      }
      
      if (Sec::aeadOpen(key, c.nonce, opt)) {
        c.isSecure = false;
        c.payload = opt.text;
        c.decodeInner(c.payload);
        
        // Remove from unused rail tracker since it's used
        for (usz i = 0; i < unusedRailTracker.size(); ++i) {
          if (unusedRailTracker[i] == c.rail) {
            unusedRailTracker.splice(i, 1);
            unusedRailTracker.push(millis() + 1337);
            break;
          }
        }
      } else {
        return; // Drop
      }
    } else if (c.isSecure && !isSecure) {
      // Pass directly to onCart, user drops it if they want
    } else if (!c.isSecure && isSecure) {
      push(c);
      return;
    }
    
    lastRecvUS = Xi::micros();
    
    auto* listeners = railListeners.get(c.rail);
    if (listeners && listeners->size() > 0) {
      for (usz i = 0; i < listeners->size(); i++) {
        if ((*listeners)[i].isValid()) {
          (*listeners)[i](c);
        }
      }
    } else if (cartListener.isValid()) {
      cartListener(c);
    }
  }
};

} // namespace Rho

#endif
