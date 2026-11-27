// PS5 port frontend: the console side. The shelf runs on its own Vulkan device (the driver linked
// into the eboot) with a VK_KHR_display swapchain on VideoOut, reads the DualSense, plays its key
// sounds on an audio port of its own, and downloads missing covers and HD texture packs over our own
// HTTPS client (fe_https.h). Everything is torn down again before PCSX2 opens its device, VideoOut and
// audio.
//
// Copyright (C) 2026 swordpdf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_ps5.h"
#include "caliza/native/ps4_shell.h"

#include "fe_covers.h"
#include "fe_games.h"
#include "fe_i18n.h"
#include "fe_text.h"
#include "fe_https.h"
#include "fe_texpacks.h"
#include "fe_patchdl.h" // 2026-10-08
#include "fe_vk.h"
#include "fe_web.h"
#include "ps5/coreorbis/orbis-shims/ProsperoNotify.h" // 2026-10-05: the texture packs' popup
#ifdef PS5SX2_ACHIEVEMENTS
#include "ps5/coreorbis/orbis-shims/ProsperoAchievements.h"
#include "pcsx2/Achievements.h"
#endif
#include "third_party/qrcodegen/qrcodegen.h" // vk-285-113: orbis_web_qr

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <ctime>
#include <dirent.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mount.h> // statfs: the texture packs' free space
#ifdef ORBIS_PS4
#include <sys/statfs.h> // PS4 port (AI-assisted): OpenOrbis' struct statfs (unused there, see below)
#include "pcsx2/VMManager.h"
extern "C" uint64_t ps4_probe_frames();
#endif
#include <sys/time.h>
#include <thread>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

// What sceKernelConvertUtcToLocaltime fills in: 16 bytes (as the open-source KytyPS5 emulator
// implements it), not a struct timezone. vk-285-40 passed an 8-byte struct timezone and an int for
// the DST offset; the call wrote the zone offsets (7200, 3600) over the next stack slot, which held
// the swapchain pointer, and the first vkQueuePresentKHR faulted on it.
struct KernelTimesec
{
	int64_t t;
	uint32_t west_sec; // seconds east of UTC, despite the name
	uint32_t dst_sec;
};

extern "C" {
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
int scePadInit(void);
int scePadOpen(int32_t userId, int32_t type, int32_t index, const void* param);
int scePadClose(int32_t handle);
int scePadReadState(int32_t handle, void* data);
int sceUserServiceInitialize(const void* params);
int sceUserServiceGetInitialUser(int32_t* userId);
int sceKernelConvertUtcToLocaltime(time_t utc, time_t* local, KernelTimesec* sec, uint64_t* dst_sec);
int sceSystemServiceParamGetInt(int param, int* value);
int sceSystemServiceHideSplashScreen(void);
int sceKernelGetModuleList(int32_t* handles, size_t count, size_t* actual);
int sceKernelGetModuleInfo(int32_t handle, void* info);
int sceKernelLoadStartModule(const char* path, size_t args, const void* argp, uint32_t flags, void* opt, int* res);
int sceKernelDlsym(int handle, const char* symbol, void** addrp);
// 2026-10-05: where the texture packs' threads run (libkernel; orbis-shims/orbis_eeprof.cpp uses the same calls).
int scePthreadGetaffinity(pthread_t thread, unsigned long long* mask);
int scePthreadSetaffinity(pthread_t thread, unsigned long long mask);
int scePthreadGetprio(pthread_t thread, int* prio);
int sceKernelGetCurrentCpu(void);
int sceKernelGetCpumode(void);
#ifdef PS5SX2_IME_IMPORT
int sceImeDialogInit(void* param, void* extended);
int sceImeDialogGetStatus(void);
int sceImeDialogGetResult(void* result);
int sceImeDialogTerm(void);
#endif

// libSceNet and NetCtl for the frontend's own mbedTLS HTTPS client (fe_https.cpp): raw sockets, DNS
// and connectivity state. The certificate verification is not the console's libSceSsl.
int sceNetInit(void);
int sceNetPoolCreate(const char* name, int size, int flags);
int sceNetPoolDestroy(int pool);
int sceNetCtlInit(void);
void sceNetCtlTerm(void);
int sceNetCtlGetState(int* state);

}

// The fonts, embedded: PCSX2's Roboto (Apache-2.0) and PromptFont (OFL-1.1), from its resources.
#ifndef FE_FONT_DIR
#error "FE_FONT_DIR must name PCSX2's bin/resources/fonts"
#endif
#define FE_INCBIN(sym, file) \
	__asm__(".section .rodata." #sym ",\"a\",@progbits\n" \
			".balign 16\n" \
			".global " #sym "\n" #sym ":\n" \
			".incbin \"" FE_FONT_DIR "/" file "\"\n" \
			".global " #sym "_end\n" #sym "_end:\n" \
			".byte 0\n" \
			".previous\n")
FE_INCBIN(fe_font_text, "Roboto-Regular.ttf");
FE_INCBIN(fe_font_icons, "promptfont.otf");
extern "C" const uint8_t fe_font_text[], fe_font_text_end[], fe_font_icons[], fe_font_icons_end[];

// vk-285-50: the frontend's own files (frontend/assets): Font Awesome Free's brands font (SIL OFL
// 1.1) for the handles' icons, and the settings page with its two icons (Font Awesome, CC BY 4.0).
#ifndef FE_ASSET_DIR
#error "FE_ASSET_DIR must name frontend/assets"
#endif
#define FE_INCBIN_ASSET(sym, file) \
	__asm__(".section .rodata." #sym ",\"a\",@progbits\n" \
			".balign 16\n" \
			".global " #sym "\n" #sym ":\n" \
			".incbin \"" FE_ASSET_DIR "/" file "\"\n" \
			".global " #sym "_end\n" #sym "_end:\n" \
			".byte 0\n" \
			".previous\n")
#ifndef CALIZA_WEB_DIR
#error "CALIZA_WEB_DIR must name the canonical Caliza web panel source"
#endif
#define CALIZA_INCBIN_ASSET(sym, file) \
	__asm__(".section .rodata." #sym ",\"a\",@progbits\n" \
			".balign 16\n" \
			".global " #sym "\n" #sym ":\n" \
			".incbin \"" CALIZA_WEB_DIR "/" file "\"\n" \
			".global " #sym "_end\n" #sym "_end:\n" \
			".byte 0\n" \
			".previous\n")
FE_INCBIN_ASSET(fe_font_brands, "fonts/fa-brands-400.otf");
CALIZA_INCBIN_ASSET(fe_web_page, "index.html");
CALIZA_INCBIN_ASSET(fe_web_css, "caliza.css");
CALIZA_INCBIN_ASSET(fe_web_app, "app.js");
CALIZA_INCBIN_ASSET(fe_web_mark, "assets/caliza-mark.svg");
CALIZA_INCBIN_ASSET(fe_web_mark_mono, "assets/caliza-mark-mono.svg");
CALIZA_INCBIN_ASSET(fe_web_strata, "assets/strata-ornament.svg");
CALIZA_INCBIN_ASSET(fe_web_source_sans, "assets/fonts/source-sans-3-latin.woff2");
CALIZA_INCBIN_ASSET(fe_web_source_sans_ext, "assets/fonts/source-sans-3-latin-ext.woff2");
// vk-285-51: the recommended settings the page's Recommended button restores.
FE_INCBIN_ASSET(fe_presets, "presets.ini");
extern "C" const uint8_t fe_font_brands[], fe_font_brands_end[], fe_web_page[], fe_web_page_end[], fe_web_css[], fe_web_css_end[],
	fe_web_app[], fe_web_app_end[], fe_web_mark[], fe_web_mark_end[], fe_web_mark_mono[], fe_web_mark_mono_end[], fe_web_strata[],
	fe_web_strata_end[], fe_web_source_sans[], fe_web_source_sans_end[], fe_web_source_sans_ext[], fe_web_source_sans_ext_end[],
	fe_presets[], fe_presets_end[];

// main-boot.cpp (2026-10-05): the folder the emulator would take a game's texture pack from (a USB drive, the settings
// page's folder, /data), for the HD texture pack row.
std::string OrbisTexturesGameDir(const std::string& serial, std::string& how);

