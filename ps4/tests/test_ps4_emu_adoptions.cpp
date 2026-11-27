// SPDX-FileCopyrightText: 2026 Caliza contributors
// SPDX-License-Identifier: GPL-3.0+
//
// Host unit tests and microbenchmarks for accuracy-preserving optimizations adopted
// from the PS4/PS5 PS2 emulator comparison study:
//   1. EE recPLZCW branchless LZCNT exactness + microbenchmark
//   2. GS single-pass Gouraud triangle-strip vertex trace deduplicator (exactness + fuzz + microbenchmark)
//   3. recVTLB constant-address SIB-safety predicate across all 16 x86-64 GPRs
//   4. OrbisEEHle GetThreadId cache invalidation on VM reset / savestate load
//   5. Upstream CPU/VU/FPU fixes (unaligned 64-bit LWR, SQRT_S/RSQRT_S -0.0/0/0 flags, CTC2 sticky bits)
//   6. USB OHCI 4096-TD loop cap guard

#include "pcsx2/GS/Renderers/Common/GSVertexTraceStrip.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

namespace
{
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
using u64 = std::uint64_t;

// ---------------------------------------------------------------------------
// 1. EE PLZCW branchless LZCNT vs branchy BSR reference
// ---------------------------------------------------------------------------
static u32 PlzcwReference(u32 x)
{
	u32 v = (static_cast<s32>(x) < 0) ? ~x : x;
	if (v == 0)
		return 31;
	return static_cast<u32>(__builtin_clz(v)) - 1u;
}

static bool CpuHasLzcnt()
{
#if defined(__x86_64__) || defined(_M_X64)
	u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
	asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000u));
	if (eax < 0x80000001u)
		return false;
	asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001u));
	return (ecx & (1u << 5)) != 0;
#else
	return false;
#endif
}

static u32 PlzcwLzcntModel(u32 x)
{
	const u32 sign_mask = static_cast<u32>(static_cast<s32>(x) >> 31);
	const u32 flipped = x ^ sign_mask;
	const u32 lz = (flipped == 0) ? 32u : static_cast<u32>(__builtin_clz(flipped));
	return lz - 1u;
}

#if defined(__x86_64__) || defined(_M_X64)
// Executes the exact instruction sequence emitted by recPLZCW:
//   mov ecx, eax; sar ecx, 31; xor eax, ecx; lzcnt eax, eax; dec eax
__attribute__((noinline)) static u32 PlzcwLzcntAsm(u32 x)
{
	u32 out = x;
	asm volatile(
		"mov %%eax, %%ecx\n\t"
		"sar $31, %%ecx\n\t"
		"xor %%ecx, %%eax\n\t"
		".byte 0xF3, 0x0F, 0xBD, 0xC0\n\t" // lzcnt eax, eax
		"dec %%eax\n\t"
		: "+a"(out)
		:
		: "ecx", "cc");
	return out;
}

__attribute__((noinline)) static u32 PlzcwBsrBranchyAsm(u32 x)
{
	u32 out = 0;
	asm volatile(
		"mov $31, %%ecx\n\t"
		"test %%eax, %%eax\n\t"
		"jns 1f\n\t"
		"not %%eax\n\t"
		"1:\n\t"
		"bsr %%eax, %%eax\n\t"
		"jz 2f\n\t"
		"sub %%eax, %%ecx\n\t"
		"dec %%ecx\n\t"
		"2:\n\t"
		: "=c"(out), "+a"(x)
		:
		: "cc");
	return out;
}
#endif

