// SPDX-FileCopyrightText: 2026 Caliza contributors
// SPDX-License-Identifier: GPL-3.0+
//
// Host unit tests for PS4 non-speed-hack CPU/VIF/MTVU optimizations:
//   1. Exact COP0 event-test scheduling for ERET, EI, and WriteCP0Status
//   2. VIF1 fast inner loop & whole-UNPACK classifier/equivalence
//   3. MTVU_VU_WRITE_MICRO unchanged-microcode invalidation skip

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
using u64 = std::uint64_t;

// ---------------------------------------------------------------------------
// 1. COP0 event-test elision model (mirrors pcsx2/COP0.cpp OrbisCop0NeedsImmediateEvent)
// ---------------------------------------------------------------------------
struct MockCop0State
{
	u32 status = 0;
	u32 count = 0;
	u32 compare = 0;
	u64 cycle = 0;
	u64 last_cop0_cycle = 0;
	u64 next_event_cycle = 0;
	u32 intc_stat = 0;
	u32 intc_mask = 0;
	u16 dmac_cis = 0;  // 0xe010
	u16 dmac_cim = 0;  // 0xe012
	u32 vpu_stat = 0;
	bool eret_fast = true;

	u64 eret_skips = 0;
	u64 ei_skips = 0;
	u64 status_skips = 0;
	u64 timer_sched = 0;
	u64 eret_block[4] = {};

	void SetNextEventDelta(s32 delta)
	{
		if (static_cast<s32>(next_event_cycle - cycle) > delta)
			next_event_cycle = cycle + static_cast<u64>(static_cast<s64>(delta));
	}

	bool NeedsImmediateEvent(bool record_eret_block)
	{
		const bool ints_enabled = (status & 0x10007u) == 0x10001u;
		const bool intc = ints_enabled && (status & 0x400u) != 0 && (intc_stat & intc_mask) != 0;
		const bool dmac = ints_enabled && (status & 0x800u) != 0 &&
			(((dmac_cim & dmac_cis) != 0) || ((dmac_cis & 0x8000u) != 0));
		bool timer = false;
		if (ints_enabled && (status & 0x8000u) != 0)
		{
			const u32 now = count + static_cast<u32>(cycle - last_cop0_cycle);
			const s32 until = static_cast<s32>(compare - now);
			if (until <= 4)
				timer = (until > -1000);
			else
			{
				SetNextEventDelta(until);
				timer_sched++;
			}
		}
		const bool vu0 = (vpu_stat & 1u) != 0;
		if (record_eret_block)
		{
			eret_block[0] += intc;
			eret_block[1] += dmac;
			eret_block[2] += timer;
			eret_block[3] += vu0;
		}
		return intc || dmac || timer || vu0;
	}

	void WriteCP0Status(u32 value)
	{
		status = value;
		if (eret_fast && !NeedsImmediateEvent(false))
			status_skips++;
		else
			SetNextEventDelta(4);
	}

	void RecERET()
	{
		if (!eret_fast)
			next_event_cycle = cycle;
		status &= ~0x2u; // clear EXL
		if (eret_fast)
		{
			if (!NeedsImmediateEvent(true))
				eret_skips++;
			else
				SetNextEventDelta(4);
		}
		else
		{
			SetNextEventDelta(4);
		}
	}

	void RecEI()
	{
		if (!eret_fast)
			next_event_cycle = cycle;
		status |= 0x10000u; // set EIE
		if (eret_fast && !NeedsImmediateEvent(false))
			ei_skips++;
		else
			SetNextEventDelta(4);
	}
};

