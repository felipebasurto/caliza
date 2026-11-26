// Caliza in-game pause menu: the platform glue between the pad thread, the CPU thread that owns
// the VM and the GS thread's present path. The menu model itself (caliza/core/pause_menu.cpp) is
// host-tested; this file only moves input, commands, events, status and pixels between threads.
//
// Threads:
//   - OrbisPauseMenuPadTick runs on the pad thread (main-boot.cpp orbis_pad_thread): it owns the
//     menu model, translates the controller into caliza::Input and answers whether the game's
//     input must be neutralised this iteration.
//   - OrbisPauseMenuPumpCpu runs on the CPU/main thread, both from StubHost's vsync pump while
//     the VM executes and from main()'s paused loop: every VMManager call stays on the thread
//     that owns the VM.
//   - OrbisPauseMenuDraw runs on the GS thread inside the present pass (GSRenderer EndPresentFrame,
//     which the idle re-presents while paused also reach). It recomposes the panel's pixels only
//     when the published view's revision changes.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

struct OrbisPausePadState
{
	uint32_t buttons = 0; // ScePad bits; 0x1 is the DualSense's Create button (the old Share)
	uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
};

// Pad thread. True when this iteration's input must not reach the game (the overlay is open, or
// the buttons that closed it are still held). The Create button edge opens the overlay while the
// VM runs or is paused.
bool OrbisPauseMenuPadTick(const OrbisPausePadState& pad, double dt);

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
