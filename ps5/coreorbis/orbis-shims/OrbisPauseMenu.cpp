// Caliza in-game pause menu: platform glue (see OrbisPauseMenu.h). The menu's behaviour is the
// host-tested model in caliza/core/pause_menu.cpp; this file is the thread plumbing plus the
// panel's CPU rasterisation, in the Estratos palette, with the same Roboto/PromptFont data the
// frontend embeds. needs proper testing on the console: the Create button's delivery, save/load
// while paused, and the blend pipeline over the paused re-presents.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OrbisPauseMenu.h"

#include "caliza/core/pause_menu.h"

#include "MTGS.h"
#include "VMManager.h"
#include "common/Error.h"
#include "common/FileSystem.h"

#ifdef ORBIS_VULKAN
#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSTexture.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

// main-boot.cpp: the existing back-to-menu flow (stops the VM; main() flushes cards/NVRAM and
// re-executes into the frontend).
void OrbisBackToMenuCpu();

namespace
{
// -------------------------------------------------------------------------------------------------
// Shared state. s_vm is written by the CPU thread and read by the pad thread; s_view is written by
// the pad thread and read by the GS thread. Both are small and mutex-protected; the readers copy.
// -------------------------------------------------------------------------------------------------
std::mutex s_state_mtx;
caliza::VmStatus s_vm;
caliza::PauseMenuView s_view;
std::atomic<bool> s_view_visible{false};
std::atomic<uint64_t> s_view_revision{0};

struct QueuedCommand
{
	caliza::Command command;
	caliza::CommandId id = 0;
};
std::mutex s_cmd_mtx;
std::deque<QueuedCommand> s_commands;
std::mutex s_evt_mtx;
std::deque<caliza::CommandEvent> s_events;
std::atomic<caliza::CommandId> s_next_id{1};

class RuntimeSink : public caliza::PauseMenuBackend
{
public:
	caliza::CommandId Post(const caliza::Command& command) override
	{
		const caliza::CommandId id = s_next_id.fetch_add(1, std::memory_order_relaxed);
		{
			std::lock_guard<std::mutex> lock(s_cmd_mtx);
			s_commands.push_back({command, id});
		}
		return id;
	}
};

RuntimeSink s_sink;
caliza::PauseMenu s_menu(&s_sink);

void PushEvent(caliza::CommandId id, caliza::CommandType type, caliza::CommandStatus status, std::string message)
{
	caliza::CommandEvent event;
	event.command = id;
	event.type = type;
	event.status = status;
	event.message = std::move(message);
	std::lock_guard<std::mutex> lock(s_evt_mtx);
	s_events.push_back(std::move(event));
}

caliza::VmState MapVmState(VMState state)
{
	switch (state)
	{
		case VMState::Shutdown: return caliza::VmState::Idle;
		case VMState::Initializing: return caliza::VmState::Starting;
		case VMState::Running: return caliza::VmState::Running;
		case VMState::Paused: return caliza::VmState::Paused;
		case VMState::Resetting: return caliza::VmState::Running;
		case VMState::Stopping: return caliza::VmState::Stopping;
	}
	return caliza::VmState::Unavailable;
}

void ExecuteCommand(const QueuedCommand& qc)
{
	switch (qc.command.type)
	{
		case caliza::CommandType::PauseVm:
		{
			if (!VMManager::HasValidVM())
			{
				PushEvent(qc.id, qc.command.type, caliza::CommandStatus::Failed, "No game is running");
				return;
			}
			VMManager::SetPaused(true);
			std::printf("[pause] vm paused\n");
			std::fflush(stdout);
			PushEvent(qc.id, qc.command.type, caliza::CommandStatus::Succeeded, "Paused");
			return;
		}
		case caliza::CommandType::ResumeVm:
		{
			if (!VMManager::HasValidVM())
			{
				PushEvent(qc.id, qc.command.type, caliza::CommandStatus::Failed, "No game is running");
				return;
			}
			VMManager::SetPaused(false);
			std::printf("[pause] vm resumed\n");
			std::fflush(stdout);
			PushEvent(qc.id, qc.command.type, caliza::CommandStatus::Succeeded, "Resumed");
			return;
		}
		case caliza::CommandType::SaveState:
		{
			// The hotkey path's own call (StubHost.cpp): same slot policy, same synchronous write.
			bool failed = false;
			std::string error;
			VMManager::SaveStateToSlot(qc.command.number, false, [&](const std::string& err) {
				failed = true;
				error = err;
			});
			std::printf("[pause] save slot %d: %s\n", qc.command.number, failed ? error.c_str() : "saved");
			std::fflush(stdout);
			PushEvent(qc.id, qc.command.type, failed ? caliza::CommandStatus::Failed : caliza::CommandStatus::Succeeded,
				failed ? (error.empty() ? "Save failed" : error) : "State saved");
			return;
		}
		case caliza::CommandType::LoadState:
		{
			Error err;
			if (VMManager::LoadStateFromSlot(qc.command.number, false, &err))
			{
				PushEvent(qc.id, qc.command.type, caliza::CommandStatus::Succeeded, "State loaded");
				return;
			}
			// A power loss between the writer's two renames can leave only .backup, which is the
			// surviving consistent state (as the hotkey path handles it).
			Error backup_err;
			if (VMManager::LoadStateFromSlot(qc.command.number, true, &backup_err))
			{
				std::printf("[pause] slot %d unusable (%s); loaded its backup\n", qc.command.number,
					err.GetDescription().c_str());
				std::fflush(stdout);
				PushEvent(qc.id, qc.command.type, caliza::CommandStatus::Succeeded, "State loaded");
				return;
			}
			std::printf("[pause] load failed: %s (backup: %s)\n", err.GetDescription().c_str(),
				backup_err.GetDescription().c_str());
			std::fflush(stdout);
			PushEvent(qc.id, qc.command.type, caliza::CommandStatus::Failed, "Load failed");
			return;
		}
		case caliza::CommandType::ReturnToLibrary:
		{
			PushEvent(qc.id, qc.command.type, caliza::CommandStatus::Succeeded, "Returning");
			OrbisBackToMenuCpu();
			return;
		}
		default:
			PushEvent(qc.id, qc.command.type, caliza::CommandStatus::Failed, "Unsupported");
			return;
	}
}
} // namespace

