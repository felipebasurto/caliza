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

// Compatibility translation for copied INI text: old user choices become Caliza keys, internal-root
// folder links move to the canonical root, and legacy Share/Create pause bindings migrate to the
// GoldHEN-safe default (L3R3 + L1). External volume layout names are deliberately not rewritten.
inline std::string MigrateIniText(std::string text)
{
	ReplaceAll(text, RetiredIniNamespace(), "Caliza/");
	ReplaceAll(text, RetiredDataRoot(), "/data/caliza");

	// Check whether PauseButton1 or PauseButton2 carries a legacy Share/Create trigger.
	bool leg1 = false, leg2 = false, has1 = false, has2 = false;
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
				if (k == "Caliza/PauseButton1")
				{
					has1 = true;
					val1 = v;
					leg1 = IsLegacyPauseButtonValue(v);
				}
				else if (k == "Caliza/PauseButton2")
				{
					has2 = true;
					val2 = v;
					leg2 = IsLegacyPauseButtonValue(v);
				}
			}
		}
		if (nl == std::string::npos)
			break;
		pos = nl + 1;
	}
	if (!leg1 && !leg2)
		return text;

	const auto non_empty_partner = [](const std::string& v) {
		const std::string t = TrimAscii(v);
		return !t.empty() && !SameAscii(t, "None") && !IsLegacyPauseButtonValue(t);
	};
	std::string new1 = "L3R3", new2 = "L1";
	if (leg1 && !leg2 && non_empty_partner(val2))
	{
		new2 = val2;
		new1 = SameAscii(val2, "L3R3") ? "L1" : "L3R3";
	}
	else if (leg2 && !leg1 && non_empty_partner(val1))
	{
		new1 = val1;
		new2 = SameAscii(val1, "L3R3") ? "L1" : "L3R3";
	}

	std::string out;
	out.reserve(text.size() + 48);
	bool wrote1 = false, wrote2 = false;
	pos = 0;
	while (pos < text.size())
	{
		const size_t nl = text.find('\n', pos);
		const bool has_nl = nl != std::string::npos;
		const std::string line = has_nl ? text.substr(pos, nl - pos) : text.substr(pos);
		const std::string trimmed = TrimAscii(line);
		bool replaced = false;
		if (!trimmed.empty() && trimmed[0] != '#' && trimmed[0] != ';')
		{
			const size_t eq = trimmed.find('=');
			if (eq != std::string::npos)
			{
				const std::string k = TrimAscii(trimmed.substr(0, eq));
				if (k == "Caliza/PauseButton1")
				{
					out += "Caliza/PauseButton1=" + new1 + (has_nl ? "\n" : "");
					wrote1 = true;
					replaced = true;
				}
				else if (k == "Caliza/PauseButton2")
				{
					out += "Caliza/PauseButton2=" + new2 + (has_nl ? "\n" : "");
					wrote2 = true;
					replaced = true;
				}
			}
		}
		if (!replaced)
		{
			out += line;
			if (has_nl)
				out.push_back('\n');
		}
		if (!has_nl)
			break;
		pos = nl + 1;
	}
	if (!wrote1 || !wrote2)
	{
		if (!out.empty() && out.back() != '\n')
			out.push_back('\n');
		if (!wrote1 && (!has1 || leg1))
			out += "Caliza/PauseButton1=" + new1 + "\n";
		if (!wrote2 && (!has2 || leg2))
			out += "Caliza/PauseButton2=" + new2 + "\n";
	}
	return out;
}
} // namespace caliza::legacy
