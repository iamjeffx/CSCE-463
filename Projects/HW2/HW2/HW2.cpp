/*  HW2: DNS Lookup
* 
*   Author: Jeffrey Xu
*   Email: jeffreyxu@tamu.edu
*   Date: 03/09/2021
*   Professor Dmitri Loguinov
*   CSCE 463
*/

#include "pch.h"

#pragma comment(lib, "Ws2_32.lib")

#define MAX_DNS_SIZE 512
#define MAX_ATTEMPTS 3

// Flags
#define DNS_QUERY (0 << 15) /* 0 = query; 1 = response */
#define DNS_RESPONSE (1 << 15)
#define DNS_STDQUERY (0 << 11) /* opcode - 4 bits */
#define DNS_AA (1 << 10) /* authoritative answer */
#define DNS_TC (1 << 9) /* truncated */
#define DNS_RD (1 << 8) /* recursion desired */
#define DNS_RA (1 << 7) /* recursion available */

// Response codes
#define DNS_OK 0 /* success */
#define DNS_FORMAT 1 /* format error (unable to interpret) */
#define DNS_SERVERFAIL 2 /* can’t find authority nameserver */
#define DNS_ERROR 3 /* no DNS entry */
#define DNS_NOTIMPL 4 /* not implemented */
#define DNS_REFUSED 5 /* server refused the query */

// DNS Query types
#define DNS_A 1 /* name -> IP */
#define DNS_NS 2 /* name server */
#define DNS_CNAME 5 /* canonical name */
#define DNS_PTR 12 /* IP -> name */
#define DNS_HINFO 13 /* host info/SOA */
#define DNS_MX 15 /* mail exchange */
#define DNS_AXFR 252 /* request for zone transfer */
#define DNS_ANY 255 /* all records */ 

// DNS Class
#define DNS_INET 1

using namespace std;

#pragma pack(push, 1)

// Query header class
class QueryHeader {
public:
    USHORT qType;
    USHORT qClass;
};

// Fixed DNS header class
class FixedDNSHeader {
public:
    USHORT ID;
    USHORT flags;
    USHORT questions;
    USHORT answers;
    USHORT authority;
    USHORT additional;
};

// Response RR
class FixedRR {
public:
    u_short qType;
    u_short qClass;
    u_short TTL1;
    u_short TTL2;
    u_short length;
};

#pragma pack(pop)

void makeDNSQuestion(char* buf, char* host) {
    // Set delimiter
    const char* dot = ".";

    char* current;
    char* tempHost = host;
    int nextSize = 0;
    int i = 0;

    while (true) {
        current = strstr(tempHost, dot);

        if (current == NULL) {
            buf[i++] = strlen(tempHost);
            memcpy(buf + i, tempHost, strlen(tempHost));
            i += strlen(tempHost);
            buf[i] = 0;
            break;
        }
        else {
            nextSize = current - tempHost;
            buf[i++] = nextSize;
            memcpy(buf + i, tempHost, nextSize);
            i += nextSize;
            tempHost = current + 1;
        }
    }
}

string reverseIP(string currentIP) {
    char delimiter = '.';
    size_t pos = 0; 
    string token;

    string outputIP = "";
    bool first = true;

    while ((pos = currentIP.find(delimiter)) != string::npos) {
        token = currentIP.substr(0, pos);
        if (first) {
            outputIP = token;
            first = false;
        }
        else {
            outputIP = token + "." + outputIP;
        }
        
        currentIP.erase(0, pos + 1);
    }

    outputIP = currentIP + "." + outputIP;

    return outputIP;
}