void OrbisPauseMenuPumpCpu()
{
	// The pause menu needs the GS thread to keep re-presenting while the VM is paused. Upstream
	// enables the same mode for its fullscreen UI (FullscreenUI.cpp MTGS::SetRunIdle); here nothing
	// else ever does. The flag is a plain atomic and the re-presents only happen outside Running,
	// so gameplay's present path is untouched.
	static bool s_idle_enabled = false;
	if (!s_idle_enabled && MTGS::IsOpen())
	{
		s_idle_enabled = true;
		MTGS::SetRunIdle(true);
		std::printf("[pause] idle re-presents enabled for the paused overlay\n");
		std::fflush(stdout);
	}

	for (;;)
	{
		QueuedCommand qc;
		{
			std::lock_guard<std::mutex> lock(s_cmd_mtx);
			if (s_commands.empty())
				break;
			qc = s_commands.front();
			s_commands.pop_front();
		}
		ExecuteCommand(qc);
	}

	// Publish the VM status. The title is cached by serial: GetTitle walks the game list.
	caliza::VmStatus vm;
	vm.state = MapVmState(VMManager::GetState());
	const bool valid = VMManager::HasValidVM();
	vm.can_save = valid && (vm.state == caliza::VmState::Running || vm.state == caliza::VmState::Paused);
	static std::string s_serial;
	static u32 s_crc = 0;
	static std::string s_title;
	const std::string serial = valid ? VMManager::GetDiscSerial() : std::string();
	const u32 crc = valid ? VMManager::GetDiscCRC() : 0;
	if (serial != s_serial || crc != s_crc)
	{
		s_serial = serial;
		s_crc = crc;
		s_title = valid ? VMManager::GetTitle(true) : std::string();
		if (s_title.empty())
			s_title = serial;
	}
	vm.title = s_title;

	// The slot's presence is a file check: refresh it throttled, and at once when the overlay is
	// open so a state saved in this session appears without delay.
	static int s_load_refresh_countdown = 0;
	static bool s_can_load = false;
	if (!valid)
		s_can_load = false;
	else if (--s_load_refresh_countdown <= 0 || s_view_visible.load(std::memory_order_acquire))
	{
		// The backup of slot 1 is loadable too (the load path falls back to it), so it counts.
		const std::string backup = VMManager::GetSaveStateFileName(serial.c_str(), crc, 1, true);
		s_can_load = VMManager::HasSaveStateInSlot(serial.c_str(), crc, 1) ||
			(!backup.empty() && FileSystem::FileExists(backup.c_str()));
		s_load_refresh_countdown = 60;
	}
	vm.can_load = valid && s_can_load;

	{
		std::lock_guard<std::mutex> lock(s_state_mtx);
		s_vm = vm;
	}
}

