// Caliza in-game pause menu: the platform glue between the pad thread, the CPU thread that owns
// the VM and the GS thread's present path. The menu model itself (caliza/core/pause_menu.cpp) is
// host-tested; this file only moves input, commands, events, status and pixels between threads.
//
// Threads:
//   - OrbisPauseMenuPadTick runs on the pad thread (main-boot.cpp orbis_pad_thread): it owns the
//     menu model, evaluates the configured pause combo, translates the controller into
//     caliza::Input and answers whether the game's input must be neutralised this iteration.
//   - OrbisPauseMenuPumpCpu runs on the CPU/main thread, both from StubHost's vsync pump while
//     the VM executes and from main()'s paused loop: every VMManager call stays on the thread
//     that owns the VM.
//   - OrbisPauseMenuDraw runs on the GS thread inside the present pass (GSRenderer EndPresentFrame,
//     which the idle re-presents while paused also reach). It recomposes the panel's pixels only
//     when the published view's revision changes.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "OrbisPadMap.h"

#include <cstdint>

struct OrbisPausePadState
{
	uint32_t buttons = 0; // ScePad bits (never uses Share/Create bit 0x1 to avoid GoldHEN clash)
	uint8_t l2 = 0, r2 = 0;
	uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
	int touch = 0; // 0 none/middle, 1 touchpad left third, 2 touchpad right third
};

// Pad thread. True when this iteration's input must not reach the game (the overlay is open, or
// the buttons that closed it are still held). Uses cfg.pause (default L3+R3 + L1) and cfg.hold_ms.
bool OrbisPauseMenuPadTick(const OrbisPausePadState& pad, const orbis_padmap::Config& cfg, long long now_ms, double dt);

// CPU thread. Executes the queued runtime commands in post order and publishes the VM status.
void OrbisPauseMenuPumpCpu();

// GS thread, outside the swapchain render pass (with OrbisPreparePanels in the vsync path and at
// the start of the idle re-present): recomposes the panel's pixels when the view changed and
// uploads the textures. Never draw inside the pass from here.
void OrbisPauseMenuPrepare();

// GS thread, inside the swapchain render pass of a present. Draws the dim layer and the panel
// over the frame when the overlay is visible; a no-op otherwise.
void OrbisPauseMenuDraw();

// Session boundaries (main-boot.cpp): the menu state belongs to one VM run.
void OrbisPauseMenuReset();