void TestCop0EventElision()
{
	// Typical PS2 user/kernel status with IM2 (INTC), IM3 (DMAC), IM7 (COP0 timer), IE, EIE all set.
	constexpr u32 kPs2EnabledStatus = 0x70038c01u; // EIE=1, IM7=1 (0x8000), IM3=1, IM2=1, IE=1, EXL=0, ERL=0

	// 1. Steady state: IM7 is set (as PS2 kernel always leaves it), Compare=0, Count=10M -> no immediate test!
	{
		MockCop0State s;
		s.status = kPs2EnabledStatus | 0x2u; // EXL=1 before ERET
		s.count = 10'000'000u;
		s.compare = 0u;
		s.cycle = 1'000'000ull;
		s.last_cop0_cycle = 1'000'000ull;
		s.next_event_cycle = s.cycle + 5000ull;
		s.RecERET();
		assert(s.eret_skips == 1);
		assert(s.next_event_cycle == s.cycle + 5000ull);
		assert(s.eret_block[0] == 0 && s.eret_block[1] == 0 && s.eret_block[2] == 0 && s.eret_block[3] == 0);
	}

	// 2. Pending unmasked INTC -> MUST schedule 4-cycle event test.
	{
		MockCop0State s;
		s.status = kPs2EnabledStatus | 0x2u;
		s.count = 10'000'000u;
		s.cycle = 5000ull;
		s.last_cop0_cycle = 5000ull;
		s.next_event_cycle = s.cycle + 5000ull;
		s.intc_stat = 0x04u;
		s.intc_mask = 0x04u;
		s.RecERET();
		assert(s.eret_skips == 0);
		assert(s.eret_block[0] == 1);
		assert(s.next_event_cycle == s.cycle + 4ull);
	}

	// 3. Pending INTC when WriteCP0Status disables interrupts (EXL=1 or EIE=0) -> MUST skip!
	{
		MockCop0State s;
		s.count = 10'000'000u;
		s.cycle = 5000ull;
		s.last_cop0_cycle = 5000ull;
		s.next_event_cycle = s.cycle + 5000ull;
		s.intc_stat = 0x04u;
		s.intc_mask = 0x04u;
		s.WriteCP0Status(kPs2EnabledStatus & ~0x10000u); // EIE=0
		assert(s.status_skips == 1);
		assert(s.next_event_cycle == s.cycle + 5000ull);
		s.WriteCP0Status(kPs2EnabledStatus | 0x2u); // EXL=1
		assert(s.status_skips == 2);
		assert(s.next_event_cycle == s.cycle + 5000ull);
		s.WriteCP0Status(kPs2EnabledStatus & ~0x400u); // IM2=0 masks INTC
		assert(s.status_skips == 3);
		assert(s.next_event_cycle == s.cycle + 5000ull);
	}

	// 4. Pending unmasked DMAC (CIS & CIM or MEIS bit 15) -> MUST schedule 4-cycle test.
	{
		MockCop0State s;
		s.status = kPs2EnabledStatus & ~0x10000u;
		s.count = 10'000'000u;
		s.cycle = 8000ull;
		s.last_cop0_cycle = 8000ull;
		s.next_event_cycle = s.cycle + 5000ull;
		s.dmac_cis = 0x8000u; // MEIS
		s.RecEI();
		assert(s.ei_skips == 0);
		assert(s.next_event_cycle == s.cycle + 4ull);
	}

	// 5. Running VU0 microprogram -> MUST schedule 4-cycle test even if interrupts are disabled.
	{
		MockCop0State s;
		s.cycle = 9000ull;
		s.last_cop0_cycle = 9000ull;
		s.next_event_cycle = s.cycle + 5000ull;
		s.vpu_stat = 1u;
		s.WriteCP0Status(0u);
		assert(s.status_skips == 0);
		assert(s.next_event_cycle == s.cycle + 4ull);
	}

	// 6. COP0 Count/Compare exact window and wraparound handling.
	{
		// 6a. 20 cycles before Compare -> schedules exact cycle + 20, no immediate 4-cycle test.
		MockCop0State s;
		s.status = kPs2EnabledStatus | 0x2u;
		s.count = 1000u;
		s.compare = 1030u;
		s.cycle = 2010ull;
		s.last_cop0_cycle = 2000ull; // now = 1010, until = +20
		s.next_event_cycle = s.cycle + 5000ull;
		s.RecERET();
		assert(s.eret_skips == 1);
		assert(s.timer_sched == 1);
		assert(s.next_event_cycle == s.cycle + 20ull);

		// 6b. 4 cycles before Compare -> immediate 4-cycle test.
		s.count = 1026u;
		s.last_cop0_cycle = s.cycle;
		s.next_event_cycle = s.cycle + 5000ull;
		s.status |= 0x2u;
		s.RecERET();
		assert(s.eret_block[2] == 1);
		assert(s.next_event_cycle == s.cycle + 4ull);

		// 6c. Inside [Compare, Compare + 1000) window (until == -999) -> immediate.
		s.count = 1030u + 999u;
		s.last_cop0_cycle = s.cycle;
		s.next_event_cycle = s.cycle + 5000ull;
		s.status |= 0x2u;
		s.RecERET();
		assert(s.eret_block[2] == 2);
		assert(s.next_event_cycle == s.cycle + 4ull);

		// 6d. Past window (until == -1000) -> NOT immediate.
		s.count = 1030u + 1000u;
		s.last_cop0_cycle = s.cycle;
		s.next_event_cycle = s.cycle + 5000ull;
		s.status |= 0x2u;
		s.RecERET();
		assert(s.eret_block[2] == 2);
		assert(s.next_event_cycle == s.cycle + 5000ull);

		// 6e. 32-bit wraparound: Compare = 0, Count = 0xFFFFFFFC (until = +4) -> immediate.
		s.compare = 0u;
		s.count = 0xFFFFFFFCu;
		s.last_cop0_cycle = s.cycle;
		s.next_event_cycle = s.cycle + 5000ull;
		s.status |= 0x2u;
		s.RecERET();
		assert(s.eret_block[2] == 3);
		assert(s.next_event_cycle == s.cycle + 4ull);

		// 6f. 32-bit wraparound: Compare = 0, Count = 0xFFFFFFE0 (until = +32) -> exact cycle + 32.
		s.count = 0xFFFFFFE0u;
		s.last_cop0_cycle = s.cycle;
		s.next_event_cycle = s.cycle + 5000ull;
		s.status |= 0x2u;
		s.RecERET();
		assert(s.next_event_cycle == s.cycle + 32ull);
	}

	// 7. Escape hatch (flags/noeretfast -> eret_fast = false) forces immediate nextEventCycle = cycle.
	{
		MockCop0State s;
		s.eret_fast = false;
		s.status = kPs2EnabledStatus | 0x2u;
		s.count = 10'000'000u;
		s.compare = 0u;
		s.cycle = 50000ull;
		s.last_cop0_cycle = 50000ull;
		s.next_event_cycle = s.cycle + 5000ull;
		s.RecERET();
		assert(s.next_event_cycle == s.cycle);
		s.next_event_cycle = s.cycle + 5000ull;
		s.RecEI();
		assert(s.next_event_cycle == s.cycle);
		s.next_event_cycle = s.cycle + 5000ull;
		s.WriteCP0Status(kPs2EnabledStatus);
		assert(s.next_event_cycle == s.cycle + 4ull);
	}
}