// -------------------------------------------------------------------------------------------------
// Pad thread.
// -------------------------------------------------------------------------------------------------
namespace
{
// ScePad bits as the rest of the port reads them (orbis-shims/OrbisPadMap.h).
constexpr uint32_t kBitCreate = 0x00000001u;
constexpr uint32_t kBitCircle = 0x00002000u;
constexpr uint32_t kBitCross = 0x00004000u;
constexpr uint32_t kBitUp = 0x00000010u;
constexpr uint32_t kBitRight = 0x00000020u;
constexpr uint32_t kBitDown = 0x00000040u;
constexpr uint32_t kBitLeft = 0x00000080u;
constexpr uint8_t kStickLo = 80;
constexpr uint8_t kStickHi = 176;
} // namespace

bool OrbisPauseMenuPadTick(const OrbisPausePadState& pad, double dt)
{
	caliza::VmStatus vm;
	{
		std::lock_guard<std::mutex> lock(s_state_mtx);
		vm = s_vm;
	}

	std::vector<caliza::CommandEvent> events;
	{
		std::lock_guard<std::mutex> lock(s_evt_mtx);
		while (!s_events.empty())
		{
			events.push_back(s_events.front());
			s_events.pop_front();
		}
	}

	caliza::Input in;
	in.up = (pad.buttons & kBitUp) != 0 || pad.ly < kStickLo;
	in.down = (pad.buttons & kBitDown) != 0 || pad.ly > kStickHi;
	in.left = (pad.buttons & kBitLeft) != 0 || pad.lx < kStickLo;
	in.right = (pad.buttons & kBitRight) != 0 || pad.lx > kStickHi;
	in.cross = (pad.buttons & kBitCross) != 0;
	in.circle = (pad.buttons & kBitCircle) != 0;
	in.options = (pad.buttons & kBitCreate) != 0;

	static bool s_prev_create = false;
	const bool create = (pad.buttons & kBitCreate) != 0;
	const bool create_edge = create && !s_prev_create;
	s_prev_create = create;

	if (!s_menu.IsOpen() && create_edge &&
		(vm.state == caliza::VmState::Running || vm.state == caliza::VmState::Paused))
	{
		std::printf("[pause] Create button: opening the pause menu\n");
		std::fflush(stdout);
		s_menu.Open(vm.title);
	}

	const bool was_open = s_menu.IsOpen();
	s_menu.Tick(vm, events, in, dt);
	const bool open = s_menu.IsOpen();

	// Publish the view when anything visible changed.
	const caliza::PauseMenuView view = s_menu.BuildView();
	static uint64_t s_published_revision = 0;
	static bool s_published_open = false;
	if (view.revision != s_published_revision || view.open != s_published_open)
	{
		s_published_revision = view.revision;
		s_published_open = view.open;
		{
			std::lock_guard<std::mutex> lock(s_state_mtx);
			s_view = view;
		}
		s_view_revision.store(view.revision, std::memory_order_release);
		s_view_visible.store(view.open, std::memory_order_release);
	}

	// Input capture: while the menu is open everything is the menu's. After it closes, the buttons
	// still held (the cross/circle/Create press that closed it) must not reach the game either;
	// capture ends when every button is up, so the game only ever sees a clean release.
	static bool s_close_capture = false;
	if (!open && was_open)
		s_close_capture = true;
	if (s_close_capture && pad.buttons == 0)
		s_close_capture = false;
	return open || s_close_capture;
}

void OrbisPauseMenuReset()
{
	s_menu.Close(false);
	std::lock_guard<std::mutex> lock(s_state_mtx);
	s_view = caliza::PauseMenuView();
	s_view_visible.store(false, std::memory_order_release);
}

// -------------------------------------------------------------------------------------------------
// GS thread: the panel's pixels and the draw. Composition happens only when the published view's
// revision changes (a navigation edge, a toast, the dialog), never per frame; the upload runs
// outside the swapchain's render pass (OrbisPauseMenuPrepare, as OrbisPreparePanels does for the
// other panels) and only the alpha-blended draws happen inside it.
// -------------------------------------------------------------------------------------------------
#ifdef ORBIS_VULKAN

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "ps5/frontend/third_party/stb_truetype.h"

// The frontend's embedded fonts (fe_ps5.cpp): PCSX2's Roboto (Apache-2.0) and PromptFont (OFL-1.1).
extern "C" const uint8_t fe_font_text[], fe_font_text_end[], fe_font_icons[], fe_font_icons_end[];

// GSDeviceVK.cpp: alpha-blends a texture over the frame inside the open present pass.
void OrbisVkPresentBlend(GSTexture* tex, const GSVector4& sRect, const GSVector4& dRect);

