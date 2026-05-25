#ifndef RHO_TUNNEL_HPP
#define RHO_TUNNEL_HPP

#include <cstdlib>
#include <Collection/Array.hpp>
#include <Sec/Crypto.hpp>
#include <Xi/Func.hpp>
#include <Collection/Map.hpp>
#include <Collection/String.hpp>
#include <cstdio>
#include <Rho/Railway.hpp>
#include <Rho/Meta.hpp>

using namespace Xi;

namespace Xi {
  using namespace Collection;
}

#ifndef RHO_CRYPTO_POLY1305_DECLARED
#define RHO_CRYPTO_POLY1305_DECLARED
extern "C" {
  void crypto_poly1305(unsigned char *mac, const unsigned char *message, decltype(sizeof(0)) message_size, const unsigned char *key);
}
#endif

namespace Rho {
struct Packet {
  Xi::String payload;
  u64 channel = 1;
  bool bypassHOL = false;
  bool important = true;
  u64 id = 0;
  u64 fragmentStartID = 0;
  u8 fragmentStatus = 0;
  Packet()
      : channel(1), bypassHOL(false), important(true), id(0),
        fragmentStartID(0), fragmentStatus(0) {}
  Packet(const Xi::String &p, u64 c = 1)
      : payload(p), channel(c), bypassHOL(false), important(true), id(0),
        fragmentStartID(0), fragmentStatus(0) {}
};

struct FromTo {
  u64 from;
  u64 to;
};
struct InflightBundle {
  u64 id;
  Xi::String data;
  bool important;
};

using PacketListener = Xi::Func<void(Packet)>;
using MapListener = Xi::Func<void(Xi::Map<u64, Xi::String>)>;
using VoidListener = Xi::Func<void()>;

struct Fragment {
  u64 bID = 0;
  usz parseIndex = 0;
  u8 status = 0;
  Xi::String payload;
};

class Tunnel {
public:
  Xi::String name = "Tunnel";
  Xi::String key;
  bool isSecure = false, isWindowed = false, isAsleep = false;
  usz bitmapLength = 64;
  u64 lastSent = 0, lastSentHeartbeat = 0, lastSeen = 0, lastReceived = 0;
  bool destroyAfterFlush = false, isDestroyed = false;
  u64 activeTimeout = 5000;
  u64 destroyTimeout = 10000;
  u64 nextHeartbeatInterval = 2500;
  u64 lastSentNonce = 0, lastReceivedNonce = 0, receiveWindowMask = 0;

  usz maxInflight = 64; // Congestion control: max inflight bundles

  Sec::KeyPair ephemeralKeypair;
  Xi::String theirEphemeralPublic;

  // Hooks
  Station* hookedStation = nullptr;
  u32 hookedRail = 0;
  bool hasHookedTarget = false;
  Resource::NumericalAddress hookedTarget;
  bool hasHookedTargetStr = false;
  Xi::String hookedTargetStr;

  Tunnel() { 
    isDestroyed = false;
    _updateNextHeartbeatInterval();
  }

  void destroy() {
    if (isDestroyed) return;
    if (_destroyListener.isValid()) {
      _destroyListener();
    }

    u64 now = Xi::millis();
    lastSent = now;
    lastSentHeartbeat = now;
    lastSeen = now;
    isAsleep = false;
    destroyAfterFlush = false;
    isDestroyed = true;
    lastSentNonce = 0;
    lastReceivedNonce = 0;
    receiveWindowMask = 0;

    _nextSentSeq.clear();
    _nextSentUnorderedID.clear();
    _expectedRecvSeq.clear();
    _orderQueue.clear();
    _reassemblyQueue.clear();

    unhookAll();
  }

  bool active() const {
    if (isAsleep || isDestroyed) return false;
    if (lastReceived == 0) return true;
    if (Xi::millis() - lastReceived > activeTimeout) return false;
    return true;
  }

  bool isActive() const {
    return active();
  }

  /// Returns true if there is room in the inflight register to push more data.
  bool canPush() const {
    return _inflightBundles.size() < maxInflight;
  }