// ---------------------------------------------------------------------------
// 2. VIF1 unpack classifier & inner-loop bit-exactness test
// ---------------------------------------------------------------------------
bool IsValidVif1UnpackCmd(u32 cmd)
{
	if (cmd < 0x60u || cmd >= 0x80u)
		return false;
	const u32 upk = cmd & 0x0fu;
	return (upk & 3u) != 3u || upk == 15u;
}

static constexpr u8 kVifT[16] = {
	4, 2, 1, 0, // S-32, S-16, S-8, invalid
	8, 4, 2, 0, // V2-32, V2-16, V2-8, invalid
	12, 6, 3, 0, // V3-32, V3-16, V3-8, invalid
	16, 8, 4, 2, // V4-32, V4-16, V4-8, V4-5
};

struct MockVif1State
{
	u32 code = 0;
	u32 cmd = 0;
	u32 pass = 0;
	u32 irq = 0;
	u32 mode = 0;
	u32 cycle = 0;
	u32 base = 0;
	u32 ofst = 0;
	u32 tops = 0;
	u32 itops = 0;
	u32 num = 0;
	u32 tag_addr = 0;
	u32 tag_size = 0;
	u32 tag_cmd = 0;
	u32 start_aligned = 0;
	u32 vifpacketsize = 0;
	bool dbf = false;
	bool stalled = false;
	u32 stall_val = 0;
	u32 bsize = 0;
	u32 other_cmds = 0;
	std::vector<u32> emitted_unpacks; // flattened metadata + payload words

	bool operator==(const MockVif1State& o) const
	{
		return code == o.code && cmd == o.cmd && pass == o.pass && irq == o.irq && mode == o.mode &&
			cycle == o.cycle && base == o.base && ofst == o.ofst && tops == o.tops && itops == o.itops &&
			num == o.num && tag_addr == o.tag_addr && tag_size == o.tag_size && tag_cmd == o.tag_cmd &&
			start_aligned == o.start_aligned && vifpacketsize == o.vifpacketsize && dbf == o.dbf &&
			stalled == o.stalled && stall_val == o.stall_val && bsize == o.bsize &&
			other_cmds == o.other_cmds && emitted_unpacks == o.emitted_unpacks;
	}

