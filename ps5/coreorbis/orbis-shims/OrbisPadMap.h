// PS5SX2 (vk-285-116, AI-assisted): the controller remapping, as functions of what is held: no console library, no emulator,
// so tests/padmap/test_pad_map.cpp checks it on a PC. main-boot.cpp's orbis_pad_apply uses it for both PS2 ports.
//
// The settings (the Controls tab of the settings page and of the shelf's sheet; gs.ini or the game's own file):
//   Caliza/Button<Cross|Circle|Square|Triangle|L1|R1|L2|R2|L3|R3|Options|Touchpad|Up|Down|Left|Right> = the PS2 input that
//     controller button presses: Cross Circle Square Triangle L1 R1 L2 R2 L3 R3 Start Select Up Down Left Right Analog
//     (the DualShock 2's analog mode button) Pressure (PCSX2's pressure modifier: while held, buttons press at half
//     strength) or None. Unset: the same button (Options: Start, the touchpad's click: Select).
//   Caliza/SwapSticks = true: the left stick moves the PS2's right stick and the right stick its left one.
//   Caliza/InvertLeft, InvertRight = 0 no, 1 up-down, 2 left-right, 3 both: the PS2 stick's axes (after the swap).
//   Caliza/LeftStickDpad = 0 no, 1 the PS2's left stick also presses the D-pad, 2 it presses only the D-pad (the stick
//     itself stays in the middle).
//   Caliza/PauseTrigger = preset to open or close the in-game pause menu (all avoid GoldHEN's long-press Share /
//     double-press PS and PS4 system Share/PS):
//       l3_r3 (default): L3 + R3 on release only if no other button was pressed during the hold (keeps L3+R3+D-pad
//         save/load/disc shortcuts and Touchpad/Options free).
//       touchpad: single Touchpad click (intercepted; never leaks PS2 Select).
//       l3_r3_l1: L3 + R3 + L1.
//       touchpad_hold: hold Touchpad 0.5 s to pause; short tap sends PS2 Select on release.
//       l1_r1_touchpad: L1 + R1 + Touchpad click (keeps single Touchpad click as PS2 Select).
//       options_hold: hold Options 0.5 s to pause; short tap sends PS2 Start on release.
//     Legacy Caliza/PauseButton1/2 migrate automatically: Share/Create clears to "not chosen" (runtime falls back to
//     l3_r3), and other legacy button pairs map to the closest preset.
//   Caliza/SaveButton1, SaveButton2, LoadButton1, LoadButton2 = the two buttons held together to save or load the state
//     (slot 1): L3R3 (both stick clicks) Cross Circle Square Triangle L1 R1 L2 R2 L3 R3 Options Touchpad (its click)
//     TouchLeft TouchRight (a finger on the touchpad's left or right third) Up Down Left Right, or None. Unset: L3R3 + Up
//     saves, L3R3 + Down loads. Caliza/StateHold = how long (seconds, 0 to 10) they're held first; 0, the default: at once.
// PS5SX2's own button combos (save states, the menu, the settings page) read the controller's real buttons before this.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace orbis_padmap
{
// What a controller button can press on the PS2 controller.
enum Target : uint8_t
{
	T_CROSS,
	T_CIRCLE,
	T_SQUARE,
	T_TRIANGLE,
	T_L1,
	T_R1,
	T_L2,
	T_R2,
	T_L3,
	T_R3,
	T_START,
	T_SELECT,
	T_UP,
	T_DOWN,
	T_LEFT,
	T_RIGHT,
	T_ANALOG,
	T_PRESSURE,
	T_NONE, // nothing
	T_COUNT = T_NONE, // the PS2 inputs (T_NONE is not one)
};

// The controller's buttons that can be remapped.
enum Source : uint8_t
{
	S_CROSS,
	S_CIRCLE,
	S_SQUARE,
	S_TRIANGLE,
	S_L1,
	S_R1,
	S_L2,
	S_R2,
	S_L3,
	S_R3,
	S_OPTIONS,
	S_TOUCHPAD,
	S_UP,
	S_DOWN,
	S_LEFT,
	S_RIGHT,
	S_COUNT,
};

struct SourceInfo
{
	const char* key;  // the setting, in [Caliza]
	const char* name; // for the log
	uint32_t bits;    // ScePad's button bits (L2 and R2 are read from their triggers)
	Target def;       // what it presses when the setting is unset
};

// ScePad's bits, as orbis_pad_apply always read them. The touchpad's click is 0x00100000; 0x40000000 is the keyboard's
// Select (Backspace, orbis-shims/ProsperoKbdMap.h PAD_SELECT). vk-285-122 (AI-assisted): ScePad's 0x1, the DualSense's
// Create button (the old Share, left of the touchpad), presses nothing -- it pressed Select with the touchpad's click
// until now (swordpdf: "unbind select from the share button").
inline const SourceInfo& SourceAt(int s)
{
	static const SourceInfo k[S_COUNT] = {
		{"ButtonCross", "Cross", 0x00004000u, T_CROSS},
		{"ButtonCircle", "Circle", 0x00002000u, T_CIRCLE},
		{"ButtonSquare", "Square", 0x00008000u, T_SQUARE},
		{"ButtonTriangle", "Triangle", 0x00001000u, T_TRIANGLE},
		{"ButtonL1", "L1", 0x00000400u, T_L1},
		{"ButtonR1", "R1", 0x00000800u, T_R1},
		{"ButtonL2", "L2", 0x00000100u, T_L2},
		{"ButtonR2", "R2", 0x00000200u, T_R2},
		{"ButtonL3", "L3", 0x00000002u, T_L3},
		{"ButtonR3", "R3", 0x00000004u, T_R3},
		{"ButtonOptions", "Options", 0x00000008u, T_START},
		{"ButtonTouchpad", "the touchpad's click", 0x40100000u, T_SELECT},
		{"ButtonUp", "D-pad up", 0x00000010u, T_UP},
		{"ButtonDown", "D-pad down", 0x00000040u, T_DOWN},
		{"ButtonLeft", "D-pad left", 0x00000080u, T_LEFT},
		{"ButtonRight", "D-pad right", 0x00000020u, T_RIGHT},
	};
	return k[s];
}

// The setting's values, in Target order (T_NONE last).
inline const char* TargetName(int t)
{
	static const char* const k[T_NONE + 1] = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Start",
		"Select", "Up", "Down", "Left", "Right", "Analog", "Pressure", "None"};
	return (t >= 0 && t <= T_NONE) ? k[t] : "None";
}

