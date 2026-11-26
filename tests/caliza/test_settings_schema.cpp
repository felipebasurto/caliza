// Caliza PS4 schema inventory tests. Self-contained: build settings_schema.cpp and catalog_model.cpp with this file.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../caliza/core/settings_schema.h"
#include "../../caliza/core/catalog_model.h"
#include "../../caliza/core/legacy_compat.h"

#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void Check(bool condition, const char* expression, int line)
{
	if (!condition)
	{
		std::cerr << "FAIL line " << line << ": " << expression << "\n";
		++failures;
	}
}

#define CHECK(x) Check((x), #x, __LINE__)

const char* const kCompatibilityKeys[] = {
	"Caliza/GameFolders",
	"Caliza/BezelDir",
	"Caliza/NfsShares",
	"Caliza/BiosFolder",
	"Caliza/HideGame",
	"Caliza/ShowHiddenGames",
	"Caliza/CheatDiscStart",
	"upscale_multiplier",
	"AspectRatio",
	"EmuCore/EnableWideScreenPatches",
	"TVShader",
	"Caliza/FrameGeneration",
	"Caliza/Renderer",
	"filter",
	"MaxAnisotropy",
	"accurate_blending_unit",
	"hw_mipmap",
	"LoadTextureReplacements",
	"CASMode",
	"Caliza/TexturesDir",
	"EmuCore/Speedhacks/EECycleRate",
	"EmuCore/Speedhacks/EECycleSkip",
	"EmuCore/Speedhacks/vuThread",
	"EmuCore/Speedhacks/OrbisMTVUSpin",
	"EmuCore/Speedhacks/OrbisMTVUBatch",
	"EmuCore/Speedhacks/OrbisMTVURingKB",
	"HWDownloadMode",
	"Caliza/GameLanguage",
	"DEV9/Eth/EthEnable",
	"EmuCore/HostFs",
	"EmuCore/EnableFastBoot",
	"Caliza/Overlay",
	"Caliza/FpsGraph",
	"Caliza/Bezel",
	"Caliza/RAChallengeIcons",
	"UserHacks",
	"UserHacks_HalfPixelOffset",
	"UserHacks_native_scaling",
	"UserHacks_round_sprite_offset",
	"UserHacks_align_sprite_X",
	"UserHacks_merge_pp_sprite",
	"UserHacks_BilinearHack",
	"UserHacks_TCOffsetX",
	"UserHacks_TCOffsetY",
	"UserHacks_AutoFlushLevel",
	"UserHacks_TextureInsideRt",
	"CropLeft",
	"CropTop",
	"CropRight",
	"CropBottom",
	"Caliza/SaveButton1",
	"Caliza/SaveButton2",
	"Caliza/LoadButton1",
	"Caliza/LoadButton2",
	"Caliza/StateHold",
	"Caliza/FastButton1",
	"Caliza/FastButton2",
	"Caliza/FastSpeed",
	"Caliza/DiscButton1",
	"Caliza/DiscButton2",
	"Caliza/ButtonCross",
	"Caliza/ButtonCircle",
	"Caliza/ButtonSquare",
	"Caliza/ButtonTriangle",
	"Caliza/ButtonL1",
	"Caliza/ButtonR1",
	"Caliza/ButtonL2",
	"Caliza/ButtonR2",
	"Caliza/ButtonL3",
	"Caliza/ButtonR3",
	"Caliza/ButtonOptions",
	"Caliza/ButtonTouchpad",
	"Caliza/ButtonUp",
	"Caliza/ButtonDown",
	"Caliza/ButtonLeft",
	"Caliza/ButtonRight",
	"Caliza/ButtonCrossPressure",
	"Caliza/ButtonCirclePressure",
	"Caliza/ButtonSquarePressure",
	"Caliza/ButtonTrianglePressure",
	"Caliza/ButtonL1Pressure",
	"Caliza/ButtonR1Pressure",
	"Caliza/ButtonL2Pressure",
	"Caliza/ButtonR2Pressure",
	"Caliza/ButtonTouchpadPressure",
	"Caliza/ButtonUpPressure",
	"Caliza/ButtonDownPressure",
	"Caliza/ButtonLeftPressure",
	"Caliza/ButtonRightPressure",
	"Caliza/SwapSticks",
	"Caliza/InvertLeft",
	"Caliza/InvertRight",
	"Caliza/LeftStickDpad",
	"Caliza/DeadzoneLeft",
	"Caliza/DeadzoneRight",
	"Caliza/Rumble",
	"Caliza/RumbleStrength",
	"Caliza/Multitap",
	"Caliza/KeyboardMouse",
	"Caliza/MouseAim",
	"Caliza/MouseSpeed",
	"Caliza/MouseButtons",
	"Caliza/ElfDisc",
	"Caliza/CheatDisc",
	"Filenames/BIOS",
	"MemoryCards/Slot1_Filename",
	"MemoryCards/Slot1_Enable",
	"MemoryCards/Slot2_Filename",
	"MemoryCards/Slot2_Enable",
	"Patches/Enable",
	"Cheats/Enable",
	"EmuCore/EnableCheats",
	"Caliza/UiTextScalePercent",
	"Caliza/UiHighContrast",
	"Caliza/UiReduceMotion",
	"Caliza/UiLargeFocus",
};