  void enableSecurity(const Xi::String& s, usz bitmapLen = 64) {
    key = s;
    bitmapLength = bitmapLen;
    isSecure = true;
    lastSentNonce = 0;
    lastReceivedNonce = 0;
    receiveWindowMask = 0;
    _outbox.clear();

    _nextSentSeq.clear();
    _nextSentUnorderedID.clear();
    _expectedRecvSeq.clear();
    _orderQueue.clear();
    _reassemblyQueue.clear();

    _updateNextHeartbeatInterval();
  }

  void enableWindowing(int windowSize = 64) {
    isWindowed = true;
    lastSentNonce = 0;
    lastReceivedNonce = 0;
    receiveWindowMask = 0;
    _outbox.clear();

    _nextSentSeq.clear();
    _nextSentUnorderedID.clear();
    _expectedRecvSeq.clear();
    _orderQueue.clear();
    _reassemblyQueue.clear();
  }

  void enableSecureX(const Xi::String& theirPublic, const Sec::KeyPair& ourKeypair) {
    theirEphemeralPublic = theirPublic;
    ephemeralKeypair = ourKeypair;
    if (theirEphemeralPublic.length() != 32 || ephemeralKeypair.secretKey.length() != 32)
      return;

    Xi::String shared = Sec::sharedKey(ephemeralKeypair.secretKey, theirEphemeralPublic);
    Xi::String newKey = Sec::kdf(shared, "RhoPufferV1", 32);

    enableSecurity(newKey);
    enableWindowing();
    
    // Trigger ready listener if we have it
    if (_readyListener.isValid()) {
      _readyListener();
    }
  }

  void hook(Station& st, u32 rail) {
    unhookAll();
    hookedStation = &st;
    hookedRail = rail;
    st.addRailListener(rail, [this](Cart& c) { this->receive(c); });
  }

  void hook(Station& st) {
    unhookAll();
    hookedStation = &st;
    hookedRail = 0;
    st.onCart([this](Cart& c) { this->receive(c); });
  }

  void hook(Station& st, u32 rail, const Resource::NumericalAddress& target) {
    hook(st, rail);
    hookedTarget = target;
    hasHookedTarget = true;
    hasHookedTargetStr = false;
  }

  void hook(Station& st, const Resource::NumericalAddress& target) {
    hook(st);
    hookedTarget = target;
    hasHookedTarget = true;
    hasHookedTargetStr = false;
  }

  void hook(Station& st, u32 rail, const Xi::String& target) {
    hook(st, rail);
    hookedTargetStr = target;
    hasHookedTargetStr = true;
    hasHookedTarget = false;
  }

  void hook(Station& st, const Xi::String& target) {
    hook(st);
    hookedTargetStr = target;
    hasHookedTargetStr = true;
    hasHookedTarget = false;
  }

  void unhookAll() {
    hasHookedTarget = false;
    hasHookedTargetStr = false;
    hookedTarget = Resource::NumericalAddress();
    hookedTargetStr = Xi::String();
    if (hookedStation) {
      if (hookedRail != 0) {
        hookedStation->removeRailListener(hookedRail);
      } else {
        hookedStation->cartListener = Xi::Func<void(Cart&)>();
      }
      hookedStation = nullptr;
      hookedRail = 0;
    }
  }

  void update() {
    if (isDestroyed) return;
    
    u64 now = Xi::millis();
    
    // Destroy timeout check
    if (destroyTimeout > 0 && lastReceived > 0 && (now - lastReceived > destroyTimeout)) {
      destroy();
      return;
    }

    if (hookedStation) {
      // Auto-flush to hooked station
      while (readyToSend()) {
        Xi::String out = flush();
        if (out.length() > 0) {
          Cart c;
          c.rail = hookedRail;
          c.payload = out;
          c.isSecure = false;
          if (hasHookedTarget) {
            c.target = hookedTarget;
            c.isAddressed = true;
          } else if (hasHookedTargetStr) {
            c.meta.put(Meta::Target, hookedTargetStr);
            c.hasMeta = true;
          }
          hookedStation->push(c);
        } else {
          break;
        }
      }
    }
  }

  void onPacket(PacketListener cb) { _packetListener = Xi::Move(cb); }
  void onDisconnect(MapListener cb) { _disconnectListener = Xi::Move(cb); }
  void onDestroy(VoidListener cb) { _destroyListener = Xi::Move(cb); }
  void onReady(VoidListener cb) { _readyListener = Xi::Move(cb); }

