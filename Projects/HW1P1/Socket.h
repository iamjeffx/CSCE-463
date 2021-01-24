/** CSCE 463 Homework 1 Part 1
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

