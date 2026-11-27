// Caliza: canonical in-game pause menu trigger presets shared by schema, UI model, and chooser.
// All presets avoid Share/Create (GoldHEN cheat menu / PS4 Share) and PS (PS4 system / GoldHEN).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

namespace caliza
{
struct PausePresetInfo
{
	const char* id;          // wire value stored in Caliza/PauseTrigger
	const char* label;       // short display label
	const char* description; // one-line description with trade-off
	bool recommended;
	bool warn_select;        // true when Touchpad click replaces PS2 Select
	bool warn_start;         // true when Hold Options uses PS2 Start
	const char* warning;     // optional explicit warning text (empty when none)
};

inline constexpr PausePresetInfo kPausePresets[] = {
	{
		"l3_r3",
		"L3 + R3",
		"Press and release both sticks alone. Keeps L3+R3+D-pad shortcuts and Select/Start free; disables L3+R3 filter cycle.",
		true,
		false,
		false,
		"",
	},
	{
		"touchpad",
		"Touchpad click",
		"Single press. Touchpad acts as PS2 Select; choosing it means Select must be mapped to another button for games that use it.",
		false,
		true,
		false,
		"Touchpad acts as PS2 Select; choosing it means Select must be mapped to another button for games that use it.",
	},
	{
		"l3_r3_l1",
		"L3 + R3 + L1",
		"Hold both sticks and press L1. Keeps Touchpad (Select) and Options (Start) free; requires three buttons.",
		false,
		false,
		false,
		"",
	},
	{
		"touchpad_hold",
		"Hold Touchpad (0.5 s)",
		"Hold Touchpad 0.5 s to pause; short tap still sends PS2 Select on release (slight delay on Select).",
		false,
		false,
		false,
		"",
	},
	{
		"l1_r1_touchpad",
		"L1 + R1 + Touchpad",
		"Hold L1 + R1 and click Touchpad. Keeps single Touchpad click as Select; replaces the old instant exit shortcut.",
		false,
		false,
		false,
		"",
	},
	{
		"options_hold",
		"Hold Options (0.5 s)",
		"Hold Options 0.5 s to pause. Options is PS2 Start; short tap sends Start on release (avoid if a game needs Start held).",
		false,
		false,
		true,
		"Options is PS2 Start. Short tap sends Start on release; avoid this preset in games that require holding Start.",
	},
};

inline constexpr std::size_t kPausePresetCount = sizeof(kPausePresets) / sizeof(kPausePresets[0]);
} // namespace caliza
