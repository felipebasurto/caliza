#include "pcsx2/CDVD/CDVDcommon.h"

#include <cstdio>
#include <cstring>

int main()
{
	const unsigned char input[] = "123456789";
	if (cdvdSubQCrc(input, 9) != 0x31C3)
	{
		std::printf("unexpected CRC-16/CCITT check value: %04x\n", cdvdSubQCrc(input, 9));
		return 1;
	}

	cdvdSubQ q = {};
	q.ctrl = 4;
	q.adr = 1;
	q.trackNum = 0x01;
	q.trackIndex = 0x01;
	q.trackM = 0x00;
	q.trackS = 0x02;
	q.trackF = 0x00;
	q.discM = 0x00;
	q.discS = 0x02;
	q.discF = 0x00;

	u8 raw[16] = {};
	cdvdWriteSubQ16(raw, q);
	static constexpr u8 prefix[] = {0x41, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00};
	if (std::memcmp(raw, prefix, sizeof(prefix)) != 0)
	{
		std::printf("unexpected P/Q prefix\n");
		return 1;
	}
	const u16 crc = cdvdSubQCrc(raw, 10);
	if (crc != 0x5c8d || raw[10] != 0x5c || raw[11] != 0x8d || raw[12] || raw[13] || raw[14] || raw[15])
	{
		std::printf("bad P/Q CRC or reserved bytes\n");
		return 1;
	}
	std::printf("cdvd subq packing: PASS\n");
	return 0;
}
