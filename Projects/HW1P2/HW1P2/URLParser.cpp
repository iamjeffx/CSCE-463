/** CSCE 463 Homework 1 Part 2: Spring 2021
*
	Author: Jeffrey Xu
	UIN: 527008162
	Email: jeffreyxu@tamu.edu
	Professor Dmitri Loguinov
	Filename: URLParser.cpp

	Definition of URLParser functions. Note that fragments are not included in this parser.
**/

#include "pch.h"
#include "URLParser.h"

using namespace std;

URLParser::URLParser(std::string url) {
	// Set values to default values (except for URL)
	reset(url);
};

URLParser::~URLParser() {
	// Clear all fields to default values
	reset("");
};

void URLParser::reset(string URL) {
	this->url = URL;
	this->host = "";
	this->path = "/";
	this->port = 80;
	this->query = "";
};

int URLParser::parse() {
	string URL = this->url;
	// Check that the HTTP protocol is being used (HTTPS not considered at this stage)
	if (URL.substr(0, 7) != "http://") {
		reset("");
		return -1;
	}

	// Remove the HTTP scheme and fragments
	URL = URL.substr(7, (int)URL.size());
	int fragmentIndex = (int)URL.find_first_of("#");
	URL = URL.substr(0, fragmentIndex);

	// Extract query
	int queryIndex = (int)URL.find_first_of("?");
	if (queryIndex != -1) {
		setQuery(URL.substr(queryIndex, (int)URL.size()));
		URL = URL.substr(0, queryIndex);
	}

	// Extract path
	int pathIndex = (int)URL.find_first_of("/");
	if (pathIndex != -1) {
		setPath(URL.substr(pathIndex, (int)URL.size()));
		URL = URL.substr(0, pathIndex);
	}

	// Extract host and port number
	int portIndex = (int)URL.find_first_of(":");
	if (portIndex == -1) {
		setHost(URL);
	}
	else {
		// No port specified
		if (portIndex == URL.size() - 1) {
			reset("");
			return -2;
		}

		// Initialize port number
		this->port = atoi(URL.substr((size_t)portIndex + 1, (int)URL.size()).c_str());

		// Invalid port number provided (handles case if port number is negative or isn't numeric)
		if (port <= 0) {
			reset("");
			return -2;
		}
		setHost(URL.substr(0, portIndex));
	}
	// URL successfully parsed
	return 0;
};

string URLParser::generateQuery() {
	// Concatenates path and query to generate the entire query
	return getPath() + getQuery();
}

string URLParser::generateRequest(string requestType) {
	// Generates entire HTTP request
	string request = requestType + " " + generateQuery() + " HTTP/1.0\r\n";
	request += "User-agent: JXCrawler/1.2\r\n";
	request += "Host: " + getHost() + "\r\n";
	request += "Connection: close\r\n\r\n";
	return request;
}

string URLParser::generateRobotsRequest() {
	string request = "HEAD /robots.txt HTTP/1.0\r\n";
	request += "User-agent: JXCrawler/1.2\r\n";
	request += "Host: " + getHost() + "\r\n";
	request += "Connection: close\r\n\r\n";
	return request;
}