// PS5 port, Vulkan build (vk-285-8): a sampling profiler for PCSX2's EE thread.
//
// At a fight's peak the EE thread (PCSX2's CPU thread: the EE, the IOP, VU0 and the event
// loop) is busy about 990 ms of every second, and that is what holds the frame rate at
// 45-49. This profiler finds out where the time goes.
//  - A sampler thread interrupts the EE thread with SIGPROF about every millisecond. It
//    skips the moments when the thread is in one of its accounted waits (OrbisEEWaitScope,
//    pcsx2/OrbisEEProf.h), so the samples are busy time. The handler stores the
//    interrupted RIP.
//  - PCSX2 registers every piece of JIT code it emits (common/Perf.cpp): EE and IOP blocks
//    with their PS2 PC, VU0/VU1 micro programs, VIF unpackers, dispatchers and the SW
//    renderer's code. Those registrations are logged with their host ranges, so a sample in
//    recompiled code maps back to a PS2 block.
//  - A writer thread appends samples and registrations to /data/caliza/eeprof.bin every few
//    seconds, so no file I/O runs on the EE thread.
//  - Each [load] line is followed by "[eeprof] n=<samples so far>", which ties each second's
//    samples to its [perf]/[load] figures.
// The analysis is offline (tools/eeprof.py in the port). vk-285-12: opt-in, a file
// /data/caliza/eeprof turns it on (main-boot's ITIMER profiler flag prof keeps it off).
//
// The PS5's signal context is FreeBSD's shifted by 0x30 (ProsperoCrash.cpp): RIP at +0xe0,
// RSP at +0xf8. That is checked at start with one signal to this thread.
//
// vk-285-24: the same profiler can sample the GS thread instead, which on the HW renderer
// records every draw (PCSX2's GSRendererHW and the Vulkan driver, both in the eboot) and holds
// the heavy scenes' frame rate. A file /data/caliza/gsprof starts it, also while the game runs:
// the GS thread checks for the file once a second (OrbisGSProfStart, from its [load] line), and
// the samples go to /data/caliza/gsprof.bin with "[gsprof] n=" marks. Only one thread is
// profiled per run; the EE profiler wins when both files are there at start.
//
// vk-285-29: and the MTVU thread (VU1's micro programs, VIF unpacks and XGKICKs when MTVU is on),
// which is busy the whole second in the crowd fights' dips. A file /data/caliza/vuprof, there at
// start, starts it on that thread's first wake (OrbisVUProfStart, MTVU.cpp); its ring waits are
// skipped, the samples go to /data/caliza/vuprof.bin with "[vuprof] n=" marks.
//
// Copyright (C) 2026 swordpdf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Memory.h"
#include "OrbisPaths.h" // vk-285-33
#include "OrbisDeferredLog.h" // vk-285-107: OrbisEEProfMark's line

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <semaphore.h>
#include <mutex>
#include <new>
#include <pthread.h>
#include <pthread_np.h> // vk-285-36: pthread_attr_get_np, for the profiled thread's stack bounds
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <vector>

extern std::atomic<int> g_orbis_ee_waiting; // pcsx2/MTGS.cpp
extern std::atomic<int> g_orbis_vu_waiting; // pcsx2/MTVU.cpp (vk-285-29)
extern "C" int sceKernelGetCurrentCpu(void);
extern "C" const unsigned char __orbis_text_start[], __orbis_text_end[];

void OrbisEEProfStart();
void OrbisGSProfStart();
void OrbisVUProfStart();
static void PrintLibraryAddresses();

