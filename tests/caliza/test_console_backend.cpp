// Caliza console adapter tests: complete schema projection, serialized mutation ownership and honest launch/runtime handoff.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test_contract_common.h"
#include "../../caliza/core/console_backend.h"

#include <algorithm>
#include <set>

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
	CHECK_TRUE(ReadFile(root.path + "/settings/Valid Game (USA).ini").find("AspectRatio=4:3") != std::string::npos);
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
	if (Failures() == 0)
	{
		std::printf("test_console_backend: PASS\n");
		return 0;
	}
	std::fprintf(stderr, "test_console_backend: %d failure(s)\n", Failures());
	return 1;
}
