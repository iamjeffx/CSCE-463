/** CSCE 463 Homework 1 Part 2: Spring 2021
*
    Author: Jeffrey Xu
    UIN: 527008162
    Email: jeffreyxu@tamu.edu
    Professor Dmitri Loguinov
    Filename: main.cpp

    Basic Web-crawler that uses TCP/IP and WinSock.
**/

#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable:4996)
#pragma warning(disable:4099)
#include "pch.h"
#include "URLParser.h"
#include "Socket.h"

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

using namespace std;

int parseStatus(string response) {
    printf("\tVerifying header... ");
    int status = atoi(response.substr(9, 3).c_str());

    if (status <= 99) {
        printf("failed with non-HTTP header\n");
        return -1;
    }

    return status;
}

bool parseLinksFile(string filename, queue<string>& links) {
    ifstream file(filename, ios::binary | ios::in);
    string line;

    if (!file.is_open()) {
        return false;
    }
    
    while (getline(file, line)) {
        if (!file.eof()) {
            line = line.substr(0, line.size() - 1);
        }
        links.push(line);
    }
    return true;
}

void parseURL(string URL, unordered_set<string>& hosts, unordered_set<DWORD>& IPs) {
    // Output URL
    printf("URL: %s\n", URL.c_str());
    URLParser parser(URL);

    // Parse URL
    printf("\tParsing URL... ");
    int parseOutput = parser.parse();

    // Invalid scheme detected
    if (parseOutput == -1) {
        printf("failed with invalid scheme");
        return;
    }
    // Invalid port detected
    else if (parseOutput == -2) {
        printf("failed with invalid port");
        return;
    }
    else {
        // URL parsed successfully
        printf("host %s, port %d\n", parser.getHost().c_str(), parser.getPort());

        // Check host uniqueness
        printf("\tChecking host uniqueness... ");
        int prevSize = hosts.size();
        hosts.insert(parser.getHost());

        // Unique host
        if (prevSize < hosts.size()) {
            printf("passed\n");
        }
        // Duplicate
        else {
            printf("failed\n");
            return;
        }

        // Perform DNS search
        Socket s;
        struct sockaddr_in tempServer;
        if (!s.performDNS(parser.getHost())) {
            return;
        }
        tempServer = s.getServer();

        // Check IP uniqueness
        printf("\tChecking IP uniqueness... ");
        DWORD IP = s.getIP();
        prevSize = IPs.size();
        IPs.insert(IP);

        // Unique IP
        if (prevSize < IPs.size()) {
            printf("passed\n");
        }
        // Duplicate
        else {
            printf("failed\n");
            return;
        }

        // Connect to robots page and parse response
        printf("\tConnecting on robots... ");
        if (s.Connect(parser.getPort())) {
            if (s.Send(parser.generateRobotsRequest())) {
                if (s.Read(true)) {
                    // Extract response
                    string response = string(s.getBuf());
                    s.close();
                    WSACleanup();

                    // Extract header
                    size_t headerIndex = response.find("\r\n\r\n");
                    string header = response.substr(0, headerIndex);

                    // HTML header not present
                    if (header.size() <= 0) {
                        printf("\tHTML error: no header present\n");
                        return;
                    }

                    // Extract status
                    int status;
                    if ((status = parseStatus(response)) == -1) {
                        return;
                    }
                    printf("status code %d\n", status);

                    // Verify header

                    // Free to crawl
                    if (status >= 400) {
                        // Reconnect socket
                        printf("\t\b\b* Connecting on page... ");
                        s = Socket();
                        s.setServer(tempServer);
                        if (s.Connect(parser.getPort())) {
                            if (s.Send(parser.generateRequest("GET"))) {
                                if (s.Read(false)) {
                                    // Extract response
                                    response = string(s.getBuf());

                                    // Clean up socket
                                    s.close();
                                    WSACleanup();

                                    // Extract header
                                    headerIndex = response.find("\r\n\r\n");
                                    header = response.substr(0, headerIndex);

                                    // Parse status code
                                    if ((status = parseStatus(header)) == -1) {
                                        return;
                                    }
                                    printf("status code %d\n", status);

                                    if (status >= 200 && status < 300) {
                                        // Parse page HTML
                                        HTMLParserBase HTMLParser;
                                        printf("\t\b\b+ Parsing page... ");
                                        int numLinks;

                                        clock_t timer = clock();
                                        char* buffer = HTMLParser.Parse((char*)response.c_str(), (int)strlen(response.c_str()), (char*)parser.getURL().c_str(), (int)strlen(parser.getURL().c_str()), &numLinks);
                                        double timeElapsed = ((double)clock() - (double)timer) / (double)CLOCKS_PER_SEC;

                                        printf("done in %.1f ms with %d links\n", timeElapsed * 1000, numLinks);
                                    }
                                    else {
                                        return;
                                    }
                                }
                            }
                        }
                    }
                    // Not free to crawl
                    else if (status < 400 && status >= 200) {
                        return;
                    }
                }
            }
        }
    }
}

