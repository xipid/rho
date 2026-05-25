#include <Lines/Gateway.hpp>
#include <Util/Client.hpp>
#include <Util/Server.hpp>
#include <Lines/Router.hpp>
#include <Collection/Array.hpp>
#include <Collection/Map.hpp>
#include <Collection/String.hpp>
#include <Xi/Time.hpp>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <unistd.h>
#include <thread>
#include <vector>
#include <mutex>

using namespace Xi;
using namespace Rho;
using namespace Lines;

struct AddressPath {
  u32 c = 0;   // country (1-20)
  u32 ci = 0;  // city (1-20)
  u32 n = 0;   // neighborhood (1-20)
  u32 h = 0;   // home (1-20)
  u32 d = 0;   // device (1-20)
  u32 p = 0;   // program (1-20)

  Resource::NumericalAddress toAddress() const {
    Resource::NumericalAddress addr;
    addr.push(c);
    addr.push(ci);
    addr.push(n);
    addr.push(h);
    addr.push(d);
    addr.push(p);
    return addr;
  }

  String toString() const {
    return String((long long)c) + "." + String((long long)ci) + "." +
           String((long long)n) + "." + String((long long)h) + "." +
           String((long long)d) + "." + String((long long)p);
  }
};

struct RunContext {
  Map<String, Gateway*> gatewayCache;
  Array<Station*> globalWires;
  Array<Client*> appClients;
  Array<Client*> linkClients;
  Array<Server*> globalServers;
  Array<bool*> doneFlags;
  Map<u32, Station*> countryWires;
  Gateway* country1GW = nullptr;
  bool country1UpgradeSet = false;

  void hookCountries(Gateway& c1, Gateway& ci, u32 countryID) {
    Station* c1Wire = new Station();
    Station* ciWire = new Station();
    c1Wire->onCartPushed([ciWire](Cart& c) { ciWire->receive(c); });
    ciWire->onCartPushed([c1Wire](Cart& c) { c1Wire->receive(c); });

    globalWires.push(c1Wire);
    globalWires.push(ciWire);

    c1.hook(*c1Wire);
    ci.hook(*ciWire);

    Client* cli = new Client();
    cli->hook(*ciWire, countryID);

    countryWires.put(countryID, c1Wire);

    if (!country1UpgradeSet) {
      country1UpgradeSet = true;
      c1.onUpgrade([this](Packet& pkt, Tunnel& tunnel, Cart& cart) -> RoutingEntry* {
        u32 countryID = (u32)cart.rail;
        printf("[DEBUG] Country 1 server received upgrade request from countryID %u!\n", countryID);
        Station** wirePtr = countryWires.get(countryID);
        if (!wirePtr) {
          printf("[DEBUG] ERROR: countryWires did not contain countryID %u!\n", countryID);
          return nullptr;
        }
        Station* c1Wire = *wirePtr;

        Station* sessStation = new Station();
        String derivedKey = Sec::kdf(tunnel.key, "GatewayStation", 32);
        sessStation->enableSecurity(derivedKey);
        sessStation->name = "GW-Country-Session";

        sessStation->onCart([this](Cart& c) {
          if (country1GW) {
            country1GW->router.route(c);
          }
        });

        sessStation->hook(*c1Wire, cart.rail);

        Packet readyPkt;
        readyPkt.channel = 1;
        readyPkt.payload = "ready";
        tunnel.push(readyPkt);

        RoutingEntry* entry = new RoutingEntry();
        entry->station = sessStation;
        Resource::NumericalAddress ciAddr;
        ciAddr.push(countryID);
        entry->address = ciAddr;
        return entry;
      });
    }

    cli->onAnnounce([cli, countryID](Cart& c) {
      printf("[DEBUG] Country %u link client received announce! Upgrading...\n", countryID);
      cli->upgrade();
      cli->push(Packet("upgrade_request"));
      cli->pushCart();
    });

    cli->onReady([this, &c1, &ci, cli, ciWire, countryID](Packet pkt, Cart cart) {
      printf("[DEBUG] Country %u link client ready!\n", countryID);
      Resource::NumericalAddress parentAddr;
      parentAddr.push(1); // Country 1 address

      Station* sessStation = new Station();
      String derivedKey = Sec::kdf(cli->tunnel->key, "GatewayStation", 32);
      sessStation->enableSecurity(derivedKey);
      sessStation->name = "GW-Country-Session-Client";

      sessStation->onCart([&ci](Cart& c) {
        ci.router.route(c);
      });

      sessStation->hook(*ciWire, cli->rail);
      ci.router.hook(sessStation, parentAddr);

      for (int other = 2; other <= 20; ++other) {
        if (other != (int)countryID) {
          Resource::NumericalAddress otherAddr;
          otherAddr.push(other);
          ci.router.hook(sessStation, otherAddr);
        }
      }

      Gateway::TunnelSession sess;
      sess.tunnel = cli->tunnel;
      sess.sessionStation = sessStation;
      sess.assignedAddress = parentAddr;
      ci.sessions.push(sess);
      cli->unhook();
    });

    printf("[DEBUG] Country %u link client sending probe...\n", countryID);
    cli->probe();
    ci.clients.push(cli);
    linkClients.push(cli);
  }

