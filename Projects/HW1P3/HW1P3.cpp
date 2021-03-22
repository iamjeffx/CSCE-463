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
    volatile int numXXX = 0;

    float totalElapsedTime;
    volatile int numTAMUInternal = 0;
    volatile int numTAMUExternal = 0;

    unordered_set<string> hostsUniqueness;
    unordered_set<DWORD> IPUniqueness;
    queue<char*> URLQueue;

    HANDLE URLQueueMutex;
    HANDLE IPListMutex;
    HANDLE hostListMutex;
    HANDLE paramMutex;
    HANDLE statQuit;

    string filename;
};

void parsePageLinks(HTMLParserBase* HTMLParser, Parameters* p, string page, string URL) {
    int numLinks;
    bool containsTAMULink = false;
    char* linkBuf = HTMLParser->Parse((char*)page.c_str(), (int)strlen(page.c_str()), (char*)URL.c_str(), (int)strlen(URL.c_str()), &numLinks);

    WaitForSingleObject(p->paramMutex, INFINITE);
    p->numTotalLinks += numLinks;
    ReleaseMutex(p->paramMutex);
}

int parseStatus(string response) {
    // Status is not present
    if (response.size() <= 14) {
        return -1;
    }

    // Extract status and return
    int status = atoi(response.substr(9, 3).c_str());
    return status;
}

UINT parseLinksFile(LPVOID params) {
    Parameters* p = (Parameters*)params;

    ifstream file(p->filename, ios::binary | ios::in);
    string line;

    if (!file.is_open()) {
        return -1;
    }
    try {
        while (getline(file, line)) {
            if (!file.eof()) {
                line = line.substr(0, line.size() - 1);
            }
            char* tempLine = new char[line.size() + 1];
            memcpy(tempLine, line.c_str(), line.size());
            tempLine[line.size()] = '\0';

            WaitForSingleObject(p->URLQueueMutex, INFINITE);
            p->URLQueue.push(tempLine);
            ReleaseMutex(p->URLQueueMutex);
        }
        return 0;
    }
    catch (int errno) {
        printf("Issue reading file\n");
        return -1;
    }

}