inline bool SameText(const std::string& a, const char* b)
{
	size_t i = 0;
	for (; i < a.size() && b[i]; i++)
	{
		const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
		const char y = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
		if (x != y)
			return false;
	}
	return i == a.size() && b[i] == '\0';
}

// "Circle" (any case, spaces around it allowed) -> T_CIRCLE. False for anything else.
inline bool ParseTarget(std::string v, Target& out)
{
	while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r'))
		v.pop_back();
	size_t start = 0;
	while (start < v.size() && (v[start] == ' ' || v[start] == '\t'))
		start++;
	v.erase(0, start);
	for (int t = 0; t <= T_NONE; t++)
		if (SameText(v, TargetName(t)))
		{
			out = static_cast<Target>(t);
			return true;
		}
	return false;
}

// vk-285-117: what a save or load combo's button can be.
enum ComboButton : uint8_t
{
	CB_NONE,
	CB_L3R3, // both stick clicks
	CB_CROSS,
	CB_CIRCLE,
	CB_SQUARE,
	CB_TRIANGLE,
	CB_L1,
	CB_R1,
	CB_L2,
	CB_R2,
	CB_L3,
	CB_R3,
	CB_OPTIONS,
	CB_TOUCHPAD,    // its click
	CB_TOUCH_LEFT,  // a finger on its left third (eerec-284's zone)
	CB_TOUCH_RIGHT, // on its right third
	CB_UP,
	CB_DOWN,
	CB_LEFT,
	CB_RIGHT,
	CB_COUNT,
};

// The settings' values, in ComboButton order.
inline const char* ComboButtonName(int b)
{
	static const char* const k[CB_COUNT] = {"None", "L3R3", "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3",
		"Options", "Touchpad", "TouchLeft", "TouchRight", "Up", "Down", "Left", "Right"};
	return (b >= 0 && b < CB_COUNT) ? k[b] : "None";
}

// For the log.
inline const char* ComboButtonLabel(int b)
{
	static const char* const k[CB_COUNT] = {"nothing", "L3+R3", "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3",
		"R3", "Options", "the touchpad's click", "the touchpad's left side", "the touchpad's right side", "D-pad up", "D-pad down",
		"D-pad left", "D-pad right"};
	return (b >= 0 && b < CB_COUNT) ? k[b] : "nothing";
}

inline bool ParseComboButton(std::string v, ComboButton& out)
{
	while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r'))
		v.pop_back();
	size_t start = 0;
	while (start < v.size() && (v[start] == ' ' || v[start] == '\t'))
		start++;
	v.erase(0, start);
	for (int b = 0; b < CB_COUNT; b++)
		if (SameText(v, ComboButtonName(b)))
		{
			out = static_cast<ComboButton>(b);
			return true;
		}
	return false;
}

enum PauseTrigger : uint8_t
{
	PT_L3_R3,          // L3 + R3 on release only if no other button pressed during hold (default)
	PT_TOUCHPAD,       // Single Touchpad click (intercepted; never leaks PS2 Select)
	PT_L3_R3_L1,       // L3 + R3 + L1
	PT_TOUCHPAD_HOLD,  // Hold Touchpad 0.5 s; short tap pulses PS2 Select on release
	PT_L1_R1_TOUCHPAD, // L1 + R1 + Touchpad click
	PT_OPTIONS_HOLD,   // Hold Options 0.5 s; short tap pulses PS2 Start on release
	PT_COUNT,
};

