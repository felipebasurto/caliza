// Caliza console adapter tests: complete schema projection, serialized mutation ownership and honest launch/runtime handoff.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test_contract_common.h"
#include "../../caliza/core/console_backend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <set>
#include <thread>

using namespace caliza;
using namespace caliza_test;

namespace
{
Command MakeCommand(CommandType type, std::string id = {}, std::string path = {}, std::string value = {}, int number = 0)
{
	Command command;
	command.type = type;
	command.id = std::move(id);
	command.path = std::move(path);
	command.value = std::move(value);
	command.number = number;
	return command;
}

bool TestEditSettings(const std::string& path, const std::vector<settings::Mutation>& mutations, std::string* summary, std::string* error)
{
	std::string text;
	storage::fs::ReadFile(path, &text, error);
	settings::DocumentState doc = settings::ParseSettingsDocument(text);
	for (const settings::Mutation& mutation : mutations)
	{
		if (mutation.kind == settings::Mutation::Kind::Set)
		{
			bool found = false;
			for (auto& kv : doc.values)
			{
				if (kv.first == settings::CanonKey(mutation.key))
				{
					kv.second = mutation.value;
					found = true;
				}
			}
			if (!found)
				doc.values.emplace_back(settings::CanonKey(mutation.key), mutation.value);
		}
		else if (mutation.kind == settings::Mutation::Kind::Unset)
		{
			doc.values.erase(std::remove_if(doc.values.begin(), doc.values.end(),
								 [&](const auto& kv) { return kv.first == settings::CanonKey(mutation.key); }),
				doc.values.end());
		}
		else if (mutation.kind == settings::Mutation::Kind::PatchOn)
		{
			if (std::find(doc.patches.begin(), doc.patches.end(), mutation.key) == doc.patches.end())
				doc.patches.push_back(mutation.key);
		}
		else if (mutation.kind == settings::Mutation::Kind::PatchOff)
			doc.patches.erase(std::remove(doc.patches.begin(), doc.patches.end(), mutation.key), doc.patches.end());
		else if (mutation.kind == settings::Mutation::Kind::CheatOn)
		{
			if (std::find(doc.cheats.begin(), doc.cheats.end(), mutation.key) == doc.cheats.end())
				doc.cheats.push_back(mutation.key);
		}
		else if (mutation.kind == settings::Mutation::Kind::CheatOff)
			doc.cheats.erase(std::remove(doc.cheats.begin(), doc.cheats.end(), mutation.key), doc.cheats.end());
		else
		{
			*error = "recommended mutations use the preset hook";
			return false;
		}
	}
	if (!WriteFile(path, settings::SerializeSettingsDocument(doc)))
	{
		*error = "could not write test settings";
		return false;
	}
	if (summary)
		*summary = "test edit";
	return true;
}

ConsoleBackendOptions TestOptions(const TempDir& root)
{
	ConsoleBackendOptions options;
	options.storage.data_root = root.path;
	options.storage.legacy_data_root = root.path + "/unused-legacy";
	options.storage.create_data_root = true;
	options.global_settings_path = root.path + "/settings/gs.ini";
	options.game_settings_dir = root.path + "/settings";
	options.memory_cards_dir = root.path + "/memcards";
	options.patches_dir = root.path + "/patches";
	options.cheats_dir = root.path + "/cheats";
	options.bios_dir = root.path + "/bios";
	options.last_game_path = root.path + "/lastgame.txt";
	options.projection_refresh_ms = 0;
	options.hooks.edit_settings_file = TestEditSettings;
	options.hooks.list_memory_card_files = [root_path = root.path] {
		std::vector<std::pair<std::string, uint64_t>> out;
		out.emplace_back("Mcd001.ps2", 8ull * 1024ull * 528ull * 2ull);
		out.emplace_back("Extra.ps2", 8ull * 1024ull * 528ull * 2ull);
		return out;
	};
	options.hooks.bios_status = [] {
		BiosStatus bios;
		bios.present = true;
		bios.path = "bios";
		return bios;
	};
	return options;
}

const SettingItem* FindSettingItem(const Snapshot& snapshot, const std::string& id)
{
	for (const SettingCategory& category : snapshot.settings)
		for (const SettingItem& item : category.items)
			if (item.id == id)
				return &item;
	return nullptr;
}

CommandEvent WaitTerminal(Backend& backend, EventLog& log, CommandId id)
{
	const CommandEvent* event = log.WaitTerminal(backend, id, 10000);
	if (!event)
	{
		Fail(__FILE__, __LINE__, "timed out waiting for command");
		return {};
	}
	return *event;
}

void TestCompleteSchemaProjection()
{
	TempDir root;
	CHECK_FALSE(root.path.empty());
	ConsoleBackend backend(TestOptions(root));
	const Snapshot snapshot = backend.GetSnapshot();
	std::set<std::string> projected;
	for (const SettingCategory& category : snapshot.settings)
		for (const SettingItem& item : category.items)
			projected.insert(item.id);
	for (const settings::SettingDef& def : settings::Settings())
	{
		if (def.presentation == settings::Presentation::Hidden)
			continue;
		if (projected.count(def.id) == 0 && projected.count(def.compatibility_key) == 0)
			Fail(__FILE__, __LINE__, "schema setting missing from snapshot: " + def.id);
	}
	CHECK_TRUE(projected.count("Caliza/UiTextScalePercent") != 0);
	CHECK_TRUE(projected.count("UserHacks_HalfPixelOffset") != 0);
	CHECK_TRUE(projected.count("Caliza/MouseButtons") != 0);
	CHECK_TRUE(snapshot.settings.size() >= settings::Groups().size());
}

void TestSerializedSettingsAndAccessibilityMutation()
{
	TempDir root;
	WriteFile(root.path + "/bios/scph10000.bin", std::string(4096, '\0'));
	ConsoleBackend backend(TestOptions(root));
	EventLog log;
	const CommandId first = backend.Post(MakeCommand(CommandType::SetAccessibility, "high_contrast", {}, "true"));
	const CommandId second = backend.Post(MakeCommand(CommandType::SetSetting, "display.aspect-ratio", {}, "16:9"));
	const CommandEvent first_done = WaitTerminal(backend, log, first);
	const CommandEvent second_done = WaitTerminal(backend, log, second);
	CHECK_EQ(static_cast<int>(first_done.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_EQ(static_cast<int>(second_done.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_TRUE(first_done.revision < second_done.revision);
	const Snapshot snapshot = backend.GetSnapshot();
	CHECK_TRUE(snapshot.revision >= second_done.revision);
	CHECK_TRUE(snapshot.accessibility.high_contrast);
	const SettingItem* aspect = FindSettingItem(snapshot, "AspectRatio");
	CHECK_TRUE(aspect != nullptr);
	if (aspect)
	{
		CHECK_EQ(aspect->value, std::string("16:9"));
		CHECK_TRUE(aspect->modified);
	}
	const std::string written = ReadFile(root.path + "/settings/gs.ini");
	CHECK_TRUE(written.find("Caliza/UiHighContrast=true") != std::string::npos);
	CHECK_TRUE(written.find("AspectRatio=16:9") != std::string::npos);
	const CommandEvent bios = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SelectBios, "scph10000.bin")));
	CHECK_EQ(static_cast<int>(bios.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_TRUE(ReadFile(root.path + "/settings/gs.ini").find("Filenames/BIOS=scph10000.bin") != std::string::npos);
}

void TestLibraryFolderCommandUsesSettingsOwner()
{
	TempDir root;
	ConsoleBackend backend(TestOptions(root));
	EventLog log;
	const std::string folder = root.path + "/games/Custom";
	MakeMinimalIso(folder + "/Custom Game (USA).iso");
	const CommandEvent added = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::AddLibraryFolder, {}, folder, "Custom")));
	CHECK_EQ(static_cast<int>(added.status), static_cast<int>(CommandStatus::Succeeded));
	const std::string written = ReadFile(root.path + "/settings/gs.ini");
	CHECK_TRUE(written.find("Caliza/GameFolders=") != std::string::npos);
	CHECK_TRUE(written.find(folder) != std::string::npos);
}

void TestLibraryFolderRejectsDataRootAndRestoresSettings()
{
	TempDir root;
	const std::string existing = root.path + "/games/Existing";
	MakeMinimalIso(existing + "/Existing Game (USA).iso");
	CHECK_TRUE(WriteFile(root.path + "/settings/gs.ini", "Caliza/GameFolders=" + existing + "\n"));
	ConsoleBackend backend(TestOptions(root));
	EventLog log;

	const CommandEvent rejected = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::AddLibraryFolder, {}, root.path, "Data root")));
	CHECK_EQ(static_cast<int>(rejected.status), static_cast<int>(CommandStatus::Failed));
	CHECK_TRUE(rejected.message.find("data root") != std::string::npos);
	const std::string written = ReadFile(root.path + "/settings/gs.ini");
	CHECK_EQ(written, "Caliza/GameFolders=" + existing + "\n");
	const Snapshot snapshot = backend.GetSnapshot();
	CHECK_FALSE(std::any_of(snapshot.sources.begin(), snapshot.sources.end(), [&](const LibrarySource& source) {
		return source.user_added && source.path == root.path;
	}));
}

