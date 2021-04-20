#pragma comment(lib, "ws2_32")

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "pch.h"
#include "SenderSocket.h"
#include "Checksum.h"

using namespace std;

struct Parameters {
public:
    SenderSocket* ss;
    char* charBuf;
    UINT64 byteBufferSize;
};

DWORD WINAPI SendThread(LPVOID params) {
    Parameters* p = (Parameters*)params;
    DWORD status;

    UINT off = 0;
    while (off < p->byteBufferSize) {
        int bytes = min(p->byteBufferSize - off, MAX_PKT_SIZE - sizeof(SenderDataHeader));

        if ((status = p->ss->Send(p->charBuf + off, bytes)) != STATUS_OK) {
            printf("Main:\tsend failed with status %d\n", status);
            return 0;
        }

        off += bytes;
    }

    return 0;
}

int main(int argc, char** argv) {
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

    printf("Main:\tsender W = %d, RTT %.3f sec, loss %g / %g, link %d Mbps\n",
        senderWindow,
        lp.RTT,
        lp.pLoss[FORWARD_PATH],
        lp.pLoss[RETURN_PATH],
        atoi(argv[7]));

    int power = atoi(argv[2]);
    printf("Main:\tinitializing DWORD array with 2^%d elements... ", power);

    clock_t timer = clock();
    UINT64 dwordBufSize = (UINT64)1 << power;
    DWORD* dwordBuf = new DWORD[dwordBufSize]; // user-requested buffer
    for (UINT64 i = 0; i < dwordBufSize; i++) // required initialization
        dwordBuf[i] = i;

    printf(" done in %d ms\n", 1000 * (clock() - timer) / CLOCKS_PER_SEC);

    SenderSocket* ss = new SenderSocket();;
    Checksum cs;
    DWORD status;

    clock_t openClock = clock();
    if ((status = ss->Open(targetHost, MAGIC_PORT, senderWindow, &lp)) != STATUS_OK) {
        printf("Main:\tconnect failed with status %d\n", status);
        return 0;
    }

    printf("Main:\tconnected to %s in %.3f sec, pkt size %d bytes\n", 
        targetHost.c_str(), 
        (float)(clock() - openClock) / CLOCKS_PER_SEC, 
        MAX_PKT_SIZE);

    char* charBuf = (char*)dwordBuf;
    UINT64 byteBufferSize = dwordBufSize << 2;

    Parameters p;
    p.ss = ss;
    p.charBuf = charBuf;
    p.byteBufferSize = byteBufferSize;

    clock_t start = clock();

    HANDLE send = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SendThread, &p, 0, NULL);
    WaitForSingleObject(send, INFINITE);

    clock_t end = clock();
    double timeElapsed = (double)(end - start) / CLOCKS_PER_SEC;

    if ((status = ss->Close(timeElapsed)) != STATUS_OK) {
        printf("Main:\tclose failed with status %d\n", status);
        return 0;
    }
	DWORD check = cs.CRC32((unsigned char*)charBuf, byteBufferSize);
    printf("Main:\ttransfer finished in %.3f sec, %.2f Kbps, checksum %X\n", 
        (ss->transferEnd - ss->transferStart) / (double)CLOCKS_PER_SEC, 
        (double)dwordBufSize * 32 / (double)(1000 * ((ss->transferEnd - ss->transferStart) / (double)CLOCKS_PER_SEC)), 
        check);
    printf("Main:\testRTT %.3f, ideal rate %.2f Kbps\n", ss->estRTT, ss->windowSize * 8 * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) / (ss->estRTT * 1000));
}