constexpr PauseTrigger kDefaultPauseTrigger = PT_L3_R3;
constexpr long long kPauseHoldThresholdMs = 500;
constexpr long long kPauseTapPulseMs = 80;

inline const char* PauseTriggerName(PauseTrigger t)
{
	static const char* const k[PT_COUNT] = {
		"l3_r3", "touchpad", "l3_r3_l1", "touchpad_hold", "l1_r1_touchpad", "options_hold"};
	return (t >= 0 && t < PT_COUNT) ? k[t] : k[kDefaultPauseTrigger];
}

inline const char* PauseTriggerLabel(PauseTrigger t)
{
	static const char* const k[PT_COUNT] = {
		"L3 + R3", "Touchpad click", "L3 + R3 + L1", "Hold Touchpad (0.5 s)", "L1 + R1 + Touchpad", "Hold Options (0.5 s)"};
	return (t >= 0 && t < PT_COUNT) ? k[t] : k[kDefaultPauseTrigger];
}

inline bool ParsePauseTrigger(std::string v, PauseTrigger& out)
{
	while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r'))
		v.pop_back();
	size_t start = 0;
	while (start < v.size() && (v[start] == ' ' || v[start] == '\t'))
		start++;
	v.erase(0, start);
	for (int i = 0; i < PT_COUNT; i++)
		if (SameText(v, PauseTriggerName(static_cast<PauseTrigger>(i))))
		{
			out = static_cast<PauseTrigger>(i);
			return true;
		}
	return false;
}

// Legacy Share/Create pause trigger names that clash with GoldHEN's cheat menu (long-press Share)
// and PS4 system Share interception.
inline bool IsLegacyPauseTrigger(std::string v)
{
	while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r'))
		v.pop_back();
	size_t start = 0;
	while (start < v.size() && (v[start] == ' ' || v[start] == '\t'))
		start++;
	v.erase(0, start);
	return SameText(v, "Share") || SameText(v, "Create") || SameText(v, "ShareHold") || SameText(v, "CreateHold") ||
	       SameText(v, "ShareLong") || SameText(v, "CreateLong");
}

// Maps legacy Caliza/PauseButton1 + PauseButton2 values to the closest PauseTrigger preset.
// Returns false if either button is legacy Share/Create (caller treats it as "not chosen" so the
// first-run chooser appears, while runtime falls back to kDefaultPauseTrigger) or if both are empty.
inline bool MapLegacyPauseButtonsToPreset(const std::string& button1, const std::string& button2, PauseTrigger& out,
	bool* was_legacy_share = nullptr)
{
	if (was_legacy_share)
		*was_legacy_share = false;
	if (IsLegacyPauseTrigger(button1) || IsLegacyPauseTrigger(button2))
	{
		if (was_legacy_share)
			*was_legacy_share = true;
		return false;
	}
	ComboButton b1 = CB_NONE, b2 = CB_NONE;
	const bool ok1 = ParseComboButton(button1, b1);
	const bool ok2 = ParseComboButton(button2, b2);
	if (!ok1 && !ok2)
		return false;
	auto has = [&](ComboButton b) { return b1 == b || b2 == b; };
	auto only = [&](ComboButton b) { return (b1 == b && (b2 == CB_NONE || b2 == b)) || (b2 == b && b1 == CB_NONE); };

	if (only(CB_TOUCHPAD))
		out = PT_TOUCHPAD;
	else if (has(CB_L3R3) && has(CB_L1))
		out = PT_L3_R3_L1;
	else if (only(CB_L3R3) || (has(CB_L3) && has(CB_R3)))
		out = PT_L3_R3;
	else if (has(CB_TOUCHPAD) && (has(CB_L1) || has(CB_R1)))
		out = PT_L1_R1_TOUCHPAD;
	else if (has(CB_OPTIONS) && !has(CB_TOUCHPAD) && !has(CB_L3R3))
		out = PT_OPTIONS_HOLD;
	else if (has(CB_L3R3))
		out = PT_L3_R3_L1;
	else if (has(CB_TOUCHPAD))
		out = PT_TOUCHPAD_HOLD;
	else
		out = kDefaultPauseTrigger;
	return true;
}

// The ScePad bits a combo button is (0 for the touchpad's zones, which are a touch, not a button; L2 and R2 are also
// held by their triggers).
inline uint32_t ComboBits(ComboButton b)
{
	switch (b)
	{
		case CB_L3R3: return 0x00000006u;
		case CB_CROSS: return 0x00004000u;
		case CB_CIRCLE: return 0x00002000u;
		case CB_SQUARE: return 0x00008000u;
		case CB_TRIANGLE: return 0x00001000u;
		case CB_L1: return 0x00000400u;
		case CB_R1: return 0x00000800u;
		case CB_L2: return 0x00000100u;
		case CB_R2: return 0x00000200u;
		case CB_L3: return 0x00000002u;
		case CB_R3: return 0x00000004u;
		case CB_OPTIONS: return 0x00000008u;
		case CB_TOUCHPAD: return 0x40100000u; // the click, and the keyboard's Backspace (as the remapping's source)
		case CB_UP: return 0x00000010u;
		case CB_DOWN: return 0x00000040u;
		case CB_LEFT: return 0x00000080u;
		case CB_RIGHT: return 0x00000020u;
		default: return 0;
	}
}