  void push(Packet pkt) {
    if (isWindowed) {
      if (!pkt.bypassHOL) {
        u64 seq = _nextSentSeq.has(pkt.channel) ? *_nextSentSeq.get(pkt.channel) : 0;
        seq++;
        pkt.id = seq;
        _nextSentSeq.put(pkt.channel, seq);
      } else {
        u64 seq = _nextSentUnorderedID.has(pkt.channel) ? *_nextSentUnorderedID.get(pkt.channel) : (1ULL << 60);
        seq++;
        pkt.id = seq;
        _nextSentUnorderedID.put(pkt.channel, seq);
      }
    } else {
      pkt.id = 0;
    }
    _outbox.push(pkt);
  }
  void push(Xi::String s, u64 c = 1) { push(Packet(s, c)); }

  void disconnect(Xi::Map<u64, Xi::String> reason) {
    Packet p;
    p.channel = 0;
    p.important = true;
    p.payload.pushVarLong(1000);
    p.payload += reason.serialize();
    push(p);
  }

  bool receive(const Xi::String &bundle) {
    if (isAsleep) isAsleep = false;
    usz at = 0;
    u64 bID = 0;
    if (isWindowed) {
      auto res = bundle.peekVarLong(at);
      if (res.error) return false;
      bID = (u64)res.value;
      at += res.bytes;
      if (_hasReceived(bID)) return true;
    } else {
      bID = lastReceivedNonce + 1;
    }
    Xi::String plain;
    Xi::String payload = bundle.substring(at, bundle.length());
    if (isSecure) {
      if (payload.length() < 9) return false;
      Xi::String aad;
      if (isWindowed) aad.pushVarLong((long long)bID);

      Sec::AEADOptions opt;
      opt.tag = payload.substring(0, 8);
      opt.text = payload.substring(8, payload.length());
      opt.ad = aad;
      opt.tagLength = 8;

      if (!Sec::aeadOpen(key, bID, opt)) {
        return false;
      }
      plain = opt.text;
    } else {
      plain = payload;
    }

    if (plain.length() == 0) return false;

    // Success! Update nonce tracker.
    lastSeen = Xi::millis();
    lastReceived = Xi::millis();
    
    if (isWindowed) _pretendReceived(bID);
    else lastReceivedNonce = bID;

    usz pAt = 0;
    if (pAt >= plain.length()) return true;
    u8 hb = plain[pAt++];
    bool padded = (hb >> 2) & 1, single = (hb >> 3) & 1;
    Xi::String content;
    if (padded) {
      auto res = plain.peekVarLong(pAt);
      if (res.error) return true;
      pAt += res.bytes;
      u64 pLen = (u64)res.value;
      if (pAt + (usz)pLen <= plain.length())
        content = plain.substring(pAt, pAt + (usz)pLen);
      else
        return true;
    } else {
      if (pAt > plain.length()) return true;
      content = plain.substring(pAt, plain.length());
    }
    
    usz parseIdx = 0;
    if (single) {
      _parsePacket(content, bID, parseIdx++);
    } else {
      usz sAt = 0;
      while (sAt < content.length()) {
        auto res = content.peekVarLong(sAt);
        if (res.error) break;
        sAt += res.bytes;
        u64 pkL = (u64)res.value;
        if (sAt + (usz)pkL > content.length()) break;
        _parsePacket(content.substring(sAt, sAt + (usz)pkL), bID, parseIdx++);
        sAt += (usz)pkL;
      }
    }

    return true;
  }
  
  bool receive(Cart& c) {
    if (c.isSecure) return false;
    return receive(c.payload);
  }

  bool readyToSend() const {
    if (isAsleep) return false;
    u64 now = Xi::millis();
    bool hb =
        isWindowed &&
        ((now > lastSent + activeTimeout) || (now > lastSentHeartbeat + nextHeartbeatInterval));
    return _nonImportantInflightBundles.size() > 0 ||
           _priorityResendQueue.size() > 0 ||
           (_resendPosition < _inflightBundles.size()) || _outbox.size() > 0 || hb;
  }