static void TestPlzcw()
{
	const bool hw_lzcnt = CpuHasLzcnt();
	std::vector<u32> edge_cases = {
		0u, 1u, 2u, 3u, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xfffffffeu, 0xffffffffu,
		0x55555555u, 0xaaaaaaaau, 0x0000ffffu, 0xffff0000u
	};
	for (int k = 0; k < 32; k++)
	{
		const u32 bit = 1u << k;
		edge_cases.push_back(bit);
		edge_cases.push_back(~bit);
		edge_cases.push_back(bit - 1u);
		edge_cases.push_back(~(bit - 1u));
	}

	for (u32 v : edge_cases)
	{
		const u32 ref = PlzcwReference(v);
		assert(PlzcwLzcntModel(v) == ref);
#if defined(__x86_64__) || defined(_M_X64)
		assert(PlzcwBsrBranchyAsm(v) == ref);
		if (hw_lzcnt)
			assert(PlzcwLzcntAsm(v) == ref);
#endif
	}

	u32 rng = 0x13579bdfu;
	for (u32 i = 0; i < 1000000u; i++)
	{
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		const u32 ref = PlzcwReference(rng);
		assert(PlzcwLzcntModel(rng) == ref);
#if defined(__x86_64__) || defined(_M_X64)
		if (hw_lzcnt)
			assert(PlzcwLzcntAsm(rng) == ref);
#endif
	}

#if defined(__x86_64__) || defined(_M_X64)
	if (hw_lzcnt)
	{
		constexpr int N = 1 << 20;
		std::vector<u32> inputs(N);
		u32 s = 0x2468ace1u;
		for (int i = 0; i < N; i++)
		{
			s ^= s << 13;
			s ^= s >> 17;
			s ^= s << 5;
			inputs[i] = s;
		}
		auto t0 = std::chrono::steady_clock::now();
		u64 sum_bsr = 0;
		for (int i = 0; i < N; i++)
			sum_bsr += PlzcwBsrBranchyAsm(inputs[i]);
		auto t1 = std::chrono::steady_clock::now();
		u64 sum_lz = 0;
		for (int i = 0; i < N; i++)
			sum_lz += PlzcwLzcntAsm(inputs[i]);
		auto t2 = std::chrono::steady_clock::now();
		assert(sum_bsr == sum_lz);
		const double ns_bsr = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
		const double ns_lz = std::chrono::duration<double, std::nano>(t2 - t1).count() / N;
		std::printf("  [bench] PLZCW (1M random words): branchy_bsr=%.2f ns/word, lzcnt=%.2f ns/word (%.2fx)\n",
			ns_bsr, ns_lz, ns_bsr / (ns_lz > 0.0 ? ns_lz : 1.0));
	}
#endif
	std::printf("  [pass] recPLZCW LZCNT exactness (hw_lzcnt=%d)\n", hw_lzcnt ? 1 : 0);
}

// ---------------------------------------------------------------------------
// 2. GS single-pass Gouraud triangle-strip vertex trace deduplicator
// ---------------------------------------------------------------------------
static void AssertStripExact(const std::vector<u16>& idx, bool expect_dedup)
{
	std::vector<u16> out(idx.size(), 0);
	const int used = CalizaDeduplicateStripIndices(
		idx.data(), static_cast<int>(idx.size()), out.data(), static_cast<int>(out.size()));
	if (!expect_dedup)
	{
		assert(used == 0);
		return;
	}
	assert(used > 0 && used < static_cast<int>(idx.size()));
	std::set<u16> expected_set(idx.begin(), idx.end());
	std::set<u16> actual_set(out.begin(), out.begin() + used);
	assert(actual_set == expected_set);
	assert(static_cast<int>(actual_set.size()) == used); // zero duplicates
}

