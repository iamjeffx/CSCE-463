#pragma once

#include "pch.h"

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
	bool Read();

};