void TestExternalPs2FolderDiscoveredAfterStartup()
{
	TempDir root, drive;
	ConsoleBackendOptions options = TestOptions(root);
	const std::string usb = drive.path + "/usb0";
	const std::string folder = usb + "/ps2";
	options.storage.discovery.usb_roots = {usb};
	options.storage.discovery.extended_roots = {};
	{
		ConsoleBackend backend(options);
		CHECK_TRUE(backend.GetSnapshot().games.empty());
		std::set<std::string> images;
		for (int i = 0; i < 6; i++)
		{
			const std::string image = folder + "/Game " + std::to_string(i) + ".CHD";
			CHECK_TRUE(WriteFile(image, "MComprHD"));
			images.insert(image);
		}
		const auto listing = backend.BrowseFolder(folder, {}, 0, 128);
		CHECK_TRUE(listing.error.empty());
		CHECK_TRUE(listing.contains_compatible);
		CHECK_EQ(listing.entries.size(), 6u);

		EventLog log;
		const auto added = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::AddLibraryFolder, {}, folder)));
		CHECK_EQ(static_cast<int>(added.status), static_cast<int>(CommandStatus::Succeeded));
		const Snapshot linked = backend.GetSnapshot();
		const auto source = std::find_if(linked.sources.begin(), linked.sources.end(), [&](const LibrarySource& item) {
			return item.user_added && item.path == folder;
		});
		CHECK_TRUE(source != linked.sources.end());
		if (source == linked.sources.end())
			return;
		const auto scanned = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::ScanSource, source->id)));
		CHECK_EQ(static_cast<int>(scanned.status), static_cast<int>(CommandStatus::Succeeded));
		const Snapshot library = backend.GetSnapshot();
		CHECK_EQ(library.games.size(), 6u);
		for (const auto& game : library.games)
		{
			CHECK_TRUE(images.count(game.path) != 0 && game.can_launch);
			CHECK_EQ(ReadFile(game.path), std::string("MComprHD"));
		}
	}

	ConsoleBackend reopened(options);
	EventLog log;
	const auto scanned = WaitTerminal(reopened, log, reopened.Post(MakeCommand(CommandType::RescanAll)));
	CHECK_EQ(static_cast<int>(scanned.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_EQ(reopened.GetSnapshot().games.size(), 6u);
}

void TestLaunchHandoffAndRuntimeHonesty()
{
	TempDir root;
	ConsoleBackend backend(TestOptions(root));
	EventLog log;
	MakeMinimalIso(root.path + "/games/Valid Game (USA).iso");
	const CommandEvent scanned = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::RescanAll)));
	CHECK_EQ(static_cast<int>(scanned.status), static_cast<int>(CommandStatus::Succeeded));
	const Snapshot scanned_snapshot = backend.GetSnapshot();
	CHECK_EQ(scanned_snapshot.games.size(), 1u);
	if (scanned_snapshot.games.empty())
		return;
	const std::string scoped_id = ConsoleBackend::EncodeSettingId(ConsoleSettingScope::Game, "display.aspect-ratio", scanned_snapshot.games.front().id);
	const CommandEvent scoped = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SetSetting, scoped_id, {}, "4:3")));
	CHECK_EQ(static_cast<int>(scoped.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_TRUE(ReadFile(root.path + "/settings/SLUS-20010.ini").find("AspectRatio=4:3") != std::string::npos);
	const CommandEvent select = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SelectGame, scanned_snapshot.games.front().id)));
	CHECK_EQ(static_cast<int>(select.status), static_cast<int>(CommandStatus::Succeeded));
	const Snapshot selected_snapshot = backend.GetSnapshot();
	CHECK_EQ(selected_snapshot.selected_game_id, scanned_snapshot.games.front().id);
	const SettingItem* aspect = FindSettingItem(selected_snapshot, "AspectRatio");
	CHECK_TRUE(aspect != nullptr);
	if (aspect)
		CHECK_EQ(aspect->value, std::string("4:3"));
	const CommandEvent bad_scope = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SetSetting, "discs.elf-disc-image", {}, "Valid Game (USA)")));
	CHECK_EQ(static_cast<int>(bad_scope.status), static_cast<int>(CommandStatus::Failed));
	const CommandEvent launched = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::LaunchGame, scanned_snapshot.games.front().id)));
	CHECK_EQ(static_cast<int>(launched.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_TRUE(launched.message.find("handoff") != std::string::npos);
	ConsoleLaunchRequest request;
	CHECK_TRUE(backend.ConsumeLaunchRequest(&request));
	CHECK_EQ(static_cast<int>(request.kind), static_cast<int>(ConsoleLaunchRequest::Kind::Game));
	CHECK_EQ(request.title, std::string("Valid Game"));
	CHECK_FALSE(backend.ConsumeLaunchRequest(&request));

	const CommandEvent pause = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::PauseVm)));
	CHECK_EQ(static_cast<int>(pause.status), static_cast<int>(CommandStatus::Failed));
	CHECK_TRUE(pause.message.find("not supported") != std::string::npos);
}