	void UnpackSetup()
	{
		u32 n = (code >> 16) & 0xffu;
		if (n == 0)
			n = 256;
		num = n;
		const u8 gsize = kVifT[cmd & 0x0fu];
		const u32 cl = cycle & 0xffu;
		u32 wl = (cycle >> 8) & 0xffu;
		if (wl == 0)
			wl = 256;
		if (wl <= cl)
			tag_size = ((n * gsize) + 3u) / 4u;
		else
		{
			const u32 rem = n % wl;
			const u32 count = cl * (n / wl) + (rem < cl ? rem : cl);
			tag_size = ((count * gsize) + 3u) >> 2;
		}
		u32 addr = code;
		if ((addr >> 15) & 1u)
			addr += tops;
		tag_addr = (addr << 4) & 0x3ff0u;
		tag_cmd = cmd;
		pass = 1;
		start_aligned = 4u - ((vifpacketsize - 1u) & 3u);
	}

	void EmitUnpack(const u32* payload, u32 words)
	{
		emitted_unpacks.push_back(tag_addr);
		emitted_unpacks.push_back(tag_cmd);
		emitted_unpacks.push_back(num);
		emitted_unpacks.push_back(start_aligned);
		emitted_unpacks.push_back(words);
		for (u32 i = 0; i < words; ++i)
			emitted_unpacks.push_back(payload[i]);
	}
};

void RunVif1Loop(MockVif1State& s, u32*& data, bool use_fast_inner)
{
	while (s.vifpacketsize > 0 && !s.stalled)
	{
		if (use_fast_inner && !s.cmd && !s.pass && !s.irq)
		{
			u32* d = data;
			u32 left = s.vifpacketsize;
			while (left > 0 && !s.stalled)
			{
				const u32 code = d[0];
				if (static_cast<s32>(code) < 0)
					break;
				const u32 cmd = code >> 24;
				if (cmd == 0)
				{
					s.code = code;
					if (left > 1 && ((d[1] >> 24) & 0x7fu) == 0x6u && (d[1] & 1u))
					{
						s.stalled = true;
						s.stall_val = 1;
					}
					d += 1;
					left -= 1;
					continue;
				}
				if (cmd <= 0x05u)
				{
					s.code = code;
					switch (cmd)
					{
						case 0x05u: s.mode = code & 3u; break;
						case 0x01u: s.cycle = code & 0xffffu; break;
						case 0x03u: s.base = code & 0x3ffu; break;
						case 0x02u: s.dbf = false; s.ofst = code & 0x3ffu; s.tops = s.base; break;
						case 0x04u: s.itops = code & 0x3ffu; break;
					}
					d += 1;
					left -= 1;
					continue;
				}
				if (IsValidVif1UnpackCmd(cmd) && s.bsize == 0)
				{
					s.code = code;
					s.cmd = cmd;
					s.vifpacketsize = left;
					s.UnpackSetup();
					d += 1;
					left -= 1;
					const u32 words = s.tag_size;
					if (words <= left && !s.stalled)
					{
						s.EmitUnpack(d, words);
						s.pass = 0;
						s.tag_size = 0;
						s.cmd = 0;
						s.num = 0;
						d += words;
						left -= words;
						continue;
					}
					break;
				}
				break;
			}
			data = d;
			s.vifpacketsize = left;
			if (s.vifpacketsize == 0 || s.stalled)
				break;
		}

		if (!s.cmd)
		{
			if (s.irq)
				break;
			s.irq |= data[0] >> 31;
			s.code = data[0];
			s.cmd = data[0] >> 24;
		}

		const u32 op = s.cmd & 0x7fu;
		if (s.pass == 0)
		{
			if (op == 0)
			{
				if (s.vifpacketsize > 1 && ((data[1] >> 24) & 0x7fu) == 0x6u && (data[1] & 1u))
				{
					s.stalled = true;
					s.stall_val = 1;
				}
				s.cmd = 0;
				data += 1;
				s.vifpacketsize -= 1;
			}
			else if (op <= 0x05u)
			{
				switch (op)
				{
					case 0x05u: s.mode = s.code & 3u; break;
					case 0x01u: s.cycle = s.code & 0xffffu; break;
					case 0x03u: s.base = s.code & 0x3ffu; break;
					case 0x02u: s.dbf = false; s.ofst = s.code & 0x3ffu; s.tops = s.base; break;
					case 0x04u: s.itops = s.code & 0x3ffu; break;
				}
				s.cmd = 0;
				data += 1;
				s.vifpacketsize -= 1;
			}
			else if (op >= 0x60u && op != 0x67u && op != 0x77u && op != 0x7bu)
			{
				s.UnpackSetup();
				data += 1;
				s.vifpacketsize -= 1;
			}
			else
			{
				// Other commands (MSCAL, MSKPATH3, NULL, etc.) consume 1 word in mock.
				s.other_cmds++;
				s.cmd = 0;
				data += 1;
				s.vifpacketsize -= 1;
			}
		}
		else
		{
			const u32 ret = s.vifpacketsize < s.tag_size ? s.vifpacketsize : s.tag_size;
			if (ret == s.tag_size)
			{
				s.EmitUnpack(data, ret);
				s.pass = 0;
				s.tag_size = 0;
				s.cmd = 0;
				s.num = 0;
			}
			else
			{
				s.tag_size -= ret;
				s.bsize += ret << 2;
			}
			data += ret;
			s.vifpacketsize -= ret;
		}
	}
}