  Xi::String flush(usz bBS = 32, usz bMS = 1400) {
    if (isAsleep) return Xi::String();
    u64 now = Xi::millis();
    if (destroyAfterFlush && _inflightBundles.size() == 0 &&
        _nonImportantInflightBundles.size() == 0 && _outbox.size() == 0) {
      destroy();
      return Xi::String();
    }
    if (isWindowed) {
      if ((now > lastSent + activeTimeout) || (now > lastSentHeartbeat + nextHeartbeatInterval)) {
        Packet h;
        h.channel = 0;
        h.important = false;
        h.payload.pushVarLong(0);
        auto rec = _showReceived();
        h.payload.pushVarLong((long long)rec.size());
        for (auto &f : rec) {
          h.payload.pushVarLong((long long)f.from);
          h.payload.pushVarLong((long long)f.to);
        }
        auto un = _showUnavailable();
        h.payload.pushVarLong((long long)un.size());
        for (auto &f : un) {
          h.payload.pushVarLong((long long)f.from);
          h.payload.pushVarLong((long long)f.to);
        }
        _outbox.unshift(h);
        lastSentHeartbeat = now;
        _updateNextHeartbeatInterval();
        _resendPosition = 0; // Trigger resend of inflight packets
      }
    }
    if (_outbox.size() > 0) _develop(bBS, bMS);

    Xi::String ret;
    if (_nonImportantInflightBundles.size() > 0) {
      InflightBundle ib = _nonImportantInflightBundles.shift();
      ret = Xi::Move(ib.data);
    } else if (_priorityResendQueue.size() > 0) {
      InflightBundle ib = _priorityResendQueue.shift();
      ret = Xi::Move(ib.data);
    } else if (_resendPosition < _inflightBundles.size()) {
      InflightBundle &ib = _inflightBundles[_resendPosition++];
      ret = Xi::String(ib.data.data(), ib.data.length());
    }

    if (ret.length() > 0) {
      lastSent = Xi::millis();
      if (!isWindowed) {
        _inflightBundles.clear();
        _resendPosition = 0;
      }
    }

    return ret;
  }

private:
  Xi::Array<InflightBundle> _inflightBundles, _nonImportantInflightBundles;
  Xi::Array<InflightBundle> _priorityResendQueue;
  usz _resendPosition = 0;
  Xi::Array<u64> _droppedBundles;
  Xi::Map<Xi::String, Xi::Array<Fragment>> _reassemblyQueue;
  Xi::Map<u64, u64> _nextSentSeq;
  Xi::Map<u64, u64> _nextSentUnorderedID;
  Xi::Map<u64, u64> _expectedRecvSeq;
  Xi::Map<u64, Xi::Array<Packet>> _orderQueue;
  Xi::Array<Packet> _outbox;

  PacketListener _packetListener;
  MapListener _disconnectListener;
  VoidListener _destroyListener, _readyListener;

  void _updateNextHeartbeatInterval() {
    u64 timeToBeInactive = activeTimeout;
    double halfTheTimeToBeInactive = timeToBeInactive * 0.5;
    double minOffset = -timeToBeInactive * 0.5;
    double maxOffset = timeToBeInactive * 0.5;
    double r = (double)::rand() / (double)RAND_MAX;
    double offset = minOffset + r * (maxOffset - minOffset);
    double val = halfTheTimeToBeInactive + offset;
    if (val < 100.0) val = 100.0;
    nextHeartbeatInterval = (u64)val;
  }

  void _parsePacket(const Xi::String &raw, u64 bID, usz parseIdx) {
    usz cursor = 0;
    u8 header = raw[cursor++];
    Packet p;
    p.fragmentStatus = header & 0x03;
    bool hasChannel = (header >> 2) & 1;
    p.bypassHOL = (header >> 3) & 1;
    if (isWindowed) {
      auto res = raw.peekVarLong(cursor);
      if (!res.error) {
        p.id = (u64)res.value;
        cursor += res.bytes;
      }
    } else
      p.id = 0;

    if (hasChannel) {
      auto res = raw.peekVarLong(cursor);
      if (!res.error) {
        p.channel = (u64)res.value;
        cursor += res.bytes;
      }
    } else
      p.channel = 1;
    if (p.fragmentStatus != 0) {
      auto res = raw.peekVarLong(cursor);
      if (!res.error) {
        p.fragmentStartID = (u64)res.value;
        cursor += res.bytes;
      }
    }
    if (cursor < raw.length())
      p.payload = raw.substring(cursor, raw.length());
    _handleFragmentAndDeliver(p, bID, parseIdx);
  }