DWORD WINAPI statThread(LPVOID params) {
    Parameters* p = (Parameters*)params;

    clock_t startTime = clock();
    int numBytes = p->numBytesDownloaded;
    int numPages = p->numPagesDownloaded;
    double bytesDownloaded;
    double pagesDownloaded;
    bool last = false;

    clock_t prevTime = clock();

    while (WaitForSingleObject(p->statQuit, 2000) == WAIT_TIMEOUT) {
        // Output stats for current interval
        printf("[%3d] %4d Q %6d E %7d H %6d D %6d I %5d R %5d C %5d L %4dK\n",
            (clock() - startTime) / CLOCKS_PER_SEC,
            p->numThreadsRunning, (int)p->URLQueue.size(), p->numExtractedURLs, p->numHostUniqueness,
            p->numDNSSuccess, p->numIPUniqueness, p->numRobotsSuccess, p->numCrawledURLs, p->numTotalLinks / 1000);

        // Compute number of bytes downloaded during this interval (2s)
        bytesDownloaded = ((double)p->numBytesDownloaded - (double)numBytes) / (double)(((double)clock() - (double)prevTime) / (double)CLOCKS_PER_SEC);
        pagesDownloaded = (double)((double)p->numPagesDownloaded - (double)numPages) / (double)(((double)clock() - (double)prevTime) / (double)CLOCKS_PER_SEC);

        // Output crawling information
        printf("\t*** crawling %.1f pps @ %.1f Mbps\n", (double)pagesDownloaded, bytesDownloaded / double(125000));

        numBytes = p->numBytesDownloaded;
        numPages = p->numPagesDownloaded;

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

    // Update total time taken
    double totalTimeElapsed = (double)((double)clock() - startTime) / (double)CLOCKS_PER_SEC;

    // Output final stats
    printf("\nExtracted %d URLs @ %d/s\n", p->numExtractedURLs, (int)(p->numExtractedURLs / totalTimeElapsed));
    printf("Looked up %d DNS names @ %d/s\n", p->numHostUniqueness, (int)(p->numDNSSuccess / totalTimeElapsed));
    printf("Attempted %d robots @ %d/s\n", p->numIPUniqueness, (int)(p->numRobotsSuccess / totalTimeElapsed));
    printf("Crawled %d pages @ %d/s (%.2f MB)\n", p->numCrawledURLs, (int)(p->numCrawledURLs / totalTimeElapsed), (double)(p->numBytesDownloaded / (double)1048576));
    printf("Parsed %d links @ %d/s\n", p->numTotalLinks, (int)(p->numTotalLinks / totalTimeElapsed));
    printf("HTTP codes: 2xx = %d, 3xx = %d, 4xx = %d, 5xx = %d, other = %d\n", p->num2XX, p->num3XX, p->num4XX, p->num5XX, p->numXXX);
    //printf("Number of TAMU links from TAMU sites = %d and from non-TAMU sites = %d\n", p->numTAMUInternal, p->numTAMUExternal);

    return 0;
}

DWORD WINAPI crawlThread(LPVOID param) {
    // Set up parameters
    Parameters* p = (Parameters*)param;
    bool hostUnique, IPUnique;

    HTMLParserBase* HTMLParser = new HTMLParserBase;

    int prevSize;
    struct sockaddr_in tempServer;
    string response;
    int statusCode;
    int parseResult;

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
            URLCurrent = string(p->URLQueue.front());
            delete p->URLQueue.front();
            p->URLQueue.pop();
        }
        catch (int errno) {
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
        URLParser* parser = new URLParser(URLCurrent);
        parseResult = parser->parse();

        if (parseResult != 0) {
            delete parser;
            continue;
        }

        // Reinitialize socket at each iteration
        Socket* s;
        s = new Socket();
        s->ReInitSock();

        // Check host uniqueness
        WaitForSingleObject(p->hostListMutex, INFINITE);
        prevSize = (int)p->hostsUniqueness.size();
        p->hostsUniqueness.insert(parser->getHost());

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
            s->close();
            delete s;
            delete parser;
            continue;
        }

        // Perform DNS lookup
        if (!s->performDNS(parser->getHost())) {
            s->close();
            delete s;
            delete parser;
            continue;
        }
        tempServer = s->getServer();

        WaitForSingleObject(p->paramMutex, INFINITE);
        p->numDNSSuccess++;
        ReleaseMutex(p->paramMutex);

        // Check IP Uniqueness
        WaitForSingleObject(p->IPListMutex, INFINITE);
        prevSize = (int)p->IPUniqueness.size();
        p->IPUniqueness.insert(inet_addr(inet_ntoa(tempServer.sin_addr)));

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
            s->close();
            delete s;
            delete parser;
            continue;
        }

        // Connect and send robots request
        if (s->Connect(parser->getPort())) {
            if (s->Send(parser->generateRobotsRequest())) {
                if (s->Read(true)) {
                    // Extract HTTP response
                    response = s->getBuf();

                    WaitForSingleObject(p->paramMutex, INFINITE);
                    p->numBytesDownloaded += strlen(response.c_str());
                    p->numPagesDownloaded++;
                    ReleaseMutex(p->paramMutex);

                    // Close socket
                    s->close();
                    delete s;

                    // Extract robots status code
                    statusCode = parseStatus(response);

                    if (statusCode == -1) {
                        delete parser;
                        continue;
                    }

                    // Free to crawl page
                    if (statusCode >= 400 && statusCode < 500) {
                        Socket* s2 = new Socket();
                        s2->ReInitSock();
                        s2->setServer(tempServer);

                        // Update number of robots success
                        WaitForSingleObject(p->paramMutex, INFINITE);
                        p->numRobotsSuccess++;
                        ReleaseMutex(p->paramMutex);

                        // Connect to actual page
                        if (s2->Connect(parser->getPort())) {
                            if (s2->Send(parser->generateRequest("GET"))) {
                                if (s2->Read(false)) {
                                    // Extract HTTP response
                                    response = s2->getBuf();

                                    WaitForSingleObject(p->paramMutex, INFINITE);
                                    p->numBytesDownloaded += strlen(response.c_str());
                                    p->numPagesDownloaded++;
                                    ReleaseMutex(p->paramMutex);

                                    statusCode = parseStatus(response.substr(0, response.find("\r\n\r\n")));

                                    if (statusCode >= 200 && statusCode < 300) {
                                        WaitForSingleObject(p->paramMutex, INFINITE);
                                        p->num2XX++;
                                        p->numCrawledURLs++;
                                        ReleaseMutex(p->paramMutex);

                                        // Parse page
                                        parsePageLinks(HTMLParser, p, response, parser->getURL());
                                    }
                                    else if (statusCode >= 300 && statusCode < 400) {
                                        WaitForSingleObject(p->paramMutex, INFINITE);
                                        p->num3XX++;
                                        ReleaseMutex(p->paramMutex);
                                    }
                                    else if (statusCode >= 400 && statusCode < 500) {
                                        WaitForSingleObject(p->paramMutex, INFINITE);
                                        p->num4XX++;
                                        ReleaseMutex(p->paramMutex);
                                    }
                                    else if (statusCode >= 500 && statusCode < 600) {
                                        WaitForSingleObject(p->paramMutex, INFINITE);
                                        p->num5XX++;
                                        ReleaseMutex(p->paramMutex);
                                    }
                                    else {
                                        WaitForSingleObject(p->paramMutex, INFINITE);
                                        p->numXXX++;
                                        ReleaseMutex(p->paramMutex);
                                    }
                                    delete parser;
                                    s2->close();
                                    delete s2;
                                }
                            }
                        }
                    }
                }
            }
        } 
    }

    delete HTMLParser;

    return 0;
}

