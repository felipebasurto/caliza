// PS5 port frontend: see fe_modern_controls.h.
//
// Copyright (C) 2026 swordpdf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_modern_controls.h"

namespace fe
{
namespace
{
std::string Lower(const std::string& value)
{
	std::string out = value;
	for (char& c : out)
		c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	return out;
}
} // namespace

bool IsModernControlsGroup(const std::string& name)
{
	const std::string lower = Lower(name);
	return lower.find("modern") != std::string::npos && lower.find("control") != std::string::npos;
}
} // namespace fe