namespace
{
// vk-285-33: in logs/ (OrbisPaths.h), decided when the profiler starts.
const char* const kPath = "eeprof.bin";
const char* const kGSPath = "gsprof.bin";
const char* const kVUPath = "vuprof.bin"; // vk-285-29: the MTVU (VU1) thread
constexpr uint32_t kMaxSamples = 2u << 20; // 16 MiB: about 35 minutes at 1 kHz
constexpr uint32_t kDualMaxSamples = 1u << 20; // 8 MiB per thread when profiling EE+VU1 together
constexpr long kPeriodUs = 1000;
constexpr unsigned kFlushSeconds = 3;

struct FileHeader
{
	char magic[8]; // "EEPROF1"
	uint32_t version; // 1
	uint32_t rip_offset; // bytes into the signal context
	uint64_t code_base; // SysMemory::GetCodePtr(0): the recompilers' code area
	uint64_t code_size; // HostMemoryMap::CodeSize
	uint64_t self_fn; // run-time address of OrbisEEProfStart: the eboot's load base is this minus its ELF value
	uint64_t ee_stack; // an address on the profiled thread's stack
	uint64_t period_us;
	uint64_t thread; // vk-285-24: 0 the EE thread, 1 the GS thread (was reserved, 0); vk-285-29: 2 the VU1 thread
	uint64_t reserved[3];
};
static_assert(sizeof(FileHeader) == 88);

struct RecordHeader
{
	uint32_t type; // 1 samples (u64 RIPs), 2 JIT registrations (JitEntry), 3 library samples' stacks (LibSample)
	uint32_t count;
	uint64_t first; // type 1: the index of the first sample in this record
};
static_assert(sizeof(RecordHeader) == 16);

struct JitEntry
{
	uint64_t ptr;
	uint64_t key; // the PS2 PC for EE/IOP/VU blocks, a key for keyed code, 0 for named code
	uint32_t size;
	uint32_t at; // the sample count when it was registered: it covers samples from here on
	char group[8]; // Perf group: "EE", "IOP", "VU0", "VU1", "VIF", "" (any)
	char name[32]; // the symbol or key prefix, or empty
};
static_assert(sizeof(JitEntry) == 64);

// vk-285-11: a sample in the system libraries (memcpy was 3.9% in vk-285-10) also keeps the top six
// words of the stack, so the offline tool can find the eboot or JIT return address that called it.
constexpr uint64_t kLibLo = 0x800000000ull, kLibHi = 0x900000000ull; // libkernel and the other modules
constexpr uint32_t kStackWords = 6, kMaxLibSamples = 1u << 16, kDualMaxLibSamples = 1u << 15;
struct LibSample
{
	uint64_t index;
	uint64_t words[kStackWords];
};
static_assert(sizeof(LibSample) == 56);

constexpr uint32_t kScanWords = 512; // 4 KiB of stack

struct ProfileSession
{
	uint64_t* samples = nullptr;
	uint32_t max_samples = 0;
	std::atomic<uint32_t> count{0};
	LibSample* lib_samples = nullptr;
	uint32_t max_lib_samples = 0;
	std::atomic<uint32_t> lib_count{0};
	std::atomic<uint32_t> dropped{0};
	std::atomic<uint32_t> sent{0};
	std::atomic<uint32_t> skipped{0};
	std::atomic<int> kill_error{0};
	std::atomic<bool> started{false};
	std::atomic<bool> active{false};
	std::atomic<bool> running{false};
	int mode = 0; // 0 EE, 1 GS, 2 VU1
	pthread_t target{};
	uint64_t thread_stack = 0;
	uint64_t stack_lo = 0;
	uint64_t stack_hi = 0;
	std::vector<JitEntry> jit_pending;
};

ProfileSession s_sessions[3]; // 0 EE, 1 GS, 2 VU1
uint32_t s_rip_index = 0xe0 / 8;
std::mutex s_sig_mutex;
bool s_rip_calibrated = false;
bool s_sample_handler_installed = false;

bool InEbootText(uint64_t a)
{
	return a >= reinterpret_cast<uint64_t>(__orbis_text_start) && a < reinterpret_cast<uint64_t>(__orbis_text_end);
}

bool AnyFlagProfStarted()
{
	for (const ProfileSession& s : s_sessions)
		if (s.started.load(std::memory_order_relaxed))
			return true;
	return false;
}

const char* ModeTag(int mode)
{
	return mode == 2 ? "vuprof" : mode == 1 ? "gsprof" : "eeprof";
}

const char* ModeName(int mode)
{
	return mode == 2 ? "VU1" : mode == 1 ? "GS" : "EE";
}

const char* ModePath(int mode)
{
	return mode == 2 ? kVUPath : mode == 1 ? kGSPath : kPath;
}

std::mutex& JitMutex()
{
	static std::mutex m;
	return m;
}

// Dispatches SIGPROF on the interrupted target thread (EE, GS, or VU1) to its ProfileSession.
void SampleHandler(int, siginfo_t*, void* ctx)
{
	if (!ctx)
		return;
	const pthread_t self = pthread_self();
	ProfileSession* sess = nullptr;
	for (int m = 0; m < 3; m++)
	{
		if (s_sessions[m].active.load(std::memory_order_acquire) && pthread_equal(self, s_sessions[m].target))
		{
			sess = &s_sessions[m];
			break;
		}
	}
	if (!sess)
		return;
	const uint64_t rip = static_cast<const uint64_t*>(ctx)[s_rip_index];
	const uint32_t i = sess->count.load(std::memory_order_relaxed);
	if (i >= sess->max_samples)
	{
		sess->dropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	sess->samples[i] = rip;
#ifdef ORBIS_PS4
	// PS4: memcpy and memmove live in the eboot, so capture their stack callers alongside library samples.
	const uint64_t copy_lo = reinterpret_cast<uint64_t>(&memcpy), move_lo = reinterpret_cast<uint64_t>(&memmove);
	const bool in_copy = (rip >= copy_lo && rip < copy_lo + 0x400) || (rip >= move_lo && rip < move_lo + 0x400);
	if ((rip >= kLibLo && rip < kLibHi) || in_copy)
#else
	if (rip >= kLibLo && rip < kLibHi)
#endif
	{
		const uint32_t j = sess->lib_count.load(std::memory_order_relaxed);
		if (j < sess->max_lib_samples)
		{
			// The interrupted thread's own stack: RSP is three slots above RIP in the context.
			const uint64_t* sp = reinterpret_cast<const uint64_t*>(static_cast<const uint64_t*>(ctx)[s_rip_index + 3]);
			LibSample& l = sess->lib_samples[j];
			l.index = i;
			const uint64_t at = reinterpret_cast<uint64_t>(sp);
			if (sess->stack_hi != 0 && at >= sess->stack_lo && at + 8 * kStackWords <= sess->stack_hi)
			{
				// vk-285-36: the first six possible eboot return addresses above RSP, within the stack.
				const uint64_t end = at + 8ull * kScanWords < sess->stack_hi ? at + 8ull * kScanWords : sess->stack_hi;
				uint32_t found = 0;
				for (const uint64_t* p = sp; reinterpret_cast<uint64_t>(p) + 8 <= end && found < kStackWords; p++)
				{
					if (InEbootText(*p))
						l.words[found++] = *p;
				}
				for (; found < kStackWords; found++)
					l.words[found] = 0;
			}
			else
			{
				for (uint32_t k = 0; k < kStackWords; k++)
					l.words[k] = sp[k];
			}
			sess->lib_count.store(j + 1, std::memory_order_release);
		}
	}
	sess->count.store(i + 1, std::memory_order_release);
}

volatile uint64_t s_calibration[0x140 / 8];
volatile int s_calibrated;

void CalibrationHandler(int, siginfo_t*, void* ctx)
{
	if (!ctx)
		return;
	const uint64_t* q = static_cast<const uint64_t*>(ctx);
	for (size_t i = 0; i < sizeof(s_calibration) / sizeof(s_calibration[0]); i++)
		s_calibration[i] = q[i];
	s_calibrated = 1;
}

bool Install(void (*handler)(int, siginfo_t*, void*))
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	return sigaction(SIGPROF, &sa, nullptr) == 0;
}

// One signal to this thread: which slot of the context holds its stack pointer says which
// layout the kernel uses. Orbis shifted (+0xf8/+0xe0) is checked alongside FreeBSD (+0xc8/+0xb0).
void CalibrateLocked()
{
	if (s_rip_calibrated)
		return;
	s_rip_calibrated = true;
	volatile int here = 0;
	const uint64_t local = reinterpret_cast<uint64_t>(&here);
	s_calibrated = 0;
	if (!Install(CalibrationHandler) || pthread_kill(pthread_self(), SIGPROF) != 0 || !s_calibrated)
	{
		std::printf("[eeprof] context check: no signal came back (errno=%d); RIP taken at +0xe0\n", errno);
		return;
	}
	auto on_stack = [local](uint64_t v) { return v > local - (1u << 20) && v < local + (1u << 20); };
	const uint64_t rsp_orbis = s_calibration[0xf8 / 8], rsp_bsd = s_calibration[0xc8 / 8];
	if (on_stack(rsp_orbis))
		s_rip_index = 0xe0 / 8;
	else if (on_stack(rsp_bsd))
		s_rip_index = 0xb0 / 8;
	std::printf("[eeprof] context check: local %#llx, rsp@+0xf8 %#llx, rsp@+0xc8 %#llx, rip@+0xe0 %#llx, rip@+0xb0 %#llx -> RIP at +%#x%s\n",
		static_cast<unsigned long long>(local), static_cast<unsigned long long>(rsp_orbis),
		static_cast<unsigned long long>(rsp_bsd), static_cast<unsigned long long>(s_calibration[0xe0 / 8]),
		static_cast<unsigned long long>(s_calibration[0xb0 / 8]), s_rip_index * 8,
		(on_stack(rsp_orbis) || on_stack(rsp_bsd)) ? "" : " (neither slot held this stack; kept the default)");
}

void CalibrateOnce()
{
	std::lock_guard<std::mutex> lock(s_sig_mutex);
	CalibrateLocked();
}

bool EnsureSampleHandler()
{
	std::lock_guard<std::mutex> lock(s_sig_mutex);
	if (s_sample_handler_installed)
		return true;
	CalibrateLocked();
	if (!Install(SampleHandler))
		return false;
	s_sample_handler_installed = true;
	return true;
}

// vk-285-88: a profiled thread pinned alone to a CPU (live.ini pin=3) got its SIGPROF only at its next
// system call -- the signal waits for the thread to enter the kernel when nothing interrupts it -- so every
// sample of vk-285-87's GS profile fell on a libkernel syscall stub. The sampler therefore follows the
// profiled thread's affinity: on its CPU, its wake-up preempts the thread, which then takes the signal at
// the instruction it was interrupted at.
extern "C" int scePthreadGetaffinity(pthread_t thread, unsigned long long* mask);
extern "C" int scePthreadSetaffinity(pthread_t thread, unsigned long long mask);
static void FollowAffinity(const ProfileSession& sess, unsigned long long& last_mask)
{
	unsigned long long mask = 0;
	if (scePthreadGetaffinity(sess.target, &mask) != 0 || mask == 0 || mask == last_mask)
		return;
	last_mask = mask;
	const int rc = scePthreadSetaffinity(pthread_self(), mask);
	std::printf("[%s] sampler follows the profiled thread's CPUs %#llx (rc=%d)\n", ModeTag(sess.mode), mask, rc);
	std::fflush(stdout);
}

// vk-285-89: and at the highest game priority (256; range is 256-767, lower first), since
// a sampler on the EE's CPU at normal priority hardly ever preempted it.
extern "C" int scePthreadGetprio(pthread_t thread, int* prio);
extern "C" int scePthreadSetprio(pthread_t thread, int prio);
static void RaisePriority(const ProfileSession& sess)
{
	int target = -1, before = -1;
	scePthreadGetprio(sess.target, &target);
	scePthreadGetprio(pthread_self(), &before);
	const int rc = scePthreadSetprio(pthread_self(), 256);
	int after = -1;
	scePthreadGetprio(pthread_self(), &after);
	std::printf("[%s] sampler priority %d -> %d (rc=%d), profiled thread's %d\n", ModeTag(sess.mode), before, after, rc, target);
	std::fflush(stdout);
}

void* SamplerThread(void* arg)
{
	ProfileSession& sess = *static_cast<ProfileSession*>(arg);
	const timespec period = {0, kPeriodUs * 1000};
	unsigned tick = 0;
	unsigned long long last_mask = 0;
	RaisePriority(sess);
	for (;;)
	{
		if ((tick++ & 255u) == 0)
			FollowAffinity(sess, last_mask);
		nanosleep(&period, nullptr);
		if (sess.count.load(std::memory_order_relaxed) >= sess.max_samples)
			continue;
		// vk-285-29: the VU1 thread's ring wait is skipped the same way, so its samples are busy time.
		if ((sess.mode == 0 && g_orbis_ee_waiting.load(std::memory_order_relaxed) != 0) ||
			(sess.mode == 2 && g_orbis_vu_waiting.load(std::memory_order_relaxed) != 0))
		{
			sess.skipped.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		const int rc = pthread_kill(sess.target, SIGPROF);
		if (rc == 0)
			sess.sent.fetch_add(1, std::memory_order_relaxed);
		else
			sess.kill_error.store(rc, std::memory_order_relaxed);
	}
	return nullptr;
}

void Flush(ProfileSession& sess, FILE* f, uint32_t& written, uint32_t& lib_written)
{
	const uint32_t n = sess.count.load(std::memory_order_acquire);
	if (n > written)
	{
		const RecordHeader r = {1, n - written, written};
		std::fwrite(&r, sizeof(r), 1, f);
		std::fwrite(sess.samples + written, sizeof(uint64_t), n - written, f);
		written = n;
	}
	const uint32_t nl = sess.lib_count.load(std::memory_order_acquire);
	if (nl > lib_written)
	{
		const RecordHeader r = {3, nl - lib_written, lib_written};
		std::fwrite(&r, sizeof(r), 1, f);
		std::fwrite(sess.lib_samples + lib_written, sizeof(LibSample), nl - lib_written, f);
		lib_written = nl;
	}
	std::vector<JitEntry> jit;
	{
		std::lock_guard<std::mutex> lock(JitMutex());
		jit.swap(sess.jit_pending);
	}
	if (!jit.empty())
	{
		const RecordHeader r = {2, static_cast<uint32_t>(jit.size()), 0};
		std::fwrite(&r, sizeof(r), 1, f);
		std::fwrite(jit.data(), sizeof(JitEntry), jit.size(), f);
	}
	std::fflush(f);
}

void* WriterThread(void* arg)
{
	ProfileSession& sess = *static_cast<ProfileSession*>(arg);
#ifdef ORBIS_PS4
	scePthreadSetprio(pthread_self(), 740);
#endif
	const std::string out = OrbisLogPath(ModePath(sess.mode)); // vk-285-33
	const char* const path = out.c_str();
	FILE* f = std::fopen(path, "wb");
	if (!f)
	{
		std::printf("[%s] cannot write %s (errno=%d); profiler off\n", ModeTag(sess.mode), path, errno);
		std::fflush(stdout);
		sess.running.store(false, std::memory_order_relaxed);
		return nullptr;
	}
	FileHeader h = {};
	std::memcpy(h.magic, "EEPROF1", 8);
	h.version = 1;
	h.rip_offset = s_rip_index * 8;
	h.code_base = reinterpret_cast<uint64_t>(SysMemory::GetCodePtr(0));
	h.code_size = HostMemoryMap::CodeSize;
	h.self_fn = reinterpret_cast<uint64_t>(&OrbisEEProfStart);
	h.ee_stack = sess.thread_stack;
	h.period_us = kPeriodUs;
	h.thread = static_cast<uint64_t>(sess.mode);
	std::fwrite(&h, sizeof(h), 1, f);
	std::fflush(f);
	uint32_t written = 0, lib_written = 0;
	for (;;)
	{
		const timespec wait = {kFlushSeconds, 0};
		nanosleep(&wait, nullptr);
		Flush(sess, f, written, lib_written);
	}
	return nullptr;
}
} // namespace

namespace
{
// vk-285-12: opt-in. Decided once, before the first JIT registration, so nothing is
// buffered when the profiler is off.
bool WantEEProf()
{
	static const bool wanted = (OrbisFlag("eeprof") || OrbisFlag("eevuprof")) && !OrbisFlag("prof");
	return wanted;
}

bool WantVUProf()
{
	static const bool wanted = (OrbisFlag("vuprof") || OrbisFlag("eevuprof")) && !OrbisFlag("prof");
	return wanted;
}

bool ProfilerWanted()
{
	return WantEEProf() || WantVUProf();
}
} // namespace

void OrbisJitRegister(const char* group, const void* ptr, size_t size, u64 key, const char* name)
{
	if (!ProfilerWanted())
		return;
	JitEntry e = {};
	e.ptr = reinterpret_cast<uint64_t>(ptr);
	e.key = key;
	e.size = static_cast<uint32_t>(size);
	if (group)
		std::strncpy(e.group, group, sizeof(e.group) - 1);
	if (name)
		std::strncpy(e.name, name, sizeof(e.name) - 1);
	std::lock_guard<std::mutex> lock(JitMutex());
	if (WantEEProf())
	{
		e.at = s_sessions[0].count.load(std::memory_order_relaxed);
		s_sessions[0].jit_pending.push_back(e);
	}
	if (WantVUProf())
	{
		e.at = s_sessions[2].count.load(std::memory_order_relaxed);
		s_sessions[2].jit_pending.push_back(e);
	}
}

namespace
{
// Starts sampling the calling thread: mode 0 the EE thread, 1 the GS thread, 2 the VU1 thread.
void StartOnThisThread(int mode)
{
	ProfileSession& sess = s_sessions[mode];
	const bool dual = WantEEProf() && WantVUProf();
	const uint32_t max_samples = dual ? kDualMaxSamples : kMaxSamples;
	const uint32_t max_lib_samples = dual ? kDualMaxLibSamples : kMaxLibSamples;
	sess.mode = mode;
	sess.max_samples = max_samples;
	sess.max_lib_samples = max_lib_samples;
	sess.samples = new (std::nothrow) uint64_t[max_samples];
	sess.lib_samples = new (std::nothrow) LibSample[max_lib_samples];
	if (!sess.samples || !sess.lib_samples)
	{
		std::printf("[%s] no memory for %u samples; profiler off\n", ModeTag(mode), max_samples);
		std::fflush(stdout);
		return;
	}
	volatile int here = 0;
	sess.thread_stack = reinterpret_cast<uint64_t>(&here);
	// vk-285-36: the stack's bounds, for the library samples' deeper scan (SampleHandler).
	{
		pthread_attr_t attr;
		if (pthread_attr_init(&attr) == 0)
		{
			void* stack_addr = nullptr;
			size_t stack_size = 0;
			if (pthread_attr_get_np(pthread_self(), &attr) == 0 &&
				pthread_attr_getstack(&attr, &stack_addr, &stack_size) == 0 && stack_addr != nullptr &&
				sess.thread_stack >= reinterpret_cast<uint64_t>(stack_addr) &&
				sess.thread_stack < reinterpret_cast<uint64_t>(stack_addr) + stack_size)
			{
				sess.stack_lo = reinterpret_cast<uint64_t>(stack_addr);
				sess.stack_hi = sess.stack_lo + stack_size;
			}
			pthread_attr_destroy(&attr);
		}
		std::printf("[%s] stack %#llx-%#llx (here %#llx)%s\n", ModeTag(mode),
			static_cast<unsigned long long>(sess.stack_lo), static_cast<unsigned long long>(sess.stack_hi),
			static_cast<unsigned long long>(sess.thread_stack),
			sess.stack_hi != 0 ? "" : ": unknown, library samples keep their top six words");
	}
	sess.target = pthread_self();
	sess.active.store(true, std::memory_order_release);
	if (!EnsureSampleHandler())
	{
		sess.active.store(false, std::memory_order_release);
		std::printf("[%s] sigaction failed (errno=%d); profiler off\n", ModeTag(mode), errno);
		std::fflush(stdout);
		return;
	}
	sess.running.store(true, std::memory_order_relaxed);
	pthread_t writer, sampler;
	const int rw = pthread_create(&writer, nullptr, WriterThread, &sess);
	const int rs = pthread_create(&sampler, nullptr, SamplerThread, &sess);
	if (rw == 0)
		pthread_detach(writer);
	if (rs == 0)
		pthread_detach(sampler);
	static std::atomic<bool> s_libs_printed{false};
	if (!s_libs_printed.exchange(true, std::memory_order_relaxed))
		PrintLibraryAddresses();
	std::printf("[%s] start: %s thread %#llx, code %p + %#x, eboot fn %p, every %ld us, writer %d sampler %d -> %s\n",
		ModeTag(mode), ModeName(mode), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(sess.target)),
		SysMemory::GetCodePtr(0), HostMemoryMap::CodeSize, reinterpret_cast<void*>(&OrbisEEProfStart), kPeriodUs, rw, rs,
		OrbisLogPath(ModePath(mode)).c_str());
	std::fflush(stdout);
}
} // namespace

