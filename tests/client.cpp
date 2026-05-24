#include <Lines/Bind.hpp>
#include <Util/Client.hpp>
#include <Rho/Tunnel.hpp>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

using namespace Xi;
using namespace Rho;
using namespace Lines;

int main() {
    Bind cliBind("127.0.0.1:0"); // Bind to random port
    Client cli;
    cli.hook(cliBind, "127.0.0.1:8080");

    bool connected = false;
    bool received = false;
    String fullRecvData;

    printf("Generating 128 KB of random data...\n");
    String mbData;
    srand(12345);
    for (int i = 0; i < 128 * 1024; i++) mbData.push((u8)(rand() % 256));
    String mbHash = Sec::hash(mbData, 32);

    cli.onPacket([&](Packet pkt) {
        fullRecvData += pkt.payload;
        printf("Client Tunnel received %zu bytes! Total: %zu\n", pkt.payload.size(), fullRecvData.size());
        if (fullRecvData.size() == 128 * 1024) {
            printf("Client Tunnel received full 128 KB!\n");
            
            int diffCount = 0;
            for (usz i = 0; i < 128 * 1024; ++i) {
                if (fullRecvData[i] != mbData[i]) {
                    if (diffCount < 10) printf("Diff at byte %zu: expected %d, got %d\n", i, mbData[i], fullRecvData[i]);
                    diffCount++;
                }
            }
            if (diffCount == 0) {
                printf("Data matches perfectly.\n");
            } else {
                printf("Total diffs: %d\n", diffCount);
            }
            
            String recvHash = Sec::hash(fullRecvData, 32);
            if (recvHash.constantTimeEquals(mbHash)) {
                printf("Hash MATCHES! Success.\n");
            } else {
                printf("Hash MISMATCH!\n");
            }
            received = true;
        }
    });

    cli.onAnnounce([&](Cart& c) {
        printf("Client received Announce, upgrading...\n");
        cli.upgrade();
        connected = true;
        
        // We will push the rest in the main loop
    });

    cli.probe();

    int totalPushed = 0;
    for (int i = 0; i < 2000; i++) {
        cliBind.update();
        cli.update();
        
        if (connected && cli.tunnel) {
            while (cli.tunnel->canPush() && totalPushed < 128) {
                String chunk = mbData.substring(totalPushed * 1024, (totalPushed + 1) * 1024);
                cli.push(Packet(chunk));
                totalPushed++;
                cli.pushCart();
            }
        }
        
        if (received) break;
        usleep(10000);
    }
    return 0;
}