void requestHTTP(string URL) {
    printf("URL: %s\n", URL.c_str());

    // Begin parsing URL
    printf("\tParsing URL... ");
    URLParser parser(URL);
    int parseResult = parser.parse();

    // Not HTTP scheme
    if (parseResult == -1) {
        printf("failed with invalid scheme");
        return;
    }
    // Invalid port 
    if (parseResult == -2) {
        printf("failed with invalid port");
        return;
    }
    printf("host %s, port %d, request %s\n", parser.getHost().c_str(), parser.getPort(), parser.generateQuery().c_str());

    // Initialize socket
    Socket s;
    if (!s.performDNS(parser.getHost())) {
        return;
    }
    printf("\t\b\b* Connecting on page... ");
    if (!s.Connect(parser.getPort())) {
        return;
    }

    // Send request: If request gets properly sent, then read the response from the server if packets are properly received
    if (s.Send(parser.generateRequest("GET"))) {
        if (s.Read(false)) {
            // Extract response and close the socket
            s.close();
            string res = string(s.getBuf());

            // Extract the header from the response
            size_t headerIndex = res.find("\r\n\r\n");
            string header = res.substr(0, headerIndex);

            // Extract status code
            int statusCode = parseStatus(header);

            printf("status code %d\n", statusCode);
            // Only parse the HTML if a 2XX status code is present
            if (!(statusCode < 200 || statusCode >= 300)) {
                // Parse the HTML page
                int numLinks;
                HTMLParserBase HTMLParser;

                printf("\t\b\b+ Parsing page... ");

                clock_t timer = clock();
                char* buffer = HTMLParser.Parse((char*)res.c_str(), (int)strlen(res.c_str()), (char*)parser.getURL().c_str(), (int)strlen(parser.getURL().c_str()), &numLinks);
                double timeElapsed = ((double)clock() - (double)timer) / (double)CLOCKS_PER_SEC;

                printf("done in %.1f ms with %d links\n", timeElapsed * 1000, numLinks);
            }

            // Output HTTP response header
            printf("----------------------------------------\n");
            printf("%s\n", header.c_str());
        }
    }
    // Clean up all socket information
    WSACleanup();
    return;
}

int main(int argc, char* argv[]) {
    string filename;
    int numThreads;

    // Extract command line information (2 arguments => P1 code executes, 3 arguments => P2 code executes if threads specified are 1)
    if (argc == 2) {
        // Extract URL
        string URL = string(argv[1]);
        
        // Request HTML from host
        requestHTTP(URL);
        return 0;
    }
    else if (argc == 3) {
        // Extract filename and number of threads
        filename = string(argv[2]);
        numThreads = atoi(argv[1]);

        // Invalid number of threads provided (we are currently only using one thread)
        if (numThreads != 1) {
            printf("Incorrect number of threads provided\n");
            return 0;
        }
    }
    else {
        printf("Usage: HW1P2.exe [[numThreads] arg1]\n");
        return 0;
    }

    // Output filename and file size
    struct stat sb;
    int rc = stat(filename.c_str(), &sb);
    if (rc != 0) {
        printf("Could not open %s: file is corrupted or does not exist\n", filename.c_str());
        return 0;
    }
    printf("Opened %s with size %d\n", filename.c_str(), sb.st_size);

    // Parse file and extract links to crawl and parse
    queue<string> links;
    if (!parseLinksFile(filename, links)) {
        printf("Could not open file: File either corrupted or does not exist\n");
        return 0;
    }

    unordered_set<string> hosts;
    unordered_set<DWORD> IPs;

    // Begin to crawl through links
    while (links.size() > 0) {
        // Pop off next link
        string URL = links.front();
        links.pop();

        // Parse URL
        parseURL(URL, hosts, IPs);
        WSACleanup();
    }
}