// Host::PumpMessagesOnCPUThread (StubHost.cpp) calls this at every vsync; the first call, on
// the EE thread, starts the profiler.
void OrbisAutoProfNoteThread(int which); // vk-285-118, below

void OrbisEEProfStart()
{
	OrbisAutoProfNoteThread(0); // vk-285-118: the autoprof samples this thread when it is the busy one
	static std::atomic<bool> s_ee_checked{false};
	if (s_ee_checked.load(std::memory_order_relaxed) || s_ee_checked.exchange(true))
		return;
	if (!ProfilerWanted())
	{
		std::printf("[eeprof] off (opt-in: /data/caliza/eeprof, vuprof, eevuprof, or gsprof)\n");
		std::fflush(stdout);
		return;
	}
	if (!WantEEProf())
		return; // vk-285-29: vuprof alone asks for the VU1 thread (OrbisVUProfStart)
	if (s_sessions[0].started.exchange(true))
		return;
	StartOnThisThread(0);
}

// vk-285-24: GSRenderer.cpp's OrbisPrintLoad calls this once a second on the GS thread; it starts
// the profiler on that thread once /data/caliza/gsprof exists.
void OrbisGSProfStart()
{
	if (s_sessions[1].started.load(std::memory_order_relaxed))
		return;
	if (!OrbisFlag("gsprof") || OrbisFlag("prof"))
		return;
	if (s_sessions[1].started.exchange(true))
		return;
	StartOnThisThread(1);
}