namespace
{
// The Estratos palette (caliza/native/estratos_theme.h), as 0xAABBGGRR straight-alpha pixels.
constexpr uint32_t kVoid = 0xF40E0403u;     // 03040E, a few points under opaque so the frame reads
constexpr uint32_t kVoidTop = 0xF4260807u;  // 070826
constexpr uint32_t kText = 0xFFFFF5F2u;     // F2F5FF
constexpr uint32_t kTextDim = 0xFFDAACA3u;  // A3ACDA
constexpr uint32_t kTextFaint = 0xFFAA766Du;// 6D76AA
constexpr uint32_t kIce = 0xFFFFE7A5u;    // A5E7FF
constexpr uint32_t kFocus = 0xFFFF956Fu;    // 6F95FF
constexpr uint32_t kAmber = 0xFF92D0FFu;    // FFD092
constexpr uint32_t kDanger = 0xFF9E8FFFu;   // FF8F9E
constexpr uint32_t kGlass = 0xFFFFB0A9u;    // A9B0FF
constexpr uint32_t kLine = 0x4DFF9B8Cu;     // 8C9BFF at ~0.30

uint32_t WithAlpha(uint32_t color, float alpha)
{
	const uint32_t a = static_cast<uint32_t>(std::lround(alpha * 255.0f)) & 0xffu;
	return (color & 0x00FFFFFFu) | (a << 24);
}

uint32_t RgbA(uint32_t rgb, float alpha)
{
	const uint32_t r = (rgb >> 16) & 0xffu, g = (rgb >> 8) & 0xffu, b = rgb & 0xffu;
	const uint32_t a = static_cast<uint32_t>(std::lround(alpha * 255.0f)) & 0xffu;
	return (a << 24) | (b << 16) | (g << 8) | r;
}

struct PanelFont
{
	stbtt_fontinfo info;
	bool ok = false;
};

PanelFont s_text_font, s_icon_font;

void EnsureFonts()
{
	static bool s_tried = false;
	if (s_tried)
		return;
	s_tried = true;
	s_text_font.ok = stbtt_InitFont(&s_text_font.info, fe_font_text,
						 stbtt_GetFontOffsetForIndex(fe_font_text, 0)) != 0;
	s_icon_font.ok = stbtt_InitFont(&s_icon_font.info, fe_font_icons,
						 stbtt_GetFontOffsetForIndex(fe_font_icons, 0)) != 0;
	if (!s_text_font.ok)
		std::printf("[pause] text font unreadable: the menu will not draw\n");
}

uint32_t NextCp(const char*& s)
{
	const unsigned char c = static_cast<unsigned char>(*s);
	if (c < 0x80)
	{
		s++;
		return c;
	}
	int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
	if (extra < 0)
	{
		s++;
		return 0xFFFD;
	}
	uint32_t cp = c & (0x3F >> extra);
	s++;
	for (int i = 0; i < extra; i++)
	{
		const unsigned char cc = static_cast<unsigned char>(*s);
		if ((cc & 0xC0) != 0x80)
			return 0xFFFD;
		cp = (cp << 6) | (cc & 0x3F);
		s++;
	}
	return cp;
}

struct Bitmap
{
	std::vector<uint32_t> px;
	int w = 0, h = 0;

