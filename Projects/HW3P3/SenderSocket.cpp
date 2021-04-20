#define _WINSOCK_DEPRECATED_NO_WARNINGS

#define DEBUG false

#include "SenderSocket.h"

using namespace std;

SenderSocket::SenderSocket() {
	base = 0;
	nextSeq = 0;
	nextSend = 0;
	quit = CreateEvent(NULL, false, false, NULL);
	receive = CreateEvent(NULL, false, false, NULL);
	complete = CreateEvent(NULL, true, false, NULL);

	WSADATA wsaData;
	WORD wVersionRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVersionRequested, &wsaData) != 0) {
		WSACleanup();
		exit(0);
	}

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET) {
		closesocket(sock);
		WSACleanup();
		exit(0);
	}

	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(0);

	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
		closesocket(sock);
		WSACleanup();
		exit(0);
	}
}

SenderSocket::~SenderSocket() {
	DWORD exitCode;
	GetExitCodeThread(workers, &exitCode);
	
	if (exitCode == STILL_ACTIVE) {
		SetEvent(quit);
		WaitForSingleObject(workers, INFINITE);
		CloseHandle(workers);

		WaitForSingleObject(stats, INFINITE);
		CloseHandle(stats);
	}

	if (packets != NULL) {
		delete packets;
	}
}

DWORD WINAPI SenderSocket::Stats(LPVOID self) {
	SenderSocket* s = (SenderSocket*)self;

	s->start = clock();
	clock_t current;
	clock_t prev = s->start;

	int prevSize = s->base;
	int nextSize;

	while (WaitForSingleObject(s->complete, 2000) == WAIT_TIMEOUT) {
		current = clock();
		nextSize = s->base;

		if(!DEBUG)
			printf("[%3d] B %6d (%3.1f) N %6d T %d F %d W %d S %.3f Mbps RTT %.3f\n", 
				(current - s->start) / CLOCKS_PER_SEC,
				s->base,
				max(s->base - 1, 0) * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) / (double)1e6,
				s->nextSeq,
				s->numTO,
				s->numRtx,
				s->windowSize,
				(double)(nextSize - prevSize) * 8 * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) / (1e6 * ((double)(current - prev) / CLOCKS_PER_SEC)), 
				s->estRTT);

		prevSize = nextSize;
		prev = current;
	}

	return 0;
}

DWORD WINAPI SenderSocket::Worker(LPVOID self) {
	SenderSocket* s = (SenderSocket*)self;

	int kernelBuffer = 20e6;
	if (setsockopt(s->sock, SOL_SOCKET, SO_RCVBUF, (const char*)&kernelBuffer, sizeof(int)) == SOCKET_ERROR) {
		return 0;
	}
	kernelBuffer = 20e6;
	if (setsockopt(s->sock, SOL_SOCKET, SO_SNDBUF, (const char*)&kernelBuffer, sizeof(int)) == SOCKET_ERROR) {
		return 0;
	}

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	HANDLE events[] = { s->receive, s->full, s->quit };

	bool complete = false;

	int* dup = new int(0);
	int* rtx = new int(0);

	s->transferStart = clock();

	clock_t timerExpire;
	int recvOutput = 0;

	while (!complete || (complete && s->base < s->nextSend)) {
		if (s->nextSend > s->base) {
			s->timeout = (long)(1000 * (timerExpire - clock()) / (double)CLOCKS_PER_SEC);
		}
		else {
			s->timeout = INFINITE;
		}

		recvOutput = 0;

		int ret = WaitForMultipleObjects(3, events, false, s->timeout);

		switch (ret) {
		case WAIT_OBJECT_0 + 0:
			recvOutput = s->ReceiveACK(dup, rtx);
			break;

		case WAIT_OBJECT_0 + 1:
			if (sendto(s->sock, (char*)&(s->packets[s->nextSend % s->windowSize].sdh),
				s->packets[s->nextSend % s->windowSize].size,
				0,
				(struct sockaddr*)&(s->server),
				sizeof(s->server)) == SOCKET_ERROR) {
				break;
			}

			s->packets[s->nextSend % s->windowSize].txTime = clock();
			if (DEBUG)
				printf("[%.2f] --> Packet %d sent txTime %d\n", (clock() - s->start) / (double)CLOCKS_PER_SEC, 
					s->nextSend, 
					clock());
			s->nextSend++;
			break;
		
		case WAIT_OBJECT_0 + 2:
			complete = true;
			break;

		case WAIT_TIMEOUT:
			if (sendto(s->sock, (char*)&(s->packets[s->base % s->windowSize].sdh), 
						s->packets[s->base % s->windowSize].size, 
						0, 
						(struct sockaddr*)&(s->server), 
						sizeof(s->server)) == SOCKET_ERROR) {
				break;
			}
			if(DEBUG)
				printf("[%.2f] --> Packet %d rtx on TO txTime %d\n", (clock() - s->start) / (double)CLOCKS_PER_SEC, s->base, clock());

			if (*rtx == 50) {
				break;
			}

			s->packets[s->base % s->windowSize].txTime = clock();
			(*rtx)++;
			s->numTO++;
			break;

		default:
			break;
		}

		if (s->nextSend == s->base + 1 || recvOutput == 1 || ret == WAIT_TIMEOUT) {
			timerExpire = clock() + s->RTO * CLOCKS_PER_SEC;
		}
	}

	s->transferEnd = clock();

	delete dup;
	delete rtx;

	return STATUS_OK;
}