namespace
{
using namespace fe;

// xlenore/ps2-covers, the default set: <serial>.jpg, 512x736 (the same source as the user's Twiso).
constexpr const char* kCoverUrl = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/${serial}.jpg";

// libScePad's state, as game_select.cpp and main-boot.cpp read it.
struct PadData
{
	uint32_t buttons;
	uint8_t lx, ly, rx, ry, l2, r2, pad0, pad1;
	uint8_t rest[256];
};
double Now()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<double>(ts.tv_sec) + ts.tv_nsec * 1e-9;
}

// The time of day in the console's time zone and clock format.
std::string Clock()
{
	const time_t utc = time(nullptr);
	time_t local = utc;
	// Room to spare after both outputs, so a firmware that writes more cannot reach our locals.
	KernelTimesec sec[2] = {};
	uint64_t dst[2] = {};
	if (sceKernelConvertUtcToLocaltime(utc, &local, &sec[0], &dst[0]) != 0)
		local = utc;
	struct tm tm = {};
	gmtime_r(&local, &tm);
	static int s_format = -1;
	if (s_format < 0)
	{
		int v = 1;
		s_format = sceSystemServiceParamGetInt(3 /* time format */, &v) == 0 ? v : 1;
	}
	char buf[16];
	if (s_format == 0) // 12-hour
		std::snprintf(buf, sizeof(buf), "%d:%02d %s", (tm.tm_hour + 11) % 12 + 1, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
	else
		std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
	return buf;
}

// ---- 2026-10-05 (AI-assisted): the HD texture packs' downloads (fe_texpacks.h) -----------------------------------------
// archive.org's file list and the packs, in ranges of up to 32 MB that the manager asks for one after another, over our
// own HTTPS (fe_https.h). The console's libSceHttp2 can't do it after the jailbreak: libSceSsl can't reach its certificate
// store there, and with roots handed to it, it still can't follow archive.org's cross-signed chain (texnet3-5 on the
// console). mbedTLS checks each server's chain against Mozilla's roots, and its name; texnet6 fetched the list and a whole
// pack through the download redirect this way on the console. Needs proper testing in the app.
struct TexHttp
{
	explicit TexHttp(const char* name = "pcsx2-texpacks", const char* tag = "texpacks")
		: pool_name(name)
		, log_tag(tag)
	{
	}
	const char* pool_name; // 2026-10-08: a second client for the online patches (its own pool and log tag)
	const char* log_tag;
	std::mutex mutex; // the client's setup
	int pool = -1;
	std::unique_ptr<HttpsClient> client;

	// The client, made once the console is online; null (and tried again next time) when it isn't.
	HttpsClient* Client()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (client)
			return client.get();
		// NetCtl stays up once started (the covers and DEV9's DNS lookup use it too): no sceNetCtlTerm here.
		(void)sceNetCtlInit();
		int state[4] = {-1, 0, 0, 0};
		const int gs = sceNetCtlGetState(state);
		if (gs == 0 && state[0] >= 0 && state[0] < 3)
		{
			std::printf("[%s] the console is not connected (netctl state %d)\n", log_tag, state[0]);
			std::fflush(stdout);
			return nullptr;
		}
		sceNetInit();
		if (pool < 0)
			pool = sceNetPoolCreate(pool_name, 64 * 1024, 0);
		if (pool < 0)
		{
			std::printf("[%s] no libnet pool (%#x)\n", log_tag, static_cast<unsigned>(pool));
			std::fflush(stdout);
			return nullptr;
		}
		const char* const tag = log_tag;
		auto made = std::make_unique<HttpsClient>(MakeConsoleHttpsPlatform(pool, [tag](const std::string& line) {
			std::printf("[%s] %s\n", tag, line.c_str());
			std::fflush(stdout);
		}));
		std::string error;
		if (!made->Init(error))
			return nullptr; // the client said why
		client = std::move(made);
		return client.get();
	}

	void Term()
	{
		std::lock_guard<std::mutex> lock(mutex);
		client.reset();
		if (pool >= 0)
			sceNetPoolDestroy(pool);
		pool = -1;
	}

	// Fails the request in flight (another thread: Cancel, the shelf's end).
	void Abort()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (client)
			client->Abort();
	}

	// One GET: the whole answer (ranged false) or bytes [offset, offset + length). The HTTP status, or < 0 (-3: the
	// server ignored a range after the file's start, and nothing was delivered).
	int Get(const std::string& url, bool ranged, uint64_t offset, uint64_t length, const std::function<bool(const void*, size_t)>& sink)
	{
		HttpsClient* c = Client();
		if (!c)
			return -1;
		// A 32 MB range: 15 minutes at most; the list: a minute.
		return c->Get(url, ranged, offset, length, sink, ranged ? 900 * 1000 : 60 * 1000);
	}
};
TexHttp g_texhttp;
TexHttp g_patchhttp("pcsx2-patches", "patches"); // 2026-10-08: the online patches' own client (fe_patchdl.h)

// Covers use the same certificate-checking mbedTLS client rather than the PS5-only libSceHttp2 API. A
// dedicated client keeps cover traffic from queueing behind texture-pack or patch downloads.
struct CoverHttp
{
	TexHttp http{"pcsx2-covers", "covers"};

	int Get(const std::string& url, std::vector<uint8_t>& out)
	{
		out.clear();
		bool overflow = false;
		const int status = http.Get(url, false, 0, 0, [&](const void* data, size_t len) {
			if (out.size() + len > (16u << 20))
			{
				overflow = true;
				return false;
			}
			const uint8_t* const bytes = static_cast<const uint8_t*>(data);
			out.insert(out.end(), bytes, bytes + len);
			return true;
		});
		if (overflow)
		{
			out.clear();
			return -1;
		}
		return status;
	}

	void Abort() { http.Abort(); }
	void Term() { http.Term(); }
};
CoverHttp g_coverhttp;

// The free space of a folder's disk for the texture packs, or UINT64_MAX when the console won't say (the manager then
// skips its room check; a full disk still fails the writes, which it reports). Two ways crashed the app on the console:
// statvfs (pr9f: libSceLibcInternal's jumps through a libkernel import an app doesn't get; klog: SIGSEGV at an address
// with nothing there) and the SDK libc's statfs stub (pr9g: a system call from outside libkernel; no report at all).
// This asks libkernel's own _fstatfs, looked up by name in the loaded modules as orbis-shims/ProsperoKbdMouse.cpp does,
// so a console without it gets a null and an "unknown", never a jump to nowhere. Needs testing on the console.
// Where a thread runs: "cpu N, priority P, cpus MASK".
std::string ThreadPlace()
{
	unsigned long long mask = 0;
	int prio = -1;
	const int got = scePthreadGetaffinity(pthread_self(), &mask);
	scePthreadGetprio(pthread_self(), &prio);
	char buf[96];
	std::snprintf(buf, sizeof(buf), "cpu %d, priority %d, cpus %s%llx", sceKernelGetCurrentCpu(), prio, got == 0 ? "0x" : "? ", mask);
	return buf;
}

// The start of each texture-pack thread (TexturePackPlatform::thread_start). A thread takes the CPUs of the thread that
// made it, and the console's first unpack ran some 25 times slower than the same work in a payload (pr9h: 13,200 files
// in over 30 minutes; a payload wrote about 190 files a second): if the shelf's thread is held to one CPU, the
// texture packs' threads share it with a thread that waits for every frame. So they get all 13 of the title's CPUs (0 to
// 12, as measured for the RPCS3 port). Logged, with what the system answered. Needs testing on the console.
void TexturePackThreadStart(const char* role)
{
#ifdef ORBIS_PS4
	extern std::atomic<unsigned long long> g_orbis_title_cpu_mask;
	unsigned long long kTitleCpus = g_orbis_title_cpu_mask.load(std::memory_order_acquire);
	if (kTitleCpus == 0)
	{
		const int cpumode = sceKernelGetCpumode();
		kTitleCpus = (cpumode == 1 || cpumode == 5) ? 0x7full : 0x3full;
	}
#else
	constexpr unsigned long long kTitleCpus = 0x1fffull;
#endif
	unsigned long long mask = 0;
	const std::string before = ThreadPlace();
	std::string what;
	if (scePthreadGetaffinity(pthread_self(), &mask) == 0 && mask != 0 && (mask & kTitleCpus) != kTitleCpus)
	{
		const int rc = scePthreadSetaffinity(pthread_self(), kTitleCpus);
		char buf[64];
		std::snprintf(buf, sizeof(buf), "; all the title's CPUs: %s", rc == 0 ? "yes" : "refused");
		what = buf;
		if (rc != 0)
		{
			std::snprintf(buf, sizeof(buf), " (%#x)", static_cast<unsigned>(rc));
			what += buf;
		}
	}
	std::printf("[texpacks] %s thread: %s%s\n", role, before.c_str(), what.c_str());
	std::fflush(stdout);
}

