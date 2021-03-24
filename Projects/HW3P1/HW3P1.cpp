#pragma comment(lib, "ws2_32")

#include "pch.h"
#include "SenderSocket.h"

using namespace std;

int main(int argc, char** argv) {
    /** Step 1: Parse command line argument inputs
    *
    *       Extract all command line argument inputs and
    *       provide proper usage instructions if user doesn't
    *       have the correct amount of arguments present
    */

    LinkProperties lp;

    if (argc != 8) {
        printf("Incorrect Usage: HW3P1.exe <host> <buffer size exponent> <sender window size> <RTT> <loss rate forward> <loss rate backward> <bottleneck-link-speed>\n");
        return 0;
    }

    lp.RTT = atof(argv[4]);
    lp.speed = 1e6 * atof(argv[7]);
    lp.pLoss[FORWARD_PATH] = atof(argv[5]);
    lp.pLoss[RETURN_PATH] = atof(argv[6]);

    string targetHost = argv[1];
    int senderWindow = atoi(argv[3]);

    if (senderWindow <= 0) {
        return 0;
    }

    printf("Main: sender W = %d, RTT %.3f sec, loss %g / %g, link %d Mbps\n", 
        senderWindow,
        lp.RTT,
        lp.pLoss[FORWARD_PATH],
        lp.pLoss[RETURN_PATH],
        atoi(argv[7]));

    int power = atoi(argv[2]);
    printf("Main: initializing DWORD array with 2^%d elements... ", power);

    clock_t timer = clock();
    UINT64 dwordBufSize = (UINT64)1 << power;
    DWORD* dwordBuf = new DWORD[dwordBufSize]; // user-requested buffer
    for (UINT64 i = 0; i < dwordBufSize; i++) // required initialization
        dwordBuf[i] = i;
    
    printf(" done in %d ms\n", (clock() - timer) / CLOCKS_PER_SEC);

    /** Step 2: Open UDP socket and send SYN packet to server
    *       
    *       Open a UDP connection to the server host and send 
    *       SYN packet (must also generate the SYN packet)
    */
    SenderSocket ss;
    DWORD status;
    if ((status = ss.Open(targetHost, MAGIC_PORT, senderWindow, &lp)) != STATUS_OK) {
        return 0;
    }
}
