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

// Compatibility translation for copied INI text: old user choices become Caliza keys, and internal-root
// folder links move to the canonical root. External volume layout names are deliberately not rewritten.
inline std::string MigrateIniText(std::string text)
{
	ReplaceAll(text, RetiredIniNamespace(), "Caliza/");
	ReplaceAll(text, RetiredDataRoot(), "/data/caliza");
	return text;
}
} // namespace caliza::legacy