char* jump(char* buffer, char* current, char* header, int totalSize) {
    // End of block reached
    if (current[0] == 0) {
        return current + 1;
    }

    // Jump is required
    if ((unsigned char)current[0] >= 0xC0) {
        int offset = (((unsigned char)current[0] & 0x3F) << 8) + (unsigned char)current[1];

        if (buffer + offset - header > 0 && buffer + offset - header < sizeof(FixedDNSHeader)) {
            printf("\t++ Invalid record: jump into fixed DNS header\n");
            WSACleanup();
            exit(0);
        }
        else if (current + 1 - buffer >= totalSize) {
            printf("\t++ Invalid record: truncated jump offset\n");
            WSACleanup();
            exit(0);
        }
        else if (offset > totalSize) {
            printf("\t++ Invalid record: jump beyond packet boundary\n");
            WSACleanup();
            exit(0);
        }
        else if (*((unsigned char*)(buffer + offset)) >= 0XC0) {
            printf("\t++ Invalid record: jump loop\n");
            WSACleanup();
            exit(0);
        }

        jump(buffer, buffer + offset, header, totalSize);
        return current + 2;
    }
    // Normal block
    else {
        int blockSize = current[0];

        // End of block reached
        if (blockSize == 0) {
            return 0;
        }
        current++;

        char temp[MAX_DNS_SIZE];
        memcpy(temp, current, blockSize);

        // Check if block goes past buffer
        if (current + blockSize - buffer >= totalSize) {
            // Output current block (within the buffer)
            temp[current + blockSize - buffer - totalSize] = '\0';
            printf("%s\n", temp);

            // Output error statement
            printf("\t++ Invalid record: truncated name\n");

            // Cleanup and exit the program
            WSACleanup();
            exit(0);
        }

        temp[blockSize] = '\0';
        printf(temp);
        current += blockSize;

        if (current[0] != 0) {
            printf(".");
        }
        else {
            printf(" ");
        }

        current = jump(buffer, current, header, totalSize);
        return current;
    }
}