  void _handleFragmentAndDeliver(const Packet &p, u64 bID, usz parseIdx) {
    if (p.fragmentStatus == 0) {
      _deliverOrQueue(p);
    } else {
      String key = String((long long)p.channel) + "_" + String((long long)p.fragmentStartID);
      Array<Fragment>* frags = _reassemblyQueue.get(key);
      if (!frags) {
        Array<Fragment> newFrags;
        _reassemblyQueue.put(key, Move(newFrags));
        frags = _reassemblyQueue.get(key);
      }

      bool duplicate = false;
      for (usz i = 0; i < frags->size(); ++i) {
        if ((*frags)[i].bID == bID && (*frags)[i].parseIndex == parseIdx) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) return;

      Fragment newFrag;
      newFrag.bID = bID;
      newFrag.parseIndex = parseIdx;
      newFrag.status = p.fragmentStatus;
      newFrag.payload = p.payload;
      frags->push(Move(newFrag));

      // Sort fragments by bID, then by parseIndex
      for (usz i = 1; i < frags->size(); ++i) {
        Fragment keyFrag = (*frags)[i];
        long long j = (long long)i - 1;
        while (j >= 0 && ((*frags)[j].bID > keyFrag.bID || ((*frags)[j].bID == keyFrag.bID && (*frags)[j].parseIndex > keyFrag.parseIndex))) {
          (*frags)[j + 1] = (*frags)[j];
          j--;
        }
        (*frags)[j + 1] = keyFrag;
      }

      // Check if we have start and end fragments
      bool hasStart = false;
      bool hasEnd = false;
      u64 startBID = 0;
      u64 endBID = 0;
      for (usz i = 0; i < frags->size(); ++i) {
        if ((*frags)[i].status == 1) {
          hasStart = true;
          startBID = (*frags)[i].bID;
        }
        if ((*frags)[i].status == 3) {
          hasEnd = true;
          endBID = (*frags)[i].bID;
        }
      }

      if (hasStart && hasEnd) {
        // Verify all intermediate bundles are received
        bool ready = true;
        for (u64 b = startBID; b <= endBID; ++b) {
          if (!_hasReceived(b)) {
            ready = false;
            break;
          }
        }
        if (ready) {
          String fullPayload;
          for (usz i = 0; i < frags->size(); ++i) {
            fullPayload += (*frags)[i].payload;
          }
          _reassemblyQueue.remove(key);

          Packet fullP = p;
          fullP.payload = fullPayload;
          fullP.fragmentStatus = 0;
          _deliverOrQueue(fullP);
        }
      }
    }
  }

  void _deliverOrQueue(const Packet &p) {
    if (p.bypassHOL || !isWindowed || p.id == 0 || p.id >= (1ULL << 60)) {
      _dispatchPacket(p);
      return;
    }

    u64 expected = _expectedRecvSeq.has(p.channel) ? *_expectedRecvSeq.get(p.channel) : 1;
    if (p.id == expected) {
      _dispatchPacket(p);
      expected++;
      _expectedRecvSeq.put(p.channel, expected);

      auto* q = _orderQueue.get(p.channel);
      if (q) {
        bool foundNext = true;
        while (foundNext) {
          foundNext = false;
          for (usz i = 0; i < q->size(); ++i) {
            if ((*q)[i].id == expected) {
              Packet nextP = (*q)[i];
              q->splice(i, 1);
              _dispatchPacket(nextP);
              expected++;
              _expectedRecvSeq.put(p.channel, expected);
              foundNext = true;
              break;
            }
          }
        }
      }
    } else if (p.id > expected) {
      auto* q = _orderQueue.get(p.channel);
      if (!q) {
        Array<Packet> newQ;
        newQ.push(p);
        _orderQueue.put(p.channel, Move(newQ));
      } else {
        bool duplicate = false;
        for (usz i = 0; i < q->size(); ++i) {
          if ((*q)[i].id == p.id) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) {
          q->push(p);
        }
      }
    }
  }