int hexToDec(char hexVal[]) {
    // Note that this function was taken from geeksforgeeks
    // Link: https://www.geeksforgeeks.org/program-for-hexadecimal-to-decimal/

    int len = strlen(hexVal);

    // Initializing base value to 1, i.e 16^0 
    int base = 1;

    int dec_val = 0;

    // Extracting characters as digits from last character 
    for (int i = len - 1; i >= 0; i--) {
        // if character lies in '0'-'9', converting  
        // it to integral 0-9 by subtracting 48 from 
        // ASCII value. 
        if (hexVal[i] >= '0' && hexVal[i] <= '9') {
            dec_val += (hexVal[i] - 48) * base;

            // incrementing base by power 
            base = base * 16;
        }

        // If character lies in 'A'-'F' , converting  
        // It to integral 10 - 15 by subtracting 55 from ASCII value 
        else if (hexVal[i] >= 'A' && hexVal[i] <= 'F') {
            dec_val += (hexVal[i] - 55) * base;

            // incrementing base by power 
            base = base * 16;
        }
    }

    return dec_val;
}

int parseChunks(string body) {
    char* prevBody = new char[strlen(body.c_str()) + 1];
    memcpy(prevBody, body.c_str(), strlen(body.c_str()));
    prevBody[strlen(body.c_str())] = '\0';

    char* test = prevBody;

    int total = 0;

    while (true) {
        // Extract hex value
        char* temp = strstr(prevBody, "\r\n");
        int length = (int)(temp - prevBody);
        char* lenBuffer = new char[length + 1];
        memcpy(lenBuffer, prevBody, length);
        lenBuffer[length] = '\0';

        // Convert hex value to decimal
        int len = hexToDec(lenBuffer);

        if (len == 0) {
            delete lenBuffer;
            break;
        }

        if (test + strlen(test) <= prevBody + len) {
            delete lenBuffer;
            return -1;
        }

        total += len;
        prevBody = temp + 2;
        prevBody += (len + 2);

        delete lenBuffer;
    }

    //int total = 0;

    /*while (true) {
        string l = body.substr(0, body.find("\r\n"));
        int lConverted = hexToDec((char*)l.c_str());

        if (lConverted == 0) {
            break;
        }

        total += lConverted;

        body = body.substr(body.find("\r\n") + 2, -1);
        body = body.substr(lConverted + (int)2, -1);
    }*/

    return total;
}

