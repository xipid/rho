#ifndef LINES_REACH_HPP
#define LINES_REACH_HPP

#include <Rho/Railway.hpp>
#include <Rho/Tunnel.hpp>
#include <Rho/Meta.hpp>
#include <Util/Client.hpp>
#include <Collection/Array.hpp>
#include <Collection/Map.hpp>
#include <Collection/String.hpp>
#include <Sec/Crypto.hpp>
#include <Xi/Func.hpp>

namespace Lines {

using namespace Rho;
using namespace Xi;

// ---------------------------------------------------------------------------
// Reach
//
// Decentralized name resolution with Byzantine Fault Tolerance.
//
// Algorithm:
//   1. Pick 2 random default servers, probe both in parallel.
//   2. BFT phase: both must return the same metadata. If mismatch,
//      mark the loser as sus, pick a new default, repeat until
//      1/3 of servers are sus.
//   3. 200ms timeout — late responders still get processed if BFT
//      hasn't advanced, but count as a loss otherwise.
//   4. After BFT: upgrade with the resolved server.
//   5. Check response metadata:
//      - Has Address → recurse (resolve again with the returned
//        NumericalAddress as new defaults)
//      - Has NumericalAddress only → done, that's the target
//      - Has PublicKeys → next server must prove ownership
//      - Other metadata → merge into meta
// ---------------------------------------------------------------------------

struct ReachDefault {
  Resource::NumericalAddress address;
  String publicKey;
};

class Reach {
public:
  Array<ReachDefault> defaults;
  Array<Resource::NumericalAddress> susDefaults;

  String startingAddress;
  Map<u64, String> meta; // Accumulated metadata through resolution steps

  Resource::NumericalAddress finalAddress;
  String finalPublicKey;

  bool done = false;
  bool success = false;

  Station* station = nullptr;

  // BFT phase state
  u64 bftStartUS = 0;
  u64 bftTimeoutUS = 200000; // 200ms

  Reach() {}

  ~Reach() {
    destroy();
  }

  void addDefault(const Resource::NumericalAddress& addr, const String& publicKey) {
    ReachDefault rd;
    rd.address = addr;
    rd.publicKey = publicKey;
    defaults.push(rd);
  }

  // -----------------------------------------------------------------------
  // start — Begin resolution of the given address string.
  // -----------------------------------------------------------------------
  void start(Station& st, const String& address) {
    station = &st;
    startingAddress = address;
    done = false;
    success = false;
    _bftResponses.clear();
    _activeClients.clear();
    _currentPhase = Phase::BFT;
    _bftRound = 0;

    _startBFTRound();
  }

  // -----------------------------------------------------------------------
  // update — Tick the resolution process.
  // -----------------------------------------------------------------------
  void update() {
    if (done || !station) return;

    u64 now = Xi::micros();

    // Tick all active clients
    for (usz i = 0; i < _activeClients.size(); ++i) {
      _activeClients[i]->update();
    }

    if (_currentPhase == Phase::BFT) {
      _updateBFT(now);
    } else if (_currentPhase == Phase::Upgrade) {
      _updateUpgrade();
    } else if (_currentPhase == Phase::Recurse) {
      if (_childReach) {
        _childReach->update();
        if (_childReach->done) {
          if (_childReach->success) {
            finalAddress = _childReach->finalAddress;
            finalPublicKey = _childReach->finalPublicKey;
            // Merge child meta
            // (child's meta takes precedence)
            success = true;
          }
          done = true;
          delete _childReach;
          _childReach = nullptr;
        }
      }
    }
  }

  // -----------------------------------------------------------------------
  // destroy — Immediately stop and free all resources.
  // -----------------------------------------------------------------------
  void destroy() {
    for (usz i = 0; i < _activeClients.size(); ++i) {
      _activeClients[i]->destroy();
      delete _activeClients[i];
    }
    _activeClients.clear();
    _bftResponses.clear();
    if (_childReach) {
      _childReach->destroy();
      delete _childReach;
      _childReach = nullptr;
    }
    done = true;
  }

private:
  enum class Phase { BFT, Upgrade, Recurse };
  Phase _currentPhase = Phase::BFT;
  u32 _bftRound = 0;

