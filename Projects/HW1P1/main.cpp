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
#include "HTMLParserBase.h"
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

            // Extract the header from the response
            int headerIndex = res.find("\r\n\r\n");
            string header = res.substr(0, headerIndex);

            // Extract status code
            int statusCode = atoi(header.substr(9, 3).c_str());

            printf("\tVerifying header... ");

            if (statusCode <= 99) {
                printf("failed with non-HTTP header");
                return 0;
            }

            printf("status code %d\n", statusCode);

            if (!(statusCode < 200 || statusCode >= 300)) {
                // Parse the HTML page
                int numLinks;
                HTMLParserBase HTMLParser;

                printf("\t\b\b+ Parsing page... ");

                clock_t timer = clock();
                char* buffer = HTMLParser.Parse((char*)res.c_str(), (int)strlen(res.c_str()), (char*)parser.getURL().c_str(), (int)strlen(parser.getURL().c_str()), &numLinks);
                double timeElapsed = (clock() - timer) / (double)CLOCKS_PER_SEC;

                printf("done in %.1f ms with %d links\n", timeElapsed * 1000, numLinks);
                
            }
            printf("----------------------------------------\n");
            printf("%s\n", header.c_str());
        }
    }

    WSACleanup();
}
