#include <Lines/Bind.hpp>
#include <Util/Server.hpp>
#include <Rho/Tunnel.hpp>
#include <Lines/Meterer.hpp>
#include <unistd.h>
#include <cstdio>

using namespace Xi;
using namespace Rho;
using namespace Lines;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    // 1. String construction for Bind
    Bind srvBind("127.0.0.1:8080");
    
    // 2. Test Meterer
    Meterer meter;
    meter.inQuotaPerSecond = 10 * 1024 * 1024; // 10 MB/s
    meter.outQuotaPerSecond = 10 * 1024 * 1024; // 10 MB/s
    meter.hook(srvBind);
    
    // 3. Test Server
    Server srv;
    srv.hook(meter); // Hook Server to Meterer, which is hooked to Bind

    srv.onPacket([&](Packet pkt, Tunnel& clientTunnel, Cart cart) {
        printf("Server Tunnel received %zu bytes!\n", pkt.payload.size());
        
        // 4. Test Tunnel features
        printf("  - canPush(): %s\n", clientTunnel.canPush() ? "true" : "false");
        
        clientTunnel.push(pkt.payload); // Echo back
    });



    printf("Server listening on 127.0.0.1:8080...\n");

    while (true) {
        srvBind.update();
        srv.update();
        usleep(10000);
    }
    return 0;
}