uint64_t TexturePackFreeBytes(const std::string& dir)
{
	using FstatfsFn = int (*)(int, struct statfs*);
	static const FstatfsFn fstatfs_fn = [] {
		int32_t handles[256];
		size_t count = 0;
		FstatfsFn found = nullptr;
		if (sceKernelGetModuleList(handles, 256, &count) == 0)
		{
			for (size_t i = 0; i < count && i < 256 && !found; i++)
			{
				void* address = nullptr;
				if (sceKernelDlsym(handles[i], "_fstatfs", &address) == 0 && address)
					found = reinterpret_cast<FstatfsFn>(address);
			}
		}
		std::printf("[texpacks] free space: %s\n", found ? "libkernel's _fstatfs" : "no _fstatfs in this process (no room check)");
		std::fflush(stdout);
		return found;
	}();
	if (!fstatfs_fn)
		return UINT64_MAX;
#ifdef ORBIS_PS4
	// PS4 port (AI-assisted): OpenOrbis' struct statfs is Linux's layout, not the FreeBSD kernel's: no room check.
	return UINT64_MAX;
#endif
	const int fd = open(dir.c_str(), O_RDONLY);
	if (fd < 0)
		return UINT64_MAX;
	// Room to spare in case the kernel's struct is bigger than the header's.
	union
	{
		struct statfs s;
		char room[sizeof(struct statfs) + 1024];
	} v;
	std::memset(&v, 0, sizeof(v));
	const int rc = fstatfs_fn(fd, &v.s);
	close(fd);
	if (rc != 0 || v.s.f_bsize == 0 || v.s.f_bavail < 0)
		return UINT64_MAX;
	const uint64_t bytes = static_cast<uint64_t>(v.s.f_bavail) * v.s.f_bsize;
	static std::atomic<bool> said{false};
	if (!said.exchange(true))
	{
		std::printf("[texpacks] free space in %s: %llu MB\n", dir.c_str(), static_cast<unsigned long long>(bytes >> 20));
		std::fflush(stdout);
	}
	return bytes;
}


// ---- The display ----
struct Display
{
	Vk vk;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice pd = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t qf = 0;
	VkQueue queue = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> images;
	VkExtent2D extent = {};
	VkSemaphore acquired[2] = {}, rendered[2] = {};

	bool Fail(const char* what, VkResult r)
	{
		std::printf("[frontend] %s failed: %d\n", what, static_cast<int>(r));
		std::fflush(stdout);
		return false;
	}

	bool Init()
	{
		const char* missing = nullptr;
		if (!vk.LoadGlobal(vk_icdGetInstanceProcAddr, &missing))
		{
			std::printf("[frontend] no %s\n", missing);
			return false;
		}
		const char* inst_ext[2] = {"VK_KHR_surface", "VK_KHR_display"};
		VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		ai.pApplicationName = "Caliza";
		ai.apiVersion = VK_API_VERSION_1_1;
		VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		ici.pApplicationInfo = &ai;
		ici.enabledExtensionCount = 2;
		ici.ppEnabledExtensionNames = inst_ext;
		VkResult r = vk.vkCreateInstance(&ici, nullptr, &instance);
		if (r != VK_SUCCESS)
			return Fail("vkCreateInstance", r);
		if (!vk.LoadInstance(instance, true, &missing))
		{
			std::printf("[frontend] no %s\n", missing);
			return false;
		}
		uint32_t count = 1;
		r = vk.vkEnumeratePhysicalDevices(instance, &count, &pd);
		if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || count == 0)
			return Fail("vkEnumeratePhysicalDevices", r);
		uint32_t qcount = 0;
		vk.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
		std::vector<VkQueueFamilyProperties> qfs(qcount);
		vk.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qfs.data());
		qf = UINT32_MAX;
		for (uint32_t i = 0; i < qcount; i++)
			if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				qf = i;
				break;
			}
		if (qf == UINT32_MAX)
			return Fail("a graphics queue", VK_ERROR_UNKNOWN);
		const float prio = 1.0f;
		VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		qi.queueFamilyIndex = qf;
		qi.queueCount = 1;
		qi.pQueuePriorities = &prio;
		const char* dev_ext[1] = {"VK_KHR_swapchain"};
		VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		dci.queueCreateInfoCount = 1;
		dci.pQueueCreateInfos = &qi;
		dci.enabledExtensionCount = 1;
		dci.ppEnabledExtensionNames = dev_ext;
		r = vk.vkCreateDevice(pd, &dci, nullptr, &device);
		if (r != VK_SUCCESS)
			return Fail("vkCreateDevice", r);
		if (!vk.LoadDevice(device, true, &missing))
		{
			std::printf("[frontend] no %s\n", missing);
			return false;
		}
		vk.vkGetDeviceQueue(device, qf, 0, &queue);
		return CreateSurface() && CreateSwapchain();
	}

	// As PCSX2's VKSwapChain does on the PS5: the first display with a mode, its largest mode (of equally
	// large ones the one nearest 60 Hz, vk-285-115), and a plane that can drive it.
	bool CreateSurface()
	{
		uint32_t display_count = 0;
		if (vk.vkGetPhysicalDeviceDisplayPropertiesKHR(pd, &display_count, nullptr) != VK_SUCCESS || !display_count)
			return Fail("vkGetPhysicalDeviceDisplayPropertiesKHR", VK_ERROR_UNKNOWN);
		std::vector<VkDisplayPropertiesKHR> displays(display_count);
		vk.vkGetPhysicalDeviceDisplayPropertiesKHR(pd, &display_count, displays.data());
		uint32_t plane_count = 0;
		if (vk.vkGetPhysicalDeviceDisplayPlanePropertiesKHR(pd, &plane_count, nullptr) != VK_SUCCESS || !plane_count)
			return Fail("vkGetPhysicalDeviceDisplayPlanePropertiesKHR", VK_ERROR_UNKNOWN);
		std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
		vk.vkGetPhysicalDeviceDisplayPlanePropertiesKHR(pd, &plane_count, planes.data());
		VkDisplayKHR display = VK_NULL_HANDLE;
		VkDisplayModePropertiesKHR mode = {};
		for (const VkDisplayPropertiesKHR& d : displays)
		{
			uint32_t mode_count = 0;
			if (vk.vkGetDisplayModePropertiesKHR(pd, d.display, &mode_count, nullptr) != VK_SUCCESS || !mode_count)
				continue;
			std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
			vk.vkGetDisplayModePropertiesKHR(pd, d.display, &mode_count, modes.data());
			auto off_60hz = [](const VkDisplayModePropertiesKHR& x) {
				const int64_t mhz = static_cast<int64_t>(x.parameters.refreshRate);
				return mhz > 60000 ? mhz - 60000 : 60000 - mhz;
			};
			for (const VkDisplayModePropertiesKHR& m : modes)
			{
				const uint64_t area = static_cast<uint64_t>(m.parameters.visibleRegion.width) * m.parameters.visibleRegion.height;
				const uint64_t best = static_cast<uint64_t>(mode.parameters.visibleRegion.width) * mode.parameters.visibleRegion.height;
				if (!display || area > best || (area == best && off_60hz(m) < off_60hz(mode)))
				{
					display = d.display;
					mode = m;
				}
			}
			if (display)
				break;
		}
		if (!display)
			return Fail("a display mode", VK_ERROR_UNKNOWN);
		uint32_t plane = UINT32_MAX;
		for (uint32_t i = 0; i < plane_count; i++)
			if (planes[i].currentDisplay == VK_NULL_HANDLE || planes[i].currentDisplay == display)
			{
				plane = i;
				break;
			}
		if (plane == UINT32_MAX)
			return Fail("a display plane", VK_ERROR_UNKNOWN);
		const VkDisplaySurfaceCreateInfoKHR ci = {VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR, nullptr, 0,
			mode.displayMode, plane, planes[plane].currentStackIndex, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, 1.0f,
			VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR, mode.parameters.visibleRegion};
		const VkResult r = vk.vkCreateDisplayPlaneSurfaceKHR(instance, &ci, nullptr, &surface);
		if (r != VK_SUCCESS)
			return Fail("vkCreateDisplayPlaneSurfaceKHR", r);
		extent = mode.parameters.visibleRegion;
		std::printf("[frontend] display %ux%u at %.2f Hz, plane %u\n", extent.width, extent.height, mode.parameters.refreshRate / 1000.0,
			plane);
		return true;
	}

	bool CreateSwapchain()
	{
		VkSurfaceCapabilitiesKHR caps = {};
		VkResult r = vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps);
		if (r != VK_SUCCESS)
			return Fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r);
		if (caps.currentExtent.width != UINT32_MAX)
			extent = caps.currentExtent;
		uint32_t images_wanted = std::max(2u, caps.minImageCount);
		if (caps.maxImageCount)
			images_wanted = std::min(images_wanted, caps.maxImageCount);
		VkSwapchainCreateInfoKHR sci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
		sci.surface = surface;
		sci.minImageCount = images_wanted;
		sci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
		sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		sci.imageExtent = extent;
		sci.imageArrayLayers = 1;
		sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		sci.clipped = VK_TRUE;
		r = vk.vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain);
		if (r != VK_SUCCESS)
			return Fail("vkCreateSwapchainKHR", r);
		uint32_t n = 0;
		vk.vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
		images.resize(n);
		vk.vkGetSwapchainImagesKHR(device, swapchain, &n, images.data());
		VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
		for (int i = 0; i < 2; i++)
			if (vk.vkCreateSemaphore(device, &si, nullptr, &acquired[i]) != VK_SUCCESS ||
				vk.vkCreateSemaphore(device, &si, nullptr, &rendered[i]) != VK_SUCCESS)
				return Fail("vkCreateSemaphore", VK_ERROR_UNKNOWN);
		std::printf("[frontend] swapchain: %u images %ux%u\n", n, extent.width, extent.height);
		return true;
	}

	void Destroy()
	{
		if (device)
		{
			vk.vkDeviceWaitIdle(device);
			for (int i = 0; i < 2; i++)
			{
				if (acquired[i])
					vk.vkDestroySemaphore(device, acquired[i], nullptr);
				if (rendered[i])
					vk.vkDestroySemaphore(device, rendered[i], nullptr);
				acquired[i] = rendered[i] = VK_NULL_HANDLE;
			}
			if (swapchain)
				vk.vkDestroySwapchainKHR(device, swapchain, nullptr); // closes VideoOut for PCSX2
			swapchain = VK_NULL_HANDLE;
			vk.vkDestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
		}
		if (surface)
			vk.vkDestroySurfaceKHR(instance, surface, nullptr);
		surface = VK_NULL_HANDLE;
		if (instance)
			vk.vkDestroyInstance(instance, nullptr);
		instance = VK_NULL_HANDLE;
	}
};

