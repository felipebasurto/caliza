// PS5 port frontend: "modern controls" group detection for the per-game patch/cheat toggle system.
//
// No PS2 emulator ships a separate "modern controls" patch format or a bundled database of per-game dual-stick
// remaps (see docs/research/modern-controls.md): ARMSX2, the PCSX2 fork this is modeled on, applies the same
// pnach patches and cheats as upstream PCSX2 through the same patches/cheats folders. The handful of real
// remaps that exist are ordinary pnach groups the community happens to name things like "Modern Control
// Profiles" (PCSX2/pcsx2_patches, Sprint Car Challenge) or "Modern analog controls" (King's Field: The Ancient
// City). This is a name match over whatever group is already present, not a new patch source.
//
// Copyright (C) 2026 swordpdf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace fe
{
// True when a patch or cheat group's name identifies it as remapping a game's old-style controls (tank
// controls, camera on face buttons) to modern dual-stick controls, case-insensitively. Deliberately has no
// dependency on the rest of fe_settings.cpp/fe_games.cpp so it stays trivial to host-test on its own.
bool IsModernControlsGroup(const std::string& name);
} // namespace fe
