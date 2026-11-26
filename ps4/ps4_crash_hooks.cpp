// PS4 port (AI-assisted): emulator state for CEMUPS4's crash/hang reports.
// These hooks can run in a fatal-signal context: no allocation, no locks, no frontend state.
// Needs proper testing on the console.

#include "R3000A.h"
#include "R5900.h"

#include <cstdint>
#include <unistd.h>

extern "C" volatile unsigned long long g_orbis_data_base;
extern "C" volatile unsigned long long g_orbis_data_size;
extern "C" volatile unsigned long long g_orbis_code_base;
extern "C" volatile unsigned long long g_orbis_code_size;
extern "C" int g_orbis_code_direct;

namespace
{
void write_text(int fd, const char* text, int length)
{
	while (length > 0)
	{
		const ssize_t written = write(fd, text, static_cast<size_t>(length));
		if (written <= 0)
			return;
		text += written;
		length -= static_cast<int>(written);
	}
}

struct Line
{
	char buffer[384];
	size_t length = 0;

	void ch(char c)
	{
		if (length + 1 < sizeof(buffer))
			buffer[length++] = c;
	}

	void text(const char* value)
	{
		while (*value)
			ch(*value++);
	}

	void hex(unsigned long long value, int width)
	{
		for (int shift = (width - 1) * 4; shift >= 0; shift -= 4)
			ch("0123456789abcdef"[(value >> shift) & 0xf]);
	}

	void hex64(unsigned long long value)
	{
		text("0x");
		if (!value)
		{
			ch('0');
			return;
		}
		bool started = false;
		for (int shift = 60; shift >= 0; shift -= 4)
		{
			const unsigned digit = (value >> shift) & 0xf;
			if (digit || started || shift == 0)
			{
				ch("0123456789abcdef"[digit]);
				started = true;
			}
		}
	}

	void dec(unsigned long long value)
	{
		char digits[20];
		int count = 0;
		do
		{
			digits[count++] = static_cast<char>('0' + value % 10);
			value /= 10;
		} while (value && count < 20);
		while (count > 0)
			ch(digits[--count]);
	}

	void flush(int fd)
	{
		write_text(fd, buffer, static_cast<int>(length));
	}
};

bool in_range(unsigned long long address, unsigned long long base, unsigned long long size)
{
	return base != 0 && address >= base && address - base < size;
}
} // namespace

extern "C" {

// A compact guest-program-counter snapshot beside the runtime's host register dump. The values may be
// mid-update if another thread crashed, but they do not require any lock to read.
void ps4_crash_guest_regs(int fd)
{
	Line line;
	line.text("guest: eepc=");
	line.hex(cpuRegs.pc, 8);
	line.text(" eecy=");
	line.dec(cpuRegs.cycle);
	line.text(" eeinsn=");
	line.hex(cpuRegs.code, 8);
	line.text(" ioppc=");
	line.hex(psxRegs.pc, 8);
	line.text(" iopcy=");
	line.dec(psxRegs.cycle);
	line.text(" iopinsn=");
	line.hex(psxRegs.code, 8);
	line.ch('\n');
	line.flush(fd);
}

// The recompiler's exact block ownership is not yet recoverable. Even this range check is useful in
// crash.log: it distinguishes JIT code, guest-data corruption, and library/runtime code immediately.
void ps4_crash_jit_info(int fd, uint64_t rip, int brief)
{
	const unsigned long long base = g_orbis_code_base;
	const unsigned long long size = g_orbis_code_size;
	if (!in_range(rip, base, size))
	{
		if (!brief)
		{
			Line line;
			line.text("jit: rip ");
			line.hex64(rip);
			line.text(" is outside recompiler memory [");
			line.hex64(base);
			line.ch(',');
			line.hex64(base + size);
			line.text(")\n");
			line.flush(fd);
		}
		return;
	}
	Line line;
	line.text("jit: rip ");
	line.hex64(rip);
	line.text(" is recompiler memory +");
	line.hex64(rip - base);
	line.text(" (block owner not mapped yet)");
	if (!brief)
		line.text("; range is direct executable memory");
	line.ch('\n');
	line.flush(fd);
}

void ps4_recompiler_status(int fd)
{
	Line line;
	line.text("recompiler memory: code=[");
	line.hex64(g_orbis_code_base);
	line.ch(',');
	line.hex64(g_orbis_code_base + g_orbis_code_size);
	line.text(") direct=");
	line.dec(static_cast<unsigned long long>(g_orbis_code_direct));
	line.text(" data=[");
	line.hex64(g_orbis_data_base);
	line.ch(',');
	line.hex64(g_orbis_data_base + g_orbis_data_size);
	line.text(")\n");
	line.flush(fd);
}

// A full PCSX2 state dump allocates and takes locks, so it is intentionally not attempted in the
// signal path. The runtime still records host registers, stacks, and the compact guest state above.
void ps4_crash_guest_info() {}
void ps4_crash_note() {}
void ps4_crash_restart() {}
void ps4_hang_guest_threads(int fd)
{
	// The sampler invokes this in the same lock-free diagnostic context; at minimum identify where the guest CPUs are.
	ps4_crash_guest_regs(fd);
}
}