struct Config
{
	Target target[S_COUNT];
	// vk-285-118: how hard each button presses what it presses, 0.05..1 (Caliza/Button<Name>Pressure; 1, a full press, by
	// default). A DualShock 2's face buttons, D-pad and shoulders read pressure, and some games act on a partial press: SOCOM
	// II crouches at about 0.20 on Triangle, Combined Assault at 0.30 (a full press goes prone). The triggers press at most
	// this hard. Targets that only read on or off (Start, Select, L3, R3, Analog, Light press) press the same at any strength.
	float pressure[S_COUNT];
	bool swap_sticks = false;
	uint8_t left_dpad = 0;    // 0 no, 1 the D-pad too, 2 the D-pad only
	// vk-285-118: each stick's dead zone, percent of the way out (0..50; Caliza/DeadzoneLeft, DeadzoneRight): inside it the
	// stick reads as centred, past it the rest of the way is stretched over the whole range (no jump at its edge). For
	// sticks that drift, or games that read a resting stick as a push. Of the controller's sticks, before Swap sticks.
	uint8_t deadzone_left = 0;
	uint8_t deadzone_right = 0;
	uint8_t invert_left = 0;  // bit 0 up-down, bit 1 left-right
	uint8_t invert_right = 0;
	// In-game pause menu trigger preset (Caliza/PauseTrigger): defaults to L3 + R3 on release
	// (only if no other button was pressed during the hold, keeping L3+R3+D-pad shortcuts free).
	// All presets avoid GoldHEN's long-press Share / double-press PS and PS4 system Share/PS.
	PauseTrigger pause_trigger = kDefaultPauseTrigger;
	// vk-285-117: the save and load state combos (two buttons each) and how long they're held first.
	ComboButton save[2] = {CB_L3R3, CB_UP};
	ComboButton load[2] = {CB_L3R3, CB_DOWN};
	int hold_ms = 0;
	// vk-285-118: the fast forward combo (Caliza/FastButton1/2, experimental, to skip videos): it turns fast forward on and
	// off, after the same hold time. Nothing on both (the default): no combo.
	ComboButton fast[2] = {CB_NONE, CB_NONE};
	// vk-285-139: the change disc combo (Caliza/DiscButton1/2): the next disc of the game's set ("(Disc 2)" beside it).
	ComboButton disc[2] = {CB_L3R3, CB_RIGHT};

	Config()
	{
		for (int s = 0; s < S_COUNT; s++)
		{
			target[s] = SourceAt(s).def;
			pressure[s] = 1.0f;
		}
	}

	bool operator==(const Config& o) const
	{
		for (int s = 0; s < S_COUNT; s++)
			if (target[s] != o.target[s] || pressure[s] != o.pressure[s])
				return false;
		return swap_sticks == o.swap_sticks && left_dpad == o.left_dpad && deadzone_left == o.deadzone_left &&
		       deadzone_right == o.deadzone_right && invert_left == o.invert_left && invert_right == o.invert_right &&
		       pause_trigger == o.pause_trigger &&
		       save[0] == o.save[0] && save[1] == o.save[1] && load[0] == o.load[0] && load[1] == o.load[1] && hold_ms == o.hold_ms &&
		       fast[0] == o.fast[0] && fast[1] == o.fast[1] && disc[0] == o.disc[0] && disc[1] == o.disc[1];
	}
	bool operator!=(const Config& o) const { return !(*this == o); }
	bool IsDefault() const { return *this == Config(); }
};

inline bool Truthy(const std::string& v)
{
	return SameText(v, "true") || v == "1" || SameText(v, "on") || SameText(v, "yes");
}

// A small number setting (0..max); anything else is 0.
inline uint8_t SmallInt(const std::string& v, int max)
{
	char* end = nullptr;
	const long n = std::strtol(v.c_str(), &end, 10);
	if (v.empty() || end == v.c_str() || n < 0 || n > max)
		return 0;
	return static_cast<uint8_t>(n);
}

// vk-285-118: a button's strength, "0.2" or "20%"; anything outside 0.05..1, or not a number, is a full press.
inline float ParsePressure(const std::string& v)
{
	char* end = nullptr;
	double p = std::strtod(v.c_str(), &end);
	if (v.empty() || end == v.c_str())
		return 1.0f;
	if (*end == '%')
		p /= 100.0;
	return (p >= 0.05 && p <= 1.0) ? static_cast<float>(p) : 1.0f;
}