  Gateway* getOrCreateGateway(const Resource::NumericalAddress& addr) {
    String key;
    for (usz i = 0; i < addr.size(); ++i) {
      if (i > 0) key += ".";
      key += String((long long)addr[i]);
    }

    if (gatewayCache.has(key)) {
      return *gatewayCache.get(key);
    }

    Gateway* gw = new Gateway();
    gw->address = addr;
    gatewayCache.put(key, gw);

    if (addr.size() == 1) {
      u32 countryID = (u32)addr[0];
      if (countryID > 1) {
        Resource::NumericalAddress c1Addr; c1Addr.push(1);
        Gateway* c1 = getOrCreateGateway(c1Addr);
        country1GW = c1;
        hookCountries(*c1, *gw, countryID);
      }
    } else if (addr.size() > 1) {
      Resource::NumericalAddress parentAddr;
      for (usz i = 0; i < addr.size() - 1; ++i) {
        parentAddr.push(addr[i]);
      }
      Gateway* parent = getOrCreateGateway(parentAddr);

      u32 childPort = (u32)addr[addr.size() - 1];

      Station* parentWire = new Station();
      Station* childWire = new Station();
      parentWire->onCartPushed([childWire](Cart& c) { childWire->receive(c); });
      childWire->onCartPushed([parentWire](Cart& c) { parentWire->receive(c); });

      globalWires.push(parentWire);
      globalWires.push(childWire);

      parent->hook(*parentWire);
      gw->hook(*childWire);

      Client* cli = new Client();
      cli->password = "descendant_pwd";
      cli->hook(*childWire, childPort);

      parent->onUpgrade([](Packet& pkt, Tunnel& tunnel, Cart& cart) -> RoutingEntry* {
        const String* pwd = cart.meta.get(Meta::Password);
        if (!pwd || *pwd != "descendant_pwd") {
          tunnel.destroy();
        }
        return nullptr;
      });

      cli->onAnnounce([cli](Cart& c) {
        cli->upgrade();
        cli->push(Packet("upgrade_request"));
        cli->pushCart();
      });

      cli->onReady([this, parent, gw, cli, childWire, key](Packet pkt, Cart cart) {
        printf("[DEBUG] Descendant gateway %s link client ready!\n", key.c_str());
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
          gw->address = childAddr;

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

          sessStation->onCart([gw](Cart& c) {
            if (c.isAddressed && c.target.size() > 0) {
              gw->router.route(c);
            }
          });

          sessStation->hook(*childWire, cli->rail);

          if (parentAddr.size() > 0) {
            gw->router.hook(sessStation, parentAddr);
          }

          Gateway::TunnelSession sess;
          sess.tunnel = cli->tunnel;
          sess.sessionStation = sessStation;
          sess.assignedAddress = parentAddr;
          gw->sessions.push(sess);
        }
        cli->unhook();
      });

      cli->probe();
      gw->clients.push(cli);
      linkClients.push(cli);
    }