std::string ReadLastGame(const std::string& dir)
{
	std::string name;
	if (FILE* f = std::fopen((dir + "/lastgame.txt").c_str(), "r"))
	{
		char buf[512] = {};
		if (std::fgets(buf, sizeof(buf), f))
			name = buf;
		std::fclose(f);
	}
	while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
		name.pop_back();
	return name;
}

void WriteLastGame(const std::string& dir, const std::string& file)
{
	if (FILE* f = std::fopen((dir + "/lastgame.txt").c_str(), "w"))
	{
		std::fprintf(f, "%s\n", file.c_str());
		std::fclose(f);
	}
}

// ---- Test build 1 (vk-285-55): games on USB drives.

std::string LowerAscii(std::string s)
{
	for (char& c : s)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return s;
}

// The disc images (.iso, .chd) straight in `dir`, or -1 when it can't be opened as a folder.
int CountImages(const std::string& dir)
{
	DIR* d = opendir(dir.c_str());
	if (!d)
		return -1;
	int n = 0;
	while (const dirent* e = readdir(d))
		n += IsDiscImageName(e->d_name) ? 1 : 0;
	closedir(d);
	return n;
}

bool OnUsb(const std::string& path)
{
	return path.compare(0, 8, "/mnt/usb") == 0 || path.compare(0, 8, "/mnt/ext") == 0; // 2026-10-08: and the ext/M.2 drives
}

// cache/usb-games.txt, one "<serial>\t<stem>\t<title>" line per USB game: before the jailbreak,
// where covers download, the app may not see the drives, so the covers of the games found on them
// after the jailbreak are fetched at the next start from this list.
void WriteUsbList(const std::string& path, const std::vector<GameInfo>& games)
{
	if (path.empty())
		return;
	std::string text;
	for (const GameInfo& g : games)
		if (OnUsb(g.path) && !g.serial.empty())
			text += g.serial + "\t" + g.stem + "\t" + g.title + "\n";
	std::string old;
	if (FILE* f = std::fopen(path.c_str(), "rb"))
	{
		char buf[4096];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
			old.append(buf, n);
		std::fclose(f);
	}
	if (text.empty() || text == old)
		return;
	const size_t slash = path.rfind('/');
	if (slash != std::string::npos)
		mkdir(path.substr(0, slash).c_str(), 0777);
	if (FILE* f = std::fopen(path.c_str(), "wb"))
	{
		std::fwrite(text.data(), 1, text.size(), f);
		std::fclose(f);
	}
}

// Adds the listed USB games that `games` doesn't hold (by serial), with no image path: enough for the
// cover prefetch, which works from serials.
int AddUsbListGames(const std::string& path, std::vector<GameInfo>& games)
{
	FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
	if (!f)
		return 0;
	int added = 0;
	char line[1024];
	while (std::fgets(line, sizeof(line), f))
	{
		std::string l = line;
		while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
			l.pop_back();
		const size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
		if (t2 == std::string::npos || t1 == 0)
			continue;
		GameInfo g;
		g.serial = l.substr(0, t1);
		g.stem = l.substr(t1 + 1, t2 - t1 - 1);
		g.title = l.substr(t2 + 1);
		if (std::any_of(games.begin(), games.end(), [&](const GameInfo& x) { return x.serial == g.serial; }))
			continue;
		games.push_back(g);
		added++;
	}
	std::fclose(f);
	return added;
}
} // namespace

// 2026-10-05 (AI-assisted): the PS5's own keyboard for the account panel (libSceImeDialog), swordpdf: "use the shell
// keyboard". The parameter block is the PS4 SDK's SceImeDialogParam as shadPS4's reimplementation lays it out (96 bytes);
// its checks there: a password needs the BasicLatin type, the reserved bytes must be zero, the position is in 1920x1080.
// This process may not load the library itself: firmware 11.40 refuses libSceNotification, libSceKeyboard and libSceMouse
// with 0x80020063. So the library is used when it is already in the process (the list of loaded modules is logged once,
// to boot.log), or when the load is allowed; otherwise TextEntryService::open fails and the panel shows its own keyboard.
// With PS5SX2_IME_IMPORT the functions are imported instead (the system loads the library at the app's start, if it will):
// an experiment, since an app whose import is refused doesn't start.
namespace
{
struct ImeDialogParam
{
	int32_t user_id;
	uint32_t type; // 0 default, 1 basic latin, 2 URL, 3 mail, 4 number
	uint64_t supported_languages;
	uint32_t enter_label;
	uint32_t input_method;
	void* filter;
	uint32_t option;
	uint32_t max_text_length;
	char16_t* input_text_buffer;
	float posx, posy;
	uint32_t horizontal_alignment, vertical_alignment; // 0 left/top, 1 centre, 2 right/bottom
	const char16_t* placeholder;
	const char16_t* title;
	int8_t reserved[16];
};
static_assert(sizeof(ImeDialogParam) == 96, "SceImeDialogParam is 96 bytes");
struct ImeDialogResult
{
	uint32_t end_status; // 0 OK, 1 cancelled, 2 aborted
	// The SDKs disagree on the size of what follows (12 bytes, or shadPS4's 12 ints); room for either, so a library
	// that writes all of it can't write past this on the stack.
	int32_t reserved[16];
};
constexpr uint32_t kImeTypeBasicLatin = 1;
constexpr uint32_t kImeOptionNoAutoCapitalization = 0x2, kImeOptionPassword = 0x4, kImeOptionNoLearning = 0x20;
using ImeInitFn = int (*)(ImeDialogParam*, void*);
using ImeStatusFn = int (*)();
using ImeResultFn = int (*)(ImeDialogResult*);
using ImeTermFn = int (*)();

struct SystemKeyboard
{
	bool tried = false;
	ImeInitFn init = nullptr;
	ImeStatusFn status = nullptr;
	ImeResultFn result = nullptr;
	ImeTermFn term = nullptr;
	bool running = false;
	int32_t user = -1;
	char16_t buffer[512 + 1] = {};
	char16_t title[96] = {};
};
SystemKeyboard g_ime;

std::u16string Utf8To16(const std::string& s)
{
	std::u16string out;
	for (size_t i = 0; i < s.size();)
	{
		const unsigned char c = static_cast<unsigned char>(s[i]);
		uint32_t cp = c;
		size_t n = 1;
		if (c >= 0xF0 && i + 3 < s.size())
			cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu), n = 4;
		else if (c >= 0xE0 && i + 2 < s.size())
			cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu), n = 3;
		else if (c >= 0xC0 && i + 1 < s.size())
			cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu), n = 2;
		i += n;
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			out += static_cast<char16_t>(0xD800 + (cp >> 10));
			out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
		}
		else
			out += static_cast<char16_t>(cp);
	}
	return out;
}

