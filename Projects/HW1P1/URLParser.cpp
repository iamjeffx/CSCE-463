#include "pch.h"
#include "URLParser.h"

using namespace std;

URLParser::URLParser(std::string url) {
	this->url = url;
	this->host = "";
	this->path = "";
	this->port = 0;
	this->query = "";
};

URLParser::~URLParser() {
	reset();
};

void URLParser::reset() {
	this->url = "";
	this->host = "";
	this->path = "";
	this->port = 0;
	this->query = "";
};

int URLParser::parse() {
	string URL = this->url;
	// Check that the HTTP protocol is being used
	if (URL.substr(0, 7) != "http://") {
		reset();
		return -1;
	}

	// Remove the HTTP scheme and fragments
	URL = URL.substr(7, (int)URL.size());
	int fragmentIndex = (int)URL.find_first_of("#");
	URL = URL.substr(0, fragmentIndex);

	// Extract query
	int queryIndex = (int)URL.find_first_of("?");
	if (queryIndex == -1) {
		this->query = "";
	}
	else {
		this->query = URL.substr(queryIndex, (int)URL.size());
		URL = URL.substr(0, queryIndex);
	}

	// Extract path
	int pathIndex = (int)URL.find_first_of("/");
	if (pathIndex == -1) {
		this->path = "/";
	}
	else {
		this->path = URL.substr(pathIndex, (int)URL.size());
		URL = URL.substr(0, pathIndex);
	}

	// Extract host and port number
	int portIndex = (int)URL.find_first_of(":");
	if (portIndex == -1) {
		this->port = 80;
		this->host = URL;
	}
	else {
		if (portIndex == URL.size() - 1) {
			reset();
			return -2;
		}
		this->port = atoi(URL.substr(portIndex + 1, (int)URL.size()).c_str());
		if (port <= 0) {
			reset();
			return -2;
		}
		this->host = URL.substr(0, portIndex);
	}

	return 0;
};

string URLParser::generateQuery() {
	return this->path + this->query;
}

string URLParser::generateRequest(string requestType) {
	string request = requestType + " " + generateQuery() + " HTTP/1.0\r\nUser-agent: JXCrawler/1.1\r\n";
	request += "Host: " + getHost() + "\r\nConnection: close\r\n\r\n";
	return request;
}