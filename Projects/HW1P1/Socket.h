#pragma once

#include "pch.h"

using namespace std;

class Socket
{
private:
	SOCKET sock;
	char* buf;
	int allocatedSize;
	int curPos;

public:
	Socket();
	~Socket();
	bool Read(void);
	bool Send(string request, string host, int port);
	void close();
	char* getBuf() {
		return this->buf;
	}
};