    return gw;
  }

  void cleanup() {
    for (usz i = 0; i < appClients.size(); ++i) {
      delete appClients[i];
    }
    appClients.clear();
    linkClients.clear();

    for (usz i = 0; i < globalServers.size(); ++i) {
      delete globalServers[i];
    }
    globalServers.clear();

    for (auto &kv : gatewayCache) {
      Gateway* gw = kv.value;
      delete gw;
    }
    gatewayCache.clear();

    for (usz i = 0; i < globalWires.size(); ++i) {
      delete globalWires[i];
    }
    globalWires.clear();

    for (usz i = 0; i < doneFlags.size(); ++i) {
      delete doneFlags[i];
    }
    doneFlags.clear();

    countryWires.clear();
    country1GW = nullptr;
  }
};

void runSingleSimulation(int run, String testPayload, String testHash, std::mutex& printMutex, bool& allSuccess) {
  RunContext ctx;
  unsigned int seed = (unsigned int)(Xi::millis() + run * 1000);

  // Construct 20 random client & server paths
  for (int pIdx = 0; pIdx < 20; ++pIdx) {
    AddressPath cliPath;
    cliPath.c  = 1 + (rand_r(&seed) % 20);
    cliPath.ci = 1 + (rand_r(&seed) % 20);
    cliPath.n  = 1 + (rand_r(&seed) % 20);
    cliPath.h  = 1 + (rand_r(&seed) % 20);
    cliPath.d  = 1 + (rand_r(&seed) % 20);
    cliPath.p  = 1 + (rand_r(&seed) % 20);

    AddressPath srvPath;
    do {
      srvPath.c  = 1 + (rand_r(&seed) % 20);
      srvPath.ci = 1 + (rand_r(&seed) % 20);
      srvPath.n  = 1 + (rand_r(&seed) % 20);
      srvPath.h  = 1 + (rand_r(&seed) % 20);
      srvPath.d  = 1 + (rand_r(&seed) % 20);
      srvPath.p  = 1 + (rand_r(&seed) % 20);
    } while (srvPath.c == cliPath.c && srvPath.ci == cliPath.ci);

    if (run == 1 && pIdx == 0) {
      printf("[DEBUG] Client path: %s, Server path: %s\n", cliPath.toString().c_str(), srvPath.toString().c_str());
    }

    Gateway* clientGW = ctx.getOrCreateGateway(cliPath.toAddress());
    Gateway* serverGW = ctx.getOrCreateGateway(srvPath.toAddress());

    // Bind ports for application endpoints
    Station* clientPortSt = clientGW->bind(100);
    Station* serverPortSt = serverGW->bind(200);

    // Create Server endpoint
    Server* srv = new Server();
    srv->hook(*serverPortSt);
    srv->onPacket([pIdx](Packet pkt, Tunnel& tunnel, Cart cart) {
      printf("[DEBUG] Server received packet of size %zu, echoing back.\n", pkt.payload.size());
      tunnel.push(pkt);
    });
    ctx.globalServers.push(srv);

    // Construct destination routing address: [server address, port]
    Resource::NumericalAddress destAddr = srvPath.toAddress();
    destAddr.push(200);

    // Create Client endpoint
    Client* cli = new Client();
    cli->hook(*clientPortSt, destAddr);

    bool* done = new bool(false);
    ctx.doneFlags.push(done);

    cli->onAnnounce([cli, testPayload, pIdx](Cart& c) {
      printf("[DEBUG] App Client %d received announce! Upgrading & pushing payload...\n", pIdx);
      cli->upgrade();
      cli->push(Packet(testPayload));
      cli->pushCart();
    });

    cli->onReady([pIdx](Packet pkt, Cart cart) {
      printf("[DEBUG] App Client %d ready!\n", pIdx);
    });

    cli->onPacket([done, testHash, pIdx](Packet pkt) {
      printf("[DEBUG] App Client %d received response packet of size %zu.\n", pIdx, pkt.payload.size());
      String recvHash = Sec::hash(pkt.payload, 32);
      if (recvHash.constantTimeEquals(testHash)) {
        printf("[DEBUG] App Client %d hash matches!\n", pIdx);
        *done = true;
      } else {
        printf("[DEBUG] App Client %d hash mismatch!\n", pIdx);
      }
    });

    cli->probe();
    ctx.appClients.push(cli);
  }

  // Cooperative event loop running all pairs concurrently
  int ticks = 0;
  bool allDone = false;
  while (!allDone && ticks < 2000) {
    ticks++;

    // Periodically probe App Clients that haven't received announces yet (every 10 ticks for faster debug)
    if (ticks % 10 == 0) {
      for (usz i = 0; i < ctx.appClients.size(); ++i) {
        if (ctx.appClients[i]->serverEphemeralKey.isEmpty()) {
          printf("[DEBUG] Tick %d: App Client %zu still waiting for announce. Probing again...\n", ticks, i);
          ctx.appClients[i]->probe();
        }
      }
    }

    // Update all active client processes
    for (usz i = 0; i < ctx.appClients.size(); ++i) {
      ctx.appClients[i]->update();
      if (ctx.appClients[i]->tunnel) {
        ctx.appClients[i]->pushCart();
      }
    }
    for (usz i = 0; i < ctx.linkClients.size(); ++i) {
      ctx.linkClients[i]->update();
      if (ctx.linkClients[i]->tunnel) {
        ctx.linkClients[i]->pushCart();
      }
    }

    // Update all active server processes
    for (usz i = 0; i < ctx.globalServers.size(); ++i) {
      ctx.globalServers[i]->update();
    }

    // Update all active Gateways and their sessions
    for (auto &kv : ctx.gatewayCache) {
      Gateway* gw = kv.value;
      gw->update();
    }

    // Update all wire stations
    for (usz i = 0; i < ctx.globalWires.size(); ++i) {
      ctx.globalWires[i]->update();
    }

    // Check if all clients successfully received the echo
    allDone = true;
    for (usz i = 0; i < ctx.doneFlags.size(); ++i) {
      if (!(*ctx.doneFlags[i])) {
        allDone = false;
        break;
      }
    }

    usleep(1000); // 1ms tick delay
  }

  if (allDone) {
    std::lock_guard<std::mutex> lock(printMutex);
    printf("  ✓ Run %d completed successfully in %d ticks.\n", run, ticks);
  } else {
    std::lock_guard<std::mutex> lock(printMutex);
    printf("  ✗ Run %d timed out after %d ticks.\n", run, ticks);
    allSuccess = false;
  }

  ctx.cleanup();
}

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("====================================================\n");
  printf("    Massive 64-Million-Node Routing Simulation      \n");
  printf("====================================================\n");

  srand((unsigned int)Xi::millis());

  // Generate random 64KB test payload
  String testPayload;
  for (int i = 0; i < 64 * 1024; ++i) {
    testPayload.push((u8)('A' + (rand() % 26)));
  }
  String testHash = Sec::hash(testPayload, 32);

  std::mutex printMutex;
  bool allSuccess = true;

  const int totalRuns = 50;
  const int batchSize = 10;
  for (int batchStart = 1; batchStart <= totalRuns; batchStart += batchSize) {
    std::vector<std::thread> threads;
    for (int run = batchStart; run < batchStart + batchSize && run <= totalRuns; ++run) {
      threads.push_back(std::thread(runSingleSimulation, run, testPayload, testHash, std::ref(printMutex), std::ref(allSuccess)));
    }
    for (auto& t : threads) {
      t.join();
    }
  }

  if (allSuccess) {
    printf("\n====================================================\n");
    printf("  ALL 50 HEAVY PARALLEL TESTS COMPLETED SUCCESSFULLY!  \n");
    printf("====================================================\n");
    return 0;
  } else {
    printf("\n====================================================\n");
    printf("  SOME HEAVY PARALLEL TESTS FAILED!                   \n");
    printf("====================================================\n");
    return 1;
  }
}