  void _dispatchPacket(const Packet &p) {
    if (p.channel == 0) {
      usz pAt = 0;
      auto typeRes = p.payload.peekVarLong(pAt);
      if (typeRes.error) return;
      u64 type = (u64)typeRes.value;
      pAt += typeRes.bytes;

      if (type == 0) {
        if (!isWindowed) return;
        auto countRes = p.payload.peekVarLong(pAt);
        if (countRes.error) return;
        pAt += countRes.bytes;
        u64 count = (u64)countRes.value;
        for (u64 i = 0; i < count; ++i) {
          auto fRes = p.payload.peekVarLong(pAt);
          if (fRes.error) break;
          pAt += fRes.bytes;
          auto tRes = p.payload.peekVarLong(pAt);
          if (tRes.error) break;
          pAt += tRes.bytes;
          for (u64 x = (u64)fRes.value; x <= (u64)tRes.value; ++x)
            _removeInflight(x);
        }
        auto countRes2 = p.payload.peekVarLong(pAt);
        if (countRes2.error) return;
        pAt += countRes2.bytes;
        u64 count2 = (u64)countRes2.value;
        for (u64 i = 0; i < count2; ++i) {
          auto fRes = p.payload.peekVarLong(pAt);
          if (fRes.error) break;
          pAt += fRes.bytes;
          auto tRes = p.payload.peekVarLong(pAt);
          if (tRes.error) break;
          pAt += tRes.bytes;
          for (usz j = 0; j < _inflightBundles.size(); ++j) {
            if (_inflightBundles[j].id >= (u64)fRes.value &&
                _inflightBundles[j].id <= (u64)tRes.value) {
              InflightBundle ib;
              ib.id = _inflightBundles[j].id;
              ib.data = Xi::String(_inflightBundles[j].data.data(),
                                   _inflightBundles[j].data.length());
              ib.important = true;
              _priorityResendQueue.push(Xi::Move(ib));
            }
          }
        }
      } else if (type == 1000) {
        if (_disconnectListener.isValid())
          _disconnectListener(
              Xi::Map<u64, Xi::String>::deserialize(p.payload, pAt));
      }
    } else {
      if (_packetListener.isValid()) {
        _packetListener(p);
      }
    }
  }

  /// develop — Builds outbox packets into bundles.
  /// Will NOT produce more bundles if inflight register is full (congestion control).
  void _develop(usz bBS = 32, usz bMS = 1400) {
    if (isAsleep) return;

    while (_outbox.size() > 0) {
      // Congestion control: stop if inflight is full (but allow non-important packets like ACKs)
      if (_inflightBundles.size() >= maxInflight && _outbox[0].important) return;

      Xi::String py;
      py.push(0);
      bool single = false, important = false;
      usz consumed = 0;
      Xi::String tF;
      _serializePacket(tF, _outbox[0]);

      usz overhead = 1 + (isWindowed ? 9 : 0) + 8 + bBS,
          avail = (bMS > overhead) ? bMS - overhead : 0;

      if (isWindowed && tF.length() > avail) {
        Packet p = _outbox.shift();
        usz fS = (avail > 15) ? avail - 15 : 1, off = 0;
        Xi::Array<Packet> frags;
        while (off < p.payload.length()) {
          usz len = (p.payload.length() - off > fS) ? fS : p.payload.length() - off;
          Packet f(p.payload.substring(off, off + (usz)len), p.channel);
          f.id = p.id;
          f.important = p.important;
          f.fragmentStartID = p.id;
          f.fragmentStatus = (off == 0)
                                 ? (p.payload.size() <= off + len ? 0 : 1)
                                 : (p.payload.size() <= off + len ? 3 : 2);
          frags.push(f);
          off += len;
        }
        for (usz i = frags.size(); i > 0; --i) {
          _outbox.unshift(frags[i - 1]);
        }
        continue;
      }
      if (_outbox.size() == 1 || !isWindowed) {
        single = true;
        py += tF;
        important |= _outbox[0].important;
        consumed = 1;
      } else {
        for (usz i = 0; i < _outbox.size(); ++i) {
          if (_inflightBundles.size() >= maxInflight && _outbox[i].important) break;
          Xi::String t;
          _serializePacket(t, _outbox[i]);
          if (py.size() + t.size() + 9 > avail) break;
          py.pushVarLong((long long)t.size());
          py += t;
          important |= _outbox[i].important;
          consumed++;
        }
      }
      if (consumed == 0) break;
      for (usz k = 0; k < consumed; ++k)
        _outbox.shift();
      Xi::String fP;
      fP.push(0);
      bool pad = false;
      usz dL = py.length() - 1;
      Xi::String lV;
      lV.pushVarLong((long long)dL);
      usz cT = 1 + lV.length() + dL, rem = cT % bBS;
      if (rem != 0) {
        pad = true;
        fP += lV;
        fP.pushEach(py.data() + 1, dL);
        fP += Sec::zeros(bBS - rem);
      } else
        fP.pushEach(py.data() + 1, dL);
      
      u8 h = 0;
      if (isSecure) h |= 1;
      if (pad) h |= (1 << 2);
      if (single) h |= (1 << 3);
      fP[0] = h;

      Xi::String bD;
      u64 cBID = isWindowed ? ++lastSentNonce : 0;
      if (isWindowed) bD.pushVarLong((long long)cBID);
      if (isSecure) {
        Xi::String aad;
        if (isWindowed) aad.pushVarLong((long long)cBID);
        Sec::AEADOptions opt;
        opt.text = fP;
        opt.ad = aad;
        opt.tagLength = 8;
        if (Sec::aeadSeal(key, cBID, opt)) {
          bD += opt.tag;
          bD += opt.text;
        }
      } else {
        bD.pushEach(fP.data(), fP.length());
      }

      InflightBundle ib;
      ib.id = cBID;
      ib.data = Xi::Move(bD);
      ib.important = isWindowed ? important : false;
      if (ib.important)
        _inflightBundles.push(Xi::Move(ib));
      else
        _nonImportantInflightBundles.push(Xi::Move(ib));
    }
  }