int SenderSocket::ReceiveACK(int* dup, int* rtx) {
	ReceiverHeader rh;

	struct sockaddr_in res;
	int size = sizeof(res);

	if (recvfrom(this->sock, (char*)&rh, sizeof(ReceiverHeader), 0, (struct sockaddr*)&(res), &size) == SOCKET_ERROR) {
		return -1;
	}

	int y = rh.ackSeq;
	double actRTT = (clock() - packets[(y - 1) % windowSize].txTime) / (double)CLOCKS_PER_SEC;

	if(DEBUG)
		printf("[%.2f] <-- ACK %d RTT %.3f txTime for packet %d: %d rcvTime %d\n", (clock() - start) / (double)CLOCKS_PER_SEC, 
			y,
			actRTT,
			y - 1,
			packets[(y - 1) % windowSize].txTime,
			clock());

	if (y > base) {
		if (*rtx == 0) {
			if (estRTT == -1) {
				estRTT = actRTT;
				RTO = estRTT + 4 * 0.01;
			}
			else if (devRTT == -1) {
				estRTT = (1 - alpha) * estRTT + alpha * actRTT;
				devRTT = abs(estRTT - actRTT);
				RTO = estRTT + 4 * max(0.01, devRTT);
			}
			else {
				estRTT = (1 - alpha) * estRTT + alpha * actRTT;
				devRTT = (1 - beta) * devRTT + beta * abs(estRTT - actRTT);
				RTO = estRTT + 4 * max(0.01, devRTT);
			}
		}

		base = y;
		*dup = 0;
		*rtx = 0;

		effectiveWindow = min(this->windowSize, rh.recvWnd);
		int newReleased = base + effectiveWindow - lastReleased;
		lastReleased += newReleased;

		ReleaseSemaphore(empty, newReleased, NULL);

		return 1;
	}
	else if (y == base) {
		(*dup)++;
		if ((*dup) == 3) {
			if (*rtx == 50) {
				return -1;
			}

			if (sendto(sock, (char*)&(packets[base % windowSize].sdh),
				packets[base % windowSize].size,
				0,
				(struct sockaddr*)&(server),
				sizeof(server)) == SOCKET_ERROR) {
				return -1;
			}

			if(DEBUG)
				printf("[%.2f] --> Packet %d rtx on FRTX\n", (clock() - start) / (double)CLOCKS_PER_SEC, nextSend);

			(*dup) = 0;
			(*rtx)++;
			numRtx++;
			packets[base % windowSize].txTime = clock();

			return 1;
		}
		return 0;
	}
}

DWORD SenderSocket::Open(string host, int port, int senderWindow, LinkProperties* lp) {
	if (open) {
		return ALREADY_CONNECTED;
	}

	this->windowSize = senderWindow;
	packets = new Packet[this->windowSize];

	empty = CreateSemaphore(NULL, 0, this->windowSize, NULL);
	full = CreateSemaphore(NULL, 0, this->windowSize, NULL);

	DWORD serverIP = inet_addr(host.c_str());
	if (serverIP == INADDR_NONE) {
		if ((remote = gethostbyname(host.c_str())) == NULL) {
			printf("target %s is invalid\n", host.c_str());
			closesocket(sock);
			WSACleanup();
			return INVALID_NAME;
		}
		memcpy((char*)&server.sin_addr, remote->h_addr, remote->h_length);
	}
	else {
		server.sin_addr.S_un.S_addr = serverIP;
	}

	char* IPStr = inet_ntoa(server.sin_addr);

	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	SenderSynHeader* packet = new SenderSynHeader();
	packet->lp.bufferSize = senderWindow + 50;
	packet->lp.pLoss[0] = lp->pLoss[0];
	packet->lp.pLoss[1] = lp->pLoss[1];
	packet->lp.RTT = lp->RTT;
	packet->lp.speed = lp->speed;

	packet->sdh.flags.reserved = 0;
	packet->sdh.flags.magic = MAGIC_PROTOCOL;
	packet->sdh.flags.SYN = 1;
	packet->sdh.flags.FIN = 0;
	packet->sdh.flags.ACK = 0;
	packet->sdh.seq = 0;

	RTO = max(1, 2 * lp->RTT);

	timeval TO;
	TO.tv_sec = floor(RTO);
	TO.tv_usec = 1e6 * (RTO - TO.tv_sec);
	long RTO_sec = TO.tv_sec;
	long RTO_usec = TO.tv_usec;

	int available;
	clock_t start = clock();

	for (int i = 0; i < 50; i++) {
		if (sendto(sock, (char*)packet, sizeof(SenderSynHeader), 0, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
			delete packet;
			printf("failed sendto with %d\n", WSAGetLastError());
			closesocket(sock);
			WSACleanup();
			return FAILED_SEND;
		}

		if(DEBUG)
			printf("[%.2f] --> SYN sent\n", (clock() - start) / (double)CLOCKS_PER_SEC);

		fd_set fdRead;
		FD_ZERO(&fdRead);
		FD_SET(sock, &fdRead);

		if ((available = select(0, &fdRead, NULL, NULL, &TO)) > 0) {
			ReceiverHeader rh;
			int responseSize;

			struct sockaddr_in res;
			int size = sizeof(res);

			if ((responseSize = recvfrom(sock, (char*)&rh, sizeof(ReceiverHeader), 0, (struct sockaddr*)&res, &size)) == SOCKET_ERROR) {
				printf("failed recvfrom with %d\n", WSAGetLastError());
				WSACleanup();
				closesocket(sock);
				delete packet;
				return FAILED_RECV;
			}

			if (rh.flags.ACK && rh.flags.SYN && !rh.flags.FIN) {
				if (i == 0) {
					estRTT = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
					this->RTO = estRTT + 4 * 0.01;
				}

				if (DEBUG)
					printf("[%.2f] <-- SYN-ACK 0 RTT %.3f\n", (clock() - start) / (double)CLOCKS_PER_SEC, estRTT);

				open = true;

				lastReleased = min(this->windowSize, rh.recvWnd);
				ReleaseSemaphore(empty, lastReleased, NULL);

				WSAEventSelect(this->sock, this->receive, FD_READ);

				stats = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&Stats, this, 0, NULL);
				workers = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&Worker, this, 0, NULL);

				delete packet;

				return STATUS_OK;
			}
			else {
				TO.tv_sec = RTO_sec;
				TO.tv_usec = RTO_usec;
			}
		}
		else if (available == 0) {
			TO.tv_sec = RTO_sec;
			TO.tv_usec = RTO_sec;
		}
	}
	delete packet;

	return TIMEOUT;
}