void HashText(uint64_t& hash, const std::string& text)
{
	for (const char raw : text)
	{
		const unsigned char c = static_cast<unsigned char>(raw);
		hash ^= c;
		hash *= 1099511628211ull;
	}
	hash ^= 0xff;
	hash *= 1099511628211ull;
}

void HashU64(uint64_t& hash, uint64_t value)
{
	for (int i = 0; i < 8; ++i)
	{
		hash ^= (value >> (i * 8)) & 0xff;
		hash *= 1099511628211ull;
	}
}

void HashDouble(uint64_t& hash, double value)
{
	uint64_t bits = 0;
	static_assert(sizeof(bits) == sizeof(value), "double size");
	std::memcpy(&bits, &value, sizeof(bits));
	HashU64(hash, bits);
}

uint64_t SettingsInventoryHash()
{
	using namespace caliza::settings;
	uint64_t hash = 1469598103934665603ull;
	for (const SettingDef& def : Settings())
	{
		HashText(hash, def.id);
		HashText(hash, def.compatibility_key);
		HashText(hash, def.label);
		HashText(hash, def.group_id);
		HashU64(hash, static_cast<uint64_t>(def.tab));
		HashU64(hash, static_cast<uint64_t>(def.scopes));
		HashU64(hash, static_cast<uint64_t>(def.inheritance));
		HashU64(hash, static_cast<uint64_t>(def.type));
		HashU64(hash, static_cast<uint64_t>(def.presentation));
		HashU64(hash, static_cast<uint64_t>(def.apply_timing));
		HashText(hash, def.default_value);
		HashText(hash, def.hint);
		HashText(hash, def.short_format);
		HashText(hash, def.later_label);
		HashText(hash, def.placeholder);
		HashU64(hash, static_cast<uint64_t>(def.columns));
		HashDouble(hash, def.number_min);
		HashDouble(hash, def.number_max);
		HashDouble(hash, def.number_step);
		HashU64(hash, def.allow_unlisted);
		HashU64(hash, def.dynamic_choices);
		HashU64(hash, def.manual_hardware_fix);
		HashU64(hash, def.manual_hardware_fix_master);
		HashU64(hash, def.player_owned);
		HashU64(hash, def.implementation_detail);
		HashU64(hash, def.list_key);
		HashText(hash, def.pair_id);
		HashU64(hash, static_cast<uint64_t>(def.pair_part));
		HashText(hash, def.note);
		for (const Choice& choice : def.choices)
		{
			HashText(hash, choice.value);
			HashText(hash, choice.label);
		}
	}
	return hash;
}

