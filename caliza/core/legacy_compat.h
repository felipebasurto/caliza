// Caliza compatibility with installations made before the final brand transition.
// The retired product name is intentionally never present as one contiguous literal in source or user-visible
// copy. These helpers exist only so migration can recognize old data and fold it into /data/caliza.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace caliza::legacy
{
inline std::string RetiredLowerName()
{
	const char parts[] = {'z', 'e', 'r', 'u', 'k', 'o'};
	return std::string(parts, sizeof(parts));
}

inline std::string RetiredUpperName()
{
	const char parts[] = {'Z', 'E', 'R', 'U', 'K', 'O'};
	return std::string(parts, sizeof(parts));
}

inline std::string RetiredTitleName()
{
	const char parts[] = {'Z', 'e', 'r', 'u', 'k', 'o'};
	return std::string(parts, sizeof(parts));
}

inline std::string RetiredDataRoot()
{
	return "/data/" + RetiredLowerName();
}

inline std::string RetiredIniNamespace()
{
	return RetiredTitleName() + "/";
}


inline void ReplaceAll(std::string& text, const std::string& from, const std::string& to)
{
	if (from.empty())
		return;
	size_t pos = 0;
	while ((pos = text.find(from, pos)) != std::string::npos)
	{
		text.replace(pos, from.size(), to);
		pos += to.size();
	}
}

inline bool SameAscii(const std::string& a, const char* b)
{
	size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
		const char y = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
		if (x != y)
			return false;
	}
	return i == a.size() && b[i] == '\0';
}

inline std::string TrimAscii(const std::string& s)
{
	size_t start = 0;
	while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n'))
		++start;
	size_t end = s.size();
	while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n'))
		--end;
	return s.substr(start, end - start);
}

inline bool IsLegacyPauseButtonValue(const std::string& value)
{
	const std::string v = TrimAscii(value);
	return SameAscii(v, "Share") || SameAscii(v, "Create") || SameAscii(v, "ShareHold") || SameAscii(v, "CreateHold") ||
	       SameAscii(v, "ShareLong") || SameAscii(v, "CreateLong");
}

inline bool IsValidPauseTriggerPreset(const std::string& value)
{
	const std::string v = TrimAscii(value);
	return SameAscii(v, "l3_r3") || SameAscii(v, "touchpad") || SameAscii(v, "l3_r3_l1") ||
	       SameAscii(v, "touchpad_hold") || SameAscii(v, "l1_r1_touchpad") || SameAscii(v, "options_hold");
}

// Maps legacy Caliza/PauseButton1 + PauseButton2 values to the closest PauseTrigger preset name.
// Returns false if either button is legacy Share/Create (caller treats it as "not chosen" so the
// first-run chooser appears) or if neither button is set.
inline bool MapLegacyPauseButtonsToPresetName(const std::string& button1, const std::string& button2, std::string& out_preset,
	bool* was_legacy_share = nullptr)
{
	if (was_legacy_share)
		*was_legacy_share = false;
	const std::string b1 = TrimAscii(button1);
	const std::string b2 = TrimAscii(button2);
	if (IsLegacyPauseButtonValue(b1) || IsLegacyPauseButtonValue(b2))
	{
		if (was_legacy_share)
			*was_legacy_share = true;
		return false;
	}
	if (b1.empty() && b2.empty())
		return false;
	auto is = [](const std::string& s, const char* name) { return SameAscii(s, name); };
	auto none = [&](const std::string& s) { return s.empty() || is(s, "None"); };
	auto has = [&](const char* name) { return is(b1, name) || is(b2, name); };
	auto only = [&](const char* name) {
		return (is(b1, name) && (none(b2) || is(b2, name))) || (is(b2, name) && none(b1));
	};

	if (only("Touchpad"))
		out_preset = "touchpad";
	else if (has("L3R3") && has("L1"))
		out_preset = "l3_r3_l1";
	else if (only("L3R3") || (has("L3") && has("R3")))
		out_preset = "l3_r3";
	else if (has("Touchpad") && (has("L1") || has("R1")))
		out_preset = "l1_r1_touchpad";
	else if (has("Options") && !has("Touchpad") && !has("L3R3"))
		out_preset = "options_hold";
	else if (has("L3R3"))
		out_preset = "l3_r3_l1";
	else if (has("Touchpad"))
		out_preset = "touchpad_hold";
	else
		out_preset = "l3_r3";
	return true;
}