std::string Utf16To8(const char16_t* s)
{
	std::string out;
	for (size_t i = 0; s[i]; i++)
	{
		uint32_t cp = s[i];
		if (cp >= 0xD800 && cp < 0xDC00 && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
			cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
		if (cp < 0x80)
			out += static_cast<char>(cp);
		else if (cp < 0x800)
			out += static_cast<char>(0xC0 | (cp >> 6)), out += static_cast<char>(0x80 | (cp & 0x3F));
		else if (cp < 0x10000)
			out += static_cast<char>(0xE0 | (cp >> 12)), out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
				out += static_cast<char>(0x80 | (cp & 0x3F));
		else
			out += static_cast<char>(0xF0 | (cp >> 18)), out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	return out;
}

// The modules in this process, by name (logged once), and the handle of `want` among them (-1 if absent).
int LoadedModule(const char* want)
{
	struct ModuleInfo
	{
		uint64_t size;
		char name[256];
		struct
		{
			uint64_t address;
			uint32_t size;
			int32_t prot;
		} segments[4];
		uint32_t num_segments;
		uint8_t fingerprint[20];
	};
	int32_t handles[256];
	size_t count = 0;
	const int rc = sceKernelGetModuleList(handles, 256, &count);
	static bool s_logged = false;
	std::string names;
	int found = -1;
	for (size_t i = 0; rc == 0 && i < count && i < 256; i++)
	{
		ModuleInfo info = {};
		info.size = sizeof(info);
		if (sceKernelGetModuleInfo(handles[i], &info) != 0)
			continue;
		info.name[sizeof(info.name) - 1] = '\0';
		if (!names.empty())
			names += ' ';
		names += info.name;
		if (std::strcmp(info.name, want) == 0)
			found = handles[i];
	}
	if (!s_logged)
	{
		s_logged = true;
		std::printf("[ime] modules in this process (list rc %#x, %zu): %s\n", static_cast<unsigned>(rc), count, names.c_str());
	}
	return found;
}

bool SystemKeyboardReady()
{
	SystemKeyboard& k = g_ime;
	if (k.tried)
		return k.init != nullptr;
	k.tried = true;
#ifdef PS5SX2_IME_IMPORT
	k.init = reinterpret_cast<ImeInitFn>(&sceImeDialogInit);
	k.status = reinterpret_cast<ImeStatusFn>(&sceImeDialogGetStatus);
	k.result = reinterpret_cast<ImeResultFn>(&sceImeDialogGetResult);
	k.term = reinterpret_cast<ImeTermFn>(&sceImeDialogTerm);
	std::printf("[ime] libSceImeDialog imported at start\n");
#else
	int module = LoadedModule("libSceImeDialog.sprx");
	if (module >= 0)
		std::printf("[ime] libSceImeDialog is already loaded (%#x)\n", static_cast<unsigned>(module));
	else
	{
		int res = 0;
		module = sceKernelLoadStartModule("/system/common/lib/libSceImeDialog.sprx", 0, nullptr, 0, nullptr, &res);
		std::printf("[ime] /system/common/lib/libSceImeDialog.sprx: load %#x (start result %d)\n", static_cast<unsigned>(module), res);
	}
	if (module >= 0)
	{
		void* a[4] = {};
		const char* const names[4] = {"sceImeDialogInit", "sceImeDialogGetStatus", "sceImeDialogGetResult", "sceImeDialogTerm"};
		bool ok = true;
		for (int i = 0; i < 4; i++)
			ok = sceKernelDlsym(module, names[i], &a[i]) == 0 && a[i] && ok;
		if (ok)
		{
			k.init = reinterpret_cast<ImeInitFn>(a[0]);
			k.status = reinterpret_cast<ImeStatusFn>(a[1]);
			k.result = reinterpret_cast<ImeResultFn>(a[2]);
			k.term = reinterpret_cast<ImeTermFn>(a[3]);
		}
		else
			std::printf("[ime] libSceImeDialog: a function is missing\n");
	}
#endif
	std::printf("[ime] the system keyboard is %s\n", k.init ? "available" : "not available: the panel's own keyboard is used");
	std::fflush(stdout);
	return k.init != nullptr;
}

bool SystemKeyboardOpen(const std::string& title, const std::string& text, bool password, unsigned max_length)
{
	SystemKeyboard& k = g_ime;
	if (k.running || k.user < 0 || !SystemKeyboardReady())
		return false;
	max_length = std::min<unsigned>(max_length, 512);
	const std::u16string start = Utf8To16(text), t = Utf8To16(title);
	std::fill(std::begin(k.buffer), std::end(k.buffer), u'\0');
	std::copy_n(start.data(), std::min<size_t>(start.size(), max_length), k.buffer);
	std::fill(std::begin(k.title), std::end(k.title), u'\0');
	std::copy_n(t.data(), std::min<size_t>(t.size(), 95), k.title);
	ImeDialogParam p = {};
	p.user_id = k.user;
	p.type = kImeTypeBasicLatin; // a password needs it, and RetroAchievements names are plain letters and digits
	p.option = kImeOptionNoAutoCapitalization | kImeOptionNoLearning | (password ? kImeOptionPassword : 0);
	p.max_text_length = max_length;
	p.input_text_buffer = k.buffer;
	p.posx = 960.0f;
	p.posy = 300.0f;
	p.horizontal_alignment = 1;
	p.vertical_alignment = 1;
	p.title = k.title;
	const int rc = k.init(&p, nullptr);
	std::printf("[ime] keyboard for %s: init %#x\n", password ? "the password" : "the username", static_cast<unsigned>(rc));
	std::fflush(stdout);
	if (rc < 0)
		return false;
	k.running = true;
	return true;
}

int SystemKeyboardPoll(std::string& text)
{
	SystemKeyboard& k = g_ime;
	if (!k.running)
		return -1;
	const int status = k.status();
	if (status == 1)
		return 0; // still typing
	ImeDialogResult r = {};
	const int rrc = status == 2 ? k.result(&r) : -1;
	const int trc = k.term();
	k.running = false;
	const bool ok = status == 2 && rrc == 0 && r.end_status == 0;
	if (ok)
		text = Utf16To8(k.buffer);
	std::fill(std::begin(k.buffer), std::end(k.buffer), u'\0'); // it may hold the password
	std::printf("[ime] keyboard closed: status %d, result %#x, end %u, term %#x%s\n", status, static_cast<unsigned>(rrc), r.end_status,
		static_cast<unsigned>(trc), ok ? ", text taken" : "");
	std::fflush(stdout);
	return ok ? 1 : -1;
}
} // namespace

std::vector<std::string> orbis_usb_game_dirs(const char* when)
{
	std::vector<std::string> dirs;
	int drives = 0;
	// 2026-10-08 (AI-assisted; testers: "some people want ext/usb or m2"): the PS5's extended storage and M.2 drives
	// (/mnt/ext0, /mnt/ext1) the same way as the USB drives, after them.
	for (int i = 0; i < 10; i++)
	{
		char root[16];
		if (i < 8)
			std::snprintf(root, sizeof(root), "/mnt/usb%d", i);
		else
			std::snprintf(root, sizeof(root), "/mnt/ext%d", i - 8);
		const char* const tag = i < 8 ? "usb" : "drive";
		DIR* d = opendir(root);
		if (!d)
		{
			if (when && errno != ENOENT)
				std::printf("[%s] %s: %s: can't open (errno %d)\n", tag, when, root, errno);
			continue;
		}
		int entries = 0, isos = 0;
		std::vector<std::string> subs;
		while (const dirent* e = readdir(d))
		{
			if (e->d_name[0] == '.')
				continue;
			entries++;
			isos += IsDiscImageName(e->d_name) ? 1 : 0;
			const std::string lower = LowerAscii(e->d_name);
			if (lower == "dvd" || lower == "cd" || lower == "caliza" || lower == "games" || lower == "ps2")
				subs.push_back(e->d_name);
			// vk-285-118 (AI-assisted): a drive laid out like /data/caliza (testers: "games in the games/ folder on the external
			// drive are NOT detected", while PCSX2/textures there works): PCSX2/games/, and caliza/games/, also DVD/ and CD/ in them.
			if (lower == "pcsx2" || lower == "caliza")
			{
				const std::string parent = std::string(root) + "/" + e->d_name;
				if (DIR* pd = opendir(parent.c_str()))
				{
					while (const dirent* pe = readdir(pd))
					{
						const std::string l2 = LowerAscii(pe->d_name);
						if (l2 == "games" || l2 == "dvd" || l2 == "cd")
							subs.push_back(std::string(e->d_name) + "/" + pe->d_name);
					}
					closedir(pd);
				}
			}
		}
		closedir(d);
		if (entries == 0)
			continue; // no drive in this slot (the folder is there either way)
		drives++;
		dirs.push_back(root);
		std::string found = "/ (" + std::to_string(isos) + ")";
		for (const std::string& sub : subs)
		{
			const std::string dir = std::string(root) + "/" + sub;
			const int n = CountImages(dir);
			if (n < 0)
				continue;
			dirs.push_back(dir);
			found += ", " + sub + "/ (" + std::to_string(n) + ")";
		}
		if (when)
			std::printf("[%s] %s: %s: %d entries; disc images in %s\n", tag, when, root, entries, found.c_str());
	}
	if (when && drives == 0)
		std::printf("[usb] %s: no USB drive with files at /mnt/usb0-7 (nor at /mnt/ext0-1)\n", when);
	if (when)
		std::fflush(stdout);
	return dirs;
}

bool orbis_frontend_watermark(const char* line1, const char* line2, const char* line3, float alpha1, float alpha2,
	std::vector<uint32_t>& rgba, int& w, int& h)
{
	Fonts fonts;
	if (!fonts.Init(fe_font_text, static_cast<size_t>(fe_font_text_end - fe_font_text), fe_font_icons,
			static_cast<size_t>(fe_font_icons_end - fe_font_icons)))
		return false;
	RasterWatermark(fonts, line1, line2, line3, alpha1, alpha2, rgba, w, h);
	return true;
}

// vk-285-50: the settings page's server; it lives until the app ends.
static fe::WebServer* g_web = nullptr;
#ifdef ORBIS_PS4
static std::atomic<uint64_t> g_probe_shelf_frames{0};
#endif

// vk-285-51: the settings log's clock, in the console's time zone (as Clock() above).
static long long SettingsLogLocalTime(long long utc)
{
	time_t local = static_cast<time_t>(utc);
	KernelTimesec sec[2] = {};
	uint64_t dst[2] = {};
	if (sceKernelConvertUtcToLocaltime(static_cast<time_t>(utc), &local, &sec[0], &dst[0]) != 0)
		local = static_cast<time_t>(utc);
	return static_cast<long long>(local);
}

// vk-285-53: sceSystemServiceHideSplashScreen once per process (later calls return the first result).
int orbis_hide_splash()
{
	static std::atomic<int> s_result{1};
	int expected = 1;
	if (s_result.compare_exchange_strong(expected, 2))
		s_result.store(sceSystemServiceHideSplashScreen());
	return s_result.load();
}

int orbis_ps5_language()
{
	static int s_lang = -2;
	if (s_lang == -2)
	{
		int v = 1;
		const int rc = sceSystemServiceParamGetInt(1 /* language */, &v);
		s_lang = rc == 0 ? v : 1;
		std::printf("[i18n] system language: rc=%x value=%d\n", static_cast<unsigned>(rc), v);
		std::fflush(stdout);
	}
	return s_lang;
}

void orbis_frontend_set_language(const std::string& lang_dir)
{
	fe::SetLanguage(orbis_ps5_language(), lang_dir);
}

// vk-285-113: the settings log on a console that shows /data only after the jailbreak. The path given at the start
// is then the top folder's (logs/ isn't visible yet) and every line written before the jailbreak was lost, the
// "app start" line with them (v112's console ...575 on firmware 10.20: sessions with no build and no game). Now:
//   - logs/ is looked for at every write, so the file is logs/settings.log as soon as that folder is there;
//   - a line that can't be written yet waits in a small buffer and goes out, in order, with the next one that can;
//   - the first write of a run first looks at the end of the file for the previous run's closing line and, when
//     there is none, says so: the app was closed from the PS button, the system killed it, the console lost power,
//     or it crashed without a signal.
// All of it is open()/pread()/write() on stack buffers, so the crash handler and the exit hooks can still call it.
static char g_event_log_path[256];     // as given
static char g_event_log_dir_logs[256]; // <dir>/logs, checked at each write ("" when the path already is inside a logs folder)
static char g_event_log_in_logs[256];  // <dir>/logs/<name>
static char g_event_pending[8192];     // formatted lines waiting for a folder
static size_t g_event_pending_len = 0;
static std::atomic<bool> g_event_busy{false};
static bool g_event_previous_checked = false;

void orbis_event_log_init(const std::string& path)
{
	std::snprintf(g_event_log_path, sizeof(g_event_log_path), "%s", path.c_str());
	g_event_log_dir_logs[0] = g_event_log_in_logs[0] = 0;
	const size_t slash = path.rfind('/');
	if (slash != std::string::npos && slash > 0)
	{
		const std::string dir = path.substr(0, slash);
		const size_t dslash = dir.rfind('/');
		const std::string last = dslash == std::string::npos ? dir : dir.substr(dslash + 1);
		if (last != "logs")
		{
			std::snprintf(g_event_log_dir_logs, sizeof(g_event_log_dir_logs), "%s/logs", dir.c_str());
			std::snprintf(g_event_log_in_logs, sizeof(g_event_log_in_logs), "%s/logs/%s", dir.c_str(), path.c_str() + slash + 1);
		}
	}
	fe::g_utc_to_local = &SettingsLogLocalTime;
}

// Whether a log line's text (after the time) says the run that wrote it is over. The lines the app itself writes
// as it ends: main-boot.cpp's exits, the crash printer, the GPU-hang exit and the new signal and exit hooks.
static bool EventEndsARun(const char* text)
{
	static const char* const kEnds[] = {"back to the menu", "the app closed", "GPU hang", "crash:", "no game to start",
		"the game didn't start", "the system ended the app", "app exit"};
	for (const char* e : kEnds)
		if (std::strstr(text, e))
			return true;
	return false;
}

// The note about the previous run, or 0 when it closed itself properly (or there is no earlier log).
static int EventPreviousRunNote(int fd, char* out, size_t out_size)
{
	struct stat st = {};
	if (fstat(fd, &st) != 0 || st.st_size <= 0)
		return 0;
	char tail[2048];
	const off_t from = st.st_size > static_cast<off_t>(sizeof(tail)) ? st.st_size - static_cast<off_t>(sizeof(tail)) : 0;
	const ssize_t got = pread(fd, tail, sizeof(tail) - 1, from);
	if (got <= 0)
		return 0;
	tail[got] = 0;
	// the last line: after the last newline that isn't the final character
	ssize_t end = got;
	while (end > 0 && (tail[end - 1] == '\n' || tail[end - 1] == '\r'))
		end--;
	if (end <= 0)
		return 0;
	ssize_t begin = end;
	while (begin > 0 && tail[begin - 1] != '\n')
		begin--;
	tail[end] = 0;
	const char* last = tail + begin;
	// "YYYY-MM-DD HH:MM:SS  text"
	const bool stamped = end - begin > 21 && last[4] == '-' && last[7] == '-' && last[10] == ' ' && last[13] == ':' && last[16] == ':';
	if (stamped && EventEndsARun(last + 21))
		return 0;
	if (!stamped)
		return 0; // not a line of ours
	return std::snprintf(out, out_size,
		"previous run: its log ends without a closing line (last entry %.19s: \"%.50s\"): the app was closed from the PS button or "
		"by the system, the console lost power, or the app crashed without a signal",
		last, last + 21);
}

extern "C" void orbis_event_log(const char* line)
{
	if (!g_event_log_path[0] || !line)
		return;
	const time_t local = static_cast<time_t>(SettingsLogLocalTime(static_cast<long long>(time(nullptr))));
	struct tm tm = {};
	gmtime_r(&local, &tm);
	char buf[1280];
	int n = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d  %s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, line);
	if (n <= 0)
		return;
	if (n >= static_cast<int>(sizeof(buf)))
	{
		n = static_cast<int>(sizeof(buf)) - 1;
		buf[n - 1] = '\n';
	}
	// logs/ once it is there, else the path as given.
	const char* path = g_event_log_path;
	if (g_event_log_dir_logs[0])
	{
		struct stat st = {};
		if (stat(g_event_log_dir_logs, &st) == 0 && S_ISDIR(st.st_mode))
			path = g_event_log_in_logs;
	}
	// One thread at a time in the buffer; a caller that finds it taken (another thread, or a signal that interrupted
	// it) writes its line by itself and leaves the buffer alone.
	const bool own = !g_event_busy.exchange(true, std::memory_order_acquire);
	const int fd = open(path, O_RDWR | O_APPEND | O_CREAT, 0666);
	if (fd < 0)
	{
		if (own)
		{
			if (g_event_pending_len + static_cast<size_t>(n) <= sizeof(g_event_pending))
			{
				std::memcpy(g_event_pending + g_event_pending_len, buf, static_cast<size_t>(n));
				g_event_pending_len += static_cast<size_t>(n);
			}
			g_event_busy.store(false, std::memory_order_release);
		}
		return;
	}
	if (own && !g_event_previous_checked)
	{
		g_event_previous_checked = true;
		char note[400];
		const int m = EventPreviousRunNote(fd, note + 0, sizeof(note) - 1);
		if (m > 0)
		{
			char stamped[480];
			const int k = std::snprintf(stamped, sizeof(stamped), "%04d-%02d-%02d %02d:%02d:%02d  %s\n", tm.tm_year + 1900, tm.tm_mon + 1,
				tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, note);
			if (k > 0)
				(void)!write(fd, stamped, static_cast<size_t>(k < static_cast<int>(sizeof(stamped)) ? k : sizeof(stamped) - 1));
		}
	}
	if (own && g_event_pending_len)
	{
		(void)!write(fd, g_event_pending, g_event_pending_len);
		g_event_pending_len = 0;
	}
	(void)!write(fd, buf, static_cast<size_t>(n));
	close(fd);
	if (own)
		g_event_busy.store(false, std::memory_order_release);
}