	void Resize(int width, int height)
	{
		w = width;
		h = height;
		px.assign(static_cast<size_t>(w) * h, 0u);
	}
};

// Straight-alpha "over" of src onto dst, per channel.
void BlendPixel(Bitmap& bmp, int x, int y, uint32_t rgb, float alpha)
{
	if (x < 0 || y < 0 || x >= bmp.w || y >= bmp.h || alpha <= 0.0f)
		return;
	uint32_t& dst = bmp.px[static_cast<size_t>(y) * bmp.w + x];
	const float da = ((dst >> 24) & 0xffu) / 255.0f;
	const float sa = std::min(1.0f, alpha);
	const float out_a = sa + da * (1.0f - sa);
	if (out_a <= 0.0f)
		return;
	const float sr = (rgb & 0xffu) / 255.0f, sg = ((rgb >> 8) & 0xffu) / 255.0f, sb = ((rgb >> 16) & 0xffu) / 255.0f;
	const float dr = (dst & 0xffu) / 255.0f, dg = ((dst >> 8) & 0xffu) / 255.0f, db = ((dst >> 16) & 0xffu) / 255.0f;
	const float orr = (sr * sa + dr * da * (1.0f - sa)) / out_a;
	const float org = (sg * sa + dg * da * (1.0f - sa)) / out_a;
	const float orb = (sb * sa + db * da * (1.0f - sa)) / out_a;
	const uint32_t a8 = static_cast<uint32_t>(std::lround(out_a * 255.0f));
	const uint32_t r8 = static_cast<uint32_t>(std::lround(std::min(1.0f, orr) * 255.0f));
	const uint32_t g8 = static_cast<uint32_t>(std::lround(std::min(1.0f, org) * 255.0f));
	const uint32_t b8 = static_cast<uint32_t>(std::lround(std::min(1.0f, orb) * 255.0f));
	dst = (a8 << 24) | (b8 << 16) | (g8 << 8) | r8;
}

// Rasterises and blends `utf8` at (x, baseline); returns the advance width. `letterspace` is extra
// pixels between glyphs. Only glyphs the font has are drawn.
float DrawText(Bitmap& bmp, PanelFont& font, const char* utf8, float x, float baseline, float px, uint32_t color,
	float letterspace = 0.0f)
{
	if (!font.ok || !utf8 || !*utf8)
		return 0.0f;
	const float scale = stbtt_ScaleForMappingEmToPixels(&font.info, px);
	const float alpha = ((color >> 24) & 0xffu) / 255.0f;
	const uint32_t rgb = color & 0x00FFFFFFu;
	float pen = x;
	uint32_t prev = 0;
	const char* p = utf8;
	while (*p)
	{
		const uint32_t cp = NextCp(p);
		if (prev)
			pen += stbtt_GetCodepointKernAdvance(&font.info, static_cast<int>(prev), static_cast<int>(cp)) * scale;
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		const float sub = pen - std::floor(pen);
		stbtt_GetCodepointBitmapBoxSubpixel(&font.info, static_cast<int>(cp), scale, scale, sub, 0, &x0, &y0, &x1, &y1);
		const int gw = x1 - x0, gh = y1 - y0;
		if (gw > 0 && gh > 0)
		{
			std::vector<uint8_t> tmp(static_cast<size_t>(gw) * gh);
			stbtt_MakeCodepointBitmapSubpixel(&font.info, tmp.data(), gw, gh, gw, scale, scale, sub, 0, static_cast<int>(cp));
			const int ox = static_cast<int>(std::floor(pen)) + x0;
			const int oy = static_cast<int>(std::lround(baseline)) + y0;
			for (int yy = 0; yy < gh; yy++)
				for (int xx = 0; xx < gw; xx++)
				{
					const float c = tmp[static_cast<size_t>(yy) * gw + xx] / 255.0f * alpha;
					if (c > 0.0f)
						BlendPixel(bmp, ox + xx, oy + yy, rgb, c);
				}
		}
		int advance = 0, lsb = 0;
		stbtt_GetCodepointHMetrics(&font.info, static_cast<int>(cp), &advance, &lsb);
		pen += advance * scale + letterspace;
		prev = cp;
	}
	return pen - x;
}

float MeasureText(PanelFont& font, const char* utf8, float px, float letterspace = 0.0f)
{
	if (!font.ok || !utf8)
		return 0.0f;
	const float scale = stbtt_ScaleForMappingEmToPixels(&font.info, px);
	float pen = 0.0f;
	uint32_t prev = 0;
	const char* p = utf8;
	while (*p)
	{
		const uint32_t cp = NextCp(p);
		if (prev)
			pen += stbtt_GetCodepointKernAdvance(&font.info, static_cast<int>(prev), static_cast<int>(cp)) * scale;
		int advance = 0, lsb = 0;
		stbtt_GetCodepointHMetrics(&font.info, static_cast<int>(cp), &advance, &lsb);
		pen += advance * scale + letterspace;
		prev = cp;
	}
	return pen;
}

// `text` truncated with an ellipsis to fit `max_w` at `px`.
std::string FitText(PanelFont& font, const std::string& text, float px, float max_w)
{
	if (MeasureText(font, text.c_str(), px) <= max_w)
		return text;
	std::string out = text;
	while (!out.empty())
	{
		out.pop_back();
		if (MeasureText(font, (out + "\xE2\x80\xA6").c_str(), px) <= max_w) // U+2026
			return out + "\xE2\x80\xA6";
	}
	return text;
}

// Rounded-rect signed distance, positive outside.
float RoundRectSd(float px, float py, float cx, float cy, float hw, float hh, float r)
{
	const float qx = std::fabs(px - cx) - (hw - r);
	const float qy = std::fabs(py - cy) - (hh - r);
	const float ax = std::max(qx, 0.0f), ay = std::max(qy, 0.0f);
	return std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.0f) - r;
}

void FillRoundRect(Bitmap& bmp, float x, float y, float w, float h, float radius, uint32_t color)
{
	const float alpha = ((color >> 24) & 0xffu) / 255.0f;
	const uint32_t rgb = color & 0x00FFFFFFu;
	const int x0 = std::max(0, static_cast<int>(std::floor(x)) - 1);
	const int y0 = std::max(0, static_cast<int>(std::floor(y)) - 1);
	const int x1 = std::min(bmp.w, static_cast<int>(std::ceil(x + w)) + 1);
	const int y1 = std::min(bmp.h, static_cast<int>(std::ceil(y + h)) + 1);
	const float cx = x + w * 0.5f, cy = y + h * 0.5f;
	for (int yy = y0; yy < y1; yy++)
		for (int xx = x0; xx < x1; xx++)
		{
			const float sd = RoundRectSd(xx + 0.5f, yy + 0.5f, cx, cy, w * 0.5f, h * 0.5f, radius);
			const float cover = std::min(1.0f, std::max(0.0f, 0.5f - sd));
			if (cover > 0.0f)
				BlendPixel(bmp, xx, yy, rgb, alpha * cover);
		}
}