  bool _hasReceived(u64 id) const {
    if (id == 0) return true;
    if (id > lastReceivedNonce) return false;
    u64 diff = lastReceivedNonce - id;
    if (diff >= 64) return true;
    return (receiveWindowMask >> diff) & 1;
  }
  
  void _pretendReceived(u64 id) {
    if (id == 0) return;
    if (id > lastReceivedNonce) {
      u64 diff = id - lastReceivedNonce;
      if (diff >= 64)
        receiveWindowMask = 1;
      else {
        receiveWindowMask <<= diff;
        receiveWindowMask |= 1;
      }
      lastReceivedNonce = id;
    } else {
      u64 diff = lastReceivedNonce - id;
      if (diff < 64)
        receiveWindowMask |= ((u64)1 << diff);
    }
  }
  
  void _removeInflight(u64 id) {
    for (usz i = 0; i < _inflightBundles.size(); ++i)
      if (_inflightBundles[i].id == id) {
        _inflightBundles.splice(i, 1);
        if (_resendPosition > i)
          _resendPosition--;
        return;
      }
  }
  
  Xi::Array<FromTo> _showReceived() const {
    Xi::Array<FromTo> res;
    if (lastReceivedNonce == 0) return res;
    FromTo cur;
    cur.to = lastReceivedNonce;
    cur.from = lastReceivedNonce;
    bool inRange = true;
    u64 mask = receiveWindowMask;
    for (int k = 1; k < 64; ++k) {
      u64 id = lastReceivedNonce - k;
      if (id == 0) break;
      bool have = (mask >> k) & 1;
      if (have) {
        if (inRange)
          cur.from = id;
        else {
          inRange = true;
          cur.to = id;
          cur.from = id;
        }
      } else if (inRange) {
        res.push(cur);
        inRange = false;
      }
    }
    if (inRange) res.push(cur);
    return res;
  }
  
  Xi::Array<FromTo> _showUnavailable() {
    Xi::Array<FromTo> res;
    for (usz i = 0; i < _droppedBundles.size(); ++i) {
      FromTo ft;
      ft.from = _droppedBundles[i];
      ft.to = _droppedBundles[i];
      res.push(ft);
    }
    _droppedBundles.clear();
    return res;
  }
  
  void _serializePacket(Xi::String &b, const Packet &p) {
    u8 h = (p.fragmentStatus & 0x03);
    if (p.channel != 1) h |= (1 << 2);
    if (p.bypassHOL) h |= (1 << 3);
    b.push(h);
    if (isWindowed) b.pushVarLong((long long)p.id);
    if (p.channel != 1) b.pushVarLong((long long)p.channel);
    if (p.fragmentStatus != 0) b.pushVarLong((long long)p.fragmentStartID);
    b += p.payload;
  }
};
} // namespace Rho
#endif
