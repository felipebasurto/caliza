// Caliza PS4: the canonical user-facing settings and action schema.
// Native UI, host tests and the companion web UI should render from this registry instead of
// keeping parallel hard-coded tables. Active compatibility ini keys are canonical Caliza/PCSX2 keys;
// pre-transition brand keys exist only in the explicit migration map, never as active schema keys.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace caliza::settings
{
constexpr uint32_t kSchemaVersion = 1;

enum class ScopeMask : uint32_t
{
	None = 0,
	Global = 1u << 0,
	Game = 1u << 1,
	RunningVm = 1u << 2,
};
constexpr ScopeMask operator|(ScopeMask a, ScopeMask b)
{
	return static_cast<ScopeMask>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool HasScope(ScopeMask value, ScopeMask wanted)
{
	return (static_cast<uint32_t>(value) & static_cast<uint32_t>(wanted)) != 0;
}

enum class UiTab
{
	Settings,
	Controls,
	Achievements,
	System,
};

enum class ValueType
{
	Boolean,
	Choice,
	Text,
	PathList,
	Number,
	MemoryCardFile,
	GroupList,
};

enum class Presentation
{
	Toggle,
	Segmented,
	ChoiceList,
	Text,
	Composite, // rendered together with another row (a memory-card slot, for example)
	Hidden,    // implementation detail coordinated by another row/action
};

enum class Inheritance
{
	Fixed,         // no inherited layer; only this file/default applies
	InheritGlobal, // a game's value follows gs.ini before the schema default
};

enum class ApplyTiming
{
	Live,                  // applied by the running emulation/live UI poll
	NextLaunch,            // read when a game/ELF/system menu boots
	NextApplicationStart,  // read while the application scans/mounts at startup
	NextMenuOpen,          // applied when the library/menu is opened again
	ImmediateUi,           // presentation-local and immediate
	RunningCommand,        // not a stored emulation setting: a command to the running VM
};

enum class Confirmation
{
	None,
	TwoPress,
};

struct Choice
{
	std::string value;
	std::string label;
};

struct GroupDef
{
	std::string id;
	std::string title;
	UiTab tab = UiTab::Settings;
	ScopeMask scopes = ScopeMask::Global | ScopeMask::Game;
	std::string hint;
	int order = 0;
};

struct SettingDef
{
	std::string id;                // stable cross-interface id, independent of storage spelling
	std::string compatibility_key; // exact active ini key; empty only for a non-ini/runtime-only value
	std::string label;
	std::string group_id;
	UiTab tab = UiTab::Settings;
	ScopeMask scopes = ScopeMask::Global | ScopeMask::Game;
	Inheritance inheritance = Inheritance::InheritGlobal;
	ValueType type = ValueType::Choice;
	Presentation presentation = Presentation::Segmented;
	ApplyTiming apply_timing = ApplyTiming::Live;
	std::string default_value;
	std::vector<Choice> choices;
	std::string hint;
	std::string short_format; // recommended-settings summary, with % replaced by the value label
	std::string later_label;  // legacy web copy used in "applies at next launch" messages
	std::string placeholder;
	int columns = 0;
	double number_min = 0.0;
	double number_max = 0.0;
	double number_step = 0.0;
	bool allow_unlisted = false;          // preserve/display a value not in choices
	bool dynamic_choices = false;         // choices come from catalog/BIOS/card state at runtime
	bool manual_hardware_fix = false;     // one of PCSX2's UserHacks rows
	bool manual_hardware_fix_master = false; // the UserHacks master switch
	bool player_owned = false;            // preserved by ApplyRecommended and excluded from tuning comparison
	bool implementation_detail = false;   // coordinated by composite rows; normally not rendered alone
	bool list_key = false;                // repeated ini key (patch/cheat group lists)
	std::string pair_id;                  // two controls rendered as one combo pair
	int pair_part = 0;                    // 1 or 2 within pair_id
	std::string note;                     // integration caveat, intentionally not user-facing copy
};

struct LegacyKeyMigration
{
	std::string legacy_key;    // constructed at runtime by legacy_compat.h; never an active schema key
	std::string canonical_key; // active compatibility_key after migration
};

enum class ActionDomain
{
	Library,
	Settings,
	Storage,
	Patches,
	Textures,
	Runtime,
	Achievements,
	Diagnostics,
	Accessibility,
	Presentation,
};

struct ActionDef
{
	std::string id;
	std::string label;
	ActionDomain domain = ActionDomain::Settings;
	ScopeMask scopes = ScopeMask::Global | ScopeMask::Game;
	std::string description;
	std::string command_type; // caliza::CommandType name when the backend contract has one; empty for UI-local actions
	std::string payload;      // compact payload contract, human-readable but stable
	Confirmation confirmation = Confirmation::None;
	double confirm_seconds = 0.0;
	bool destructive = false;
	bool requires_running_vm = false;
	bool requires_game = false;
	bool requires_elf = false;
	bool global_only = false;
	bool game_only = false;
	bool long_running = false;
	bool writes_settings = false;
	bool ui_local = false;
};

const std::vector<GroupDef>& Groups();
const std::vector<SettingDef>& Settings();
const std::vector<ActionDef>& Actions();

const GroupDef* FindGroup(const std::string& id);
const SettingDef* FindSetting(const std::string& id);
const SettingDef* FindSettingByKey(std::string compatibility_key);
const ActionDef* FindAction(const std::string& id);

// Explicit migration map for installations written before the final brand transition. The retired
// namespace is constructed at runtime by legacy_compat.h. These keys are migration input only:
// FindSettingByKey, ParseSettingsDocument and mutations operate on canonical keys and do not migrate
// implicitly. MigrateLegacyKey returns the canonical key for a mapped legacy key and otherwise returns
// the canonicalized input unchanged, preserving unknown settings for the backend writer/migrator. Use
// FindSettingByKey(MigrateLegacyKey(key)) when a migration adapter needs the active definition.
const std::vector<LegacyKeyMigration>& LegacyKeyMigrations();
std::string MigrateLegacyKey(std::string key);
std::vector<const SettingDef*> SettingsInGroup(const std::string& group_id);
std::vector<const SettingDef*> SettingsForScope(ScopeMask scope, UiTab tab);

// ---- Ini/mutation compatibility helpers -------------------------------------------------------
// These mirror the existing fe_settings.cpp wire format: a bare key belongs to EmuCore/GS unless it
// carries an explicit section; Patches/Enable and Cheats/Enable are repeated list keys.
std::string Trim(const std::string& text);
std::string CanonKey(std::string key);
bool IsListKey(const std::string& key);
bool SafeKey(const std::string& key);
bool SafeValue(const std::string& value);
bool Truthy(const std::string& value);

// Folder/NFS list compatibility: items split on semicolon, pipe or newline; edge whitespace and trailing
// slashes are removed, duplicates keep their first position, and joining uses semicolons.
std::vector<std::string> SplitFolderList(const std::string& list);
std::string JoinFolderList(const std::vector<std::string>& items);

struct ValidationResult
{
	bool ok = false;
	std::string normalized;
	std::string error;
};
ValidationResult ValidateValue(const SettingDef& def, const std::string& value);
bool SameSettingValue(const SettingDef& def, const std::string& a, const std::string& b);

struct DocumentState
{
	std::vector<std::pair<std::string, std::string>> values; // last occurrence of a scalar key wins
	std::vector<std::string> patches;                        // unique Patches/Enable values, first-seen order
	std::vector<std::string> cheats;                         // unique Cheats/Enable values, first-seen order
};
DocumentState ParseSettingsDocument(const std::string& text);
std::string SerializeSettingsDocument(const DocumentState& state);
const std::string* FindValue(const DocumentState& state, const std::string& key);
std::string EffectiveValue(const SettingDef& def, const DocumentState& own, const DocumentState& global, bool* inherited = nullptr);

struct Mutation
{
	enum class Kind
	{
		Set,
		Unset,
		PatchOn,
		PatchOff,
		CheatOn,
		CheatOff,
		ApplyRecommended,
	};
	Kind kind = Kind::Set;
	std::string key;
	std::string value;
};
bool ParseMutationLine(const std::string& line, Mutation& out, std::string* error = nullptr);
bool ParseMutationBatch(const std::string& body, std::vector<Mutation>& out, std::string* error = nullptr);
std::string SerializeMutation(const Mutation& mutation);

// Registry snapshot for web/native adapters and tests. It is intentionally schema-shaped JSON, not a
// promise to accept arbitrary JSON back; ParseSettingsDocument/ParseMutationBatch are the compatibility parsers.
std::string JsonEscape(const std::string& text);
std::string RegistryToJson(bool include_hints = true);
std::string ActionsToJson(bool include_descriptions = true);
} // namespace caliza::settings
