// Caliza console adapter tests: complete schema projection, serialized mutation ownership and honest launch/runtime handoff.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test_contract_common.h"
#include "../../caliza/core/console_backend.h"

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
	bool texture_cancelled = false;
	options.hooks.fetch_online_patches = [&patch_started](const GameEntry& game, std::string*) {
		patch_started = game.serial == "SLUS-20010" || !game.serial.empty();
		return patch_started;
	};
	options.hooks.download_texture_pack = [&texture_pack](const GameEntry& game, int pack_index, std::string*) {
		texture_pack = game.serial.empty() ? -2 : pack_index;
		return true;
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
	const CommandEvent texture = WaitTerminal(backend, log, backend.Post(MakeCommand(CommandType::DownloadTexturePack, game_id, {}, {}, 2)));
	CHECK_EQ(static_cast<int>(texture.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_EQ(texture_pack, 2);
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