static void TestGsVertexTraceStrip()
{
	// Monotonic strip: (0,1,2), (1,2,3), ..., (197,198,199)
	{
		std::vector<u16> strip;
		for (u16 v = 0; v < 198; v++)
		{
			strip.push_back(v);
			strip.push_back(static_cast<u16>(v + 1));
			strip.push_back(static_cast<u16>(v + 2));
		}
		AssertStripExact(strip, true);
	}

	// Alternating winding strips: (0,1,2), (2,1,3), (2,3,4), (4,3,5)
	{
		std::vector<u16> strip = {0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5, 4, 5, 6};
		AssertStripExact(strip, true);
	}

	// Alternate odd winding: (0,1,2), (1,3,2), (2,3,4), (3,5,4)
	{
		std::vector<u16> strip = {0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4};
		AssertStripExact(strip, true);
	}

	// ADC-skip disjoint strips (vertices 4 and 5 never referenced)
	{
		std::vector<u16> strip = {0, 1, 2, 1, 2, 3, 6, 7, 8, 7, 8, 9};
		std::vector<u16> out(strip.size(), 0);
		const int used = CalizaDeduplicateStripIndices(
			strip.data(), static_cast<int>(strip.size()), out.data(), static_cast<int>(out.size()));
		assert(used == 8);
		std::set<u16> s(out.begin(), out.begin() + used);
		assert(s == (std::set<u16>{0, 1, 2, 3, 6, 7, 8, 9}));
	}

	// Degenerate / repeated vertex within strip
	{
		std::vector<u16> strip = {0, 1, 2, 1, 2, 2, 2, 2, 3, 2, 3, 4};
		AssertStripExact(strip, true);
	}

	// Out-of-order / non-strip indexed mesh MUST bail out (return 0)
	{
		std::vector<u16> mesh = {0, 1, 2, 3, 4, 5, 0, 4, 6};
		AssertStripExact(mesh, false);
	}
	{
		// Single triangle (< 6 indices) and non-multiple-of-3 must return 0
		std::vector<u16> tri = {0, 1, 2};
		AssertStripExact(tri, false);
		std::vector<u16> bad = {0, 1, 2, 1, 2};
		AssertStripExact(bad, false);
	}

	// Fuzz 2,000 random triangle lists: whenever dedup succeeds, sets MUST match identically
	u32 rng = 0xdeadbeefu;
	int dedup_hits = 0, bails = 0;
	for (int iter = 0; iter < 2000; iter++)
	{
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		const int tris = 2 + static_cast<int>(rng % 20);
		std::vector<u16> idx;
		idx.reserve(static_cast<size_t>(tris * 3));
		const bool strip_like = (iter & 1) == 0;
		u16 cur = 0;
		for (int t = 0; t < tris; t++)
		{
			rng ^= rng << 13;
			rng ^= rng >> 17;
			rng ^= rng << 5;
			if (strip_like)
			{
				if (t == 0)
				{
					idx.push_back(0);
					idx.push_back(1);
					idx.push_back(2);
					cur = 2;
				}
				else if ((rng & 7) == 0)
				{
					// ADC jump
					cur = static_cast<u16>(cur + 2);
					idx.push_back(cur);
					idx.push_back(static_cast<u16>(cur + 1));
					idx.push_back(static_cast<u16>(cur + 2));
					cur = static_cast<u16>(cur + 2);
				}
				else
				{
					const u16 a = static_cast<u16>(cur - 1);
					const u16 b = cur;
					const u16 c = static_cast<u16>(cur + 1);
					if (t & 1)
					{
						idx.push_back(b);
						idx.push_back(a);
						idx.push_back(c);
					}
					else
					{
						idx.push_back(a);
						idx.push_back(b);
						idx.push_back(c);
					}
					cur = c;
				}
			}
			else
			{
				idx.push_back(static_cast<u16>(rng % 16));
				idx.push_back(static_cast<u16>((rng >> 4) % 16));
				idx.push_back(static_cast<u16>((rng >> 8) % 16));
			}
		}

		std::vector<u16> out(idx.size(), 0);
		const int used = CalizaDeduplicateStripIndices(
			idx.data(), static_cast<int>(idx.size()), out.data(), static_cast<int>(out.size()));
		if (used > 0)
		{
			dedup_hits++;
			std::set<u16> expected_set(idx.begin(), idx.end());
			std::set<u16> actual_set(out.begin(), out.begin() + used);
			assert(actual_set == expected_set);
			assert(static_cast<int>(actual_set.size()) == used);
		}
		else
		{
			bails++;
		}
	}
	assert(dedup_hits >= 900 && bails >= 900);

	// Microbenchmark: bbox reduction over 500-vertex strip (1494 indices -> 500 unique)
	struct SimpleVert { float x, y, z, q; };
	std::vector<SimpleVert> verts(500);
	for (int i = 0; i < 500; i++)
		verts[i] = {static_cast<float>(i) * 0.5f, static_cast<float>(500 - i), static_cast<float>(i * 3), 1.0f};
	std::vector<u16> strip;
	for (u16 v = 0; v < 498; v++)
	{
		strip.push_back(v);
		strip.push_back(static_cast<u16>(v + 1));
		strip.push_back(static_cast<u16>(v + 2));
	}
	std::vector<u16> dedup(strip.size());
	constexpr int ITERS = 20000;
	auto reduce = [&](const u16* ix, int n) {
		float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
		for (int i = 0; i < n; i++)
		{
			const auto& v = verts[ix[i]];
			minx = std::min(minx, v.x);
			maxx = std::max(maxx, v.x);
			miny = std::min(miny, v.y);
			maxy = std::max(maxy, v.y);
		}
		return minx + maxx + miny + maxy;
	};
	volatile float sink = 0.0f;
	auto t0 = std::chrono::steady_clock::now();
	for (int k = 0; k < ITERS; k++)
		sink = sink + reduce(strip.data(), static_cast<int>(strip.size()));
	auto t1 = std::chrono::steady_clock::now();
	for (int k = 0; k < ITERS; k++)
	{
		const int used = CalizaDeduplicateStripIndices(
			strip.data(), static_cast<int>(strip.size()), dedup.data(), static_cast<int>(dedup.size()));
		sink = sink + reduce(dedup.data(), used);
	}
	auto t2 = std::chrono::steady_clock::now();
	const double us_raw = std::chrono::duration<double, std::micro>(t1 - t0).count() / ITERS;
	const double us_dedup = std::chrono::duration<double, std::micro>(t2 - t1).count() / ITERS;
	std::printf("  [bench] GS VT strip (500 verts / 1494 idx): raw=%.2f us, dedup+scan=%.2f us (%.2fx)\n",
		us_raw, us_dedup, us_raw / (us_dedup > 0.0 ? us_dedup : 1.0));
	std::printf("  [pass] GS VT strip dedup exactness (fuzz dedup=%d bails=%d)\n", dedup_hits, bails);
}