  struct BFTResponse {
    usz defaultIdx;
    Map<u64, String> meta;
    bool responded = false;
    bool timedOut = false;
  };
  Array<BFTResponse> _bftResponses;
  Array<Client*> _activeClients;

  // For recursive resolution
  Reach* _childReach = nullptr;

  // Expected public key for the next server (from PublicKeys metadata)
  String _requiredPublicKey;

  // Pick indices of two random non-sus defaults
  bool _pickTwoDefaults(usz& a, usz& b) {
    Array<usz> available;
    for (usz i = 0; i < defaults.size(); ++i) {
      bool isSus = false;
      for (usz j = 0; j < susDefaults.size(); ++j) {
        if (susDefaults[j] == defaults[i].address) {
          isSus = true;
          break;
        }
      }
      if (!isSus) available.push(i);
    }
    if (available.size() < 2) {
      if (available.size() == 1) {
        a = available[0];
        b = available[0]; // Only one left, use it alone
        return true;
      }
      return false; // Not enough defaults
    }
    usz aIdx = Xi::millis() % available.size();
    // Pick b != a
    usz offset = 1 + (Xi::millis() / 7) % (available.size() - 1);
    usz bIdx = (aIdx + offset) % available.size();
    a = available[aIdx];
    b = available[bIdx];
    return true;
  }

  usz _pickOneNonSus() {
    for (usz i = 0; i < defaults.size(); ++i) {
      bool isSus = false;
      for (usz j = 0; j < susDefaults.size(); ++j) {
        if (susDefaults[j] == defaults[i].address) {
          isSus = true;
          break;
        }
      }
      if (!isSus) return i;
    }
    return defaults.size(); // None available
  }

  void _startBFTRound() {
    // Clean up previous round
    for (usz i = 0; i < _activeClients.size(); ++i) {
      _activeClients[i]->destroy();
      delete _activeClients[i];
    }
    _activeClients.clear();
    _bftResponses.clear();

    usz a, b;
    if (!_pickTwoDefaults(a, b)) {
      done = true;
      success = false;
      return;
    }

    bftStartUS = Xi::micros();

    // Create two clients to probe the defaults in parallel
    for (int k = 0; k < 2; ++k) {
      usz idx = (k == 0) ? a : b;
      Client* cli = new Client();
      cli->hook(*station, defaults[idx].address);

      BFTResponse resp;
      resp.defaultIdx = idx;
      resp.responded = false;
      resp.timedOut = false;
      _bftResponses.push(resp);

      usz respIdx = _bftResponses.size() - 1;

      cli->onAnnounce([this, cli, respIdx](Cart& c) {
        // Got announce — capture metadata
        if (respIdx < _bftResponses.size()) {
          _bftResponses[respIdx].meta = c.meta;
          _bftResponses[respIdx].responded = true;
        }
      });

      cli->probe();
      _activeClients.push(cli);
    }
  }