void TestProductionServiceCommandHooks()
{
	TempDir root;
	ConsoleBackendOptions options = TestOptions(root);
	bool patch_started = false;
	int texture_pack = -1;
	std::string texture_pack_id, imported_archive;
	bool texture_cancelled = false;
	options.hooks.fetch_online_patches = [&patch_started](const GameEntry& game, std::string*) {
		patch_started = game.serial == "SLUS-20010" || !game.serial.empty();
		return patch_started;
	};
	options.hooks.download_texture_pack = [&](const GameEntry& game, int pack_index, const std::string& pack_id, std::string*) {
		texture_pack = game.serial.empty() ? -2 : pack_index;
		texture_pack_id = pack_id;
		return true;
	};
	options.hooks.import_texture_pack = [&](const GameEntry&, const std::string& path, std::string*) {
		imported_archive = path;
		return true;
	};
	options.hooks.texture_packs = [](const GameEntry&) {
		TexturePacks packs;
		packs.catalog = TexturePacks::Catalog::Ready;
		packs.entries.push_back({"high", "High resolution", "Demo creator", "", "", 10});
		return packs;
	};
	options.hooks.cancel_texture_pack = [&texture_cancelled](const GameEntry&, std::string*) {
		texture_cancelled = true;
		return true;
	};
	options.hooks.service_operations = [] {
		OperationStatus operation;
		operation.id = "texture-pack:SLUS-20010";
		operation.label = "HD texture pack";
		operation.game_id = "SLUS-20010";
		operation.state = OperationState::Running;
		operation.progress = 0.5f;
		operation.cancelable = true;
		return std::vector<OperationStatus>{operation};
	};
	ConsoleBackend backend(options);
	EventLog log;
	MakeMinimalIso(root.path + "/games/Valid Game (USA).iso");
	const CommandEvent scanned = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::RescanAll)));
	CHECK_EQ(static_cast<int>(scanned.status), static_cast<int>(CommandStatus::Succeeded));
	const Snapshot snapshot = backend.GetSnapshot();
	CHECK_EQ(snapshot.games.size(), 1u);
	if (snapshot.games.empty())
		return;
	const std::string game_id = snapshot.games.front().id;
	const CommandEvent patches = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::FetchOnlinePatches, game_id)));
	CHECK_EQ(static_cast<int>(patches.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_TRUE(patch_started);
	const CommandEvent texture = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::DownloadTexturePack, game_id, {}, "high", 2)));
	CHECK_EQ(static_cast<int>(texture.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_EQ(texture_pack, 2);
	CHECK_EQ(texture_pack_id, std::string("high"));
	const CommandEvent imported = WaitTerminal(backend, log,
		backend.Post(MakeCommand(CommandType::ImportTexturePack, game_id, "/mnt/usb0/textures.zip")));
	CHECK_EQ(static_cast<int>(imported.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_EQ(imported_archive, std::string("/mnt/usb0/textures.zip"));
	WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SelectGame, game_id)));
	const Snapshot selected = backend.GetSnapshot();
	CHECK_EQ(selected.selected_game_id, game_id);
	CHECK_EQ(selected.texture_packs.entries.size(), 1u);
	if (!selected.texture_packs.entries.empty())
		CHECK_EQ(selected.texture_packs.entries.front().id, std::string("high"));
	const CommandEvent cancel = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::CancelTexturePack, game_id)));
	CHECK_EQ(static_cast<int>(cancel.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_TRUE(texture_cancelled);
	const Snapshot operations = backend.GetSnapshot();
	CHECK_EQ(operations.operations.size(), 1u);
	if (!operations.operations.empty())
	{
		CHECK_EQ(operations.operations.front().id, std::string("texture-pack:SLUS-20010"));
		CHECK_EQ(static_cast<int>(operations.operations.front().state), static_cast<int>(OperationState::Running));
		CHECK_TRUE(operations.operations.front().cancelable);
	}
}

void TestAsyncProjectionRefreshOffRenderThread()
{
	TempDir root;
	ConsoleBackendOptions options = TestOptions(root);
	options.projection_refresh_ms = 20;
	const std::thread::id caller_tid = std::this_thread::get_id();
	std::atomic<int> phase{0};
	std::atomic<int> caller_thread_calls{0};
	std::atomic<int> worker_thread_calls{0};
	options.hooks.bios_status = [&] {
		if (phase.load() > 0)
		{
			if (std::this_thread::get_id() == caller_tid)
				caller_thread_calls.fetch_add(1);
			else
				worker_thread_calls.fetch_add(1);
		}
		BiosStatus bios;
		bios.present = phase.load() >= 1;
		bios.path = bios.present ? (root.path + "/bios/scph39001.bin") : "";
		return bios;
	};
	options.hooks.web_panel_url = [&] {
		return phase.load() >= 1 ? std::string("http://192.168.1.50:8080") : std::string();
	};
	ConsoleBackend backend(options);
	phase.store(1);
	// Wait past projection_refresh_ms, then poll GetSnapshot on the caller thread; projection refresh
	// must execute on the background worker thread and never synchronously on the render thread.
	std::this_thread::sleep_for(std::chrono::milliseconds(35));
	Snapshot refreshed;
	for (int attempt = 0; attempt < 40; attempt++)
	{
		refreshed = backend.GetSnapshot();
		if (refreshed.bios.present && !refreshed.diagnostics.web_panel_url.empty())
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	CHECK_EQ(caller_thread_calls.load(), 0);
	CHECK_TRUE(worker_thread_calls.load() >= 1);
	CHECK_TRUE(refreshed.bios.present);
	CHECK_EQ(refreshed.diagnostics.web_panel_url, std::string("http://192.168.1.50:8080"));
}

// Each per-patch/per-cheat row the game's downloaded files actually contain (list_patch_groups, same state the web
// companion panel reads from fe_settings.cpp PatchGroups) shows as an own Toggle row once the game is selected, and
// toggling it persists to the per-game ini exactly as the web panel's patch+/patch-/cheat+/cheat- edits do: a Patches/
// Enable or Cheats/Enable list line, plus EmuCore/EnableCheats kept in lockstep with whether any cheat is on.
void TestPatchAndCheatTogglePersistence()
{
	TempDir root;
	ConsoleBackendOptions options = TestOptions(root);
	options.hooks.list_patch_groups = [](const GameEntry& game, bool cheats) {
		std::vector<ConsolePatchGroup> out;
		if (game.serial != "SLUS-20010")
			return out;
		if (cheats)
			out.push_back({"Infinite HP", "SLUS-20010_DEADBEEF.pnach", ""});
		else
			out.push_back({"60 FPS", "SLUS-20010_DEADBEEF.pnach", "Removes the frame cap"});
		return out;
	};
	ConsoleBackend backend(options);
	EventLog log;
	MakeMinimalIso(root.path + "/games/Valid Game (USA).iso");
	WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::RescanAll)));
	const Snapshot scanned = backend.GetSnapshot();
	CHECK_EQ(scanned.games.size(), 1u);
	if (scanned.games.empty())
		return;
	const std::string game_id = scanned.games.front().id;
	WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SelectGame, game_id)));

	auto find_row = [](const Snapshot& snapshot, const char* category_id, const char* label) -> const SettingItem* {
		for (const SettingCategory& category : snapshot.settings)
		{
			if (category.id != category_id)
				continue;
			for (const SettingItem& item : category.items)
				if (item.label == label)
					return &item;
		}
		return nullptr;
	};

	const Snapshot before_toggle = backend.GetSnapshot();
	const SettingItem* patch_row = find_row(before_toggle, "patches", "60 FPS");
	const SettingItem* cheat_row = find_row(before_toggle, "cheats", "Infinite HP");
	CHECK_TRUE(patch_row != nullptr);
	CHECK_TRUE(cheat_row != nullptr);
	if (!patch_row || !cheat_row)
		return;
	CHECK_EQ(patch_row->value, std::string("false"));
	CHECK_EQ(cheat_row->value, std::string("false"));
	CHECK_FALSE(patch_row->game_scoped);
	const std::string patch_id = patch_row->id, cheat_id = cheat_row->id;

	const CommandEvent patch_on = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SetSetting, patch_id, {}, "true")));
	CHECK_EQ(static_cast<int>(patch_on.status), static_cast<int>(CommandStatus::Succeeded));
	const CommandEvent cheat_on = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SetSetting, cheat_id, {}, "true")));
	CHECK_EQ(static_cast<int>(cheat_on.status), static_cast<int>(CommandStatus::Succeeded));

	const std::string ini_path = root.path + "/settings/SLUS-20010.ini";
	std::string written = ReadFile(ini_path);
	CHECK_TRUE(written.find("Patches/Enable=60 FPS") != std::string::npos);
	CHECK_TRUE(written.find("Cheats/Enable=Infinite HP") != std::string::npos);
	CHECK_TRUE(written.find("EmuCore/EnableCheats=true") != std::string::npos);

	const Snapshot after_on = backend.GetSnapshot();
	const SettingItem* patch_row_on = find_row(after_on, "patches", "60 FPS");
	const SettingItem* cheat_row_on = find_row(after_on, "cheats", "Infinite HP");
	CHECK_TRUE(patch_row_on != nullptr && patch_row_on->value == "true" && patch_row_on->modified);
	CHECK_TRUE(cheat_row_on != nullptr && cheat_row_on->value == "true" && cheat_row_on->modified);

	// Turning the only enabled cheat back off also clears the master EmuCore/EnableCheats switch this backend set,
	// the same on/off coordination the shelf's legacy options sheet uses (fe_options.cpp OptionsSheet::SetCheat).
	const CommandEvent cheat_off = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SetSetting, cheat_id, {}, "false")));
	CHECK_EQ(static_cast<int>(cheat_off.status), static_cast<int>(CommandStatus::Succeeded));
	written = ReadFile(ini_path);
	CHECK_TRUE(written.find("Cheats/Enable=Infinite HP") == std::string::npos);
	CHECK_TRUE(written.find("EmuCore/EnableCheats") == std::string::npos);
	CHECK_TRUE(written.find("Patches/Enable=60 FPS") != std::string::npos); // the patch toggle is untouched

	// Reset (the shelf's Square action on a row) turns a patch/cheat toggle back off too, same as turning it off.
	const CommandEvent patch_reset = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::ResetSetting, patch_id)));
	CHECK_EQ(static_cast<int>(patch_reset.status), static_cast<int>(CommandStatus::Succeeded));
	written = ReadFile(ini_path);
	CHECK_TRUE(written.find("Patches/Enable=60 FPS") == std::string::npos);
}