DWORD SenderSocket::Send(char* ptr, int bytes) {
	HANDLE arr[] = { quit, empty };
	WaitForMultipleObjects(2, arr, false, INFINITE);

	int slot = this->nextSeq % this->windowSize;
	Packet* p = packets + slot;

	p->sdh.seq = this->nextSeq;
	p->sdh.flags.ACK = 0;
	p->sdh.flags.SYN = 0;
	p->sdh.flags.FIN = 0;
	p->sdh.flags.reserved = 0;
	p->sdh.flags.magic = MAGIC_PROTOCOL;

	p->size = bytes + sizeof(SenderDataHeader);
	p->type = DATATYPE;

	memcpy(p->data, ptr, bytes);

	nextSeq++;
	ReleaseSemaphore(full, 1, NULL);

	return STATUS_OK;
}

DWORD SenderSocket::Close(double timeElapsed) {
	if (!open) {
		return NOT_CONNECTED;
	}

	SetEvent(quit);
	WaitForSingleObject(workers, INFINITE);
	CloseHandle(workers);

	SetEvent(complete);
	WaitForSingleObject(stats, INFINITE);
	CloseHandle(stats);

	SenderDataHeader* packet = new SenderDataHeader();
	packet->seq = max(this->base, 0);
	packet->flags.ACK = 0;
	packet->flags.FIN = 1;
	packet->flags.magic = MAGIC_PROTOCOL;
	packet->flags.reserved = 0;
	packet->flags.SYN = 0;

	for (int i = 0; i < 50; i++) {
		if (sendto(sock, (char*)packet, sizeof(SenderDataHeader), 0, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
			delete packet;
			printf("failed sendto with %d\n", WSAGetLastError());
			closesocket(sock);
			WSACleanup();
			return FAILED_SEND;
		}

		int available;
		fd_set fdRead;
		FD_ZERO(&fdRead);
		FD_SET(sock, &fdRead);

		timeval TO;
		TO.tv_sec = 1;
		TO.tv_usec = 0;

		if ((available = select(0, &fdRead, NULL, NULL, &TO)) > 0) {
			ReceiverHeader rh;
			int responseSize;

			struct sockaddr_in res;
			int size = sizeof(res);

			if ((responseSize = recvfrom(sock, (char*)&rh, sizeof(ReceiverHeader), 0, (struct sockaddr*)&res, &size)) == SOCKET_ERROR) {
				printf("failed recvfrom with %d\n", WSAGetLastError());
				WSACleanup();
				closesocket(sock);
				delete packet;
				return FAILED_RECV;
			}

			if (rh.flags.ACK && rh.flags.FIN && !rh.flags.SYN) {
				WSACleanup();
				closesocket(sock);
				delete packet;

				printf("[%.2f] <-- FIN-ACK %d window %X\n", (clock() - transferStart) / (double)CLOCKS_PER_SEC, rh.ackSeq, rh.recvWnd);
				return STATUS_OK;
			}
		}
	}

	delete packet;
	return TIMEOUT;
}