void TestVif1ClassifierAndFastLoop()
{
	// Verify all 256 command bytes against valid unpack set.
	int valid_count = 0;
	for (u32 cmd = 0; cmd < 256; ++cmd)
	{
		const bool valid = IsValidVif1UnpackCmd(cmd);
		if (cmd < 0x60u || cmd >= 0x80u)
			assert(!valid);
		else
		{
			const u32 upk = cmd & 0x0fu;
			const bool expected = (kVifT[upk] != 0);
			assert(valid == expected);
			if (valid)
				valid_count++;
		}
	}
	assert(valid_count == 26); // 13 valid formats x 2 (unmasked/masked)

	// Build mixed stream: NOP, STCYCL, BASE, OFFSET, STMOD, ITOP, V4_32 UNPACK, V3_16 UNPACK,
	// MSCAL (falls back to outer), V4_8 UNPACK, split UNPACK across chunk boundary, IRQ-flagged UNPACK,
	// and NOP + MSKPATH3 stall.
	std::vector<u32> stream;
	stream.push_back(0x00000000u); // NOP
	stream.push_back(0x01000404u); // STCYCL wl=4, cl=4
	stream.push_back(0x03000020u); // BASE 0x20
	stream.push_back(0x02000100u); // OFFSET 0x100
	stream.push_back(0x05000001u); // STMOD 1
	stream.push_back(0x04000077u); // ITOP 0x77
	// V4_32 UNPACK (0x6c), num=2 -> 8 words
	stream.push_back(0x6c028004u);
	for (u32 i = 0; i < 8; ++i)
		stream.push_back(0x1000u + i);
	// V3_16 UNPACK (0x69), num=3 -> ((3*6)+3)/4 = 5 words
	stream.push_back(0x69030010u);
	for (u32 i = 0; i < 5; ++i)
		stream.push_back(0x2000u + i);
	// MSCAL (0x14) -> exits inner loop to outer loop
	stream.push_back(0x14000000u);
	// Masked V4_8 UNPACK (0x7e), num=4 -> 4 words
	stream.push_back(0x7e048008u);
	for (u32 i = 0; i < 4; ++i)
		stream.push_back(0x3000u + i);
	// Reserved/invalid 0x67 (vifCode_Null) -> exits inner loop cleanly
	stream.push_back(0x67000000u);
	// IRQ-flagged STMOD (0x85) -> exits inner loop cleanly
	stream.push_back(0x85000002u);

	// Test 1: whole buffer at once
	{
		MockVif1State ref, fast;
		ref.vifpacketsize = static_cast<u32>(stream.size());
		fast.vifpacketsize = static_cast<u32>(stream.size());
		u32* p_ref = stream.data();
		u32* p_fast = stream.data();
		RunVif1Loop(ref, p_ref, false);
		RunVif1Loop(fast, p_fast, true);
		assert(p_ref == p_fast);
		assert(ref == fast);
		assert(ref.irq == 1);
	}

	// Test 2: split UNPACK at chunk boundary + NOP before MSKPATH3 stall
	{
		std::vector<u32> chunk1 = {
			0x01000404u, // STCYCL wl=4, cl=4
			0x6c020000u, // V4_32 num=2 -> needs 8 words, only 3 here (split!)
			0xaaaa0001u, 0xaaaa0002u, 0xaaaa0003u,
		};
		std::vector<u32> chunk2 = {
			0xaaaa0004u, 0xaaaa0005u, 0xaaaa0006u, 0xaaaa0007u, 0xaaaa0008u,
			0x00000000u, // NOP before MSKPATH3
			0x06000001u, // MSKPATH3 enable -> causes timing break on preceding NOP
		};
		MockVif1State ref, fast;
		u32* p1_ref = chunk1.data();
		u32* p1_fast = chunk1.data();
		ref.vifpacketsize = static_cast<u32>(chunk1.size());
		fast.vifpacketsize = static_cast<u32>(chunk1.size());
		RunVif1Loop(ref, p1_ref, false);
		RunVif1Loop(fast, p1_fast, true);
		assert(p1_ref == p1_fast);
		assert(ref == fast);
		assert(ref.pass == 1 && ref.tag_size == 5);

		u32* p2_ref = chunk2.data();
		u32* p2_fast = chunk2.data();
		ref.vifpacketsize = static_cast<u32>(chunk2.size());
		fast.vifpacketsize = static_cast<u32>(chunk2.size());
		RunVif1Loop(ref, p2_ref, false);
		RunVif1Loop(fast, p2_fast, true);
		assert(p2_ref == p2_fast);
		assert(ref == fast);
		assert(ref.stalled && ref.stall_val == 1);
	}
}

