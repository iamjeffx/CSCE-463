#pragma once

#include "pch.h"

class Checksum {
public:
	DWORD crc_table[256];

	Checksum();
	DWORD CRC32(unsigned char* buf, size_t len);
};