// The settings, read with `get(key, value)` (the key within [Caliza]; true when it is set). An unknown value leaves that
// button or stick option as it is by default.
template <typename Get>
inline Config FromSettings(Get get, bool* pause_migrated = nullptr)
{
	if (pause_migrated)
		*pause_migrated = false;
	Config c;
	std::string v;
	for (int s = 0; s < S_COUNT; s++)
	{
		v.clear();
		Target t;
		if (get(SourceAt(s).key, v) && ParseTarget(v, t))
			c.target[s] = t;
		v.clear();
		if (get((std::string(SourceAt(s).key) + "Pressure").c_str(), v))
			c.pressure[s] = ParsePressure(v);
	}
	v.clear();
	if (get("SwapSticks", v))
		c.swap_sticks = Truthy(v);
	v.clear();
	if (get("LeftStickDpad", v))
		c.left_dpad = SmallInt(v, 2);
	v.clear();
	if (get("DeadzoneLeft", v))
		c.deadzone_left = SmallInt(v, 50);
	v.clear();
	if (get("DeadzoneRight", v))
		c.deadzone_right = SmallInt(v, 50);
	v.clear();
	if (get("InvertLeft", v))
		c.invert_left = SmallInt(v, 3);
	v.clear();
	if (get("InvertRight", v))
		c.invert_right = SmallInt(v, 3);
	// Pause menu preset: prefer Caliza/PauseTrigger; migrate legacy Share/Create or PauseButton1/2 cleanly.
	{
		std::string pt;
		PauseTrigger parsed = kDefaultPauseTrigger;
		if (get("PauseTrigger", pt))
		{
			if (ParsePauseTrigger(pt, parsed))
			{
				c.pause_trigger = parsed;
			}
			else if (IsLegacyPauseTrigger(pt))
			{
				c.pause_trigger = kDefaultPauseTrigger;
				if (pause_migrated)
					*pause_migrated = true;
			}
		}
		else
		{
			std::string p1, p2;
			const bool has_p1 = get("PauseButton1", p1);
			const bool has_p2 = get("PauseButton2", p2);
			if (has_p1 || has_p2)
			{
				bool was_legacy_share = false;
				if (MapLegacyPauseButtonsToPreset(p1, p2, parsed, &was_legacy_share))
					c.pause_trigger = parsed;
				else
					c.pause_trigger = kDefaultPauseTrigger;
				if (pause_migrated)
					*pause_migrated = true;
			}
		}
	}
	// vk-285-117: the save and load combos; a value that isn't a button leaves that one at its default.
	static const char* const combo_keys[8] = {"SaveButton1", "SaveButton2", "LoadButton1", "LoadButton2", "FastButton1", "FastButton2",
		"DiscButton1", "DiscButton2"};
	ComboButton* const combo[8] = {&c.save[0], &c.save[1], &c.load[0], &c.load[1], &c.fast[0], &c.fast[1], &c.disc[0], &c.disc[1]};
	for (int i = 0; i < 8; i++)
	{
		v.clear();
		ComboButton b;
		if (get(combo_keys[i], v) && ParseComboButton(v, b))
			*combo[i] = b;
	}
	v.clear();
	if (get("StateHold", v))
	{
		char* end = nullptr;
		const double seconds = std::strtod(v.c_str(), &end);
		if (end != v.c_str() && seconds >= 0.0 && seconds <= 10.0)
			c.hold_ms = static_cast<int>(seconds * 1000.0 + 0.5);
	}
	return c;
}

// vk-285-117: the save and load combos. What the controller holds now, with the touchpad's zone (0 no finger there or the
// middle third, 1 the left third, 2 the right third).
struct ComboState
{
	uint32_t buttons = 0;
	uint8_t l2 = 0, r2 = 0;
	int touch = 0;
};

inline bool ComboHeld(ComboButton b, const ComboState& s)
{
	switch (b)
	{
		case CB_NONE: return false;
		case CB_L3R3: return (s.buttons & 0x6u) == 0x6u;
		case CB_L2: return (s.buttons & 0x100u) != 0 || s.l2 >= 200u; // the trigger past 200 of 255, as the other combos read it
		case CB_R2: return (s.buttons & 0x200u) != 0 || s.r2 >= 200u;
		case CB_TOUCH_LEFT: return s.touch == 1;
		case CB_TOUCH_RIGHT: return s.touch == 2;
		default: return (s.buttons & ComboBits(b)) != 0;
	}
}

inline uint32_t ComboMask(const ComboButton (&b)[2])
{
	uint32_t mask = 0;
	for (ComboButton x : b)
		if (x != CB_NONE && x < CB_COUNT)
			mask |= ComboBits(x);
	return mask;
}

inline bool ComboActive(const ComboButton (&b)[2], const ComboState& s)
{
	int n = 0;
	bool all = true;
	for (ComboButton x : b)
	{
		if (x == CB_NONE || x >= CB_COUNT)
			continue;
		n++;
		all = all && ComboHeld(x, s);
	}
	return n > 0 && all;
}