uint64_t ActionsInventoryHash()
{
	using namespace caliza::settings;
	uint64_t hash = 1469598103934665603ull;
	for (const ActionDef& action : Actions())
	{
		HashText(hash, action.id);
		HashText(hash, action.label);
		HashU64(hash, static_cast<uint64_t>(action.domain));
		HashU64(hash, static_cast<uint64_t>(action.scopes));
		HashText(hash, action.description);
		HashText(hash, action.command_type);
		HashText(hash, action.payload);
		HashU64(hash, static_cast<uint64_t>(action.confirmation));
		HashDouble(hash, action.confirm_seconds);
		HashU64(hash, action.destructive);
		HashU64(hash, action.requires_running_vm);
		HashU64(hash, action.requires_game);
		HashU64(hash, action.requires_elf);
		HashU64(hash, action.global_only);
		HashU64(hash, action.game_only);
		HashU64(hash, action.long_running);
		HashU64(hash, action.writes_settings);
		HashU64(hash, action.ui_local);
	}
	return hash;
}

void CheckHash(uint64_t actual, uint64_t expected, const char* name)
{
	if (actual != expected)
	{
		std::cerr << "FAIL " << name << " inventory hash: got " << std::hex << actual << ", expected " << expected << std::dec << "\n";
		++failures;
	}
}