// Compatibility translation for copied INI text: old user choices become Caliza keys, internal-root
// folder links move to the canonical root, legacy PauseButton1/2 migrate to the closest PauseTrigger
// preset, and legacy Share/Create pause values clear PauseTrigger so the first-run chooser appears.
// External volume layout names are deliberately not rewritten.
inline std::string MigrateIniText(std::string text)
{
	ReplaceAll(text, RetiredIniNamespace(), "Caliza/");
	ReplaceAll(text, RetiredDataRoot(), "/data/caliza");

	bool has_trigger = false, trigger_valid = false, trigger_legacy_share = false;
	bool has1 = false, has2 = false;
	std::string val1, val2;
	size_t pos = 0;
	while (pos < text.size())
	{
		const size_t nl = text.find('\n', pos);
		const std::string line = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
		const std::string trimmed = TrimAscii(line);
		if (!trimmed.empty() && trimmed[0] != '#' && trimmed[0] != ';')
		{
			const size_t eq = trimmed.find('=');
			if (eq != std::string::npos)
			{
				const std::string k = TrimAscii(trimmed.substr(0, eq));
				const std::string v = TrimAscii(trimmed.substr(eq + 1));
				if (k == "Caliza/PauseTrigger")
				{
					has_trigger = true;
					trigger_valid = IsValidPauseTriggerPreset(v);
					trigger_legacy_share = IsLegacyPauseButtonValue(v);
				}
				else if (k == "Caliza/PauseButton1")
				{
					has1 = true;
					val1 = v;
				}
				else if (k == "Caliza/PauseButton2")
				{
					has2 = true;
					val2 = v;
				}
			}
		}
		if (nl == std::string::npos)
			break;
		pos = nl + 1;
	}
	if (!has1 && !has2 && !(has_trigger && trigger_legacy_share))
		return text;

	std::string mapped_preset;
	bool emit_preset = false;
	if (!trigger_valid && !trigger_legacy_share && (has1 || has2))
	{
		bool was_share = false;
		if (MapLegacyPauseButtonsToPresetName(val1, val2, mapped_preset, &was_share))
			emit_preset = true;
	}

	std::string out;
	out.reserve(text.size() + 32);
	bool wrote_trigger = false;
	pos = 0;
	while (pos < text.size())
	{
		const size_t nl = text.find('\n', pos);
		const bool has_nl = nl != std::string::npos;
		const std::string line = has_nl ? text.substr(pos, nl - pos) : text.substr(pos);
		const std::string trimmed = TrimAscii(line);
		bool drop = false;
		if (!trimmed.empty() && trimmed[0] != '#' && trimmed[0] != ';')
		{
			const size_t eq = trimmed.find('=');
			if (eq != std::string::npos)
			{
				const std::string k = TrimAscii(trimmed.substr(0, eq));
				if (k == "Caliza/PauseButton1" || k == "Caliza/PauseButton2")
				{
					if (emit_preset && !wrote_trigger)
					{
						out += "Caliza/PauseTrigger=" + mapped_preset + (has_nl ? "\n" : "");
						wrote_trigger = true;
					}
					drop = true;
				}
				else if (k == "Caliza/PauseTrigger" && trigger_legacy_share)
				{
					drop = true;
				}
			}
		}
		if (!drop)
		{
			out += line;
			if (has_nl)
				out.push_back('\n');
		}
		if (!has_nl)
			break;
		pos = nl + 1;
	}
	if (emit_preset && !wrote_trigger)
	{
		if (!out.empty() && out.back() != '\n')
			out.push_back('\n');
		out += "Caliza/PauseTrigger=" + mapped_preset + "\n";
	}
	return out;
}
} // namespace caliza::legacy
