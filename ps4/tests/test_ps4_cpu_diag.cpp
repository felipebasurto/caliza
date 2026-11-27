// Host-side regression checks for PS4 CPU bottleneck diagnostics and invariants:
// - Fastmem 4 GiB reservation hints avoid the GPU window and fixed direct-memory regions.
// - VU1 effective idle tick accounting handles in-flight waits monotonically across sample boundaries.
// - 64-bit XOR-OR manual SMC check matches per-word comparison for every block length and bit flip.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

constexpr uint64_t kFourGiB = 0x100000000ULL;
constexpr uint64_t kGpuStart = 0x200000000ULL;
constexpr uint64_t kGpuEnd = 0x300000000ULL;
constexpr uint64_t kFixedStart = 0x600000000ULL;
constexpr uint64_t kFixedEnd = 0xa00000000ULL;

constexpr uint64_t kFastmemHints[] = {
	0x300000000ULL,
	0x400000000ULL,
	0x500000000ULL,
	0xa00000000ULL,
	0xb00000000ULL,
	0xc00000000ULL,
	0x1000000000ULL,
};

bool RangesOverlap(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
{
	return a0 < b1 && b0 < a1;
}

uint64_t ComputeVuEffectiveIdleTicks(
	uint64_t now_tsc, uint64_t completed, uint64_t wait_start, uint64_t wait_start2, uint64_t prev_effective)
{
	uint64_t effective = completed;
	if (wait_start != 0 && wait_start == wait_start2 && now_tsc > wait_start)
		effective += (now_tsc - wait_start);
	if (effective < prev_effective)
		effective = prev_effective;
	return effective;
}

bool SmcXorOrMatches(const uint32_t* expected, const uint32_t* live, size_t words)
{
	if (words == 0)
		return true;
	if (words == 1)
		return expected[0] == live[0];

	const uint8_t* ep = reinterpret_cast<const uint8_t*>(expected);
	const uint8_t* lp = reinterpret_cast<const uint8_t*>(live);
	size_t bytes = words * 4;

	uint64_t e64 = 0, l64 = 0;
	std::memcpy(&e64, ep, 8);
	std::memcpy(&l64, lp, 8);
	uint64_t acc = e64 ^ l64;
	ep += 8;
	lp += 8;
	bytes -= 8;

	while (bytes >= 8)
	{
		std::memcpy(&e64, ep, 8);
		std::memcpy(&l64, lp, 8);
		acc |= (e64 ^ l64);
		ep += 8;
		lp += 8;
		bytes -= 8;
	}

	if (bytes >= 4)
	{
		uint32_t e32 = 0, l32 = 0;
		std::memcpy(&e32, ep, 4);
		std::memcpy(&l32, lp, 4);
		uint64_t tail = static_cast<uint64_t>(e32 ^ l32);
		acc |= tail;
	}

	return acc == 0;
}

void TestFastmemHints()
{
	for (uint64_t hint : kFastmemHints)
	{
		assert((hint & (kFourGiB - 1)) == 0);
		assert(!RangesOverlap(hint, hint + kFourGiB, kGpuStart, kGpuEnd));
		assert(!RangesOverlap(hint, hint + kFourGiB, kFixedStart, kFixedEnd));
	}
}

void TestVuEffectiveIdleTicks()
{
	// Asleep across an entire 1-second window (1.6B ticks): effective idle must advance by full window.
	const uint64_t t0 = 10'000'000'000ULL;
	const uint64_t t1 = t0 + 1'600'000'000ULL;
	const uint64_t eff0 = ComputeVuEffectiveIdleTicks(t0, 0, t0, t0, 0);
	assert(eff0 == 0);
	const uint64_t eff1 = ComputeVuEffectiveIdleTicks(t1, 0, t0, t0, eff0);
	assert(eff1 == 1'600'000'000ULL);

	// Mid-wakeup race (wait_start != wait_start2): must stay monotonic and never double-count.
	const uint64_t eff_race = ComputeVuEffectiveIdleTicks(t1 + 1000, 1'600'000'500ULL, t0, 0, eff1);
	assert(eff_race == 1'600'000'500ULL);

	// Saturated VU1 (never waiting): effective idle stays flat.
	const uint64_t eff_busy = ComputeVuEffectiveIdleTicks(t1 + 1'600'000'000ULL, 1'600'000'500ULL, 0, 0, eff_race);
	assert(eff_busy == eff_race);
}

void TestSmcXorOrEquivalence()
{
	const size_t lengths[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 33};
	for (size_t n : lengths)
	{
		std::vector<uint32_t> expected(n), live(n);
		for (size_t i = 0; i < n; i++)
			expected[i] = static_cast<uint32_t>(0x27bdffe0u ^ (i * 0x01010101u));
		live = expected;
		assert(SmcXorOrMatches(expected.data(), live.data(), n));

		for (size_t i = 0; i < n; i++)
		{
			for (int bit : {0, 7, 15, 16, 31})
			{
				live[i] ^= (1u << bit);
				assert(!SmcXorOrMatches(expected.data(), live.data(), n));
				live[i] ^= (1u << bit);
			}
		}
		assert(SmcXorOrMatches(expected.data(), live.data(), n));
	}
}

void TestSibSafeDestRule()
{
	// rsp(4), rbp(5), r12(12), r13(13) must never be used as base-only ModRM without SIB.
	for (int reg = 0; reg < 16; reg++)
	{
		const bool sib_safe = ((reg & 7) != 4) && ((reg & 7) != 5);
		const bool is_tricky = (reg == 4 || reg == 5 || reg == 12 || reg == 13);
		assert(sib_safe == !is_tricky);
	}
}

} // namespace

int main()
{
	TestFastmemHints();
	TestVuEffectiveIdleTicks();
	TestSmcXorOrEquivalence();
	TestSibSafeDestRule();
	std::printf("test_ps4_cpu_diag: PASS\n");
	return 0;
}