// vk-285-29: the MTVU thread calls this each time its ring runs dry; it starts the profiler on
// that thread when /data/caliza/vuprof or /data/caliza/eevuprof was present at start.
void OrbisVUProfStart()
{
	OrbisAutoProfNoteThread(2); // vk-285-118
	if (s_sessions[2].started.load(std::memory_order_relaxed))
		return;
	if (!WantVUProf() || s_sessions[2].started.exchange(true))
		return;
	StartOnThisThread(2);
}

// Where the system libraries' hot functions are, so samples outside the eboot and the JIT can be
// named offline (vk-285-8 had two hot spots in the first MB of libkernel).
static void PrintLibraryAddresses()
{
	struct Named
	{
		const char* name;
		const void* address;
	};
	const Named named[] = {
		{"pthread_getspecific", reinterpret_cast<const void*>(&pthread_getspecific)},
		{"pthread_self", reinterpret_cast<const void*>(&pthread_self)},
		{"sched_yield", reinterpret_cast<const void*>(&sched_yield)},
		{"clock_gettime", reinterpret_cast<const void*>(&clock_gettime)},
		{"nanosleep", reinterpret_cast<const void*>(&nanosleep)},
		{"memcpy", reinterpret_cast<const void*>(&memcpy)},
		{"memmove", reinterpret_cast<const void*>(&memmove)},
		{"memset", reinterpret_cast<const void*>(&memset)},
		{"memcmp", reinterpret_cast<const void*>(&memcmp)},
		{"malloc", reinterpret_cast<const void*>(&malloc)},
		{"free", reinterpret_cast<const void*>(&free)},
		{"sem_post", reinterpret_cast<const void*>(&sem_post)},
		{"sem_wait", reinterpret_cast<const void*>(&sem_wait)},
		{"pthread_mutex_lock", reinterpret_cast<const void*>(&pthread_mutex_lock)},
		{"pthread_mutex_unlock", reinterpret_cast<const void*>(&pthread_mutex_unlock)},
		{"pthread_cond_signal", reinterpret_cast<const void*>(&pthread_cond_signal)},
		{"pthread_cond_broadcast", reinterpret_cast<const void*>(&pthread_cond_broadcast)},
		{"pthread_kill", reinterpret_cast<const void*>(&pthread_kill)},
		{"sceKernelGetCurrentCpu", reinterpret_cast<const void*>(&sceKernelGetCurrentCpu)},
	};
	std::printf("[eeprof] libs:");
	for (const Named& n : named)
		std::printf(" %s=%p", n.name, n.address);
	std::printf("\n");
}