void TestInventory()
{
	using namespace caliza::settings;
	CHECK(kSchemaVersion == 1);
	CHECK(Groups().size() == 21);
	CHECK(Settings().size() == 116);
	CHECK(Actions().size() == 48);
	CheckHash(SettingsInventoryHash(), 0xead6961695e891f9ull, "settings");
	CheckHash(ActionsInventoryHash(), 0x8578c0967cd24a83ull, "actions");
	CHECK(sizeof(kCompatibilityKeys) / sizeof(kCompatibilityKeys[0]) == Settings().size());

	std::set<std::string> ids, keys, pinned;
	for (const char* key : kCompatibilityKeys)
		pinned.insert(key);
	for (const SettingDef& def : Settings())
	{
		CHECK(ids.insert(def.id).second);
		CHECK(keys.insert(def.compatibility_key).second);
		CHECK(pinned.count(def.compatibility_key) == 1);
		CHECK(FindGroup(def.group_id) != nullptr);
		CHECK(!def.label.empty());
		CHECK(def.scopes != ScopeMask::None);
		if (!def.choices.empty() && !def.list_key)
		{
			const ValidationResult valid_default = ValidateValue(def, def.default_value);
			CHECK(valid_default.ok);
		}
	}
	CHECK(ids.size() == Settings().size());
	CHECK(keys.size() == Settings().size());

	const SettingDef* resolution = FindSettingByKey("upscale_multiplier");
	CHECK(resolution != nullptr);
	if (resolution)
	{
		CHECK(resolution->default_value == "1");
		CHECK(resolution->choices.size() == 7);
		CHECK(resolution->apply_timing == ApplyTiming::Live);
		CHECK(HasScope(resolution->scopes, ScopeMask::Global) && HasScope(resolution->scopes, ScopeMask::Game));
	}

	const SettingDef* crop = FindSettingByKey("CropLeft");
	CHECK(crop != nullptr);
	if (crop)
	{
		CHECK(crop->choices.size() == 10);
		CHECK(crop->default_value == "0");
	}

	const SettingDef* ee = FindSettingByKey("EmuCore/Speedhacks/EECycleRate");
	CHECK(ee != nullptr);
	if (ee)
	{
		CHECK(ee->default_value == "0");
		CHECK(ee->choices.size() == 7);
	}

	const SettingDef* renderer = FindSettingByKey("Caliza/Renderer");
	CHECK(renderer != nullptr);
	if (renderer)
	{
		CHECK(renderer->apply_timing == ApplyTiming::NextLaunch);
		CHECK(renderer->player_owned);
	}

	const SettingDef* cas = FindSettingByKey("CASMode");
	CHECK(cas != nullptr && cas->implementation_detail && cas->presentation == Presentation::Hidden);
	if (cas)
		CHECK(cas->choices.size() == 3 && cas->default_value == "0" && cas->apply_timing == ApplyTiming::Live);

	const SettingDef* mtvu = FindSettingByKey("EmuCore/Speedhacks/vuThread");
	CHECK(mtvu != nullptr);
	if (mtvu)
	{
		CHECK(mtvu->apply_timing == ApplyTiming::NextLaunch);
		CHECK(mtvu->default_value == "true");
		CHECK(!mtvu->player_owned);
	}

	const SettingDef* spin = FindSettingByKey("EmuCore/Speedhacks/OrbisMTVUSpin");
	const SettingDef* batch = FindSettingByKey("EmuCore/Speedhacks/OrbisMTVUBatch");
	const SettingDef* ring = FindSettingByKey("EmuCore/Speedhacks/OrbisMTVURingKB");
	CHECK(spin != nullptr && spin->default_value == "true" && spin->player_owned && spin->apply_timing == ApplyTiming::Live);
	CHECK(batch != nullptr && batch->default_value == "false" && batch->player_owned && batch->apply_timing == ApplyTiming::Live);
	CHECK(ring != nullptr && ring->default_value == "2048" && ring->player_owned && ring->type == ValueType::Number);
	if (ring)
	{
		CHECK(ring->number_min == 256.0 && ring->number_max == 2048.0 && ring->number_step == 256.0);
		CHECK(ValidateValue(*ring, "1024").ok);
		CHECK(!ValidateValue(*ring, "3000").ok);
	}

	const SettingDef* hide = FindSettingByKey("Caliza/HideGame");
	CHECK(hide != nullptr);
	if (hide)
	{
		CHECK(hide->scopes == ScopeMask::Game);
		CHECK(hide->inheritance == Inheritance::Fixed);
		CHECK(hide->apply_timing == ApplyTiming::NextMenuOpen);
	}

	size_t manual_fixes = 0;
	bool saw_master = false;
	for (const SettingDef* def : SettingsInGroup("hardware-fixes"))
	{
		manual_fixes += def->manual_hardware_fix ? 1 : 0;
		saw_master = saw_master || def->manual_hardware_fix_master;
		CHECK(def->scopes == ScopeMask::Game);
	}
	CHECK(manual_fixes == 10);
	CHECK(saw_master);

	for (const char* pair_key : {"Caliza/SaveButton1", "Caliza/LoadButton1", "Caliza/FastButton1", "Caliza/DiscButton1"})
	{
		const SettingDef* first = FindSettingByKey(pair_key);
		CHECK(first != nullptr);
		if (!first)
			continue;
		std::string second_key = pair_key;
		second_key.back() = '2';
		const SettingDef* second = FindSettingByKey(second_key);
		CHECK(second != nullptr);
		if (second)
		{
			CHECK(first->pair_id == second->pair_id);
			CHECK(first->pair_part == 1);
			CHECK(second->pair_part == 2);
		}
	}

	CHECK(FindSettingByKey("MemoryCards/Slot1_Filename") != nullptr);
	CHECK(FindSettingByKey("MemoryCards/Slot1_Enable") != nullptr);
	CHECK(FindSettingByKey("MemoryCards/Slot2_Filename") != nullptr);
	CHECK(FindSettingByKey("MemoryCards/Slot2_Enable") != nullptr);
	CHECK(FindSettingByKey("Filenames/BIOS") != nullptr);
	CHECK(FindSettingByKey("Caliza/CheatDisc") != nullptr);
	CHECK(FindSettingByKey("Caliza/ElfDisc") != nullptr);
	CHECK(FindSettingByKey("Patches/Enable") != nullptr);
	CHECK(FindSettingByKey("Cheats/Enable") != nullptr);
	CHECK(FindSettingByKey("EmuCore/EnableCheats") != nullptr);

	for (const char* key : {"Caliza/UiTextScalePercent", "Caliza/UiHighContrast", "Caliza/UiReduceMotion", "Caliza/UiLargeFocus"})
	{
		const SettingDef* accessibility = FindSettingByKey(key);
		CHECK(accessibility != nullptr);
		if (accessibility)
		{
			CHECK(accessibility->apply_timing == ApplyTiming::ImmediateUi);
			CHECK(accessibility->player_owned);
			CHECK(accessibility->scopes == ScopeMask::Global);
		}
	}

	for (const char* id : {"settings.apply-recommended", "settings.reset-settings-scope", "settings.reset-controls",
		"memory-cards.create", "textures.download-pack", "runtime.save-state", "runtime.load-state", "runtime.change-disc-next",
		"achievements.sign-in", "diagnostics.generate-report",
		"textures.select-pack-choice", "textures.retry-catalog", "storage.add-nfs-share"})
		CHECK(FindAction(id) != nullptr);

	const ActionDef* recommended = FindAction("settings.apply-recommended");
	CHECK(recommended != nullptr && recommended->destructive && recommended->confirmation == Confirmation::TwoPress);
	const ActionDef* save = FindAction("runtime.save-state");
	CHECK(save != nullptr && save->requires_running_vm && save->destructive);
}

