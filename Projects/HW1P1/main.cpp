/** CSCE 463 Homework 1 Part 1
*
    Author: Jeffrey Xu
    Email: jeffreyxu@tamu.edu
    Professor Loguinov

    Basic Web-crawler that uses TCP/IP and WinSock.
**/

#include "pch.h"
#include "URLParser.h"
#pragma comment(lib, "ws2_32.lib")

using namespace std;

int main(int argc, char* argv[]) {
    // Case if URL isn't present
    if (argc != 2) {
        return 0;
    }

    // Extract URL
    string URL = argv[1];
    printf("URL: %s\n", URL.c_str());

    // Begin parsing URL
    printf("\tParsing URL... ");
    URLParser parser(URL);
    int parseResult = parser.parse();
    if (parseResult == -1) {
        printf("failed with invalid scheme");
        return 0;
    }
    if (parseResult == -2) {
        printf("failed with invalid port");
        return 0;
    }
    printf("host %s, port %d, request %s\n", parser.getHost().c_str(), parser.getPort(), parser.generateQuery().c_str());

    // Initialize WinSock
    WSADATA wsaData;

    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        printf("WSAStartup error %d\n", WSAGetLastError());
        WSACleanup();
        return 0;
    }

    // Open a TCP socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket() generated error %d\n", WSAGetLastError());
        WSACleanup();
        return 0;
    }

    struct hostent* remote;
    struct sockaddr_in server;

    // Perform DNS lookup
    printf("\tDoing DNS ... ");
    DWORD IP = inet_addr(parser.getHost().c_str());
    if (IP == INADDR_NONE) {
        if ((remote = gethostbyname(parser.getHost().c_str())) == NULL) {
            printf("Invalid string: neither FQDN, nor IP address\n");
            return 0;
        }
        else {
            memcpy((char *)&(server.sin_addr), remote->h_addr, remote->h_length);
        }
    }
    else {
        server.sin_addr.S_un.S_addr = IP;
    }

    // Set up port number and TCP
    server.sin_family = AF_INET;
    server.sin_port = htons(parser.getPort());

    if (connect(sock, (struct sockaddr*)&server, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
        printf("Connection error: %d\n", WSAGetLastError());
        return 0;
    }

    // Send HTTP request

    // Close socket and clean up
    closesocket(sock);
    WSACleanup();
}
