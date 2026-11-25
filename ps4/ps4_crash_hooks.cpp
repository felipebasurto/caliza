// PS4 port (AI-assisted): emulator state for CEMUPS4's crash/hang reports.
// These hooks can run in a fatal-signal context: no allocation, no locks, no frontend state.
// Needs proper testing on the console.

#include "R3000A.h"
#include "R5900.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
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

void write_format(int fd, const char* format, ...)
{
	char line[384];
	va_list args;
	va_start(args, format);
	const int length = vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	if (length > 0)
		write_text(fd, line, length < static_cast<int>(sizeof(line)) ? length : static_cast<int>(sizeof(line)) - 1);
}

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
	write_format(fd, "guest: eepc=%08x eecy=%llu eecode=%08x ioppc=%08x iopcy=%llu iopcode=%08x\n",
		cpuRegs.pc, static_cast<unsigned long long>(cpuRegs.cycle), cpuRegs.code, psxRegs.pc,
		static_cast<unsigned long long>(psxRegs.cycle), psxRegs.code);
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
			write_format(fd, "jit: rip %#llx is outside recompiler memory [%#llx, %#llx)\n",
				static_cast<unsigned long long>(rip), base, base + size);
		return;
	}
	write_format(fd, "jit: rip %#llx is recompiler memory +%#llx (block owner not mapped yet)%s\n",
		static_cast<unsigned long long>(rip), static_cast<unsigned long long>(rip - base),
		brief ? "" : "; range is direct executable memory");
}

void ps4_recompiler_status(int fd)
{
	write_format(fd,
		"recompiler memory: code=[%#llx,%#llx) direct=%d data=[%#llx,%#llx)\n",
		g_orbis_code_base, g_orbis_code_base + g_orbis_code_size, g_orbis_code_direct,
		g_orbis_data_base, g_orbis_data_base + g_orbis_data_size);
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