void TestLegacyKeyMigration()
{
	using namespace caliza::settings;
	const std::string legacy_namespace = caliza::legacy::RetiredIniNamespace();
	CHECK(legacy_namespace != "Caliza/");
	for (const SettingDef& def : Settings())
		CHECK(def.compatibility_key.compare(0, legacy_namespace.size(), legacy_namespace) != 0);

	const std::vector<LegacyKeyMigration>& migrations = LegacyKeyMigrations();
	CHECK(migrations.size() == 69);
	size_t migratable_canonical_keys = 0;
	for (const SettingDef& def : Settings())
		if (def.compatibility_key.compare(0, 7, "Caliza/") == 0 && def.compatibility_key.compare(0, 9, "Caliza/Ui") != 0)
			++migratable_canonical_keys;
	CHECK(migratable_canonical_keys == migrations.size());
	std::set<std::string> legacy_keys, canonical_keys;
	for (const LegacyKeyMigration& migration : migrations)
	{
		CHECK(migration.legacy_key.compare(0, legacy_namespace.size(), legacy_namespace) == 0);
		CHECK(migration.canonical_key.compare(0, 7, "Caliza/") == 0);
		CHECK(migration.canonical_key.compare(0, 9, "Caliza/Ui") != 0);
		CHECK(migration.legacy_key.substr(legacy_namespace.size()) == migration.canonical_key.substr(7));
		CHECK(legacy_keys.insert(migration.legacy_key).second);
		CHECK(canonical_keys.insert(migration.canonical_key).second);
		const SettingDef* def = FindSettingByKey(migration.canonical_key);
		CHECK(def != nullptr);
		if (def)
			CHECK(def->compatibility_key == migration.canonical_key);
		CHECK(MigrateLegacyKey(migration.legacy_key) == migration.canonical_key);
	}
	CHECK(legacy_keys.size() == migrations.size());
	CHECK(canonical_keys.size() == migrations.size());

	const std::string legacy_renderer = legacy_namespace + "Renderer";
	const SettingDef* renderer = FindSettingByKey("Caliza/Renderer");
	CHECK(renderer != nullptr);
	CHECK(FindSettingByKey(legacy_renderer) == nullptr);
	CHECK(FindSettingByKey(MigrateLegacyKey(legacy_renderer)) == renderer);
	CHECK(MigrateLegacyKey(" " + legacy_renderer + " ") == "Caliza/Renderer");
	CHECK(MigrateLegacyKey("Caliza/Renderer") == "Caliza/Renderer");
	CHECK(MigrateLegacyKey("upscale_multiplier") == "upscale_multiplier");
	const std::string legacy_ui = legacy_namespace + "UiHighContrast";
	CHECK(MigrateLegacyKey(legacy_ui) == legacy_ui);
	const std::string unknown_legacy = legacy_namespace + "NotARealSetting";
	CHECK(MigrateLegacyKey(unknown_legacy) == unknown_legacy);

	const std::string legacy_text = legacy_renderer + "=Hardware\n" + legacy_namespace +
		"GameFolders=/data/" + caliza::legacy::RetiredLowerName() + "/games\n";
	const DocumentState unmigrated = ParseSettingsDocument(legacy_text);
	CHECK(FindValue(unmigrated, "Caliza/Renderer") == nullptr);
	const std::string migrated_text = caliza::legacy::MigrateIniText(legacy_text);
	CHECK(migrated_text.find("Caliza/Renderer=Hardware\n") != std::string::npos);
	CHECK(migrated_text.find("Caliza/GameFolders=/data/caliza/games\n") != std::string::npos);
	const DocumentState migrated = ParseSettingsDocument(migrated_text);
	const std::string* migrated_renderer = FindValue(migrated, "Caliza/Renderer");
	const std::string* migrated_folders = FindValue(migrated, "Caliza/GameFolders");
	CHECK(migrated_renderer != nullptr && *migrated_renderer == "Hardware");
	CHECK(migrated_folders != nullptr && *migrated_folders == "/data/caliza/games");
}

