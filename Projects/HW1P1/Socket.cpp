/** CSCE 463 Homework 1 Part 1
*
    Author: Jeffrey Xu
    UIN: 527008162
    Email: jeffreyxu@tamu.edu
    Professor Dmitri Loguinov
    Filename: Socket.cpp

    Definition of all Socket functions. 
**/

#include "Socket.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS

const int INITIAL_BUF_SIZE = 8192;

Socket::Socket() {
    // Initialize buffer
    buf = new char[INITIAL_BUF_SIZE];
    allocatedSize = INITIAL_BUF_SIZE;
    curPos = 0;

    // Initialize WinSock (from sample solution code)
    WSADATA wsaData;

    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        printf("WSAStartup error %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    // Open a TCP socket
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket() generated error %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }   
}

Socket::~Socket() {
    // Clear the buffer
    delete buf;
}

void Socket::close() {
    // Close the socket
    closesocket(sock);
}

bool Socket::Send(string request, string host, int port) {
    struct hostent* remote;
    struct sockaddr_in server;

    // Perform DNS lookup and time the operation (code is from sample solution)
    clock_t timer = clock();

    printf("\tDoing DNS... ");
    DWORD IP = inet_addr(host.c_str());

    // If host isn't an IP address
    if (IP == INADDR_NONE) {
        // Perform DNS lookup on hostname
        if ((remote = gethostbyname(host.c_str())) == NULL) {
            printf("failed with %d\n", WSAGetLastError());
            return false;
        }
        // Successful DNS lookup
        else {
            memcpy((char*)&(server.sin_addr), remote->h_addr, remote->h_length);
        }
    }
    // Host is a proper IP
    else {
        server.sin_addr.S_un.S_addr = IP;
    }

    double timeElapsed = ((double)clock() - (double)timer) / (double)CLOCKS_PER_SEC;
    printf("done in %.1f ms, found %s\n", timeElapsed * 1000, inet_ntoa(server.sin_addr));

    // Set up port number and TCP and connect to port
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    printf("\t\b\b* Connecting on page... ");

    timer = clock();

    // Attempt connection to host and port
    if (connect(sock, (struct sockaddr*)&server, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
        printf("failed with %d\n", WSAGetLastError());
        return false;
    }

    timeElapsed = ((double)clock() - (double)timer) / (double)CLOCKS_PER_SEC;
    printf("done in %.1f ms\n", timeElapsed * 1000);

    // Send message to specified port
    // The format of this code was taken from the following Stackoverflow post: https://stackoverflow.com/questions/1011339/how-do-you-make-a-http-request-with-c
    if (send(this->sock, request.c_str(), strlen(request.c_str()) + 1, 0) == SOCKET_ERROR) {
        printf("failed with %d\n", WSAGetLastError());
        return false;
    }
    return true;
}

bool Socket::Read(void) {
    printf("\tLoading... ");

    // Set timeout value to 10s
    timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    curPos = 0;
    fd_set fdRead;
    int ret;

    clock_t timer = clock();

    while (true) {
        // Set file descriptors
        FD_ZERO(&fdRead);
        FD_SET(sock, &fdRead);

        if ((ret = select(0, &fdRead, NULL, NULL, &timeout)) > 0) {
            // Receive new data
            int bytes = recv(sock, buf + curPos, allocatedSize - curPos, 0);

            // Error with recv function
            if (bytes < 0) {
                printf("failed with %d in recv\n", WSAGetLastError());
                break;
            }

            // Null-terminated response: Close the buffer and break
            if (bytes == 0) {
                buf[curPos + 1] = '\0';
                double timeElapsed = ((double)clock() - (double)timer) / (double)CLOCKS_PER_SEC;
                printf("done in %.1f ms with %d bytes\n", timeElapsed * 1000, curPos);
                return true;
            }
            // Update cursor
            curPos += bytes;

            // Buffer is running out of space: resize is required
            if (allocatedSize - curPos < 1024) {
                char* temp = new char[allocatedSize * 2];
                memcpy(temp, buf, allocatedSize);
                allocatedSize *= 2;
                delete buf;
                buf = temp;
            }
        }
        // Timeout expired for connection
        else if (ret == 0) {
            printf("Connection timed out\n");
            break;
        }
        // Some connection error occurred
        else {
            printf("Connection error: %d", WSAGetLastError());
            break;
        }
    }
    // If loop ever terminates and reaches here, then an error occured somewhere in the recv loop
    return false;;
}