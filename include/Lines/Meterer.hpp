#ifndef LINES_METERER_HPP
#define LINES_METERER_HPP

#include <Rho/Railway.hpp>

namespace Lines {

using namespace Rho;

// ---------------------------------------------------------------------------
// Meterer
//
// A Station whose purpose is to meter in/out data.
// Sits between an "outside" station and an "inside" station.
// Punishment for exceeding quota: silently drop (throttle).
// ---------------------------------------------------------------------------
class Meterer : public Station {
public:
  Station* hookedStation = nullptr;

  // Quota configuration (0 = unlimited)
  u64 inQuotaPerSecond = 0;    // Bytes/s allowed inbound
  u64 outQuotaPerSecond = 0;   // Bytes/s allowed outbound
  u64 inCartQuota = 0;         // Max carts/s inbound
  u64 outCartQuota = 0;        // Max carts/s outbound

  // Current-second counters (reset every second)
  u64 inBytesThisSecond = 0;
  u64 outBytesThisSecond = 0;
  u64 inCartsThisSecond = 0;
  u64 outCartsThisSecond = 0;
  u64 lastResetUS = 0;

  // Lifetime counters
  u64 totalBytesIn = 0;
  u64 totalBytesOut = 0;
  u64 totalCartsIn = 0;
  u64 totalCartsOut = 0;

  // Status
  bool throttledIn = false;
  bool throttledOut = false;

  Meterer() : Station() {
    name = "Meterer";
    lastResetUS = Xi::micros();
  }

  /// Hook to the outside station. Carts arriving from outside are metered inbound.
  /// Carts pushed from inside through this Meterer are metered outbound.
  void hook(Station& outside) {
    hookedStation = &outside;
    
    // Inbound: outside -> this (metered receive)
    outside.onCart([this](Cart& c) {
      _rollIfNeeded();
      u64 sz = c.payload.size();

      // Check inbound quota
      if (inQuotaPerSecond > 0 && inBytesThisSecond + sz > inQuotaPerSecond) {
        throttledIn = true;
        return; // Drop
      }
      if (inCartQuota > 0 && inCartsThisSecond >= inCartQuota) {
        throttledIn = true;
        return; // Drop
      }

      throttledIn = false;
      inBytesThisSecond += sz;
      inCartsThisSecond++;
      totalBytesIn += sz;
      totalCartsIn++;

      this->receive(c); // passes to cartListener
    });

    // Outbound: this -> outside (metered push)
    this->onCartPushed([this](Cart& c) {
      _rollIfNeeded();
      u64 sz = c.payload.size();

      // Check outbound quota
      if (outQuotaPerSecond > 0 && outBytesThisSecond + sz > outQuotaPerSecond) {
        throttledOut = true;
        return; // Drop
      }
      if (outCartQuota > 0 && outCartsThisSecond >= outCartQuota) {
        throttledOut = true;
        return; // Drop
      }

      throttledOut = false;
      outBytesThisSecond += sz;
      outCartsThisSecond++;
      totalBytesOut += sz;
      totalCartsOut++;

      if (hookedStation) hookedStation->push(c);
    });
  }

  void receive(Cart& c) override {
    // Usually called from the hooked station's onCart.
    // If called directly, just pass to our cartListener.
    if (cartListener.isValid()) cartListener(c);
  }

  void push(Cart& c) override {
    // Our push goes to our cartPushListener, which forwards it to the hookedStation!
    if (cartPushListener.isValid()) {
      cartPushListener(c);
    }
  }

  void resetCounters() {
    inBytesThisSecond = 0;
    outBytesThisSecond = 0;
    inCartsThisSecond = 0;
    outCartsThisSecond = 0;
    lastResetUS = Xi::micros();
    throttledIn = false;
    throttledOut = false;
  }

  void update() override {
    _rollIfNeeded();
  }

private:
  void _rollIfNeeded() {
    u64 now = Xi::micros();
    if (now - lastResetUS >= 1000000) { // 1 second
      resetCounters();
    }
  }
};

} // namespace Lines

#endif // LINES_METERER_HPP