// GSRenderer.cpp's OrbisPrintLoad calls this after each [load] line (GS thread).
void OrbisEEProfMark()
{
	for (const ProfileSession& sess : s_sessions)
	{
		if (!sess.running.load(std::memory_order_relaxed))
			continue;
		const int err = sess.kill_error.load(std::memory_order_relaxed);
		// vk-285-107: through the deferred log, as one line. This runs on the GS thread once a second, and a direct
		// printf there waited on stdout's lock while the ticker thread wrote to /data (vk-285-106's [vsslow], ~33 ms).
		char kill[32] = "";
		if (err)
			std::snprintf(kill, sizeof(kill), " kill_error=%d", err);
		OrbisDeferredPrintf("[%s] n=%u sent=%u skipped=%u dropped=%u%s\n", ModeTag(sess.mode),
			sess.count.load(std::memory_order_relaxed), sess.sent.load(std::memory_order_relaxed),
			sess.skipped.load(std::memory_order_relaxed), sess.dropped.load(std::memory_order_relaxed), kill);
	}
}

// ------------------------------------------------------------------------------------------------------------
// vk-285-118 (AI-assisted): the automatic profile ("autoprof"). Testers' reports say which thread holds a slow
// game back ([load]), not what it does there; the profiles above need a flag file and a person to run them. So
// when a game runs below 90% speed for 4 seconds in a row, the busiest of the EE, GS and VU1 threads is sampled
// for 8 seconds (SIGPROF every millisecond, as above; the EE's and VU1's accounted waits skipped), and four
// lines summarise it in boot.log, which every session report carries:
//   [autoprof] #N <thread> ... : the loads and speed that picked it, the samples, and where they fell (eboot,
//              recompiled code, system libraries, other), and ref=<run-time address of OrbisEEProfStart>
//   [autoprof] #N eboot: the 40 busiest 64-byte buckets (24 in vk-285-118) as signed offsets from ref (tools: add the ELF's address
//              of OrbisEEProfStart and symbolize with the build's llvm-pie.elf)
//   [autoprof] #N jit: the recompiled code by area (EE, IOP, VIF0/1, mVU0/1, VIF unpack, SW renderer)
//   [autoprof] #N lib: the busiest library addresses and the eboot code that called them (from the stack)
// At most 4 windows a session, 90 s apart; none while a flag-file profiler runs; the flag "noautoprof" turns it off, and
// "autoprof_now" takes one window 20 s into the game at any speed (to check it).
namespace
{
std::atomic<uintptr_t> s_auto_thread[3]; // pthread_self() of the EE, GS and VU1 threads, as last seen
struct AutoSample
{
	uint64_t rip;
	uint64_t caller; // a library sample's first eboot return address on the stack, else 0
};
constexpr uint32_t kAutoMax = 12000;
constexpr int kAutoSeconds = 8, kAutoWindows = 4, kAutoGapSeconds = 90, kAutoSlowSeconds = 4;
constexpr float kAutoSlowSpeed = 90.0f;
AutoSample* s_auto;
std::atomic<uint32_t> s_auto_n{0};
std::atomic<uint32_t> s_auto_skipped{0};
std::atomic<int> s_auto_state{0}; // 0 idle, 1 sampling
pthread_t s_auto_target;
int s_auto_which = 0;
uint64_t s_auto_lo, s_auto_hi; // the target's stack
uint64_t s_auto_ref; // &OrbisEEProfStart at run time
char s_auto_head[200];
int s_auto_index = 0;

} // namespace
namespace
{
bool AutoInEboot(uint64_t a)
{
	return InEbootText(a);
}

void AutoHandler(int, siginfo_t*, void* ctx)
{
	if (!ctx)
		return;
	const uint32_t i = s_auto_n.load(std::memory_order_relaxed);
	if (i >= kAutoMax)
		return;
	const uint64_t* q = static_cast<const uint64_t*>(ctx);
	const uint64_t rip = q[s_rip_index];
	uint64_t caller = 0;
#ifdef ORBIS_PS4
	const uint64_t copy_lo = reinterpret_cast<uint64_t>(&memcpy), move_lo = reinterpret_cast<uint64_t>(&memmove);
	const bool in_copy = (rip >= copy_lo && rip < copy_lo + 0x400) || (rip >= move_lo && rip < move_lo + 0x400);
	if (((rip >= kLibLo && rip < kLibHi) || in_copy) && s_auto_hi != 0)
#else
	if (rip >= kLibLo && rip < kLibHi && s_auto_hi != 0)
#endif
	{
		const uint64_t* sp = reinterpret_cast<const uint64_t*>(q[s_rip_index + 3]);
		const uint64_t at = reinterpret_cast<uint64_t>(sp);
		if (at >= s_auto_lo && at < s_auto_hi)
		{
			const uint64_t end = at + 8ull * kScanWords < s_auto_hi ? at + 8ull * kScanWords : s_auto_hi;
			for (const uint64_t* p = sp; reinterpret_cast<uint64_t>(p) + 8 <= end; p++)
				if (AutoInEboot(*p))
				{
					caller = *p;
					break;
				}
		}
	}
	s_auto[i].rip = rip;
	s_auto[i].caller = caller;
	s_auto_n.store(i + 1, std::memory_order_release);
}

const char* const kAutoThreadName[3] = {"EE", "GS", "VU1"};

struct Bucket
{
	int64_t key;
	uint32_t n;
};

void TopBuckets(std::vector<Bucket>& v, size_t keep)
{
	std::sort(v.begin(), v.end(), [](const Bucket& a, const Bucket& b) { return a.n > b.n; });
	if (v.size() > keep)
		v.resize(keep);
}

void AddBucket(std::vector<Bucket>& v, int64_t key)
{
	for (Bucket& b : v)
		if (b.key == key)
		{
			b.n++;
			return;
		}
	v.push_back({key, 1});
}

void AutoReport()
{
	const uint32_t n = s_auto_n.load(std::memory_order_acquire);
	const double pct = n ? 100.0 / n : 0.0;
	const uint64_t code = reinterpret_cast<uint64_t>(SysMemory::GetCodePtr(0));
	struct Area
	{
		const char* name;
		uint32_t off, size;
		uint32_t n;
	} areas[] = {
		{"ee", HostMemoryMap::EErecOffset, HostMemoryMap::EErecSize, 0}, {"iop", HostMemoryMap::IOPrecOffset, HostMemoryMap::IOPrecSize, 0},
		{"vif0", HostMemoryMap::VIF0recOffset, HostMemoryMap::VIF0recSize, 0}, {"vif1", HostMemoryMap::VIF1recOffset, HostMemoryMap::VIF1recSize, 0},
		{"mvu0", HostMemoryMap::mVU0recOffset, HostMemoryMap::mVU0recSize, 0}, {"mvu1", HostMemoryMap::mVU1recOffset, HostMemoryMap::mVU1recSize, 0},
		{"unpack", HostMemoryMap::VIFUnpackRecOffset, HostMemoryMap::VIFUnpackRecSize, 0}, {"sw", HostMemoryMap::SWrecOffset, HostMemoryMap::SWrecSize, 0},
	};
	uint32_t eboot = 0, jit = 0, lib = 0, other = 0;
	std::vector<Bucket> eb, lb, cb;
	eb.reserve(4096);
	for (uint32_t i = 0; i < n; i++)
	{
		const uint64_t r = s_auto[i].rip;
		if (r >= code && r < code + HostMemoryMap::CodeSize)
		{
			jit++;
			for (Area& a : areas)
				if (r - code >= a.off && r - code < a.off + a.size)
					a.n++;
		}
		else if (r >= kLibLo && r < kLibHi)
		{
			lib++;
			AddBucket(lb, static_cast<int64_t>(r & ~0xfull));
			AddBucket(cb, s_auto[i].caller ? (static_cast<int64_t>(s_auto[i].caller) - static_cast<int64_t>(s_auto_ref)) : INT64_MIN);
		}
		else if (AutoInEboot(r))
		{
			eboot++;
			AddBucket(eb, (static_cast<int64_t>(r) - static_cast<int64_t>(s_auto_ref)) & ~int64_t{63});
			if (s_auto[i].caller)
				AddBucket(cb, static_cast<int64_t>(s_auto[i].caller) - static_cast<int64_t>(s_auto_ref));
		}
		else
			other++;
	}
	// vk-285-119: 40 buckets (24 in vk-285-118 covered only 20-30% of a GS thread's eboot samples), and their share.
	TopBuckets(eb, 40);
	uint32_t covered = 0;
	for (const Bucket& b : eb)
		covered += b.n;
	TopBuckets(lb, 8);
	TopBuckets(cb, 8);
	char line[1600];
	std::snprintf(line, sizeof(line), "[autoprof] #%d %s | %u samples in %d s, %u skipped (waits) | eboot %.1f%% jit %.1f%% lib %.1f%% other %.1f%% | ref=%#llx",
		s_auto_index, s_auto_head, n, kAutoSeconds, s_auto_skipped.load(std::memory_order_relaxed), eboot * pct, jit * pct, lib * pct, other * pct,
		static_cast<unsigned long long>(s_auto_ref));
	OrbisDeferredPrintf("%s\n", line);
	OrbisDeferredEvent(line);
	int at = std::snprintf(line, sizeof(line), "[autoprof] #%d eboot:", s_auto_index);
	for (const Bucket& b : eb)
		if (at < static_cast<int>(sizeof(line)) - 40)
			at += std::snprintf(line + at, sizeof(line) - at, " %s%#llx %.1f%%", b.key < 0 ? "-" : "+",
				static_cast<unsigned long long>(b.key < 0 ? -b.key : b.key), b.n * pct);
	if (at < static_cast<int>(sizeof(line)) - 40)
		std::snprintf(line + at, sizeof(line) - at, " | these %u = %.1f%% of the samples", static_cast<unsigned>(eb.size()), covered * pct);
	OrbisDeferredPrintf("%s\n", line);
	OrbisDeferredEvent(line); // the settings log too: the installer's report keeps only boot.log's first 64 KB and last 448 KB
	at = std::snprintf(line, sizeof(line), "[autoprof] #%d jit:", s_auto_index);
	for (const Area& a : areas)
		if (a.n)
			at += std::snprintf(line + at, sizeof(line) - at, " %s %.1f%%", a.name, a.n * pct);
	OrbisDeferredPrintf("%s\n", line);
	OrbisDeferredEvent(line);
	at = std::snprintf(line, sizeof(line), "[autoprof] #%d lib:", s_auto_index);
	for (const Bucket& b : lb)
		if (at < static_cast<int>(sizeof(line)) - 40)
			at += std::snprintf(line + at, sizeof(line) - at, " %#llx %.1f%%", static_cast<unsigned long long>(b.key), b.n * pct);
	at += std::snprintf(line + at, sizeof(line) - at, " | callers:");
	for (const Bucket& b : cb)
		if (at < static_cast<int>(sizeof(line)) - 40)
		{
			if (b.key == INT64_MIN)
				at += std::snprintf(line + at, sizeof(line) - at, " ? %.1f%%", b.n * pct);
			else
				at += std::snprintf(line + at, sizeof(line) - at, " %s%#llx %.1f%%", b.key < 0 ? "-" : "+",
					static_cast<unsigned long long>(b.key < 0 ? -b.key : b.key), b.n * pct);
		}
	OrbisDeferredPrintf("%s\n", line);
	OrbisDeferredEvent(line);
}

void* AutoSamplerThread(void*)
{
	bool raised = false;
	(void)raised;
	const timespec idle = {0, 100 * 1000 * 1000};
	const timespec period = {0, kPeriodUs * 1000};
	for (;;)
	{
		if (s_auto_state.load(std::memory_order_acquire) != 1)
		{
			nanosleep(&idle, nullptr);
			continue;
		}
		int normal = -1;
		scePthreadGetprio(pthread_self(), &normal);
		scePthreadSetprio(pthread_self(), 256); // as the flag-file profiler's sampler (vk-285-89)
		raised = true;
		unsigned long long mask = 0;
		if (scePthreadGetaffinity(s_auto_target, &mask) == 0 && mask != 0)
			scePthreadSetaffinity(pthread_self(), mask); // on the target's CPUs (vk-285-88)
		timespec t0;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (;;)
		{
			nanosleep(&period, nullptr);
			timespec t;
			clock_gettime(CLOCK_MONOTONIC, &t);
			if (t.tv_sec - t0.tv_sec >= kAutoSeconds || s_auto_n.load(std::memory_order_relaxed) >= kAutoMax)
				break;
			if ((s_auto_which == 0 && g_orbis_ee_waiting.load(std::memory_order_relaxed) != 0) ||
				(s_auto_which == 2 && g_orbis_vu_waiting.load(std::memory_order_relaxed) != 0))
			{
				s_auto_skipped.fetch_add(1, std::memory_order_relaxed);
				continue;
			}
			pthread_kill(s_auto_target, SIGPROF);
		}
		nanosleep(&period, nullptr); // the last signal's handler
		if (normal >= 256)
			scePthreadSetprio(pthread_self(), normal); // the summary at the normal priority: it must not hold the target up
		AutoReport();
		s_auto_state.store(0, std::memory_order_release);
	}
	return nullptr;
}
} // namespace

