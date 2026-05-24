#include <Lines/Router.hpp>
#include <Rho/Railway.hpp>
#include <unistd.h>
#include <cstdio>
#include <cassert>
#include <vector>

using namespace Xi;
using namespace Rho;
using namespace Lines;
using namespace Resource;

static String addrStr(const NumericalAddress& a) {
    String s;
    for (usz i = 0; i < a.size(); ++i) {
        if (i > 0) s += ".";
        char buf[32]; snprintf(buf, sizeof(buf), "%lu", (unsigned long)a[i]);
        s += buf;
    }
    return s;
}

// Simulates a Gateway: receives carts → routes them through its router.
class GatewayNode : public Station {
public:
    Router router;
    NumericalAddress address;
    const char* label;

    GatewayNode(const char* lbl, NumericalAddress addr) : label(lbl), address(addr) {
        name = lbl;
        // When this gateway receives a cart, route it through our router
        onCart([this](Cart& c) {
            routeIncoming(c);
        });
    }

    void routeIncoming(Cart& c) {
        printf("  [%s] routing %s → %s\n", label, addrStr(c.source).c_str(), addrStr(c.target).c_str());
        router.route(c);
    }
};

// Simulates a fiber link: one-way push from sender to receiver.
// No bidirectional hook — avoids infinite loops.
class FiberLink : public Station {
public:
    GatewayNode* destination;
    const char* label;

    FiberLink(const char* lbl, GatewayNode* dest) : label(lbl), destination(dest) {
        name = lbl;
    }

