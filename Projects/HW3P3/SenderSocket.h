#pragma once
#pragma comment(lib, "ws2_32")

#include "pch.h"

using namespace std;

#define IPLENGTH 15

#define SYNTYPE 0
#define FINTYPE 1
#define DATATYPE 2

#define FORWARD_PATH 0
#define RETURN_PATH 1 
#define MAGIC_PROTOCOL 0x8311AA 

// Possible status codes
#define STATUS_OK 0 // no error
#define ALREADY_CONNECTED 1 // second call to ss.Open() without closing connection
#define NOT_CONNECTED 2 // call to ss.Send()/Close() without ss.Open()
#define INVALID_NAME 3 // ss.Open() with targetHost that has no DNS entry
#define FAILED_SEND 4 // sendto() failed in kernel
#define TIMEOUT 5 // timeout after all retx attempts are exhausted
#define FAILED_RECV 6 // recvfrom() failed in kernel

#define MAGIC_PORT 22345 // receiver listens on this port
#define MAX_PKT_SIZE (1500-28) // maximum UDP packet size accepted by receiver

#define MAX_RTX 50

#pragma pack(push, 1)
class LinkProperties {
public:
    // transfer parameters
    float RTT; // propagation RTT (in sec)
    float speed; // bottleneck bandwidth (in bits/sec)
    float pLoss[2]; // probability of loss in each direction
    DWORD bufferSize; // buffer size of emulated routers (in packets)
    LinkProperties() { memset(this, 0, sizeof(*this)); }
};

class Flags {
public:
    DWORD reserved : 5; // must be zero
    DWORD SYN : 1;
    DWORD ACK : 1;
    DWORD FIN : 1;
    DWORD magic : 24;
    Flags() { memset(this, 0, sizeof(*this)); magic = MAGIC_PROTOCOL; }
};

class SenderDataHeader {
public:
    Flags flags;
    DWORD seq; // must begin from 0
};

class SenderSynHeader {
public:
    SenderDataHeader sdh;
    LinkProperties lp;
};

class ReceiverHeader {
public:
    Flags flags;
    DWORD recvWnd; // receiver window for flow control (in pkts)
    DWORD ackSeq; // ack value = next expected sequence
};

class Packet {
public:
    int type; // SYN, FIN, data
    int size; // for the worker thread
    clock_t txTime; // transmission time
    SenderDataHeader sdh; // header
    char data[MAX_PKT_SIZE]; // payload
};
#pragma pack(pop)

class SenderSocket   {
public:
    int numTO = 0;
    int numRtx = 0;

    int windowSize = 0;
    int lastReleased;
    int effectiveWindow;

    SOCKET sock;

    HANDLE quit;
    HANDLE stats;
    HANDLE workers;
    HANDLE full;
    HANDLE empty;
    HANDLE receive;
    HANDLE complete;

    double estRTT = -1;
    double devRTT = -1;

    double alpha = 0.125;
    double beta = 0.25;

    int base;
    int nextSeq;
    int nextSend;

    bool open = false;

    Packet* packets = NULL;

    struct sockaddr_in local;
    struct sockaddr_in server;
    struct hostent* remote;

    int timeout;
    double RTO;

    SenderSocket();
    ~SenderSocket();
    DWORD Open(string host, int port, int senderWindow, LinkProperties* lp);
    DWORD Close(double timeElapsed);
    DWORD Send(char* ptr, int bytes);
    void ReceiveACK(int* dup, int* rtx);

    static DWORD WINAPI Worker(LPVOID self);
    static DWORD WINAPI Stats(LPVOID self);
};