// One combo (save, or load): it fires once its buttons have been held together for hold_ms, and again only after one of
// them was let go. Nothing, or the same button twice, makes it a one-button combo; nothing twice turns it off. When it
// fires at once (hold 0), the press that completed it is kept from the game until that button is let go (as L3+R3 +
// D-pad's D-pad press); with a hold, the game sees the buttons until the combo fires.
struct ComboWatch
{
	bool armed = true;
	bool timing = false;
	long long since_ms = 0;
	uint32_t held_back = 0;
	bool was_held[2] = {false, false};

	// `block` gets the bits to keep from the game now (0x100 / 0x200: also that trigger's pull). True when it fires.
	bool Update(const ComboButton (&b)[2], int hold_ms, const ComboState& s, long long now_ms, uint32_t& block)
	{
		ComboButton list[2] = {CB_NONE, CB_NONE};
		int n = 0;
		for (ComboButton x : b)
			if (x != CB_NONE && x < CB_COUNT && (n == 0 || list[0] != x))
				list[n++] = x;
		bool fired = false;
		if (n == 0)
		{
			armed = true;
			timing = false;
			was_held[0] = was_held[1] = false;
		}
		else
		{
			bool held[2] = {false, false};
			bool all = true;
			for (int i = 0; i < n; i++)
			{
				held[i] = ComboHeld(list[i], s);
				all = all && held[i];
			}
			if (!all)
			{
				armed = true;
				timing = false;
			}
			else if (armed)
			{
				if (!timing)
				{
					timing = true;
					since_ms = now_ms;
				}
				if (now_ms - since_ms >= hold_ms)
				{
					fired = true;
					armed = false;
					timing = false;
					if (hold_ms <= 0)
						for (int i = 0; i < n; i++)
							if (!was_held[i])
								held_back |= ComboBits(list[i]);
				}
			}
			for (int i = 0; i < 2; i++)
				was_held[i] = i < n && held[i];
		}
		// A button let go reaches the game again (a trigger: once it's fully let go).
		held_back &= s.buttons | (s.l2 != 0 ? 0x100u : 0u) | (s.r2 != 0 ? 0x200u : 0u);
		block |= held_back;
		return fired;
	}
};

struct PauseTriggerResult
{
	bool fired = false;
	uint32_t suppress_bits = 0; // bits to strip from game input while holding or on trigger
	uint32_t inject_bits = 0;   // bits to pulse into game input on short-tap release (Select / Start)
};

// Evaluates the selected PauseTrigger preset across frames:
// - PT_L3_R3: fires on release of L3+R3 only if no other button/trigger/touch was pressed during the hold,
//   so existing L3+R3+D-pad quick save/load/disc shortcuts keep working untouched.
// - PT_TOUCHPAD: fires immediately on Touchpad click and suppresses Select while held.
// - PT_L3_R3_L1: fires immediately on L3+R3+L1 and suppresses L1 until released.
// - PT_TOUCHPAD_HOLD: suppresses Touchpad while held; >= 500 ms opens pause, < 500 ms pulses Select on release.
// - PT_L1_R1_TOUCHPAD: fires on L1+R1+Touchpad click and suppresses Select until released; single Touchpad stays Select.
// - PT_OPTIONS_HOLD: suppresses Options while held; >= 500 ms opens pause, < 500 ms pulses Start on release.
struct PauseTriggerWatch
{
	PauseTrigger last_trigger = kDefaultPauseTrigger;
	bool initialized = false;
	bool prev_active = false;
	bool l3r3_armed = false;
	bool l3r3_dirty = false;
	bool holding = false;
	bool hold_fired = false;
	long long hold_since_ms = 0;
	uint32_t held_back = 0;
	uint32_t pulse_bits = 0;
	long long pulse_until_ms = 0;

	void Reset()
	{
		prev_active = false;
		l3r3_armed = false;
		l3r3_dirty = false;
		holding = false;
		hold_fired = false;
		hold_since_ms = 0;
		held_back = 0;
		pulse_bits = 0;
		pulse_until_ms = 0;
	}

