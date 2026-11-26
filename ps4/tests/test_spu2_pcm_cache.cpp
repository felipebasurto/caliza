// Host-side invariant check for the compacted SPU2 PCM cache entry layout.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;

static constexpr int pcm_DecodedSamplesPerBlock = 28;

struct PcmCacheEntry
{
	s16 Sampledata[pcm_DecodedSamplesPerBlock];
	s16 Prev1;
	s16 Prev2;
	bool Validated;
};

static const s32 tbl_XA_Factor[16][2] =
	{
		{0, 0},
		{60, 0},
		{115, -52},
		{98, -55},
		{122, -60}};

static void decode_block(s16* buffer, const s16* block, s32& prev1, s32& prev2)
{
	const s32 header = *block;
	const s32 shift = (header & 0xF) + 16;
	const int id = header >> 4 & 0xF;
	const s32 pred1 = tbl_XA_Factor[id][0];
	const s32 pred2 = tbl_XA_Factor[id][1];

	const s8* blockbytes = (s8*)&block[1];
	const s8* blockend = &blockbytes[13];

	for (; blockbytes <= blockend; ++blockbytes)
	{
		s32 data = ((*blockbytes) << 28) & 0xF0000000;
		s32 pcm = (data >> shift) + (((pred1 * prev1) + (pred2 * prev2) + 32) >> 6);

		pcm = std::clamp<s32>(pcm, -0x8000, 0x7fff);
		*(buffer++) = pcm;

		data = ((*blockbytes) << 24) & 0xF0000000;
		s32 pcm2 = (data >> shift) + (((pred1 * pcm) + (pred2 * prev1) + 32) >> 6);

		pcm2 = std::clamp<s32>(pcm2, -0x8000, 0x7fff);
		*(buffer++) = pcm2;

		prev2 = pcm;
		prev1 = pcm2;
	}
}

static int failures = 0;

static void check(bool condition, const char* what)
{
	if (!condition)
	{
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

int main()
{
	check(sizeof(PcmCacheEntry) == 62, "compacted PCM cache entry is 62 bytes");
	check(alignof(PcmCacheEntry) == 2, "compacted PCM cache entry has s16 alignment");

	std::mt19937 rng(0x53505532);
	for (int iteration = 0; iteration < 20000; iteration++)
	{
		s16 block[8];
		for (s16& word : block)
			word = static_cast<s16>(rng());

		// Valid decoder states begin at zero and thereafter are clamped output samples.
		s16 out[pcm_DecodedSamplesPerBlock] = {};
		s32 prev1 = static_cast<s16>(rng());
		s32 prev2 = static_cast<s16>(rng());
		decode_block(out, block, prev1, prev2);
		check(prev1 >= -32768 && prev1 <= 32767, "decoded prev1 stays representable as s16");
		check(prev2 >= -32768 && prev2 <= 32767, "decoded prev2 stays representable as s16");
		if (failures)
			break;
	}

	if (failures)
		return 1;
	std::puts("spu2 pcm cache invariants: PASS");
	return 0;
}