void TestSerialization()
{
	using namespace caliza::settings;
	CHECK(CanonKey("EmuCore/GS/upscale_multiplier") == "upscale_multiplier");
	CHECK(CanonKey(" EmuCore/Speedhacks/vuThread ") == "EmuCore/Speedhacks/vuThread");
	CHECK(IsListKey("Patches/Enable"));
	CHECK(SafeKey("Caliza/GameFolders"));
	CHECK(!SafeKey("Patches/Enable"));
	CHECK(!SafeKey("Achievements/Token"));
	CHECK(SafeValue("/mnt/usb0/games;nfs://host/share"));
	const std::vector<std::string> folders = SplitFolderList(" /mnt/usb0/games/ ; nfs://host/share/|/mnt/usb0/games\n/mnt/ext1/PS2/ ");
	CHECK(folders.size() == 3);
	if (folders.size() == 3)
	{
		CHECK(folders[0] == "/mnt/usb0/games");
		CHECK(folders[1] == "nfs://host/share");
		CHECK(folders[2] == "/mnt/ext1/PS2");
	}
	CHECK(JoinFolderList(folders) == "/mnt/usb0/games;nfs://host/share;/mnt/ext1/PS2");
	CHECK(!SafeValue(std::string(201, 'x')));

	const DocumentState doc = ParseSettingsDocument(
		"# comment\n"
		"upscale_multiplier=2\n"
		"EmuCore/GS/AspectRatio=16:9\n"
		"upscale_multiplier=3\n"
		"Patches/Enable=Widescreen\n"
		"Patches/Enable=Widescreen\n"
		"Patches/Enable=60 FPS\n"
		"Cheats/Enable=Infinite health\n"
		"Patches/Disable=ignored legacy line\n");
	const std::string* upscale = FindValue(doc, "upscale_multiplier");
	CHECK(upscale != nullptr && *upscale == "3");
	const std::string* aspect = FindValue(doc, "AspectRatio");
	CHECK(aspect != nullptr && *aspect == "16:9");
	CHECK(doc.patches.size() == 2);
	CHECK(doc.cheats.size() == 1);
	const std::string roundtrip = SerializeSettingsDocument(doc);
	CHECK(roundtrip.find("upscale_multiplier=3\n") != std::string::npos);
	CHECK(roundtrip.find("Patches/Enable=Widescreen\n") != std::string::npos);
	CHECK(roundtrip.find("Cheats/Enable=Infinite health\n") != std::string::npos);

	const SettingDef* resolution = FindSettingByKey("upscale_multiplier");
	CHECK(resolution != nullptr);
	if (resolution)
	{
		bool inherited = true;
		DocumentState empty, global;
		global.values.emplace_back("upscale_multiplier", "2");
		CHECK(EffectiveValue(*resolution, empty, global, &inherited) == "2");
		CHECK(inherited);
		DocumentState own;
		own.values.emplace_back("upscale_multiplier", "4");
		CHECK(EffectiveValue(*resolution, own, global, &inherited) == "4");
		CHECK(!inherited);
		CHECK(ValidateValue(*resolution, "8x").ok == false);
		CHECK(ValidateValue(*resolution, "8").ok);
	}

	const SettingDef* hold = FindSettingByKey("Caliza/StateHold");
	CHECK(hold != nullptr);
	if (hold)
	{
		CHECK(SameSettingValue(*hold, "1", "1.0"));
			CHECK(SameSettingValue(*hold, "1", "1.00"));
	}

	const SettingDef* pressure = FindSettingByKey("Caliza/ButtonCrossPressure");
	CHECK(pressure != nullptr);
	if (pressure)
	{
		const ValidationResult percent = ValidateValue(*pressure, "20%");
		CHECK(percent.ok && percent.normalized == "0.2");
	}

	Mutation mutation;
	std::string error;
	CHECK(ParseMutationLine("set upscale_multiplier=2", mutation, &error));
	CHECK(mutation.kind == Mutation::Kind::Set && mutation.key == "upscale_multiplier" && mutation.value == "2");
	CHECK(ParseMutationLine(std::string("unset Caliza/Overlay"), mutation, &error));
	CHECK(mutation.kind == Mutation::Kind::Unset);
	CHECK(ParseMutationLine("patch+ Widescreen", mutation, &error));
	CHECK(mutation.kind == Mutation::Kind::PatchOn && mutation.key == "Widescreen");
	CHECK(ParseMutationLine("cheat- Infinite health", mutation, &error));
	CHECK(mutation.kind == Mutation::Kind::CheatOff);
	CHECK(ParseMutationLine("recommended", mutation, &error));
	CHECK(mutation.kind == Mutation::Kind::ApplyRecommended);
	CHECK(SerializeMutation(mutation) == "recommended");
	CHECK(!ParseMutationLine("set Patches/Enable=x", mutation, &error));
	CHECK(!ParseMutationLine("unknown x", mutation, &error));

	std::vector<Mutation> batch;
	CHECK(ParseMutationBatch("set upscale_multiplier=2\npatch+ Widescreen\ncheat+ Infinite health\n", batch, &error));
	CHECK(batch.size() == 3);
	CHECK(!ParseMutationBatch("", batch, &error));

	const std::string json = RegistryToJson(false);
	CHECK(json.find("\"schema_version\":1") != std::string::npos);
	CHECK(json.find("\"key\":\"upscale_multiplier\"") != std::string::npos);
	CHECK(json.find("\"key\":\"Caliza/ButtonCross\"") != std::string::npos);
	CHECK(json.find(caliza::legacy::RetiredIniNamespace()) == std::string::npos);
	CHECK(ActionsToJson(false).find("\"id\":\"runtime.load-state\"") != std::string::npos);
}