// A patch/cheat group list_patch_groups marks modern_controls (ps4_shell.cpp's hook, via fe::IsModernControlsGroup;
// see docs/research/modern-controls.md) shows under its own "Modern controls" category instead of "Patches"/
// "Cheats", stays off by default, and keeps toggling through the exact same Patches/Enable id as a non-matching
// group. A game with no such group gets no such category at all. Launching also logs whether any matching group
// is actually turned on, available but off, or absent (console_backend.cpp LogModernControlsForLaunch).
void TestModernControlsCategoryAndBootLog()
{
	TempDir root;
	ConsoleBackendOptions options = TestOptions(root);
	options.hooks.list_patch_groups = [](const GameEntry& game, bool cheats) {
		// MakeMinimalIso always writes the same SYSTEM.CNF (SLUS-20010), so every fixture ISO shares one serial;
		// match by title instead, purely to give this test a second, unmatched game.
		std::vector<ConsolePatchGroup> out;
		if (game.title != "Valid Game")
			return out;
		if (cheats)
			out.push_back({"Infinite HP", "SLUS-20010_DEADBEEF.pnach", "", false});
		else
		{
			out.push_back({"60 FPS", "SLUS-20010_DEADBEEF.pnach", "Removes the frame cap", false});
			out.push_back({"Modern Control Profiles", "SLUS-20010_DEADBEEF.pnach", "", true});
		}
		return out;
	};
	std::vector<std::string> boot_lines;
	options.hooks.boot_log = [&boot_lines](const std::string& line) { boot_lines.push_back(line); };
	ConsoleBackend backend(options);
	EventLog log;
	MakeMinimalIso(root.path + "/games/Valid Game (USA).iso");
	WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::RescanAll)));
	const Snapshot scanned = backend.GetSnapshot();
	CHECK_EQ(scanned.games.size(), 1u);
	if (scanned.games.empty())
		return;
	const std::string game_id = scanned.games.front().id;
	WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SelectGame, game_id)));

	auto find_row = [](const Snapshot& snapshot, const char* category_id, const char* label) -> const SettingItem* {
		for (const SettingCategory& category : snapshot.settings)
		{
			if (category.id != category_id)
				continue;
			for (const SettingItem& item : category.items)
				if (item.label == label)
					return &item;
		}
		return nullptr;
	};

	const Snapshot before_toggle = backend.GetSnapshot();
	CHECK_TRUE(find_row(before_toggle, "patches", "60 FPS") != nullptr);
	CHECK_TRUE(find_row(before_toggle, "patches", "Modern Control Profiles") == nullptr); // moved out of "patches"
	const SettingItem* modern_row = find_row(before_toggle, "modern-controls", "Modern Control Profiles");
	CHECK_TRUE(modern_row != nullptr);
	if (!modern_row)
		return;
	CHECK_EQ(modern_row->value, std::string("false"));
	const std::string modern_id = modern_row->id;

	const CommandEvent launch_off = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::LaunchGame, game_id)));
	CHECK_EQ(static_cast<int>(launch_off.status), static_cast<int>(CommandStatus::Succeeded));
	ConsoleLaunchRequest request;
	CHECK_TRUE(backend.ConsumeLaunchRequest(&request));
	CHECK_TRUE(!boot_lines.empty() && boot_lines.back().find("modern controls") != std::string::npos);
	CHECK_TRUE(boot_lines.back().find("Modern Control Profiles") != std::string::npos);
	CHECK_TRUE(boot_lines.back().find("but off") != std::string::npos);

	const CommandEvent toggled = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SetSetting, modern_id, {}, "true")));
	CHECK_EQ(static_cast<int>(toggled.status), static_cast<int>(CommandStatus::Succeeded));
	const std::string ini_path = root.path + "/settings/SLUS-20010.ini";
	CHECK_TRUE(ReadFile(ini_path).find("Patches/Enable=Modern Control Profiles") != std::string::npos);
	const Snapshot after_toggle = backend.GetSnapshot();
	const SettingItem* modern_row_on = find_row(after_toggle, "modern-controls", "Modern Control Profiles");
	CHECK_TRUE(modern_row_on != nullptr && modern_row_on->value == "true");

	const CommandEvent launch_on = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::LaunchGame, game_id)));
	CHECK_EQ(static_cast<int>(launch_on.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_TRUE(backend.ConsumeLaunchRequest(&request));
	CHECK_TRUE(!boot_lines.empty() && boot_lines.back().find("applying \"Modern Control Profiles\"") != std::string::npos);

	// A game the hook has no groups for at all gets no boot.log mention beyond "no matching patch".
	MakeMinimalIso(root.path + "/games/Other Game (USA).iso");
	WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::RescanAll)));
	const Snapshot rescanned = backend.GetSnapshot();
	const GameEntry* other = nullptr;
	for (const GameEntry& game : rescanned.games)
		if (game.title == "Other Game")
			other = &game;
	CHECK_TRUE(other != nullptr);
	if (!other)
		return;
	boot_lines.clear();
	const CommandEvent launch_other = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::LaunchGame, other->id)));
	CHECK_EQ(static_cast<int>(launch_other.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_TRUE(backend.ConsumeLaunchRequest(&request));
	CHECK_TRUE(!boot_lines.empty() && boot_lines.back().find("no matching patch") != std::string::npos);
}

void TestLegacyPauseComboMigration()
{
	auto find_item = [](const Snapshot& snap, const std::string& id) -> const SettingItem* {
		for (const SettingCategory& category : snap.settings)
			for (const SettingItem& item : category.items)
				if (item.id == id)
					return &item;
		return nullptr;
	};

	// 1. Fresh install has no PauseTrigger yet: pause_trigger_chosen is false; saving l3_r3 persists across reload.
	{
		TempDir root;
		ConsoleBackendOptions options = TestOptions(root);
		ConsoleBackend backend(options);
		EventLog log;
		const Snapshot fresh = backend.GetSnapshot();
		CHECK_TRUE(!fresh.pause_trigger_chosen);
		const SettingItem* pt = find_item(fresh, "Caliza/PauseTrigger");
		CHECK_TRUE(pt != nullptr && pt->value == "l3_r3");

		const CommandEvent saved = WaitTerminal(backend, log,
			backend.Post(MakeCommand(CommandType::SetSetting, "buttons.pause-trigger", "", "l3_r3")));
		CHECK_EQ(static_cast<int>(saved.status), static_cast<int>(CommandStatus::Succeeded));
		CHECK_TRUE(backend.GetSnapshot().pause_trigger_chosen);
		CHECK_TRUE(ReadFile(options.global_settings_path).find("Caliza/PauseTrigger=l3_r3") != std::string::npos);

		ConsoleBackend reloaded(options);
		CHECK_TRUE(reloaded.GetSnapshot().pause_trigger_chosen);
	}

	// 2. Legacy gs.ini with Share/Create migrates to "not chosen" (removes legacy keys, leaves PauseTrigger unset).
	{
		TempDir root;
		ConsoleBackendOptions options = TestOptions(root);
		CHECK_TRUE(WriteFile(options.global_settings_path, "Caliza/PauseButton1=Share\nCaliza/PauseButton2=None\n"));
		ConsoleBackend backend(options);
		const Snapshot snap = backend.GetSnapshot();
		CHECK_TRUE(!snap.pause_trigger_chosen);
		const std::string on_disk = ReadFile(options.global_settings_path);
		CHECK_TRUE(on_disk.find("Share") == std::string::npos);
		CHECK_TRUE(on_disk.find("PauseButton1") == std::string::npos);
		CHECK_TRUE(on_disk.find("PauseTrigger") == std::string::npos);
	}

	// 3. Legacy gs.ini with non-Share PauseButton1/2 migrates on disk to closest PauseTrigger preset.
	{
		TempDir root;
		ConsoleBackendOptions options = TestOptions(root);
		CHECK_TRUE(WriteFile(options.global_settings_path, "Caliza/PauseButton1=L3R3\nCaliza/PauseButton2=L1\n"));
		ConsoleBackend backend(options);
		const Snapshot snap = backend.GetSnapshot();
		CHECK_TRUE(snap.pause_trigger_chosen);
		const SettingItem* pt = find_item(snap, "Caliza/PauseTrigger");
		CHECK_TRUE(pt != nullptr && pt->value == "l3_r3_l1");
		const std::string on_disk = ReadFile(options.global_settings_path);
		CHECK_TRUE(on_disk.find("Caliza/PauseTrigger=l3_r3_l1") != std::string::npos);
		CHECK_TRUE(on_disk.find("PauseButton1") == std::string::npos);
	}
}

void TestPerGameSettingsSerialAndMigration()
{
	// 1. Two differently-named ISOs with the same serial (SLUS-20010) share settings/SLUS-20010.ini.
	{
		TempDir root;
		ConsoleBackend backend(TestOptions(root));
		EventLog log;
		MakeMinimalIso(root.path + "/games/Alpha Copy (USA).iso");
		MakeMinimalIso(root.path + "/games/Beta Copy (USA).iso");
		CHECK_EQ(static_cast<int>(WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::RescanAll))).status),
			static_cast<int>(CommandStatus::Succeeded));
		const Snapshot snap = backend.GetSnapshot();
		CHECK_EQ(snap.games.size(), 2u);
		const GameEntry* alpha = nullptr;
		const GameEntry* beta = nullptr;
		for (const GameEntry& g : snap.games)
		{
			if (g.file == "Alpha Copy (USA).iso")
				alpha = &g;
			else if (g.file == "Beta Copy (USA).iso")
				beta = &g;
		}
		CHECK_TRUE(alpha != nullptr && beta != nullptr);
		if (alpha && beta)
		{
			const std::string id = ConsoleBackend::EncodeSettingId(ConsoleSettingScope::Game, "display.aspect-ratio", alpha->id);
			CHECK_EQ(static_cast<int>(WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SetSetting, id, {}, "4:3"))).status),
				static_cast<int>(CommandStatus::Succeeded));
			CHECK_TRUE(storage::fs::Exists(root.path + "/settings/SLUS-20010.ini"));
			CHECK_FALSE(storage::fs::Exists(root.path + "/settings/Alpha Copy (USA).ini"));

			CHECK_EQ(static_cast<int>(WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SelectGame, beta->id))).status),
				static_cast<int>(CommandStatus::Succeeded));
			const Snapshot selected = backend.GetSnapshot();
			const SettingItem* aspect = FindSettingItem(selected, "AspectRatio");
			CHECK_TRUE(aspect != nullptr && aspect->value == "4:3");
		}
	}

	// 2. Migration: if <serial>.ini does not exist but <stem>.ini already exists, keep using <stem>.ini.
	{
		TempDir root;
		CHECK_TRUE(WriteFile(root.path + "/settings/Legacy Stem (USA).ini", "AspectRatio=4:3\n"));
		ConsoleBackend backend(TestOptions(root));
		EventLog log;
		MakeMinimalIso(root.path + "/games/Legacy Stem (USA).iso");
		CHECK_EQ(static_cast<int>(WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::RescanAll))).status),
			static_cast<int>(CommandStatus::Succeeded));
		const Snapshot snap = backend.GetSnapshot();
		CHECK_EQ(snap.games.size(), 1u);
		if (!snap.games.empty())
		{
			const GameEntry& g = snap.games.front();
			CHECK_EQ(g.serial, std::string("SLUS-20010"));
			CHECK_EQ(static_cast<int>(WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SelectGame, g.id))).status),
				static_cast<int>(CommandStatus::Succeeded));
			const SettingItem* aspect = FindSettingItem(backend.GetSnapshot(), "AspectRatio");
			CHECK_TRUE(aspect != nullptr && aspect->value == "4:3");

			const std::string id = ConsoleBackend::EncodeSettingId(ConsoleSettingScope::Game, "display.aspect-ratio", g.id);
			CHECK_EQ(static_cast<int>(WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::SetSetting, id, {}, "16:9"))).status),
				static_cast<int>(CommandStatus::Succeeded));
			CHECK_TRUE(ReadFile(root.path + "/settings/Legacy Stem (USA).ini").find("AspectRatio=16:9") != std::string::npos);
			CHECK_FALSE(storage::fs::Exists(root.path + "/settings/SLUS-20010.ini"));
		}
	}

	// 3. When <serial>.ini already exists alongside <stem>.ini, <serial>.ini wins; empty serial falls back to stem.
	{
		TempDir root;
		CHECK_TRUE(WriteFile(root.path + "/settings/Both Exist (USA).ini", "AspectRatio=4:3\n"));
		CHECK_TRUE(WriteFile(root.path + "/settings/SLUS-20010.ini", "AspectRatio=16:9\n"));
		CHECK_EQ(storage::ResolveGameSettingsPath(root.path + "/settings", "SLUS-20010", "Both Exist (USA)"),
			root.path + "/settings/SLUS-20010.ini");
		CHECK_EQ(storage::ResolveGameSettingsPath(root.path + "/settings", "", "Homebrew"),
			root.path + "/settings/Homebrew.ini");
	}
}

} // namespace

int main()
{
	TestCompleteSchemaProjection();
	TestSerializedSettingsAndAccessibilityMutation();
	TestLibraryFolderCommandUsesSettingsOwner();
	TestLibraryFolderRejectsDataRootAndRestoresSettings();
	TestExternalPs2FolderDiscoveredAfterStartup();
	TestLaunchHandoffAndRuntimeHonesty();
	TestProductionServiceCommandHooks();
	TestAsyncProjectionRefreshOffRenderThread();
	TestPatchAndCheatTogglePersistence();
	TestModernControlsCategoryAndBootLog();
	TestLegacyPauseComboMigration();
	TestPerGameSettingsSerialAndMigration();
	if (Failures() == 0)
	{
		std::printf("test_console_backend: PASS\n");
		return 0;
	}
	std::fprintf(stderr, "test_console_backend: %d failure(s)\n", Failures());
	return 1;
}