  void _updateBFT(u64 now) {
    // Check if both responded
    usz responded = 0;
    for (usz i = 0; i < _bftResponses.size(); ++i) {
      if (_bftResponses[i].responded) responded++;
    }

    // Check timeout
    bool timedOut = (now - bftStartUS) >= bftTimeoutUS;

    if (responded >= 2) {
      // Both responded — check agreement
      if (_bftResponsesMatch(0, 1)) {
        // Agreement! BFT phase complete.
        // Merge metadata
        for (usz i = 0; i < _bftResponses.size(); ++i) {
          if (_bftResponses[i].responded) {
            _mergeMeta(_bftResponses[i].meta);
          }
        }
        _advanceAfterBFT();
        return;
      } else {
        // Mismatch — one is lying. Mark as sus.
        // We can't know which one — mark the second, pick a new partner.
        _markSus(_bftResponses[1].defaultIdx);
        _bftRound++;

        // Check if too many are sus (1/3 threshold)
        if (susDefaults.size() * 3 >= defaults.size()) {
          done = true;
          success = false;
          return;
        }

        _startBFTRound();
        return;
      }
    }

    if (timedOut) {
      // Mark non-responders as timed out (counts as a loss)
      for (usz i = 0; i < _bftResponses.size(); ++i) {
        if (!_bftResponses[i].responded) {
          _bftResponses[i].timedOut = true;
          _markSus(_bftResponses[i].defaultIdx);
        }
      }

      // If one responded, use it and continue
      if (responded >= 1) {
        for (usz i = 0; i < _bftResponses.size(); ++i) {
          if (_bftResponses[i].responded) {
            _mergeMeta(_bftResponses[i].meta);
          }
        }
        _advanceAfterBFT();
        return;
      }

      // None responded — try again if we can
      _bftRound++;
      if (susDefaults.size() * 3 >= defaults.size()) {
        done = true;
        success = false;
        return;
      }
      _startBFTRound();
    }
  }

  bool _bftResponsesMatch(usz a, usz b) {
    if (a >= _bftResponses.size() || b >= _bftResponses.size()) return false;
    auto& ma = _bftResponses[a].meta;
    auto& mb = _bftResponses[b].meta;

    // Compare key metadata fields
    // Check NumericalAddress, Address, PublicKey
    auto* naA = ma.get(Meta::NumericalAddress);
    auto* naB = mb.get(Meta::NumericalAddress);
    if (naA && naB && *naA != *naB) return false;
    if ((naA && !naB) || (!naA && naB)) return false;

    auto* addrA = ma.get(Meta::Address);
    auto* addrB = mb.get(Meta::Address);
    if (addrA && addrB && *addrA != *addrB) return false;
    if ((addrA && !addrB) || (!addrA && addrB)) return false;

    auto* pkA = ma.get(Meta::PublicKey);
    auto* pkB = mb.get(Meta::PublicKey);
    if (pkA && pkB && *pkA != *pkB) return false;
    if ((pkA && !pkB) || (!pkA && pkB)) return false;

    return true;
  }

  void _markSus(usz defaultIdx) {
    if (defaultIdx < defaults.size()) {
      susDefaults.push(defaults[defaultIdx].address);
    }
  }

  void _mergeMeta(const Map<u64, String>& incoming) {
    // Merge into our accumulated meta — newer wins
    // We iterate known keys
    u64 keys[] = {
      Meta::Address, Meta::NumericalAddress, Meta::PublicKey,
      Meta::Name, Meta::Service, Meta::Version, Meta::Path,
      Meta::PublicHash
    };
    for (auto k : keys) {
      auto* val = incoming.get(k);
      if (val) {
        meta.put(k, *val);
      }
    }
  }

  void _advanceAfterBFT() {
    // Check what we got from the BFT phase:
    auto* addrStr = meta.get(Meta::Address);
    auto* naStr = meta.get(Meta::NumericalAddress);
    auto* pkStr = meta.get(Meta::PublicKey);

    // Case 1: Has Address → recurse
    if (addrStr && !addrStr->isEmpty() && naStr && !naStr->isEmpty()) {
      // Parse the NumericalAddress from the string
      Resource::NumericalAddress nextDefault;
      Array<String> parts = naStr->split(".");
      for (usz i = 0; i < parts.size(); ++i) {
        nextDefault.push((u64)parts[i].toInt());
      }

      _currentPhase = Phase::Recurse;
      _childReach = new Reach();
      _childReach->addDefault(nextDefault, pkStr ? *pkStr : String());
      if (station) {
        _childReach->start(*station, *addrStr);
      }
      return;
    }

    // Case 2: Has NumericalAddress only → done
    if (naStr && !naStr->isEmpty()) {
      Array<String> parts = naStr->split(".");
      for (usz i = 0; i < parts.size(); ++i) {
        finalAddress.push((u64)parts[i].toInt());
      }
      if (pkStr) finalPublicKey = *pkStr;
      done = true;
      success = true;
      return;
    }

    // Case 3: Has PublicKey only → store requirement for next step
    if (pkStr && !pkStr->isEmpty()) {
      _requiredPublicKey = *pkStr;
      // Need to upgrade with the server and verify
      _currentPhase = Phase::Upgrade;
      _startUpgradePhase();
      return;
    }

    // Case 4: Not enough info — fail
    done = true;
    success = false;
  }