	PauseTriggerResult Update(PauseTrigger trigger, const ComboState& s, long long now_ms, bool menu_open)
	{
		if (!initialized || trigger != last_trigger)
		{
			Reset();
			last_trigger = trigger;
			initialized = true;
		}

		PauseTriggerResult res;
		switch (trigger)
		{
			case PT_L3_R3:
			{
				constexpr uint32_t kL3R3 = 0x00000006u;
				const bool both_down = (s.buttons & kL3R3) == kL3R3;
				const bool any_down = (s.buttons & kL3R3) != 0;
				const bool other_down = (s.buttons & ~kL3R3) != 0 || s.l2 >= 200u || s.r2 >= 200u || s.touch != 0;
				if (both_down)
				{
					if (!l3r3_armed)
					{
						l3r3_armed = true;
						l3r3_dirty = other_down;
					}
					else if (other_down)
					{
						l3r3_dirty = true;
					}
				}
				else if (any_down)
				{
					if (l3r3_armed && other_down)
						l3r3_dirty = true;
				}
				else if (l3r3_armed)
				{
					if (!l3r3_dirty)
						res.fired = true;
					l3r3_armed = false;
					l3r3_dirty = false;
				}
				break;
			}
			case PT_TOUCHPAD:
			{
				constexpr uint32_t kTp = 0x40100000u;
				const bool down = (s.buttons & kTp) != 0;
				if (down)
					res.suppress_bits |= kTp;
				if (down && !prev_active)
					res.fired = true;
				prev_active = down;
				break;
			}
			case PT_L3_R3_L1:
			{
				constexpr uint32_t kCombo = 0x00000406u; // L3 | R3 | L1
				const bool down = (s.buttons & kCombo) == kCombo;
				if (down && !prev_active)
				{
					res.fired = true;
					held_back |= 0x00000400u;
				}
				prev_active = down;
				held_back &= s.buttons;
				res.suppress_bits |= held_back;
				break;
			}
			case PT_L1_R1_TOUCHPAD:
			{
				constexpr uint32_t kTp = 0x40100000u;
				const bool down = (s.buttons & 0x00000C00u) == 0x00000C00u && (s.buttons & kTp) != 0;
				if (down && !prev_active)
				{
					res.fired = true;
					held_back |= kTp;
				}
				prev_active = down;
				held_back &= s.buttons;
				res.suppress_bits |= held_back;
				break;
			}
			case PT_TOUCHPAD_HOLD:
			case PT_OPTIONS_HOLD:
			{
				const uint32_t read_mask = (trigger == PT_TOUCHPAD_HOLD) ? 0x40100000u : 0x00000008u;
				const uint32_t pulse_mask = (trigger == PT_TOUCHPAD_HOLD) ? 0x00100000u : 0x00000008u;
				const bool down = (s.buttons & read_mask) != 0;
				if (down)
				{
					res.suppress_bits |= read_mask;
					if (!holding)
					{
						holding = true;
						hold_since_ms = now_ms;
						hold_fired = false;
					}
					else if (!hold_fired && (now_ms - hold_since_ms) >= kPauseHoldThresholdMs)
					{
						hold_fired = true;
						res.fired = true;
						pulse_bits = 0;
						pulse_until_ms = 0;
					}
				}
				else if (holding)
				{
					if (!hold_fired && !menu_open)
					{
						pulse_bits = pulse_mask;
						pulse_until_ms = now_ms + kPauseTapPulseMs;
					}
					holding = false;
					hold_fired = false;
				}
				if (pulse_bits != 0)
				{
					if (!menu_open && now_ms < pulse_until_ms)
						res.inject_bits |= pulse_bits;
					else
					{
						pulse_bits = 0;
						pulse_until_ms = 0;
					}
				}
				break;
			}
			default:
				break;
		}
		return res;
	}
};

// "L3+R3 + D-pad up", "the touchpad's click held 1.5 s", "nothing".
inline std::string DescribeCombo(const ComboButton (&b)[2], int hold_ms)
{
	std::string out;
	ComboButton seen = CB_NONE;
	for (ComboButton x : b)
	{
		if (x == CB_NONE || x >= CB_COUNT || x == seen)
			continue;
		out += (out.empty() ? "" : " + ") + std::string(ComboButtonLabel(x));
		seen = x;
	}
	if (out.empty())
		return "nothing";
	if (hold_ms > 0)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), " held %g s", hold_ms / 1000.0);
		out += buf;
	}
	return out;
}

// What the controller holds now (OrbisPadData's fields; the keyboard and mouse already added in).
struct State
{
	uint32_t buttons = 0;
	uint8_t l2 = 0, r2 = 0;
	uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
};

// What the PS2 controller gets: each input's value (0 released .. 1 fully pressed; the face buttons, the D-pad and the
// shoulders are pressure sensitive on a DualShock 2) and the sticks (0..255, 128 the middle).
struct Out
{
	float value[T_COUNT] = {};
	uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
};

// A trigger on anything but a trigger presses once it is pulled past an eighth (then as hard as it is pulled), so a
// finger resting on it presses nothing.
constexpr uint8_t kTriggerPress = 32;
// How far a stick is pushed (from the middle, of 128) before it presses the D-pad (Config::left_dpad).
constexpr int kStickDpad = 64;

inline uint8_t Invert(uint8_t v)
{
	return static_cast<uint8_t>(std::min(255, 256 - static_cast<int>(v)));
}

// vk-285-118: a stick through a round dead zone of `percent` (Config::deadzone_left/right).
inline void DeadZone(uint8_t& x, uint8_t& y, int percent)
{
	if (percent <= 0)
		return;
	const float dx = (static_cast<float>(x) - 128.0f) / 127.0f, dy = (static_cast<float>(y) - 128.0f) / 127.0f;
	const float r = std::sqrt(dx * dx + dy * dy), dz = static_cast<float>(percent) / 100.0f;
	if (r <= dz)
	{
		x = y = 128;
		return;
	}
	const float k = std::min(1.0f, (r - dz) / (1.0f - dz)) / r;
	auto axis = [&](float d) {
		const int v = static_cast<int>(std::lround(128.0f + d * k * 127.0f));
		return static_cast<uint8_t>(std::max(0, std::min(255, v)));
	};
	x = axis(dx);
	y = axis(dy);
}