int main(int argc, char** argv) {
    string filename;
    int numThreads;

    // Extract command line information (2 arguments => P1 code executes, 3 arguments => P2 code executes if threads specified are 1)
    if (argc == 2) {
        clock_t timer;

        // Bonus section: HTTP/1.1 chunking
        string URL = argv[1];
        printf("URL: %s\n", URL.c_str());

        // Parse the URL
        printf("\tParsing URL... ");
        URLParser URLParser(URL);
        int parseResult = URLParser.parse();

        // Not HTTP scheme
        if (parseResult == -1) {
            printf("failed with invalid scheme");
            return 0;
        }
        // Invalid port 
        if (parseResult == -2) {
            printf("failed with invalid port");
            return 0;
        }
        printf("host %s, port %d, request %s\n", URLParser.getHost().c_str(), URLParser.getPort(), URLParser.generateQuery().c_str());

        // Perform DNS lookup
        Socket s;
        s.ReInitSock();
        printf("\tDoing DNS... ");

        timer = clock();
        if (!s.performDNS(URLParser.getHost())) {
            s.close();
            printf("failed with %d\n", WSAGetLastError());
            return 0;
        }

        printf("done in %.1f ms, found %s\n", ((double)clock() - (double)timer) * 1000 / (double)CLOCKS_PER_SEC, inet_ntoa(s.getServer().sin_addr));

        printf("\t\b\b* Connecting on page... ");
        timer = clock();

        // Connect to page
        if (s.Connect(URLParser.getPort())) {
            printf("done in %.1f ms\n", (double)(((double)clock() - (double)timer) * 1000) / (double)CLOCKS_PER_SEC);

            // Send HTTP/1.1 message
            if (s.Send(URLParser.bonusGenerateRequest("GET"))) {
                printf("\tLoading... ");

                timer = clock();

                // Read the response
                if (s.Read(false)) {
                    // Extract response
                    string response = s.getBuf();
                    string header = response.substr(0, response.find("\r\n\r\n"));
                    string body = response.substr(response.find("\r\n\r\n") + 4, response.size() - (response.find("\r\n\r\n") + 4));
                    s.close();

                    printf("done in %.1f ms with %d bytes\n", (double)(((double)clock() - (double)timer) * 1000) / (double)CLOCKS_PER_SEC, (int)response.size());

                    // Verifying header
                    printf("\tVerifying header... ");

                    int statusCode = parseStatus(response);

                    if (statusCode == -1) {
                        printf("failed with non-HTTP header");
                        return 0;
                    }

                    printf("status code %d\n", statusCode);

                    string headerFind = header;
                    std::transform(headerFind.begin(), headerFind.end(), headerFind.begin(), ::tolower);

                    if (headerFind.find("transfer-encoding: chunked") != -1) {
                        // Perform dechunking
                        printf("\tDechunking... body size was %d, ", (int)body.size());

                        int newSize = parseChunks(body);
                        if (newSize == -1) {
                            printf("failed\n");
                            return 0;
                        }

                        printf("now %d\n", newSize);
                    }

                    // Parse the page
                    printf("\t\b\b+ Parsing page... ");
                    timer = clock();

                    HTMLParserBase* HTMLParser = new HTMLParserBase;

                    int links;
                    char* buffer = HTMLParser->Parse((char*)response.c_str(), (int)strlen(response.c_str()), (char*)URL.c_str(), (int)strlen(URL.c_str()), &links);

                    printf("done in %.1f ms with %d links\n\n", (double)(((double)clock() - (double)timer) * 1000) / (double)CLOCKS_PER_SEC, links);
                    delete HTMLParser;

                    printf("----------------------------------------\n");
                    printf("%s\n", header.c_str());
                }
                else {
                    printf("failed with %d\n", WSAGetLastError());
                    s.close();
                    return 0;
                }
            }
            else {
                printf("failed with %d\n", WSAGetLastError());
                s.close();
                return 0;
            }
        }
        else {
            printf("failed with %d\n", WSAGetLastError());
            s.close();
            return 0;
        }

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

    // Initialize uninitialized values of params
    params.numThreadsRunning = 0;
    params.queueSize = (int)params.URLQueue.size();
    params.URLQueueMutex = CreateMutex(NULL, 0, NULL);
    params.hostListMutex = CreateMutex(NULL, 0, NULL);
    params.IPListMutex = CreateMutex(NULL, 0, NULL);
    params.paramMutex = CreateMutex(NULL, 0, NULL);
    params.statQuit = CreateEvent(NULL, true, false, NULL);
    params.filename = filename;

    // Parse file and extract links to crawl and parse and store in shared queue
    HANDLE fileLinks = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)parseLinksFile, &params, 0, NULL);

    HANDLE* crawlers = new HANDLE[numThreads];

    HANDLE stats = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)statThread, &params, 0, NULL);

    for (int i = 0; i < numThreads; i++) {
        crawlers[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)crawlThread, &params, 0, NULL);

        WaitForSingleObject(params.paramMutex, INFINITE);
        params.numThreadsRunning++;
        ReleaseMutex(params.paramMutex);
    }

    WaitForSingleObject(fileLinks, INFINITE);

    for (int i = 0; i < numThreads; i++) {
        WaitForSingleObject(crawlers[i], INFINITE);
        CloseHandle(crawlers[i]);
    }

    SetEvent(params.statQuit);
    WaitForSingleObject(stats, INFINITE);
    CloseHandle(stats);

    delete crawlers;

    WSACleanup();
}