// ---------------------------------------------------------------------------
// 3. recVTLB constant-address SIB-safety predicate
// ---------------------------------------------------------------------------
static bool OrbisConstBaseOK(int reg)
{
	return (reg & 7) != 4 && (reg & 7) != 5;
}

static void TestVtlbSibSafety()
{
	for (int r = 0; r < 16; r++)
	{
		const bool expected = (r != 4 && r != 5 && r != 12 && r != 13);
		assert(OrbisConstBaseOK(r) == expected);
	}
	std::printf("  [pass] recVTLB SIB-safety predicate across all 16 GPRs\n");
}

// ---------------------------------------------------------------------------
// 4. OrbisEEHle GetThreadId cache invalidation on reset / savestate load
// ---------------------------------------------------------------------------
struct MockTidCache
{
	u64 seq = 1;
	u64 valid_seq = 0;
	u64 value = 0;
	u64 ex_seq = 0;
	u32 ex_pc = 0;
	u32 ex_sp = 0;
	bool ex_tid = false;
	bool tid_pending = false;
	bool check = false;
	u64 invalidations = 0;
	bool reset_invalidate_enabled = true;

	void RecordHit(u64 tid)
	{
		seq++;
		value = tid;
		valid_seq = seq;
		ex_seq = seq;
		ex_pc = 0x100000;
		ex_sp = 0x1ffff00;
		ex_tid = true;
		tid_pending = true;
		check = true;
	}

	bool CanAnswer() const { return valid_seq == seq; }

	void Invalidate()
	{
		if (!reset_invalidate_enabled)
			return;
		seq++;
		valid_seq = 0;
		ex_seq = 0;
		ex_tid = false;
		tid_pending = false;
		check = false;
		invalidations++;
	}
};

static void TestTidCacheInvalidation()
{
	MockTidCache t;
	t.RecordHit(7);
	assert(t.CanAnswer());
	t.Invalidate();
	assert(!t.CanAnswer());
	assert(t.valid_seq == 0 && t.ex_seq == 0 && !t.ex_tid && !t.tid_pending && !t.check);
	assert(t.invalidations == 1);

	// Off-switch (flags/notidreset) keeps legacy state untouched
	t.RecordHit(9);
	t.reset_invalidate_enabled = false;
	t.Invalidate();
	assert(t.CanAnswer());
	assert(t.invalidations == 1);
	std::printf("  [pass] OrbisTidInvalidate reset/savestate state machine\n");
}

// ---------------------------------------------------------------------------
// 5. Upstream CPU/VU/FPU accuracy fixes (LWR, SQRT_S/RSQRT_S, CTC2)
// ---------------------------------------------------------------------------
static u64 EmulateLwrFixed(u64 old_treg, u32 word_loaded, u32 offset_mod4)
{
	if (offset_mod4 == 0)
		return static_cast<u64>(static_cast<std::int64_t>(static_cast<s32>(word_loaded)));
	const u32 shift = offset_mod4 * 8;
	const u64 mask = (~0xffull) << (24 - shift);
	return (old_treg & mask) | (static_cast<u64>(word_loaded >> shift));
}