inline Out Apply(const Config& c, const State& s)
{
	Out o;
	for (int src = 0; src < S_COUNT; src++)
	{
		const Target t = c.target[src];
		if (t >= T_NONE)
			continue;
		float v;
		if (src == S_L2 || src == S_R2)
		{
			const uint8_t raw = src == S_L2 ? s.l2 : s.r2;
			const bool to_trigger = t == T_L2 || t == T_R2;
			v = (to_trigger || raw >= kTriggerPress) ? raw / 255.0f * c.pressure[src] : 0.0f;
		}
		else
			v = (s.buttons & SourceAt(src).bits) ? c.pressure[src] : 0.0f;
		// PCSX2 reads the analog button and the pressure modifier as on or off (it acts when they change).
		if ((t == T_ANALOG || t == T_PRESSURE) && v > 0.0f)
			v = 1.0f;
		o.value[t] = std::max(o.value[t], v);
	}

	uint8_t lx = s.lx, ly = s.ly, rx = s.rx, ry = s.ry;
	DeadZone(lx, ly, c.deadzone_left);
	DeadZone(rx, ry, c.deadzone_right);
	if (c.swap_sticks)
	{
		std::swap(lx, rx);
		std::swap(ly, ry);
	}
	if (c.invert_left & 1)
		ly = Invert(ly);
	if (c.invert_left & 2)
		lx = Invert(lx);
	if (c.invert_right & 1)
		ry = Invert(ry);
	if (c.invert_right & 2)
		rx = Invert(rx);
	if (c.left_dpad != 0)
	{
		if (lx <= 128 - kStickDpad)
			o.value[T_LEFT] = 1.0f;
		if (lx >= 128 + kStickDpad)
			o.value[T_RIGHT] = 1.0f;
		if (ly <= 128 - kStickDpad)
			o.value[T_UP] = 1.0f;
		if (ly >= 128 + kStickDpad)
			o.value[T_DOWN] = 1.0f;
		if (c.left_dpad == 2)
			lx = ly = 128;
	}
	o.lx = lx;
	o.ly = ly;
	o.rx = rx;
	o.ry = ry;
	return o;
}

// For the log: "Cross presses Circle, Circle presses Cross; sticks swapped" or "as on the controller".
inline std::string Describe(const Config& c)
{
	std::string buttons, sticks;
	for (int s = 0; s < S_COUNT; s++)
	{
		const Target t = c.target[s];
		if (t == SourceAt(s).def && c.pressure[s] == 1.0f)
			continue;
		if (!buttons.empty())
			buttons += ", ";
		buttons += SourceAt(s).name;
		buttons += t == T_NONE ? std::string(" presses nothing") : std::string(" presses ") + TargetName(t);
		if (t != T_NONE && c.pressure[s] != 1.0f)
			buttons += " at " + std::to_string(static_cast<int>(c.pressure[s] * 100.0f + 0.5f)) + "%";
	}
	auto add = [&](const std::string& what) { sticks += (sticks.empty() ? "" : ", ") + what; };
	static const char* const axes[4] = {"", "up-down", "left-right", "up-down and left-right"};
	if (c.swap_sticks)
		add("sticks swapped");
	if (c.invert_left)
		add(std::string("left stick ") + axes[c.invert_left & 3] + " inverted");
	if (c.invert_right)
		add(std::string("right stick ") + axes[c.invert_right & 3] + " inverted");
	if (c.deadzone_left)
		add("left stick dead zone " + std::to_string(c.deadzone_left) + "%");
	if (c.deadzone_right)
		add("right stick dead zone " + std::to_string(c.deadzone_right) + "%");
	if (c.left_dpad == 1)
		add("left stick also the D-pad");
	if (c.left_dpad == 2)
		add("left stick as the D-pad only");
	// vk-285-117: the save and load combos, when they aren't the default.
	const Config def;
	std::string states;
	if (c.pause_trigger != def.pause_trigger)
		states = std::string("pause menu on ") + PauseTriggerLabel(c.pause_trigger);
	if (c.save[0] != def.save[0] || c.save[1] != def.save[1] || c.load[0] != def.load[0] || c.load[1] != def.load[1] ||
		c.hold_ms != def.hold_ms)
		states += (states.empty() ? "" : ", ") + std::string("save state on ") + DescribeCombo(c.save, c.hold_ms) + ", load on " + DescribeCombo(c.load, c.hold_ms);
	if (c.fast[0] != CB_NONE || c.fast[1] != CB_NONE)
		states += (states.empty() ? "" : ", ") + std::string("fast forward on ") + DescribeCombo(c.fast, c.hold_ms);
	std::string out = buttons;
	for (const std::string* part : {&sticks, &states})
		if (!part->empty())
			out += (out.empty() ? "" : "; ") + *part;
	return out.empty() ? "as on the controller" : out;
}
} // namespace orbis_padmap