void TestCatalog()
{
	using namespace caliza::catalog;
	CHECK(IsDiscImageName("Game.ISO"));
	CHECK(IsDiscImageName("Game.chd"));
	CHECK(IsDiscImageName("Game.cso"));
	CHECK(IsDiscImageName("Game.zso"));
	CHECK(IsDiscImageName("Game.bin"));
	CHECK(IsDiscImageName("Game.img"));
	CHECK(IsElfName("Homebrew.ELF"));
	CHECK(IsCatalogMediaName("Game.iso"));
	CHECK(!IsCatalogMediaName(".hidden.iso"));
	CHECK(IsRawImageCandidateName("game.BIN"));
	CHECK(kRawImageMinimumBytes == 16ull * 1024ull * 1024ull);
	CHECK(ShouldSkipScanSubfolder("settings"));
	CHECK(ShouldSkipScanSubfolder("System Volume Information"));
	CHECK(!ShouldSkipScanSubfolder("PS2"));

	const std::vector<ScanRoot> roots = DefaultScanRoots();
	CHECK(roots.size() == 12);
	if (roots.size() == 12)
	{
		CHECK(roots[0].path == "/data/caliza/games");
		CHECK(roots[1].path == "/data/caliza");
		CHECK(roots[2].path == "/mnt/usb0");
		CHECK(roots.back().path == "/mnt/ext1");
	}
	const std::vector<ScanRoot> legacy_roots = LegacyCompatibilityScanRoots();
	CHECK(legacy_roots.size() == 2);
	if (legacy_roots.size() == 2)
	{
		CHECK(legacy_roots[0].legacy_compatibility);
		CHECK(legacy_roots[0].path == caliza::legacy::RetiredDataRoot() + "/games");
		CHECK(legacy_roots[1].path == caliza::legacy::RetiredDataRoot());
	}
	for (const ScanRoot& root : roots)
		if (root.kind == RootKind::UsbDrive)
		{
			const std::vector<ScanRoot> layouts = ExpandDriveLayouts(root);
			CHECK(layouts.size() == 15);
			const std::string retired = caliza::legacy::RetiredLowerName();
			bool saw_retired_plain = false, saw_retired_nested = false, saw_caliza_plain = false;
			for (const ScanRoot& layout : layouts)
			{
				if (layout.path == root.path + "/" + retired)
					saw_retired_plain = layout.legacy_compatibility;
				if (layout.path == root.path + "/" + retired + "/games")
					saw_retired_nested = layout.legacy_compatibility;
				if (layout.path == root.path + "/caliza")
					saw_caliza_plain = !layout.legacy_compatibility;
			}
			CHECK(saw_retired_plain);
			CHECK(saw_retired_nested);
			CHECK(saw_caliza_plain);
			break;
		}

	CHECK(NormalizePath("/mnt/usb0//games/../games/Game.iso") == "/mnt/usb0/games/Game.iso");
	CHECK(FileName("/mnt/usb0/games/Game.iso") == "Game.iso");
	CHECK(FileStem("/mnt/usb0/games/Game.iso") == "Game");
	CHECK(PathWithin("/mnt/usb0/games/Game.iso", "/mnt/usb0"));
	CHECK(!PathWithin("/mnt/usb0other/Game.iso", "/mnt/usb0"));

	GameIdentity identity = MakeIdentity("/mnt/usb0/games/Game.iso", "usb0-a1", "/mnt/usb0", "SLUS-20000", 123, MediaKind::DiscImage);
	CHECK(identity.stable_id == "volume:usb0-a1:games/Game.iso");
	CHECK(identity.legacy_web_id == "Game.iso");
	CHECK(identity.settings_stem == "Game");
	CHECK(identity.content_key == "disc:SLUS-20000:123");
	CHECK(MakeStableId("/data/caliza/games/Game.iso", "", "/data/caliza") == "path:/data/caliza/games/Game.iso");

	std::string title, region, extra;
	ParseDisplayTitle("Lord of the Rings, The - The Two Towers (USA) (En,Ja)", title, region, extra);
	CHECK(title == "The Lord of the Rings: The Two Towers");
	CHECK(region == "USA");
	CHECK(extra == "En,Ja");
	ParseDisplayTitle("Ico (Europe) [!]", title, region, extra);
	CHECK(title == "Ico");
	CHECK(region == "Europe");
	CHECK(extra == "!");

	std::string rest;
	CHECK(DiscNumber("Game (Disc 2 of 3).iso", &rest) == 2);
	CHECK(rest == "game");
	CHECK(DiscNumber("Game (disc 10).chd", &rest) == 10);
	CHECK(rest == "game");
	CHECK(DiscNumber("Game.iso", &rest) == 0);
	CHECK(DiscSetKey("Game (Disc 2).iso") == "game");

	GameEntry game;
	game.identity = identity;
	game.path = "/mnt/usb0/games/Game.iso";
	game.file = "Game.iso";
	game.title = "Game";
	game.region = "USA";
	game.extra = "En,Ja";
	game.serial = "SLUS-20000";
	game.source_root_id = "usb0";
	game.bytes = 123;
	game.hidden = true;
	game.damaged = false;
	game.badges = {"6x", "16:9"};
	game.cover_path = "/mnt/usb0/covers/SLUS-20000.png";
	game.disc_number = 2;
	game.disc_set_key = "game";
	game.scan_revision = 42;
	const std::string json = GameEntryToJson(game);
	GameEntry parsed;
	std::string error;
	CHECK(GameEntryFromJson(json, parsed, &error));
	CHECK(parsed.identity.stable_id == identity.stable_id);
	CHECK(parsed.title == "Game");
	CHECK(parsed.hidden);
	CHECK(parsed.badges.size() == 2 && parsed.badges[1] == "16:9");
	CHECK(parsed.cover_path == "/mnt/usb0/covers/SLUS-20000.png");
	CHECK(parsed.scan_revision == 42);

	const std::string lines = CatalogToJsonLines({game, game});
	std::vector<GameEntry> catalog;
	CHECK(CatalogFromJsonLines(lines, catalog, &error));
	CHECK(catalog.size() == 2);
	CHECK(!GameEntryFromJson(json + " trailing", parsed, &error));
	CHECK(!GameEntryFromJson("{}", parsed, &error));
	const size_t badges_pos = json.find("\"badges\"");
	CHECK(badges_pos != std::string::npos);
	CHECK(CatalogLess(game, game) == false);
}
} // namespace

int main()
{
	TestInventory();
	TestLegacyKeyMigration();
	TestSerialization();
	TestCatalog();
	if (failures)
	{
		std::cerr << failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "caliza settings/catalog schema tests passed\n";
	return EXIT_SUCCESS;
}
