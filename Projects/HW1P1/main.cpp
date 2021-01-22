/** CSCE 463 Homework 1 Part 1
*
    Author: Jeffrey Xu
    Email: jeffreyxu@tamu.edu
    Professor Loguinov

    Basic Web-crawler that uses TCP/IP and WinSock.
**/

#include "pch.h"
#include "URLParser.h"
#include "Socket.h"
#pragma comment(lib, "ws2_32.lib")

using namespace std;

int main(int argc, char* argv[]) {
    // Case if URL isn't present
    if (argc != 2) {
        printf("Incorrect number of arguments provided\n");
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
        printf("Failed with invalid scheme");
        return 0;
    }
    if (parseResult == -2) {
        printf("Failed with invalid port");
        return 0;
    }
    printf("host %s, port %d, request %s\n", parser.getHost().c_str(), parser.getPort(), parser.generateQuery().c_str());

    // Initialize socket
    Socket s;

    // Send request: If request gets properly sent, then read the response from the server if packets are properly received
    if (s.Send(parser.generateRequest("GET"), parser.getHost(), parser.getPort())) {
        if (s.Read()) {
            // Extract response and close the socket
            string res = string(s.getBuf());
            s.close();

            cout << res << endl;
        }
    }

    WSACleanup();
}
