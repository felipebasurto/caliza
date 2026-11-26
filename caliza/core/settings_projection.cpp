#include "settings_projection.h"

namespace caliza::settings
{
namespace
{
SettingKind KindFor(const SettingDef& def)
{
	if (def.type == ValueType::Boolean)
		return SettingKind::Toggle;
	if (def.type == ValueType::Number)
		return SettingKind::Number;
	if (!def.choices.empty() || def.type == ValueType::Choice || def.type == ValueType::MemoryCardFile || def.type == ValueType::GroupList)
		return SettingKind::Choice;
	return SettingKind::Text;
}

bool RestartRequired(ApplyTiming timing)
{
	return timing == ApplyTiming::NextLaunch || timing == ApplyTiming::NextApplicationStart || timing == ApplyTiming::NextMenuOpen;
}

bool IncludeRow(const SettingDef& def, const ProjectionOptions& options)
{
	if (def.presentation == Presentation::Hidden || !HasScope(def.scopes, options.scope))
		return false;
	if (!options.include_composite_memory_cards && def.presentation == Presentation::Composite)
		return false;
	return true;
}
} // namespace

std::vector<SettingCategory> ProjectSettings(const ProjectionOptions& options)
{
	std::vector<SettingCategory> out;
	for (const GroupDef& group : Groups())
	{
		if (!HasScope(group.scopes, options.scope))
			continue;
		SettingCategory category;
		category.id = group.id;
		category.label = group.title;
		for (const SettingDef* def_ptr : SettingsInGroup(group.id))
		{
			const SettingDef& def = *def_ptr;
			if (!IncludeRow(def, options))
				continue;
			SettingItem item;
			item.id = def.compatibility_key;
			item.label = def.label;
			item.description = def.hint;
			item.kind = KindFor(def);
			item.restart_required = RestartRequired(def.apply_timing);
			const auto value = options.values.find(def.compatibility_key);
			const auto base = options.base_values.find(def.compatibility_key);
			item.value = value != options.values.end() ? value->second : base != options.base_values.end() ? base->second : def.default_value;
			item.modified = (value != options.values.end() || base != options.base_values.end()) && !SameSettingValue(def, item.value, def.default_value);
			item.game_scoped = HasScope(def.scopes, ScopeMask::Game);
			for (const Choice& choice : def.choices)
				item.choices.push_back({choice.value, choice.label});
			category.items.push_back(std::move(item));
		}
		if (!category.items.empty())
			out.push_back(std::move(category));
	}
	return out;
}

size_t VisibleProjectedSettingCount(ScopeMask scope)
{
	ProjectionOptions options;
	options.scope = scope;
	size_t count = 0;
	for (const SettingCategory& category : ProjectSettings(options))
		count += category.items.size();
	return count;
}
} // namespace caliza::settings