void OrbisAutoProfNoteThread(int which)
{
	s_auto_thread[which].store(reinterpret_cast<uintptr_t>(pthread_self()), std::memory_order_relaxed);
}

// GSRenderer.cpp calls this once a second on the GS thread with the speed (%) and the threads' loads (%).
void OrbisAutoProfSecond(float speed, float ee, float gs, float vu)
{
	OrbisAutoProfNoteThread(1);
#ifdef ORBIS_PS4
	// PS4 port (AI-assisted): off unless the flag file autoprof is there. On the PS4 a slow stretch is the normal state
	// (GTA San Andreas runs at 40-50%), so it sampled the busy thread again and again at a cost to that thread.
	static const bool off = !OrbisFlag("autoprof");
#else
	static const bool off = OrbisFlag("noautoprof");
#endif
	static int slow = 0, windows = 0, last_which = -1;
	static timespec last = {0, 0};
	if (off || AnyFlagProfStarted() || windows >= kAutoWindows)
		return;
	// The switch autoprof_now (a test): one window 20 s into the game whatever the speed.
	static const bool now_flag = OrbisFlag("autoprof_now");
	static int seconds = 0;
	seconds++;
	slow = (speed > 2.0f && speed < kAutoSlowSpeed) ? slow + 1 : 0;
	const bool forced = now_flag && windows == 0 && seconds >= 20;
	if ((slow < kAutoSlowSeconds && !forced) || s_auto_state.load(std::memory_order_acquire) != 0)
		return;
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (windows > 0 && now.tv_sec - last.tv_sec < kAutoGapSeconds)
		return;
	const bool has_ee = s_auto_thread[0].load(std::memory_order_relaxed) != 0;
	const bool has_vu = s_auto_thread[2].load(std::memory_order_relaxed) != 0;
	int which = 1;
	float best = gs;
	if (ee > best && has_ee)
	{
		which = 0;
		best = ee;
	}
	if (vu > best && has_vu)
	{
		which = 2;
		best = vu;
	}
	// When both EE and VU1 are saturated (e.g. both at 100%), alternate between them across
	// windows instead of letting EE win every tie.
	if (has_ee && has_vu && ee >= 85.0f && vu >= 85.0f && (best - std::min(ee, vu)) <= 5.0f)
	{
		if (last_which == 0)
			which = 2;
		else if (last_which == 2)
			which = 0;
	}
	const uintptr_t handle = s_auto_thread[which].load(std::memory_order_relaxed);
	if (!handle)
		return;
	if (!s_auto)
	{
		s_auto = new (std::nothrow) AutoSample[kAutoMax];
		if (!s_auto)
		{
			windows = kAutoWindows;
			return;
		}
		pthread_t t;
		if (pthread_create(&t, nullptr, AutoSamplerThread, nullptr) != 0)
		{
			windows = kAutoWindows;
			return;
		}
		pthread_detach(t);
	}
	s_auto_target = reinterpret_cast<pthread_t>(handle);
	s_auto_which = which;
	s_auto_ref = reinterpret_cast<uint64_t>(&OrbisEEProfStart);
	s_auto_lo = s_auto_hi = 0;
	{
		pthread_attr_t attr;
		if (pthread_attr_init(&attr) == 0)
		{
			void* addr = nullptr;
			size_t size = 0;
			if (pthread_attr_get_np(s_auto_target, &attr) == 0 && pthread_attr_getstack(&attr, &addr, &size) == 0 && addr)
			{
				s_auto_lo = reinterpret_cast<uint64_t>(addr);
				s_auto_hi = s_auto_lo + size;
			}
			pthread_attr_destroy(&attr);
		}
	}
	CalibrateOnce();
	if (!Install(AutoHandler))
	{
		windows = kAutoWindows;
		return;
	}
	last_which = which;
	s_auto_index = ++windows;
	last = now;
	std::snprintf(s_auto_head, sizeof(s_auto_head), "%s thread (speed %.0f%%, load ee %.0f gs %.0f vu %.0f)", kAutoThreadName[which], speed, ee, gs, vu);
	s_auto_n.store(0, std::memory_order_relaxed);
	s_auto_skipped.store(0, std::memory_order_relaxed);
	s_auto_state.store(1, std::memory_order_release);
}
