// Caliza: schema-to-contract projection shared by native and host backends.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "contract.h"
#include "settings_schema.h"

#include <map>
#include <string>
#include <vector>

namespace caliza::settings
{
struct ProjectionOptions
{
	ScopeMask scope = ScopeMask::Global;
	bool include_composite_memory_cards = false;
	// Values are explicit canonical scalar values keyed by compatibility key for this layer. Base values are the
	// inherited global layer for a game projection; missing values use base values and then schema defaults.
	std::map<std::string, std::string> values;
	std::map<std::string, std::string> base_values;
};

// Projects every visible schema row into immutable contract categories. Backend adapters must use this rather
// than carrying a second hard-coded native settings table. Dynamic-choice rows keep their static schema choices;
// adapters may replace the value/choices afterwards when live BIOS, game, patch or memory-card state is available.
std::vector<SettingCategory> ProjectSettings(const ProjectionOptions& options);

// Returns the number of visible non-composite rows a complete settings surface must expose for a scope.
size_t VisibleProjectedSettingCount(ScopeMask scope);
} // namespace caliza::settings