    void deliver(Cart& c) {
        printf("  [%s] → %s\n", label, destination->label);
        destination->routeIncoming(c);
    }
};

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=========================================\n");
    printf("   Strict Tree Routing Test              \n");
    printf("   USA(1) ←→ DZ(2) ←→ SA(3)             \n");
    printf("=========================================\n\n");

    // ===================================================================
    // Addresses
    // ===================================================================
    NumericalAddress addr_usa;   addr_usa.push(1);
    NumericalAddress addr_dz;    addr_dz.push(2);
    NumericalAddress addr_sa;    addr_sa.push(3);

    NumericalAddress addr_usa_city;  addr_usa_city.push(1); addr_usa_city.push(1);
    NumericalAddress addr_usa_neigh; addr_usa_neigh.push(1); addr_usa_neigh.push(1); addr_usa_neigh.push(1);
    NumericalAddress addr_usa_dev;   addr_usa_dev.push(1); addr_usa_dev.push(1); addr_usa_dev.push(1); addr_usa_dev.push(1);

    NumericalAddress addr_sa_city;   addr_sa_city.push(3); addr_sa_city.push(1);
    NumericalAddress addr_sa_neigh;  addr_sa_neigh.push(3); addr_sa_neigh.push(1); addr_sa_neigh.push(1);
    NumericalAddress addr_sa_dev;    addr_sa_dev.push(3); addr_sa_dev.push(1); addr_sa_dev.push(1); addr_sa_dev.push(1);

    // ===================================================================
    // Create Gateway nodes (each one is a gateway in the strict tree)
    // ===================================================================
    GatewayNode usa_gw("USA-Telecom(1)", addr_usa);
    GatewayNode usa_city("USA-City(1.1)", addr_usa_city);
    GatewayNode usa_neigh("USA-Neigh(1.1.1)", addr_usa_neigh);

    GatewayNode dz_gw("DZ-Telecom(2)", addr_dz);

    GatewayNode sa_gw("SA-Telecom(3)", addr_sa);
    GatewayNode sa_city("SA-City(3.1)", addr_sa_city);
    GatewayNode sa_neigh("SA-Neigh(3.1.1)", addr_sa_neigh);

    Station usa_device; usa_device.name = "USA-Device(1.1.1.1)";
    Station sa_device;  sa_device.name  = "SA-Device(3.1.1.1)";

    // ===================================================================
    // Fiber links (unidirectional — one per direction)
    // ===================================================================
    // USA ↔ DZ
    FiberLink fiberA_to_dz("fiber-A→DZ", &dz_gw);
    FiberLink fiberB_to_usa("fiber-B→USA", &usa_gw);

    // DZ ↔ SA
    FiberLink fiberC_to_sa("fiber-C→SA", &sa_gw);
    FiberLink fiberD_to_dz("fiber-D→DZ", &dz_gw);

    // When a fiber station receives a cart (from a router.route() call),
    // it delivers to the destination gateway.
    fiberA_to_dz.onCart([&](Cart& c) { fiberA_to_dz.deliver(c); });
    fiberB_to_usa.onCart([&](Cart& c) { fiberB_to_usa.deliver(c); });
    fiberC_to_sa.onCart([&](Cart& c) { fiberC_to_sa.deliver(c); });
    fiberD_to_dz.onCart([&](Cart& c) { fiberD_to_dz.deliver(c); });

    // ===================================================================
    // Wire up the strict tree — each gateway ONLY knows parent + children
    // ===================================================================

    // --- USA Telecom (top level, no parent) ---
    // Manual cross-telecom entries:
    NumericalAddress prefix_2; prefix_2.push(2);
    NumericalAddress prefix_3; prefix_3.push(3);
    usa_gw.router.hook(&fiberA_to_dz, prefix_2);   // 2.x → fiber to DZ
    usa_gw.router.hook(&fiberA_to_dz, prefix_3);   // 3.x → fiber to DZ (DZ forwards)
    // Immediate child:
    usa_gw.router.hook(&usa_city, addr_usa_city);   // 1.1.x → city

    // --- USA City (knows parent + child) ---
    usa_city.router.hook(&usa_gw, addr_usa);             // parent: 1.x → telecom
    usa_city.router.hook(&usa_neigh, addr_usa_neigh);    // child: 1.1.1.x → neighborhood

    // --- USA Neighborhood (knows parent + child) ---
    usa_neigh.router.hook(&usa_city, addr_usa_city);     // parent: 1.1.x → city
    usa_neigh.router.hook(&usa_device, addr_usa_dev);    // child: 1.1.1.1 → device

    // --- DZ Telecom (top level, transit hub) ---
    NumericalAddress prefix_1; prefix_1.push(1);
    dz_gw.router.hook(&fiberB_to_usa, prefix_1);    // 1.x → fiber to USA
    dz_gw.router.hook(&fiberC_to_sa, prefix_3);     // 3.x → fiber to SA

    // --- SA Telecom (top level, no parent) ---
    sa_gw.router.hook(&fiberD_to_dz, prefix_1);     // 1.x → fiber to DZ (DZ forwards to USA)
    sa_gw.router.hook(&fiberD_to_dz, prefix_2);     // 2.x → fiber to DZ
    // Immediate child:
    sa_gw.router.hook(&sa_city, addr_sa_city);       // 3.1.x → city

    // --- SA City (knows parent + child) ---
    sa_city.router.hook(&sa_gw, addr_sa);                // parent: 3.x → telecom
    sa_city.router.hook(&sa_neigh, addr_sa_neigh);       // child: 3.1.1.x → neighborhood

    // --- SA Neighborhood (knows parent + child) ---
    sa_neigh.router.hook(&sa_city, addr_sa_city);        // parent: 3.1.x → city
    sa_neigh.router.hook(&sa_device, addr_sa_dev);       // child: 3.1.1.1 → device

    // ===================================================================
    // Test 1: SA device (3.1.1.1) → USA device (1.1.1.1)
    //
    // Expected path:
    //   3.1.1.1 → 3.1.1 (UP to parent)
    //   3.1.1   → 3.1   (UP to parent)
    //   3.1     → 3     (UP to parent)
    //   3       → fiberD→DZ (forward match: 1.x → DZ)
    //   DZ(2)   → fiberB→USA (forward match: 1.x → USA)
    //   USA(1)  → 1.1   (forward match: DOWN)
    //   1.1     → 1.1.1 (forward match: DOWN)
    //   1.1.1   → 1.1.1.1 (forward match: DOWN, delivered!)
    // ===================================================================
    printf("--- Test 1: SA 3.1.1.1 → USA 1.1.1.1 ---\n\n");

    bool test1 = false;
    usa_device.onCart([&](Cart& c) {
        printf("\n  *** USA DEVICE 1.1.1.1 RECEIVED: \"%s\" ***\n\n", c.payload.c_str());
        test1 = true;
    });

    Cart ping;
    ping.isAddressed = true;
    ping.source = addr_sa_dev;   // 3.1.1.1
    ping.target = addr_usa_dev;  // 1.1.1.1
    ping.payload = "Hello from Saudi Arabia!";

    // Device pushes to its parent (neighborhood)
    printf("  SA-Device pushes UP to SA-Neigh(3.1.1)\n");
    sa_neigh.routeIncoming(ping);

    printf("  Test 1: %s\n\n", test1 ? "PASSED ✓" : "FAILED ✗");

    // ===================================================================
    // Test 2: USA device (1.1.1.1) → SA device (3.1.1.1) — reverse
    // ===================================================================
    printf("--- Test 2: USA 1.1.1.1 → SA 3.1.1.1 ---\n\n");

    bool test2 = false;
    sa_device.onCart([&](Cart& c) {
        printf("\n  *** SA DEVICE 3.1.1.1 RECEIVED: \"%s\" ***\n\n", c.payload.c_str());
        test2 = true;
    });

    Cart pong;
    pong.isAddressed = true;
    pong.source = addr_usa_dev;  // 1.1.1.1
    pong.target = addr_sa_dev;   // 3.1.1.1
    pong.payload = "Hello from USA!";

    printf("  USA-Device pushes UP to USA-Neigh(1.1.1)\n");
    usa_neigh.routeIncoming(pong);

    printf("  Test 2: %s\n\n", test2 ? "PASSED ✓" : "FAILED ✗");

    // ===================================================================
    // Test 3: Local routing within USA (same neighborhood)
    // Source 1.1.1.1, Target 1.1.1.1 — forward match directly
    // ===================================================================
    printf("--- Test 3: Local 1.1.1.1 → 1.1.1.1 ---\n\n");

    bool test3 = false;
    usa_device.onCart([&](Cart& c) {
        printf("  *** USA DEVICE received own packet ***\n");
        test3 = true;
    });

    Cart local;
    local.isAddressed = true;
    local.source = addr_usa_dev;
    local.target = addr_usa_dev;
    local.payload = "loopback";

    usa_neigh.routeIncoming(local);

    printf("  Test 3: %s\n\n", test3 ? "PASSED ✓" : "FAILED ✗");

    // ===================================================================
    printf("=========================================\n");
    if (test1 && test2 && test3) {
        printf("  ALL TESTS PASSED ✓\n");
    } else {
        printf("  SOME TESTS FAILED ✗\n");
        if (!test1) printf("  - Test 1 FAILED (SA→USA cross-continental)\n");
        if (!test2) printf("  - Test 2 FAILED (USA→SA cross-continental)\n");
        if (!test3) printf("  - Test 3 FAILED (local routing)\n");
    }
    printf("=========================================\n");

    return (test1 && test2 && test3) ? 0 : 1;
}