void StrokeRoundRect(Bitmap& bmp, float x, float y, float w, float h, float radius, float width, uint32_t color)
{
	const float alpha = ((color >> 24) & 0xffu) / 255.0f;
	const uint32_t rgb = color & 0x00FFFFFFu;
	const int x0 = std::max(0, static_cast<int>(std::floor(x)) - 2);
	const int y0 = std::max(0, static_cast<int>(std::floor(y)) - 2);
	const int x1 = std::min(bmp.w, static_cast<int>(std::ceil(x + w)) + 2);
	const int y1 = std::min(bmp.h, static_cast<int>(std::ceil(y + h)) + 2);
	const float cx = x + w * 0.5f, cy = y + h * 0.5f;
	for (int yy = y0; yy < y1; yy++)
		for (int xx = x0; xx < x1; xx++)
		{
			const float sd = std::fabs(RoundRectSd(xx + 0.5f, yy + 0.5f, cx, cy, w * 0.5f, h * 0.5f, radius)) - width * 0.5f;
			const float cover = std::min(1.0f, std::max(0.0f, 0.5f - sd));
			if (cover > 0.0f)
				BlendPixel(bmp, xx, yy, rgb, alpha * cover);
		}
}

// PromptFont's PlayStation glyphs, as fe_text.h names them.
constexpr const char* kIconCross = "\xE2\x87\xA3";
constexpr const char* kIconCircle = "\xE2\x87\xA2";

struct PanelLayout
{
	float scale = 1.0f; // window height / 1080
	float pad = 72.0f;  // content x, scaled
	float row_x = 60.0f;
	float row_w = 588.0f;
	float row_h = 88.0f;
	float rows_y = 330.0f;
	float content_w = 560.0f; // text measure width
	float S(float v) const { return v * scale; }
};