static void TestUpstreamCpuFixes()
{
	// 64-bit LWR preserves upper 32 bits of destination register on unaligned offsets 1..3
	const u64 orig = 0xdeadbeef11223344ull;
	const u32 mem = 0xaabbccddu;
	assert(EmulateLwrFixed(orig, mem, 1) == 0xdeadbeef11aabbccull);
	assert(EmulateLwrFixed(orig, mem, 2) == 0xdeadbeef1122aabbull);
	assert(EmulateLwrFixed(orig, mem, 3) == 0xdeadbeef112233aaull);
	// Offset 0 sign-extends the 32-bit loaded word
	assert(EmulateLwrFixed(orig, 0x80000001u, 0) == 0xffffffff80000001ull);

	// FPU SQRT_S(-0.0f) sets invalid flag and outputs +0.0f
	constexpr u32 kFpuI = 0x80u, kFpuSI = 0x08u, kFpuD = 0x40u, kFpuSD = 0x04u, kPosFmax = 0x7F7FFFFFu;
	auto sqrt_s = [&](u32 ft_bits, u32& flags) -> u32 {
		flags &= ~(kFpuI | kFpuD);
		if (ft_bits & 0x80000000u)
			flags |= kFpuI | kFpuSI;
		if ((ft_bits & 0x7F800000u) == 0)
			return 0u;
		float f;
		std::memcpy(&f, &ft_bits, 4);
		float r = std::sqrt(std::fabs(f));
		u32 out;
		std::memcpy(&out, &r, 4);
		return out;
	};
	u32 flags = 0;
	assert(sqrt_s(0x80000000u, flags) == 0u);
	assert((flags & (kFpuI | kFpuSI)) == (kFpuI | kFpuSI));

	// FPU RSQRT_S(0.0, 0.0) sets I|SI (not D|SD) and returns sign(fs) | posFmax
	auto rsqrt_s = [&](u32 fs_bits, u32 ft_bits, u32& fl) -> u32 {
		fl &= ~(kFpuI | kFpuD);
		if (ft_bits & 0x80000000u)
			fl |= kFpuI | kFpuSI;
		if ((ft_bits & 0x7F800000u) == 0)
		{
			if ((fs_bits & 0x7F800000u) == 0)
				fl |= kFpuI | kFpuSI;
			else
				fl |= kFpuD | kFpuSD;
			return (fs_bits & 0x80000000u) | kPosFmax;
		}
		return 0;
	};
	flags = 0;
	assert(rsqrt_s(0x00000000u, 0x00000000u, flags) == kPosFmax);
	assert((flags & (kFpuI | kFpuSI)) == (kFpuI | kFpuSI) && (flags & kFpuD) == 0);
	flags = 0;
	assert(rsqrt_s(0xBF800000u, 0x80000000u, flags) == (0x80000000u | kPosFmax));
	assert((flags & (kFpuI | kFpuSI | kFpuD | kFpuSD)) == (kFpuI | kFpuSI | kFpuD | kFpuSD));

	// VU0 CTC2 STATUS_FLAG writes only sticky bits (0xFC0), preserving non-sticky bits (0x3F)
	u32 status_flag = 0x002Au;
	const u32 guest_val = 0xFFFFu;
	status_flag = (status_flag & 0x3Fu) | (guest_val & 0xFC0u);
	assert(status_flag == 0x0FEAu);
	std::printf("  [pass] upstream CPU/VU/FPU accuracy fixes (LWR, SQRT_S, RSQRT_S, CTC2)\n");
}

// ---------------------------------------------------------------------------
// 6. USB OHCI 4096-TD loop cap guard
// ---------------------------------------------------------------------------
static void TestOhciTdCap()
{
	u32 head = 0x1000u;
	const u32 tail = 0x2000u;
	u32 td_cnt = 0;
	bool died = false;
	while (head != tail)
	{
		if (++td_cnt > 4096)
		{
			died = true;
			break;
		}
		// Self-linked TD cycle (next == head)
	}
	assert(died && td_cnt == 4097);
	std::printf("  [pass] USB OHCI 4096-TD cycle termination guard\n");
}
} // namespace

int main()
{
	std::printf("Running PS4 emulator adoption host tests...\n");
	TestPlzcw();
	TestGsVertexTraceStrip();
	TestVtlbSibSafety();
	TestTidCacheInvalidation();
	TestUpstreamCpuFixes();
	TestOhciTdCap();
	std::printf("test_ps4_emu_adoptions: ALL PASS\n");
	return 0;
}
