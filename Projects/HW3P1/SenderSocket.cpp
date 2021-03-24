#include "SenderSocket.h"

using namespace std;

SenderSocket::SenderSocket() {
    timer = clock();
    memset(&server, 0, sizeof(server));
}

DWORD SenderSocket::Open(string targetHost, int port, int senderWindow, LinkProperties* lp) {
    // Initialize and create socket for UDP
    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        WSACleanup();
        return -1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;

    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    // Generate SYN packet
    SenderSynHeader* packet = new SenderSynHeader();
    packet->lp.bufferSize = senderWindow + 3;
    packet->lp.pLoss[0] = lp->pLoss[0];
    packet->lp.pLoss[1] = lp->pLoss[1];
    packet->lp.RTT = lp->RTT;
    packet->lp.speed = lp->speed;

    packet->sdh.flags.reserved = 0;
    packet->sdh.flags.magic = MAGIC_PROTOCOL;
    packet->sdh.flags.SYN = 1;
    packet->sdh.flags.FIN = 0;
    packet->sdh.flags.ACK = 0;
    packet->sdh.seq = 0;

    // Get server IP
    DWORD serverIP = inet_addr(targetHost.c_str());
    if (serverIP == INADDR_NONE) {
        if ((remote = gethostbyname(targetHost.c_str())) == NULL) {
            closesocket(sock);
            WSACleanup();
            return 3;
        }
        memcpy((char*)&server.sin_addr, remote->h_addr, remote->h_length);
    }
    else {
        server.sin_addr.S_un.S_addr = serverIP;
    }

    server.sin_family = AF_INET;
    server.sin_port = port;

    long RTO_sec = 1;
    long RTO_usec = 0;
    int available;
    
    // Send packet
    for (int i = 1; i <= 3; i++) {
        printf("[%.3f] --> ", (double)(clock() - timer) / CLOCKS_PER_SEC);

        if (sendto(sock, (char*)packet, sizeof(SenderSynHeader), 0, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
            printf("failed sendto with %d\n", WSAGetLastError());
            closesocket(sock);
            WSACleanup();
            return -1;
        }
        
        printf("SYN 0 (attempt %d of 3, RTO %.3f) to %s\n", i, (double)RTO_sec + (double)RTO_usec / 1e6, targetHost.c_str());

        timeval timeout;
        timeout.tv_sec = RTO_sec;
        timeout.tv_usec = RTO_usec;

        fd_set fdRead;
        FD_ZERO(&fdRead);
        FD_SET(sock, &fdRead);

        if ((available = select(0, &fdRead, NULL, NULL, &timeout)) > 0) {
            printf("Response available ");
            ReceiverHeader rh;
            int responseSize;

            struct sockaddr_in res;
            int size = sizeof(res);

            if ((responseSize = recvfrom(sock, (char*)&rh, sizeof(ReceiverHeader), 0, (struct sockaddr*)&res, &size)) == SOCKET_ERROR) {
                printf("failed recvfrom with %d\n", WSAGetLastError());
                return -1;
            }
            break;
        }
        else {
            printf("%d\n", available);
        }
    }
    delete packet;

    return STATUS_OK;
}