// The whole panel, recomposed on a view revision change. Layout follows the Estratos pause scene:
// anchored left, the game visible to the right, rows with a glass focus plate, the prompts in a
// stable strip at the bottom.
void ComposePanel(Bitmap& bmp, const caliza::PauseMenuView& view, int height)
{
	EnsureFonts();
	const float scale = static_cast<float>(height) / 1080.0f;
	PanelLayout L;
	L.scale = scale;
	const int w = static_cast<int>(std::lround(800.0f * scale));
	bmp.Resize(w, height);

	// The backing: near-opaque void at the left edge, fading out before the texture's right edge,
	// so the composed panel's end is not a hard vertical line over the game.
	const float solid = 470.0f * scale, fade_end = 780.0f * scale;
	for (int y = 0; y < bmp.h; y++)
	{
		const float t = static_cast<float>(y) / std::max(1, bmp.h - 1);
		// Vertical gradient void top -> void (both barely translucent: 0.95 at the solid edge).
		const auto mixc = [](uint32_t a, uint32_t b, float f) {
			const uint32_t r = static_cast<uint32_t>(std::lround((a & 0xffu) * (1.0f - f) + (b & 0xffu) * f));
			const uint32_t g = static_cast<uint32_t>(
				std::lround(((a >> 8) & 0xffu) * (1.0f - f) + ((b >> 8) & 0xffu) * f));
			const uint32_t bl = static_cast<uint32_t>(
				std::lround(((a >> 16) & 0xffu) * (1.0f - f) + ((b >> 16) & 0xffu) * f));
			return (bl << 16) | (g << 8) | r;
		};
		const uint32_t rgb = mixc(kVoidTop, kVoid, t);
		for (int x = 0; x < bmp.w; x++)
		{
			float a = 0.0f;
			if (x < solid)
				a = 0.95f;
			else if (x < fade_end)
				a = 0.95f * (1.0f - (x - solid) / (fade_end - solid));
			if (a > 0.0f)
				BlendPixel(bmp, x, y, rgb, a);
		}
	}

	const float pad = L.S(L.pad);
	// Eyebrow, title, status.
	DrawText(bmp, s_text_font, "CALIZA", pad, L.S(92.0f), L.S(20.0f), WithAlpha(kIce, 0.8f), L.S(7.0f));
	const std::string title = FitText(s_text_font, view.title.empty() ? "Game" : view.title, L.S(46.0f), L.S(L.content_w));
	DrawText(bmp, s_text_font, title.c_str(), pad, L.S(168.0f), L.S(46.0f), kText);
	DrawText(bmp, s_text_font, view.subtitle.c_str(), pad, L.S(212.0f), L.S(23.0f), kTextDim);

	// The rows.
	const float row_x = L.S(L.row_x), row_w = L.S(L.row_w), row_h = L.S(L.row_h);
	float y = L.S(L.rows_y);
	for (const caliza::PauseMenuRow& row : view.rows)
	{
		const float ry = y + L.S(4.0f);
		if (row.focused)
		{
			FillRoundRect(bmp, row_x, ry, row_w, row_h - L.S(8.0f), L.S(14.0f), WithAlpha(kGlass, 0.55f));
			FillRoundRect(bmp, row_x + L.S(2.0f), ry + L.S(12.0f), L.S(5.0f), row_h - L.S(32.0f), L.S(2.5f),
				WithAlpha(kFocus, 0.95f));
		}
		uint32_t label_color = kText;
		if (row.disabled)
			label_color = kTextFaint;
		else if (row.destructive)
			label_color = kDanger;
		const float text_x = row_x + L.S(32.0f);
		DrawText(bmp, s_text_font, row.label.c_str(), text_x, ry + L.S(42.0f), L.S(30.0f), label_color);
		if (!row.detail.empty())
			DrawText(bmp, s_text_font, row.detail.c_str(), text_x, ry + L.S(72.0f), L.S(19.0f),
				row.disabled ? kTextFaint : kTextDim);
		y += row_h;
	}

	// The toast, then the prompts strip at the bottom.
	if (!view.toast.empty())
		DrawText(bmp, s_text_font, FitText(s_text_font, view.toast, L.S(22.0f), L.S(L.content_w)).c_str(), pad,
			L.S(948.0f), L.S(22.0f), kAmber);
	float px_cursor = pad;
	const float prompts_base = L.S(1018.0f);
	px_cursor += DrawText(bmp, s_icon_font, kIconCross, px_cursor, prompts_base, L.S(24.0f), kTextDim);
	px_cursor += DrawText(bmp, s_text_font, " Select", px_cursor + L.S(6.0f), prompts_base, L.S(21.0f), kTextDim) + L.S(34.0f);
	px_cursor += DrawText(bmp, s_icon_font, kIconCircle, px_cursor, prompts_base, L.S(24.0f), kTextDim);
	DrawText(bmp, s_text_font, " Back", px_cursor + L.S(6.0f), prompts_base, L.S(21.0f), kTextDim);

	// The destructive confirmation: an opaque plate over the panel, cancel focused first.
	if (view.dialog_open)
	{
		const float dw = L.S(600.0f);
		const float dx = L.S(28.0f);
		const float dy = L.S(430.0f);
		const float dh = L.S(300.0f);
		FillRoundRect(bmp, dx, dy, dw, dh, L.S(18.0f), WithAlpha(kVoid, 1.0f));
		StrokeRoundRect(bmp, dx, dy, dw, dh, L.S(18.0f), L.S(1.5f), kLine);
		DrawText(bmp, s_text_font, view.dialog_title.c_str(), dx + L.S(36.0f), dy + L.S(64.0f), L.S(32.0f), kText);
		// The body, word-wrapped by measure.
		std::string words = view.dialog_body;
		std::string line;
		float ly = dy + L.S(112.0f);
		size_t pos = 0;
		const float max_w = dw - L.S(72.0f);
		while (pos < words.size())
		{
			size_t end = words.find(' ', pos);
			if (end == std::string::npos)
				end = words.size();
			std::string word = words.substr(pos, end - pos);
			std::string trial = line.empty() ? word : line + " " + word;
			if (!line.empty() && MeasureText(s_text_font, trial.c_str(), L.S(21.0f)) > max_w)
			{
				DrawText(bmp, s_text_font, line.c_str(), dx + L.S(36.0f), ly, L.S(21.0f), kTextDim);
				ly += L.S(30.0f);
				line = word;
			}
			else
				line = trial;
			pos = end + 1;
		}
		if (!line.empty())
			DrawText(bmp, s_text_font, line.c_str(), dx + L.S(36.0f), ly, L.S(21.0f), kTextDim);
		// Cancel / confirm chips.
		const float chip_w = (dw - L.S(36.0f) * 2 - L.S(20.0f)) * 0.5f;
		const float chip_h = L.S(64.0f);
		const float chip_y = dy + dh - chip_h - L.S(28.0f);
		for (int i = 0; i < 2; i++)
		{
			const float cx0 = dx + L.S(36.0f) + i * (chip_w + L.S(20.0f));
			const bool focused = view.dialog_focus == i;
			const char* label = i == 0 ? view.cancel_label.c_str() : view.confirm_label.c_str();
			const uint32_t color = i == 1 ? kDanger : kText;
			if (focused)
			{
				FillRoundRect(bmp, cx0, chip_y, chip_w, chip_h, L.S(12.0f), WithAlpha(kFocus, 0.28f));
				StrokeRoundRect(bmp, cx0, chip_y, chip_w, chip_h, L.S(12.0f), L.S(2.0f), WithAlpha(kFocus, 0.9f));
			}
			else
			{
				FillRoundRect(bmp, cx0, chip_y, chip_w, chip_h, L.S(12.0f), WithAlpha(kGlass, 0.35f));
			}
			const float tw = MeasureText(s_text_font, label, L.S(24.0f));
			DrawText(bmp, s_text_font, label, cx0 + (chip_w - tw) * 0.5f, chip_y + L.S(41.0f), L.S(24.0f), color);
		}
	}
}