// ---------------------------------------------------------------------------
// 3. MTVU_VU_WRITE_MICRO unchanged-microcode skip test
// ---------------------------------------------------------------------------
bool MtvuWriteMicroSkip(u8 (&vu_micro)[0x4000], u32 addr, const u8* src, u32 size, u32& clears)
{
	if (size > 0 && addr <= sizeof(vu_micro) && size <= sizeof(vu_micro) - addr &&
		std::memcmp(&vu_micro[addr], src, size) == 0)
	{
		return true; // skipped Clear + copy
	}
	clears++;
	if (size > 0 && addr <= sizeof(vu_micro) && size <= sizeof(vu_micro) - addr)
		std::memcpy(&vu_micro[addr], src, size);
	return false;
}

void TestMtvuMicroSkip()
{
	u8 vu_micro[0x4000];
	std::memset(vu_micro, 0, sizeof(vu_micro));
	u8 prog[512];
	for (u32 i = 0; i < sizeof(prog); ++i)
		prog[i] = static_cast<u8>((i * 37u) ^ 0x5au);

	u32 clears = 0;
	// Initial upload differs -> must Clear and copy.
	assert(!MtvuWriteMicroSkip(vu_micro, 0x200, prog, sizeof(prog), clears));
	assert(clears == 1);
	assert(std::memcmp(&vu_micro[0x200], prog, sizeof(prog)) == 0);

	// Repeated identical upload -> must skip Clear.
	assert(MtvuWriteMicroSkip(vu_micro, 0x200, prog, sizeof(prog), clears));
	assert(clears == 1);

	// Single-byte difference at end -> must Clear and update.
	prog[sizeof(prog) - 1] ^= 0x01u;
	assert(!MtvuWriteMicroSkip(vu_micro, 0x200, prog, sizeof(prog), clears));
	assert(clears == 2);
	assert(vu_micro[0x200 + sizeof(prog) - 1] == prog[sizeof(prog) - 1]);

	// Overflow-safe bounds guard: addr near end + wrapping size must not match or crash.
	assert(!MtvuWriteMicroSkip(vu_micro, 0x3f00, prog, 0x200, clears));
	assert(clears == 3);
}

} // namespace

int main()
{
	TestCop0EventElision();
	TestVif1ClassifierAndFastLoop();
	TestMtvuMicroSkip();
	std::printf("test_ps4_perf_non_speedhack: PASS\n");
	return 0;
}
