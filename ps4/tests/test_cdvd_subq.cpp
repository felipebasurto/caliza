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
	u8 raw96[96] = {};
	cdvdWriteSubQ96(raw96, q);
	u8 decoded[12] = {};
	for (int byte = 0; byte < 12; byte++)
	{
		for (int bit = 0; bit < 8; bit++)
		{
			decoded[byte] <<= 1;
			if (raw96[byte * 8 + bit] & 0x40)
				decoded[byte] |= 1;
		}
	}
	if (std::memcmp(decoded, prefix, sizeof(prefix)) != 0)
	{
		std::printf("unexpected mode-2448 Q payload\n");
		return 1;
	}
	const u16 raw_crc = static_cast<u16>((decoded[10] << 8) | decoded[11]);
	if (raw_crc != static_cast<u16>(~crc))
	{
		std::printf("bad mode-2448 complemented Q CRC: %04x\n", raw_crc);
		return 1;
	}
	for (int i = 0; i < 96; i++)
	{
		if (raw96[i] & 0xbf)
		{
			std::printf("unexpected mode-2448 non-Q bit at %d\n", i);
			return 1;
		}
	}

	std::printf("cdvd subq packing: PASS\n");
	return 0;
}
