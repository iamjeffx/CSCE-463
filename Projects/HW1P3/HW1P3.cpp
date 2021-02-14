/** CSCE 463 Homework 1 Part 3: Spring 2021
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

struct Parameters {
    volatile int queueSize;
    volatile int numExtractedURLs = 0;
    volatile int numHostUniqueness = 0;
    volatile int numDNSSuccess = 0;
    volatile int numIPUniqueness = 0;
    volatile int numRobotsSuccess = 0;
    volatile int numCrawledURLs = 0;
    volatile int numTotalLinks = 0;
    volatile int numBytesDownloaded = 0;
    volatile int numPagesDownloaded = 0;

    volatile int numThreadsRunning;

    volatile int num2XX = 0;
    volatile int num3XX = 0;
    volatile int num4XX = 0;
    volatile int num5XX = 0;

    float totalElapsedTime;
    volatile int numTAMUInternal = 0;
    volatile int numTAMUExternal = 0;

    unordered_set<string> hostsUniqueness;
    unordered_set<DWORD> IPUniqueness;
    queue<string> URLQueue;

    HANDLE URLQueueMutex;
    HANDLE IPListMutex;
    HANDLE hostListMutex;
    HANDLE paramMutex;

    HANDLE statQuit;
};

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
            string res = string(s.getBuf());
            s.close();

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

int DNSLookup(string host, sockaddr_in& server) {
    hostent* remote;
    DWORD IP = inet_addr(host.c_str());
    if (IP == INADDR_NONE) {
        if ((remote = gethostbyname(host.c_str())) == NULL) {
            return -1;
        }
        else {
            memcpy((char*)&(server.sin_addr), remote->h_addr_list, remote->h_length);
            return 0;
        }
    }

    server.sin_addr.S_un.S_addr = IP;
    return 0;
}

UINT statThread(LPVOID params) {
    Parameters* p = (Parameters*)params;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    clock_t startTime = clock();
    int numBytes = 0;
    int numPages = 0;
    bool last = false;

    while (WaitForSingleObject(p->statQuit, 2000) == WAIT_TIMEOUT) {
        numBytes = p->numBytesDownloaded;
        numPages = p->numPagesDownloaded;

        // Output stats for current interval
        printf("[%3d] %4d Q %6d E %7d H %6d D %6d I %5d R %5d C %5d L %4dK\n",
            (int)floor(((double)clock() - (double)startTime) / (double)(CLOCKS_PER_SEC)),
            p->numThreadsRunning, (int)p->URLQueue.size(), p->numExtractedURLs, p->numHostUniqueness,
            p->numDNSSuccess, p->numIPUniqueness, p->numRobotsSuccess, p->numCrawledURLs, p->numTotalLinks / 1000);

        // Compute number of bytes downloaded during this interval (2s)
        double bytesDownloaded = (p->numBytesDownloaded - numBytes) / 2;
        double pagesDownloaded = (double)((double)p->numPagesDownloaded - (double)numPages) / 2.0;

        // Output crawling information
        printf("\t*** crawling %.1f pps @ %.1f Mbps\n", pagesDownloaded, bytesDownloaded / double(125000));

        // Break condition
        if (p->numThreadsRunning == 0) {
            if (last) {
                break;
            }
            else {
                last = true;
            }
        }
    }

    return 0;
}

UINT crawlThread(LPVOID param) {
    // Set up parameters
    Parameters* p = (Parameters*)param;
    bool hostUnique, IPUnique;

    HTMLParserBase* HTMLParser = new HTMLParserBase;

    // Set up socket
    Socket s;

    // Local variables
    string URLCurrent;

    while (true) {
        hostUnique = false;
        IPUnique = false;
        WaitForSingleObject(p->URLQueueMutex, INFINITE);

        // No URLs left to crawl
        if (p->URLQueue.size() == 0) {
            // Release URL Queue mutex
            ReleaseMutex(p->URLQueueMutex);

            // Decrement number of threads running
            WaitForSingleObject(p->paramMutex, INFINITE);
            p->numThreadsRunning--;
            ReleaseMutex(p->paramMutex);

            break;
        }

        // Pop of the front of the queue
        try {
            URLCurrent = p->URLQueue.front();
            p->URLQueue.pop();
        }
        catch(int errno) {
            // Dequeue URL failed -> continue to next cycle
            ReleaseMutex(p->URLQueueMutex);
            continue;
        }

        ReleaseMutex(p->URLQueueMutex);

        // Increment number of extracted threads
        WaitForSingleObject(p->paramMutex, INFINITE);
        p->numExtractedURLs++;
        ReleaseMutex(p->paramMutex);

        // Parse URL
        URLParser parser(URLCurrent);
        int parseResult = parser.parse();

        if (parseResult != 0) {
            continue;
        }

        // Reinitialize socket at each iteration
        s.ReInitSock();

        // Check host uniqueness
        WaitForSingleObject(p->hostListMutex, INFINITE);
        int prevSize = (int)p->hostsUniqueness.size();
        p->hostsUniqueness.insert(parser.getHost());

        if (prevSize != p->hostsUniqueness.size()) {
            hostUnique = true;
        }
        ReleaseMutex(p->hostListMutex);
        
        if (hostUnique) {
            WaitForSingleObject(p->paramMutex, INFINITE);
            p->numHostUniqueness++;
            ReleaseMutex(p->paramMutex);
        }
        else {
            continue;
        }

        // Perform DNS lookup
        struct sockaddr_in tempServer;
        if (!s.performDNS(parser.getHost())) {
            continue;
        }
        tempServer = s.getServer();

        WaitForSingleObject(p->paramMutex, INFINITE);
        p->numDNSSuccess++;
        ReleaseMutex(p->paramMutex);

        // Check IP Uniqueness
        DWORD IP = inet_addr(inet_ntoa(tempServer.sin_addr));

        WaitForSingleObject(p->IPListMutex, INFINITE);
        prevSize = (int)p->IPUniqueness.size();
        p->IPUniqueness.insert(IP);

        if (prevSize != p->IPUniqueness.size()) {
            IPUnique = true;
        }
        ReleaseMutex(p->IPListMutex);

        if (IPUnique) {
            WaitForSingleObject(p->paramMutex, INFINITE);
            p->numIPUniqueness++;
            ReleaseMutex(p->paramMutex);
        }
        else {
            continue;
        }

        // Connect and send robots request
        if (s.Connect(parser.getPort())) {
            if (s.Send(parser.generateRobotsRequest())) {
                if (s.Read(true)) {
                    // Extract HTTP response
                    string response = s.getBuf();

                    WaitForSingleObject(p->paramMutex, INFINITE);
                    p->numBytesDownloaded += strlen(response.c_str());
                    ReleaseMutex(p->paramMutex);

                    // Close socket
                    closesocket(s.getSock());

                    int headerIndex = (int)response.find("\r\n\r\n");
                    string header = response.substr(0, headerIndex);

                    WaitForSingleObject(p->paramMutex, INFINITE);
                    p->numPagesDownloaded++;
                    ReleaseMutex(p->paramMutex);
                }
            }
        }
    }

    s.close();

    return 0;
}

int main(int argc, char** argv) {
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
    }
    else {
        printf("Usage: HW1P2.exe [[numThreads] arg1]\n");
        return 0;
    }

    // Shared parameters for threads
    Parameters params;

    // Output filename and file size
    struct stat sb;
    int rc = stat(filename.c_str(), &sb);
    if (rc != 0) {
        printf("Could not open %s: file is corrupted or does not exist\n", filename.c_str());
        return 0;
    }
    printf("Opened %s with size %d\n", filename.c_str(), sb.st_size);

    // Parse file and extract links to crawl and parse and store in shared queue
    if (!parseLinksFile(filename, params.URLQueue)) {
        printf("Could not open file: File either corrupted or does not exist\n");
        return 0;
    }

    // Initialize uninitialized values of params
    params.numThreadsRunning = 0;
    params.queueSize = (int)params.URLQueue.size();
    params.URLQueueMutex = CreateMutex(NULL, 0, NULL);
    params.hostListMutex = CreateMutex(NULL, 0, NULL);
    params.IPListMutex = CreateMutex(NULL, 0, NULL);
    params.paramMutex = CreateMutex(NULL, 0, NULL);
    params.statQuit = CreateEvent(NULL, true, false, NULL);
    
    HANDLE* crawlers = new HANDLE[numThreads];

    HANDLE stats = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)statThread, &params, 0, NULL);

    for (int i = 0; i < numThreads; i++) {
        crawlers[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)crawlThread, &params, 0, NULL);

        WaitForSingleObject(params.paramMutex, INFINITE);
        params.numThreadsRunning++;
        ReleaseMutex(params.paramMutex);
    }

    for (int i = 0; i < numThreads; i++) {
        WaitForSingleObject(crawlers[i], INFINITE);
        CloseHandle(crawlers[i]);
    }

    SetEvent(params.statQuit);
    WaitForSingleObject(stats, INFINITE);
    CloseHandle(stats);

    printf("Crawling complete\n");

    WSACleanup();
}