bool orbis_web_start(const OrbisFrontendPaths& paths, const char* build_tag)
{
	if (g_web)
		return true;
	SetSerialCacheFile(paths.serial_cache); // vk-285-108
	SetGameDbFile(paths.gamedb_file);       // vk-285-113
	WebConfig cfg;
	cfg.game_dirs = {paths.games_dir, paths.top_dir};
	cfg.game_dirs.insert(cfg.game_dirs.end(), paths.usb_dirs.begin(), paths.usb_dirs.end()); // test build 1
	cfg.settings_dir = paths.settings_dir;
	cfg.gs_ini = paths.gs_ini;
	cfg.patches_dir = paths.patches_dir;
	cfg.cheats_dir = paths.online_patches ? paths.cheats_dir : std::string(); // 2026-10-08
	cfg.covers_dir = paths.covers_dir;
	cfg.cache_dir = paths.cache_dir;
	cfg.build_tag = build_tag ? build_tag : "";
#ifdef ORBIS_PS4
	cfg.probe_frames = ps4_probe_frames;
	cfg.probe_shelf_frames = [] { return g_probe_shelf_frames.load(std::memory_order_relaxed); };
	cfg.probe_running = [] { return VMManager::GetState() == VMState::Running; };
#endif
	cfg.port = 8844;
	cfg.presets.assign(reinterpret_cast<const char*>(fe_presets), static_cast<size_t>(fe_presets_end - fe_presets));
	cfg.change_log = paths.settings_log;
	// Test build 1 (vk-285-55): the logs download.
	cfg.logs_dir = paths.logs_dir;
	cfg.top_dir = paths.top_dir;
	cfg.memcards_dir = paths.memcards_dir; // vk-285-113
	cfg.report_header = paths.report_header;
	cfg.test_build = paths.test_build;
	cfg.current_disc = orbis_current_disc; // vk-285-139 (main-boot.cpp)
	cfg.disc_set = orbis_disc_set;
	cfg.change_disc = orbis_request_disc;
#ifdef PS5SX2_ACHIEVEMENTS
	cfg.achievements = Achievements::GetPS5GameAchievements;
	cfg.achievement_badge = Achievements::GetPS5AchievementBadge;
#endif
	fe::g_utc_to_local = &SettingsLogLocalTime;
	cfg.assets = {
		{"/", "text/html; charset=utf-8", fe_web_page, static_cast<size_t>(fe_web_page_end - fe_web_page)},
		{"/fonts/roboto.ttf", "font/ttf", fe_font_text, static_cast<size_t>(fe_font_text_end - fe_font_text)},
		{"/caliza.css", "text/css; charset=utf-8", fe_web_css, static_cast<size_t>(fe_web_css_end - fe_web_css)},
		{"/app.js", "text/javascript; charset=utf-8", fe_web_app, static_cast<size_t>(fe_web_app_end - fe_web_app)},
		{"/assets/caliza-mark.svg", "image/svg+xml", fe_web_mark, static_cast<size_t>(fe_web_mark_end - fe_web_mark)},
		{"/assets/caliza-mark-mono.svg", "image/svg+xml", fe_web_mark_mono, static_cast<size_t>(fe_web_mark_mono_end - fe_web_mark_mono)},
		{"/assets/strata-ornament.svg", "image/svg+xml", fe_web_strata, static_cast<size_t>(fe_web_strata_end - fe_web_strata)},
		{"/assets/fonts/source-sans-3-latin.woff2", "font/woff2", fe_web_source_sans,
			static_cast<size_t>(fe_web_source_sans_end - fe_web_source_sans)},
		{"/assets/fonts/source-sans-3-latin-ext.woff2", "font/woff2", fe_web_source_sans_ext,
			static_cast<size_t>(fe_web_source_sans_ext_end - fe_web_source_sans_ext)},
	};
	fe::WebServer* server = new fe::WebServer();
	if (!server->Start(cfg))
	{
		std::printf("[web] the settings page could not start\n");
		std::fflush(stdout);
		delete server;
		return false;
	}
	g_web = server;
	std::string url, shown;
	std::printf("[web] %s\n", g_web->Address(url, shown) ? ("http://" + shown).c_str() : "no network address yet");
	std::fflush(stdout);
	return true;
}