int main(int argc, char** argv) {
    /*  Step 1: Extract command line arguments
     *
     *  There must be three arguments as input: the first one
     *  is the query and the second one is the DNS server to
     *  perform the lookup
    */

    // Case where incorrect number of command line arguments are provided
    if (argc != 3) {
        printf("Incorrect number of arguments\nUsage: HW2.exe <query> <DNS IP>\n");
        return 0;
    }

    // Set arguments
    string query_input = argv[1];
    string DNSIP = argv[2];

    // Output lookup to be performed
    printf("Lookup  : %s\n", query_input.c_str());

    DWORD IP_addr = inet_addr(query_input.c_str());


    /*  Step 2: Create the query
    *
    *   Now we need to generate the query that we want to send to the server.
    *   Note that this includes initializing the query header and fixed DNS
    *   header.
    */

    // Initialize query type
    bool isA = false;
    if (IP_addr == INADDR_NONE) {
        isA = true;
    }
    else {
        query_input = reverseIP(query_input) + ".in-addr.arpa";
    }

    // Initialize packet
    int packetSize = strlen(query_input.c_str()) + 2 + sizeof(FixedDNSHeader) + sizeof(QueryHeader);
    char* packet = new char[packetSize];

    // Fixed field initialization
    FixedDNSHeader* fdh = (FixedDNSHeader*)packet;
    QueryHeader* qh = (QueryHeader*)(packet + packetSize - sizeof(QueryHeader));

    if (isA) {
        qh->qType = htons(DNS_A);
    }
    else {
        qh->qType = htons(DNS_PTR);
    }

    // Initialize query header
    qh->qClass = htons(DNS_INET);

    // Initialize header values
    fdh->ID = htons(1);
    fdh->flags = htons(DNS_QUERY | DNS_RD | DNS_STDQUERY);
    fdh->questions = htons(1);
    fdh->answers = htons(0);
    fdh->authority = htons(0);
    fdh->additional = htons(0);

    // Output query
    makeDNSQuestion((char*)(fdh + 1), (char*)query_input.c_str());
    printf("Query   : %s, type %d, TXID 0x%04d\n", query_input.c_str(), htons(qh->qType), htons(fdh->ID));

    // Output DNS server IP
    printf("Server  : %s\n", DNSIP.c_str());
    printf("********************************\n");


    /*  Step 3: Send query through UDP to DNS server
    *
    *   Here, we initialize the socket and send the UDP packet to the
    *   destination server.
    */

    printf("Attempt 0 with %d bytes... ", packetSize);
    clock_t timer = clock();

    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        WSACleanup();
        return 0;
    }

    // Initialize socket and check if socket is valid
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        closesocket(sock);
        WSACleanup();
        return 0;
    }

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(0);

    // Bind socket and check for errors
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return 0;
    }

    // Set up socket to send UDP packet
    struct sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_addr.S_un.S_addr = inet_addr(DNSIP.c_str()); // server’s IP
    remote.sin_port = htons(53); // DNS port on server

    // Send UDP packet
    if (sendto(sock, packet, packetSize, 0, (struct sockaddr*)&remote, sizeof(remote)) == SOCKET_ERROR) {
        printf("Socket send error %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 0;
    }


    /*  Step 4: Parse response from server
    *
    *   In this step, we need to extract the response and parse
    *   it, extracting the answers and matching them with the
    *   questions.
    */

    fd_set fd;
    FD_ZERO(&fd);
    FD_SET(sock, &fd);
    timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    int available;

    // Successful response
    if ((available = select(0, &fd, NULL, NULL, &timeout)) > 0) {
        char buf[MAX_DNS_SIZE];
        struct sockaddr_in response;
        int size = sizeof(response);
        int responseBytes = 0;

        // Extract response from DNS server
        if ((responseBytes = recvfrom(sock, buf, MAX_DNS_SIZE, 0, (struct sockaddr*)&response, &size)) == SOCKET_ERROR) {
            printf("Socket receive error %d\n", WSAGetLastError());
            WSACleanup();
            closesocket(sock);
            return 0;
        }

        // Proper response
        printf(" response in %d ms with %d bytes\n", (int)((1000 * ((double)clock() - (double)timer)) / (double)CLOCKS_PER_SEC), responseBytes);

        // Check for correct response from DNS server and port
        if (response.sin_addr.s_addr != remote.sin_addr.s_addr || response.sin_port != remote.sin_port) {
            printf("Invalid IP or port\n");
            WSACleanup();
            delete packet;
            return 0;
        }

        // Check for valid packet
        char* res = strstr(buf, packet);
        char* resHeader = res;
        if (res == NULL) {
            printf(" Fixed DNS Header not found\n");
            WSACleanup();
            closesocket(sock);
            delete packet;
            return 0;
        }
        res += sizeof(FixedDNSHeader);

        FixedDNSHeader* fdhRes = (FixedDNSHeader*)buf;

        // Check if packet is smaller than fixed DNS header
        if (responseBytes < sizeof(FixedDNSHeader)) {
            printf("\t++ Invalid reply: packet smaller than fixed DNS Header\n");
            WSACleanup();
            closesocket(sock);
            delete packet;
            return 0;
        }

        printf("\tTXID 0x%04x flags 0x%04x questions %d answers %d authority %d additional %d\n",
            htons(fdhRes->ID),
            htons(fdhRes->flags),
            htons(fdhRes->questions),
            htons(fdhRes->answers),
            htons(fdhRes->authority),
            htons(fdhRes->additional));

        // Check for valid ID
        if (fdhRes->ID != fdh->ID) {
            printf("\t++ invalid reply: TXID mismatch, sent 0x%04x, received 0x%04x\n", htons(fdh->ID), htons(fdhRes->ID));
            WSACleanup();
            closesocket(sock);
            delete packet;
            return 0;
        }

        // Check Rcode
        int Rcode = htons(fdhRes->flags) & 0X000f;
        if ((Rcode) == 0) {
            printf("\tsucceeded with Rcode = %d\n", Rcode);
        }
        else {
            printf("\tfailed with Rcode = %d\n", Rcode);
            delete packet;
            WSACleanup();
            return 0;
        }

        // Questions are present
        if (htons(fdhRes->questions) > 0) {
            printf("\t------------ [questions] ----------\n");

            for (int i = 0; i < htons(fdhRes->questions); i++) {
                printf("\t\t");

                // Flag to determine whether to print dot or not
                bool printDot = false;

                // Output response blocks
                while (true) {
                    int blockSize = res[0];

                    // End of lookup reached -> break out of loop
                    if (blockSize == 0) {
                        printf(" ");
                        res++;
                        break;
                    }

                    // Not first block to be printed
                    if (printDot) {
                        printf(".");
                    }
                    else {
                        printDot = true;
                    }

                    // Update current pointer
                    res++;

                    // Copy block into temp buffer and output
                    char temp[MAX_DNS_SIZE];
                    memcpy(temp, res, blockSize);
                    temp[blockSize] = '\0';
                    printf(temp);

                    // Increment to next block
                    res += blockSize;
                }

                // Output query header information
                QueryHeader* qhTemp = (QueryHeader*)res;
                printf("type %d class %d\n", htons(qhTemp->qType), htons(qhTemp->qClass));

                // Update current pointer
                res += sizeof(QueryHeader);
            }
        }

        // Answers are present
        if (htons(fdhRes->answers) > 0) {
            printf("\t------------ [answers] ------------\n");

            for (int i = 0; i < htons(fdhRes->answers); i++) {
                printf("\t\t");

                if (res - buf >= responseBytes) {
                    printf("\t++ Invalid section: not enough records\n");
                    WSACleanup();
                    delete packet;
                    return 0;
                }

                if (res + (int)sizeof(FixedRR) - buf > responseBytes) {
                    printf("\t++ Invalid record: truncated RR answer header\n");
                    WSACleanup();
                    delete packet;
                    return 0;
                }

                res = jump(buf, res, resHeader, responseBytes);
                FixedRR* frr = (FixedRR*)res;
                res += sizeof(FixedRR);

                int qType = (int)htons(frr->qType);
                if (qType == DNS_A) {
                    printf("A ");

                    if (res + (int)htons(frr->length) - buf > responseBytes) {
                        printf("\n\t++ Invalid record: RR value length stretches the answer beyond the packet\n");
                        delete packet;
                        return 0;
                    }

                    printf("%d.", 16 * (unsigned char(res[0]) >> 4) + (unsigned char(res[0]) & 0x0f));
                    printf("%d.", 16 * (unsigned char(res[1]) >> 4) + (unsigned char(res[1]) & 0x0f));
                    printf("%d.", 16 * (unsigned char(res[2]) >> 4) + (unsigned char(res[2]) & 0x0f));
                    printf("%d ", 16 * (unsigned char(res[3]) >> 4) + (unsigned char(res[3]) & 0x0f));

                    printf(" TTL = %d\n", 256 * (int)htons(frr->TTL1) + (int)htons(frr->TTL2));
                    res += 4;
                }
                else if (qType == DNS_PTR || qType == DNS_NS || qType == DNS_CNAME) {
                    if (qType == DNS_PTR) {
                        printf("PTR ");
                    }
                    else if (qType == DNS_NS) {
                        printf("NS ");
                    }
                    else if (qType == DNS_CNAME) {
                        printf("CNAME ");
                    }

                    if (res + (int)htons(frr->length) - buf > responseBytes) {
                        printf("\n\t++ Invalid record: RR value length stretches the answer beyond the packet\n");
                        delete packet;
                        return 0;
                    }

                    // Perform jump
                    res = jump(buf, res, resHeader, responseBytes);
                    printf(" TTL = %d\n", 256 * (int)htons(frr->TTL1) + (int)htons(frr->TTL2));
                }
            }
        }

        // Authority is present
        if (htons(fdhRes->authority) > 0) {
            printf("\t------------ [authority] ----------\n");

            for (int i = 0; i < htons(fdhRes->authority); i++) {
                printf("\t\t");

                if (res - buf >= responseBytes) {
                    printf("\t++ Invalid section: not enough records\n");
                    WSACleanup();
                    delete packet;
                    return 0;
                }

                if (res + (int)sizeof(FixedRR) - buf > responseBytes) {
                    printf("\t++ Invalid record: truncated RR answer header\n");
                    WSACleanup();
                    delete packet;
                    return 0;
                }

                res = jump(buf, res, resHeader, responseBytes);
                FixedRR* frr = (FixedRR*)res;
                res += sizeof(FixedRR);

                int qType = (int)htons(frr->qType);
                if (qType == DNS_A) {
                    printf("A ");

                    if (res + (int)htons(frr->length) - buf > responseBytes) {
                        printf("\n\t++ Invalid record: RR value length stretches the answer beyond the packet\n");
                        delete packet;
                        return 0;
                    }

                    printf("%d.", 16 * (unsigned char(res[0]) >> 4) + (unsigned char(res[0]) & 0x0f));
                    printf("%d.", 16 * (unsigned char(res[1]) >> 4) + (unsigned char(res[1]) & 0x0f));
                    printf("%d.", 16 * (unsigned char(res[2]) >> 4) + (unsigned char(res[2]) & 0x0f));
                    printf("%d ", 16 * (unsigned char(res[3]) >> 4) + (unsigned char(res[3]) & 0x0f));

                    printf(" TTL = %d\n", 256 * (int)htons(frr->TTL1) + (int)htons(frr->TTL2));
                    res += 4;
                }
                else if (qType == DNS_PTR || qType == DNS_NS || qType == DNS_CNAME) {
                    if (qType == DNS_PTR) {
                        printf("PTR ");
                    }
                    else if (qType == DNS_NS) {
                        printf("NS ");
                    }
                    else if (qType == DNS_CNAME) {
                        printf("CNAME ");
                    }

                    if (res + (int)htons(frr->length) - buf > responseBytes) {
                        printf("\n\t++ Invalid record: RR value length stretches the answer beyond the packet\n");
                        delete packet;
                        return 0;
                    }

                    // Perform jump
                    res = jump(buf, res, resHeader, responseBytes);
                    printf(" TTL = %d\n", 256 * (int)htons(frr->TTL1) + (int)htons(frr->TTL2));
                }
            }
        }

        // Additional is present
        if (htons(fdhRes->additional) > 0) {
            printf("\t------------ [additional] ---------\n");

            for (int i = 0; i < htons(fdhRes->additional); i++) {
                printf("\t\t");

                if (res - buf >= responseBytes) {
                    printf("\t++ Invalid section: not enough records\n");
                    WSACleanup();
                    delete packet;
                    return 0;
                }

                if (res + (int)sizeof(FixedRR) - buf > responseBytes) {
                    printf("\t++ Invalid record: truncated RR answer header\n");
                    WSACleanup();
                    delete packet;
                    return 0;
                }

                res = jump(buf, res, resHeader, responseBytes);
                FixedRR* frr = (FixedRR*)res;
                res += sizeof(FixedRR);

                int qType = (int)htons(frr->qType);
                if (qType == DNS_A) {
                    printf("A ");

                    if (res + (int)htons(frr->length) - buf > responseBytes) {
                        printf("\n\t++ Invalid record: RR value length stretches the answer beyond the packet\n");
                        delete packet;
                        return 0;
                    }

                    printf("%d.", 16 * (unsigned char(res[0]) >> 4) + (unsigned char(res[0]) & 0x0f));
                    printf("%d.", 16 * (unsigned char(res[1]) >> 4) + (unsigned char(res[1]) & 0x0f));
                    printf("%d.", 16 * (unsigned char(res[2]) >> 4) + (unsigned char(res[2]) & 0x0f));
                    printf("%d ", 16 * (unsigned char(res[3]) >> 4) + (unsigned char(res[3]) & 0x0f));

                    printf(" TTL = %d\n", 256 * (int)htons(frr->TTL1) + (int)htons(frr->TTL2));
                    res += 4;
                }
                else if (qType == DNS_PTR || qType == DNS_NS || qType == DNS_CNAME) {
                    if (qType == DNS_PTR) {
                        printf("PTR ");
                    }
                    else if (qType == DNS_NS) {
                        printf("NS ");
                    }
                    else if (qType == DNS_CNAME) {
                        printf("CNAME ");
                    }

                    if (res + (int)htons(frr->length) - buf > responseBytes) {
                        printf("\n\t++ Invalid record: RR value length stretches the answer beyond the packet\n");
                        delete packet;
                        return 0;
                    }

                    // Perform jump
                    res = jump(buf, res, resHeader, responseBytes);
                    printf(" TTL = %d\n", 256 * (int)htons(frr->TTL1) + (int)htons(frr->TTL2));
                }
            }
        }
    }
    else if (available < 0) {
        printf("failed with %d on recv\n", WSAGetLastError());
    }
    else {
        // DNS attempt timed out
        printf("timeout in %d ms", (int)(1000 * ((double)clock() - (double)timer) / (double)CLOCKS_PER_SEC));
    }


    delete packet;
    return 0;
}

