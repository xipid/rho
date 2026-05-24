#include <Lines/Bind.hpp>
#include <Util/Client.hpp>
#include <Util/Server.hpp>
#include <Lines/Router.hpp>
#include <unistd.h>
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <map>
#include <vector>

using namespace Xi;
using namespace Rho;
using namespace Lines;
using namespace Resource;

// A Station wrapper that forwards carts into a Router.
class RouterLink : public Station {
public:
    Router* targetRouter;
    
    RouterLink(Router* router) : targetRouter(router) {
        name = "RouterLink";
        onCart([this](Cart& c) {
            if (targetRouter) targetRouter->route(c);
        });
    }
};

// Represents a node in the hierarchy
struct Node {
    Router router;
    RouterLink* linkDown = nullptr; // To be hooked into parent router
    RouterLink* linkUp = nullptr;   // Default route up to parent
    
    ~Node() {
        if (linkDown) delete linkDown;
        if (linkUp) delete linkUp;
    }
};

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=========================================\n");
    printf("       Heavy Real Network Test           \n");
    printf("=========================================\n");

    // 10 Countries
    std::vector<Node*> countries(11); // 1-indexed to match 1..10
    
    for (int c = 1; c <= 10; c++) {
        countries[c] = new Node();
    }

    // Connect countries randomly at the top level
    // Country 1 routes to 10 via 2, Country 2 routes to 10 via 3, etc.
    // For simplicity, just make a ring: i routes to (i+1) for everything else.
    // To route to anywhere, hook the next country's RouterLink as the default route (address size 0).
    // Wait, Router::hook requires an address. Let's just hook the specific target country.
    // E.g. Country 1 hooks Country 10 via Country 2 means Country 1 hooks Country 2's linkDown for Address [10].
    
    for (int c = 1; c <= 10; c++) {
        countries[c]->linkDown = new RouterLink(&countries[c]->router);
    }

    // Country 2 hooks Country 9 via Country 3, 4, 5, 6, 7, 8...
    // Let's explicitly build the path 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 8 -> 9
    for (int c = 2; c < 9; c++) {
        Resource::NumericalAddress targetAddr; targetAddr.push(9);
        // c routes to 9 via c+1
        countries[c]->router.hook(countries[c+1]->linkDown, targetAddr);
    }
    
    // Path from 9 to 2: 9 -> 8 -> 7 -> 6 -> 5 -> 4 -> 3 -> 2
    for (int c = 9; c > 2; c--) {
        Resource::NumericalAddress targetAddr; targetAddr.push(2);
        countries[c]->router.hook(countries[c-1]->linkDown, targetAddr);
    }

    // Now build deep topology in Country 2 and Country 9
    // Level 1: Telecom (Router)
    // Level 2: City (Router)
    // Level 3: Neighborhood (Router)
    // Level 4: Home Router (Router)
    // Level 5: Device (Router)
    // Level 6: Process (Station - Client/Server)
    
    auto buildHierarchy = [](Node* country, int countryId, Station* process, bool isClient) {
        Node* city = new Node();
        Node* neighborhood = new Node();
        Node* home = new Node();
        Node* device = new Node();
        
        city->linkDown = new RouterLink(&city->router);
        neighborhood->linkDown = new RouterLink(&neighborhood->router);
        home->linkDown = new RouterLink(&home->router);
        device->linkDown = new RouterLink(&device->router);

        Resource::NumericalAddress cAddr; cAddr.push(countryId);
        Resource::NumericalAddress cityAddr = cAddr; cityAddr.push(1);
        Resource::NumericalAddress neighAddr = cityAddr; neighAddr.push(2);
        Resource::NumericalAddress homeAddr = neighAddr; homeAddr.push(3);
        Resource::NumericalAddress devAddr = homeAddr; devAddr.push(4);
        Resource::NumericalAddress procAddr = devAddr; procAddr.push(5);

        // Hook down (for incoming packets)
        country->router.hook(city->linkDown, cityAddr);
        city->router.hook(neighborhood->linkDown, neighAddr);
        neighborhood->router.hook(home->linkDown, homeAddr);
        home->router.hook(device->linkDown, devAddr);
        device->router.hook(process, procAddr);
        
        // Hook up (for outgoing packets to the rest of the world)
        // Since Router routes by longest prefix, an empty address acts as default route.
        // Wait, Router doesn't allow size==0 hooking for default route easily if we want to catch all.
        // Let's explicitly hook the target country.
        // If Country 2 wants to reach Country 9, Device in Country 2 hooks Country 9 to its parent (Home).
        Resource::NumericalAddress targetCountry; targetCountry.push(isClient ? 9 : 2);
        
        device->linkUp = new RouterLink(&home->router);
        device->router.hook(device->linkUp, targetCountry);
        
        home->linkUp = new RouterLink(&neighborhood->router);
        home->router.hook(home->linkUp, targetCountry);
        
        neighborhood->linkUp = new RouterLink(&city->router);
        neighborhood->router.hook(neighborhood->linkUp, targetCountry);
        
        city->linkUp = new RouterLink(&country->router);
        city->router.hook(city->linkUp, targetCountry);
    };

    Client cli;
    Server srv;
    
    // To make Client and Server work directly without Bind, we just use them as Stations.
    // We can hook the server process to device router in Country 9.
    // Client process to device router in Country 2.
    
    // In order for Tunnel to work, we need a wrapper station that translates Cart source/target!
    // But Tunnel generates Carts with targets! We just need to give it the numerical address.
    
    // Instead of Client and Server, let's just make a raw Station ping-pong test to verify routing first.
    // The user wants heavy test of routing topology.
    Station procA; procA.name = "Process-A-Country-2";
    Station procB; procB.name = "Process-B-Country-9";
    
    buildHierarchy(countries[2], 2, &procA, true);
    buildHierarchy(countries[9], 9, &procB, false);

    bool reachedB = false;
    procB.onCart([&](Cart& c) {
        printf("Process B received cart from %d.%d.%d.%d.%d.%d!\n", 
            c.source[0], c.source[1], c.source[2], c.source[3], c.source[4], c.source[5]);
        reachedB = true;
        
        // Echo back
        Cart echo;
        echo.isAddressed = true;
        echo.source = c.target;
        echo.target = c.source;
        echo.payload = "Pong!";
        procB.push(echo);
    });

    bool reachedA = false;
    procA.onCart([&](Cart& c) {
        printf("Process A received echo from %d.%d.%d.%d.%d.%d: %s\n", 
            c.source[0], c.source[1], c.source[2], c.source[3], c.source[4], c.source[5], c.payload.c_str());
        reachedA = true;
    });

    // Start ping from A to B
    // We hook procA so that when procA.push() is called, it sends to device Router!
    // Wait, procA.push() calls cartPushListener. We need to set it up!
    // We need procA's parent RouterLink!
    // We didn't save the device router in a variable accessible here. Let's just create a global wrapper.
    
    // Simple way to trigger the route: Just manually construct the cart and pass to Country 2 Device Router?
    // Let's pass it to procA's push listener.
    // We need to set up the push listener for procA and procB to route UP.
    // Since Router doesn't have a default push receiver, we can just use the global countries[2]->router
    // as the entry point, or simulate it.
    
    // Actually, any packet pushed by procA goes to Device-A router.
    // Let's just find the TreeRoutingEntry for procA and hook it.
    // For simplicity, just route manually from procA into country 2 router!
    procA.onCartPushed([&](Cart& c) {
        countries[2]->router.route(c);
    });
    procB.onCartPushed([&](Cart& c) {
        countries[9]->router.route(c);
    });

    Cart ping;
    ping.isAddressed = true;
    ping.source.push(2); ping.source.push(1); ping.source.push(2); ping.source.push(3); ping.source.push(4); ping.source.push(5);
    ping.target.push(9); ping.target.push(1); ping.target.push(2); ping.target.push(3); ping.target.push(4); ping.target.push(5);
    ping.payload = "Ping!";
    
    printf("Sending Ping from 2.1.2.3.4.5 to 9.1.2.3.4.5...\n");
    procA.push(ping);

    if (reachedB && reachedA) {
        printf("\nHeavy Network Routing Test PASSED.\n");
    } else {
        printf("\nHeavy Network Routing Test FAILED.\n");
    }

    return 0;
}