Bitmap s_panel;
uint64_t s_composed_revision = 0;
int s_composed_height = 0;
GSTexture* s_panel_tex = nullptr;
GSTexture* s_dim_tex = nullptr;
GSDevice* s_tex_dev = nullptr;
bool s_upload_dirty = false;
} // namespace

// Outside the swapchain's render pass (OrbisPreparePanels' place, and PresentCurrentFrame's start):
// the composition and the texture uploads. Draw-only inside the pass.
void OrbisPauseMenuPrepare()
{
	if (!s_view_visible.load(std::memory_order_acquire))
		return;
	if (!g_gs_device)
		return;

	caliza::PauseMenuView view;
	{
		std::lock_guard<std::mutex> lock(s_state_mtx);
		view = s_view;
	}
	const int height = g_gs_device->GetWindowHeight();
	if (height <= 0)
		return;
	if (view.revision != s_composed_revision || height != s_composed_height)
	{
		ComposePanel(s_panel, view, height);
		s_composed_revision = view.revision;
		s_composed_height = height;
		s_upload_dirty = true;
	}

	if (s_tex_dev != g_gs_device.get())
	{
		// The textures went with the old device.
		s_panel_tex = nullptr;
		s_dim_tex = nullptr;
		s_tex_dev = g_gs_device.get();
		s_upload_dirty = true;
	}
	if (!s_dim_tex)
	{
		s_dim_tex = s_tex_dev->CreateTexture(1, 1, 1, GSTexture::Format::Color);
		if (s_dim_tex)
		{
			const uint32_t black = RgbA(0x01020A, 0.55f); // the void's black, over half strength
			s_dim_tex->Update(GSVector4i(0, 0, 1, 1), &black, 4);
		}
	}
	if (s_upload_dirty && s_panel.w > 0 && s_panel.h > 0)
	{
		if (s_panel_tex && (s_panel_tex->GetWidth() != s_panel.w || s_panel_tex->GetHeight() != s_panel.h))
		{
			s_tex_dev->Recycle(s_panel_tex);
			s_panel_tex = nullptr;
		}
		if (!s_panel_tex)
			s_panel_tex = s_tex_dev->CreateTexture(s_panel.w, s_panel.h, 1, GSTexture::Format::Color);
		if (s_panel_tex)
		{
			s_panel_tex->Update(GSVector4i(0, 0, s_panel.w, s_panel.h), s_panel.px.data(),
				static_cast<u32>(s_panel.w) * 4);
			s_upload_dirty = false;
		}
	}
}

void OrbisPauseMenuDraw()
{
	if (!s_view_visible.load(std::memory_order_acquire) || !g_gs_device || s_tex_dev != g_gs_device.get())
		return;
	const float ww = static_cast<float>(g_gs_device->GetWindowWidth());
	const float wh = static_cast<float>(g_gs_device->GetWindowHeight());
	// The dim first (the game stays readable under it), then the panel at the left edge.
	if (s_dim_tex)
		OrbisVkPresentBlend(s_dim_tex, GSVector4(0.0f, 0.0f, 1.0f, 1.0f), GSVector4(0.0f, 0.0f, ww, wh));
	if (s_panel_tex)
	{
		const float pw = static_cast<float>(s_panel_tex->GetWidth());
		const float ph = static_cast<float>(s_panel_tex->GetHeight());
		OrbisVkPresentBlend(s_panel_tex, GSVector4(0.0f, 0.0f, 1.0f, 1.0f), GSVector4(0.0f, 0.0f, pw, ph));
	}
}

#else // !ORBIS_VULKAN

void OrbisPauseMenuPrepare()
{
}

void OrbisPauseMenuDraw()
{
}

#endif
