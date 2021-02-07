/** CSCE 463 Homework 1 Part 2: Spring 2021
*
	Author: Jeffrey Xu
	UIN: 527008162
	Email: jeffreyxu@tamu.edu
	Professor Dmitri Loguinov
	Filename: Socket.h

	Declaration of Socket class.
**/

#pragma once

#include "pch.h"

using namespace std;

class Socket
{
private:
	SOCKET sock;
	char* buf;
	size_t allocatedSize;
	int curPos;
	DWORD IPAddress;
	struct hostent* remote;
	struct sockaddr_in server;

public:
	Socket();
	~Socket();
	bool Read(bool robots);
	bool Send(string request);
	bool performDNS(string host);
	bool Connect(int port);
	void close();
	DWORD getIP() {
		return this->IPAddress;
	}
	char* getBuf() {
		return this->buf;
	}
	struct sockaddr_in getServer() {
		return this->server;
	}
	void setServer(struct sockaddr_in s) {
		this->server = s;
	}
};

