#pragma once
#include "URLParser.h"
#include "pch.h"

using namespace std;

class URLParser {
private:
	string url;
	string host;
	string path;
	int port;
	string query;

public:
	URLParser(std::string url);
	~URLParser();

	void reset();
	int parse();
	string generateQuery();
	string getHost() {
		return this->host;
	}
	string getPath() {
		return this->path;
	}
	int getPort() {
		return this->port;
	}
	string getQuery() {
		return this->query;
	}
	string getURL() {
		return this->url;
	}
	string toString() {
		return "URL: " + url + "\nHOST: " + host + "\nPATH: " + path + "\nPORT: " + to_string(port) + "\nQUERY: " + query;
	}
};