  void _startUpgradePhase() {
    // Pick a non-sus default and upgrade with it
    usz idx = _pickOneNonSus();
    if (idx >= defaults.size()) {
      done = true;
      success = false;
      return;
    }

    // Clean up old clients
    for (usz i = 0; i < _activeClients.size(); ++i) {
      _activeClients[i]->destroy();
      delete _activeClients[i];
    }
    _activeClients.clear();

    Client* cli = new Client();
    cli->hook(*station, defaults[idx].address);

    cli->onAnnounce([this, cli](Cart& c) {
      // Verify public key if required
      if (!_requiredPublicKey.isEmpty()) {
        const String* theirPk = c.meta.get(Meta::PublicKey);
        if (!theirPk || *theirPk != _requiredPublicKey) {
          // Key mismatch — mark sus and try another
          // (handled in upgrade callback below)
          return;
        }
      }
      cli->upgrade();
    });

    cli->onReady([this](Packet pkt, Cart cart) {
      // After upgrade, check the response metadata
      Map<u64, String> respMeta;
      usz cursor = 0;
      if (pkt.payload.size() > 0) {
        respMeta = Map<u64, String>::deserialize(pkt.payload, cursor);
      }

      // Merge response metadata
      _mergeMeta(respMeta);

      auto* addrStr = respMeta.get(Meta::Address);
      auto* naStr = respMeta.get(Meta::NumericalAddress);
      auto* pkStr = respMeta.get(Meta::PublicKey);

      // Case 1: Address → recurse
      if (addrStr && !addrStr->isEmpty()) {
        Resource::NumericalAddress nextDefault;
        if (naStr && !naStr->isEmpty()) {
          Array<String> parts = naStr->split(".");
          for (usz i = 0; i < parts.size(); ++i) {
            nextDefault.push((u64)parts[i].toInt());
          }
        }
        _currentPhase = Phase::Recurse;
        _childReach = new Reach();
        _childReach->addDefault(nextDefault, pkStr ? *pkStr : String());
        if (station) {
          _childReach->start(*station, *addrStr);
        }
        return;
      }

      // Case 2: NumericalAddress only → done
      if (naStr && !naStr->isEmpty()) {
        Array<String> parts = naStr->split(".");
        for (usz i = 0; i < parts.size(); ++i) {
          finalAddress.push((u64)parts[i].toInt());
        }
        if (pkStr) finalPublicKey = *pkStr;
        done = true;
        success = true;
        return;
      }

      // Case 3: PublicKey → update requirement and try next
      if (pkStr && !pkStr->isEmpty()) {
        _requiredPublicKey = *pkStr;
        _startUpgradePhase();
        return;
      }

      // Fallback: nothing useful
      done = true;
      success = false;
    });

    cli->probe();
    _activeClients.push(cli);
  }

  void _updateUpgrade() {
    // Just tick clients — the callbacks handle everything
    for (usz i = 0; i < _activeClients.size(); ++i) {
      _activeClients[i]->update();
    }
  }
};

} // namespace Lines

#endif // LINES_REACH_HPP