void orbis_web_now_playing(const std::string& image_path)
{
	if (g_web)
		g_web->SetNowPlaying(image_path);
}

// vk-285-113: the page's address as a QR code (the one the shelf shows) for the panel drawn over the game.
bool orbis_web_qr(std::vector<unsigned char>& modules, int& size, std::string& shown)
{
	if (!g_web)
		return false;
	std::string url;
	if (!g_web->Address(url, shown))
		return false;
	uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
	if (!qrcodegen_encodeText(url.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
		return false;
	size = qrcodegen_getSize(qr);
	modules.assign(static_cast<size_t>(size) * size, 0);
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++)
			modules[static_cast<size_t>(y) * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
	return true;
}

bool orbis_web_browser_url(std::string& url, std::string& shown)
{
	if (!g_web)
		return false;
	if (g_web->Address(url, shown))
		return true;
	if (g_web->Port() == 0)
		return false;
	url = g_web->LoopbackUrl();
	shown = "127.0.0.1:" + std::to_string(g_web->Port());
	return true;
}

void orbis_web_request_stats(uint64_t& count, double& age_s)
{
	count = 0;
	age_s = -1.0;
	if (g_web)
		g_web->RequestStats(count, age_s);
}

void orbis_web_log_next_requests(int n)
{
	if (g_web)
		g_web->LogNextRequests(n);
}

namespace
{
fe::TexturePackManager* g_texture_packs = nullptr;
fe::OnlinePatches* g_online_patches = nullptr;
bool g_texture_pack_worker_detached = false;
bool g_online_patch_worker_detached = false;
} // namespace

bool orbis_frontend_services_start(const OrbisFrontendPaths& paths, fe::TexturePackService* texture_packs,
	fe::OnlinePatchService* online_patches)
{
	if (texture_packs)
		*texture_packs = fe::TexturePackService();
	if (online_patches)
		*online_patches = fe::OnlinePatchService();
	// HD texture packs from archive.org. The worker belongs to the menu process boundary rather
	// than one renderer object; if a slow stop ever has to be abandoned, the next menu reuses it.
	if (paths.texture_packs && !paths.textures_dir.empty() && texture_packs)
	{
		if (!g_texture_packs)
		{
			fe::TexturePackPlatform tp;
			tp.get_text = [](const std::string& url, std::string& body) {
				body.clear();
				return g_texhttp.Get(url, false, 0, 0, [&body](const void* data, size_t size) {
					if (body.size() + size > (8u << 20))
						return false;
					body.append(static_cast<const char*>(data), size);
					return true;
				});
			};
			tp.get_range = [](const std::string& url, uint64_t offset, uint64_t length,
							   const std::function<bool(const void*, size_t)>& sink) { return g_texhttp.Get(url, true, offset, length, sink); };
			tp.abort = [] { g_texhttp.Abort(); };
			tp.free_bytes = [](const std::string& dir) { return TexturePackFreeBytes(dir); };
			tp.thread_start = [](const char* role) { TexturePackThreadStart(role); };
			tp.existing_pack = [](const std::string& serial) {
				std::string how;
				return OrbisTexturesGameDir(serial, how);
			};
			tp.log = [](const std::string& line) {
				std::printf("%s\n", line.c_str());
				std::fflush(stdout);
			};
			tp.notify = [](const std::string& game, bool ok, const std::string& detail) {
				const std::string sub = detail.empty() ? game : game + " \xC2\xB7 " + detail;
				OrbisNotifyRich(fe::Tr(ok ? fe::Str::HdTexturesReady : fe::Str::HdTexturesFailed), sub.c_str(), "");
			};
			tp.now = [] { return Now(); };
			const std::string textures = paths.textures_dir;
			g_texture_packs = new fe::TexturePackManager(tp, textures, textures + "/.caliza-downloads", paths.texture_pack_list);
			g_texture_packs->Start();
			g_texture_pack_worker_detached = false;
		}
		*texture_packs = g_texture_packs->Service();
	}
	if (paths.online_patches && !paths.patches_dir.empty() && !paths.cheats_dir.empty() && online_patches)
	{
		if (!g_online_patches)
		{
			fe::OnlinePatchPlatform op;
			op.get_text = [](const std::string& url, std::string& body) {
				body.clear();
				return g_patchhttp.Get(url, false, 0, 0, [&body](const void* data, size_t size) {
					if (body.size() + size > (2u << 20))
						return false;
					body.append(static_cast<const char*>(data), size);
					return true;
				});
			};
			op.log = [](const std::string& line) {
				std::printf("%s\n", line.c_str());
				std::fflush(stdout);
			};
			op.thread_start = [] { TexturePackThreadStart("online patches"); };
			g_online_patches = new fe::OnlinePatches(op, paths.patches_dir, paths.cheats_dir, paths.online_patch_manifest);
			g_online_patch_worker_detached = false;
		}
		*online_patches = g_online_patches->Service();
	}
	return (texture_packs && static_cast<bool>(*texture_packs)) || (online_patches && static_cast<bool>(*online_patches));
}

void orbis_frontend_services_stop()
{
	if (g_texture_packs && !g_texture_pack_worker_detached)
	{
		if (g_texture_packs->Stop(1500))
		{
			delete g_texture_packs;
			g_texture_packs = nullptr;
			g_texhttp.Term();
		}
		else
		{
			g_texture_pack_worker_detached = true;
			std::printf("[frontend] a texture pack job is still stopping; leaving it to end on its own\n");
		}
	}
	if (g_online_patches && !g_online_patch_worker_detached)
	{
		g_patchhttp.Abort();
		if (g_online_patches->Stop(1500))
		{
			delete g_online_patches;
			g_online_patches = nullptr;
			g_patchhttp.Term();
		}
		else
		{
			g_online_patch_worker_detached = true;
			std::printf("[frontend] an online patch request is still stopping; leaving it to end on its own\n");
		}
	}
}

int orbis_frontend_prefetch_covers(const OrbisFrontendPaths& paths, double budget_s, void (*notify)(const char*))
{
	if (!paths.allow_download)
		return 0;
	const double t0 = Now();
	SetSerialCacheFile(paths.serial_cache); // vk-285-108
	SetGameDbFile(paths.gamedb_file);       // vk-285-113
	std::vector<std::string> dirs = {paths.games_dir, paths.top_dir};
	dirs.insert(dirs.end(), paths.usb_dirs.begin(), paths.usb_dirs.end()); // test build 1: USB drives, when visible here
	std::vector<GameInfo> games = ScanGames(dirs);
	for (GameInfo& g : games)
		g.serial = ReadSerial(g.path);
	// Test build 1: the USB games seen after the jailbreak last time (the drives may not be visible here).
	const int listed = AddUsbListGames(paths.usb_list, games);
	CoverConfig cc;
	cc.manual_dir = paths.covers_dir;
	cc.cache_dir = paths.cache_dir;
	cc.url_template = kCoverUrl;
	const std::vector<int> missing = CoverService::MissingCovers(games, cc);
	std::printf("[frontend] prefetch: %zu of %zu covers to fetch (%d from the USB list; checked in %.0f ms)\n", missing.size(),
		games.size(), listed, (Now() - t0) * 1000.0);
	std::fflush(stdout);
	if (missing.empty())
		return 0;
	if (notify)
	{
		char buf[96];
		std::snprintf(buf, sizeof(buf), fe::Tr(missing.size() == 1 ? fe::Str::NotifyCoversOne : fe::Str::NotifyCoversMany),
			static_cast<int>(missing.size())); // vk-285-110
		notify(buf);
	}
	const int saved = CoverService::Prefetch(games, missing, cc,
		[](const std::string& url, std::vector<uint8_t>& out) { return g_coverhttp.Get(url, out); }, budget_s);
	g_coverhttp.Term();
	std::printf("[frontend] prefetch: %d saved in %.1f s\n", saved, Now() - t0);
	std::fflush(stdout);
	return saved;
}

std::string orbis_frontend_run(const OrbisFrontendPaths& paths, const char* build_tag, bool* ran)
{
	// Caliza's menu is a validated boot-selection shell. main-boot remains the owner of PCSX2 creation,
	// BIOS execution, save-state policy, disc transport and the re-exec return path.
	const caliza::ps4shell::ShellResult shell = caliza::ps4shell::RunEstratos(paths, build_tag);
	if (ran)
		*ran = shell.ui_ran && shell.kind != caliza::ps4shell::ShellResult::Kind::Failed;
	if (shell.kind == caliza::ps4shell::ShellResult::Kind::Game)
	{
		std::printf("[estratos] boot selection: %s\n", shell.path.c_str());
		std::fflush(stdout);
		return shell.path;
	}
	if (shell.kind == caliza::ps4shell::ShellResult::Kind::SystemMenu)
	{
		std::printf("[estratos] boot selection: PS2 system menu\n");
		std::fflush(stdout);
		return kOrbisSystemMenuPath;
	}
	if (!shell.error.empty())
		std::printf("[estratos] shell ended without a boot selection: %s\n", shell.error.c_str());
	else
		std::printf("[estratos] shell ended without a boot selection\n");
	std::fflush(stdout);
	return {};
}
