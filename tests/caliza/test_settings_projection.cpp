#include "caliza/core/settings_projection.h"

#include <iostream>
#include <set>
#include <string>

using caliza::SettingCategory;
using caliza::SettingItem;
using caliza::settings::ProjectionOptions;
using caliza::settings::ProjectSettings;
using caliza::settings::ScopeMask;
using caliza::settings::VisibleProjectedSettingCount;

static bool Fail(const std::string& message)
{
	std::cerr << "FAIL: " << message << "\n";
	return false;
}

int main()
{
	ProjectionOptions options;
	options.scope = ScopeMask::Global;
	const std::vector<SettingCategory> global = ProjectSettings(options);
	if (global.empty()) return Fail("global projection is empty") ? 0 : 1;
	size_t count = 0;
	std::set<std::string> keys;
	bool found_renderer = false;
	bool found_accessibility = false;
	bool found_memory_composite = false;
	for (const SettingCategory& category : global)
	{
		count += category.items.size();
		for (const SettingItem& item : category.items)
		{
			if (!keys.insert(item.id).second) return Fail("duplicate projected key " + item.id) ? 0 : 1;
			if (item.id == "Caliza/Renderer" && item.kind == caliza::SettingKind::Choice && !item.choices.empty()) found_renderer = true;
			if (item.id == "Caliza/UiTextScalePercent" && item.value == "100") found_accessibility = true;
			if (item.id == "MemoryCards/Slot1_Filename") found_memory_composite = true;
		}
	}
	if (!found_renderer) return Fail("renderer choice projection missing") ? 0 : 1;
	if (!found_accessibility) return Fail("accessibility projection missing") ? 0 : 1;
	if (found_memory_composite) return Fail("memory-card composite leaked into normal settings") ? 0 : 1;
	if (count != VisibleProjectedSettingCount(ScopeMask::Global)) return Fail("count helper mismatch") ? 0 : 1;

	ProjectionOptions with_cards;
	with_cards.scope = ScopeMask::Global;
	with_cards.include_composite_memory_cards = true;
	size_t with_card_count = 0;
	bool has_card = false;
	for (const SettingCategory& category : ProjectSettings(with_cards))
	{
		with_card_count += category.items.size();
		for (const SettingItem& item : category.items)
			has_card = has_card || item.id == "MemoryCards/Slot1_Filename";
	}
	if (!has_card || with_card_count <= count) return Fail("optional memory-card composite projection missing") ? 0 : 1;

	std::cout << "settings projection tests passed (" << count << " global rows)\n";
	return 0;
}
