// Caliza contract tests: ordered commands, fake-backend flows, browsing, scan cancellation and disconnects.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test_contract_common.h"

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

CommandEvent PostAndWait(Backend& backend, EventLog& log, const Command& command, int timeout_ms = 5000)
{
	const CommandId id = backend.Post(command);
	const CommandEvent* terminal = log.WaitTerminal(backend, id, timeout_ms);
	if (!terminal)
	{
		Fail(__FILE__, __LINE__, "timed out waiting for command " + std::to_string(static_cast<int>(command.type)));
		return {};
	}
	return *terminal;
}

void ExpectSuccess(Backend& backend, EventLog& log, const Command& command, const char* what)
{
	const CommandEvent event = PostAndWait(backend, log, command);
	if (event.status != CommandStatus::Succeeded)
		Fail(__FILE__, __LINE__, std::string(what) + " failed: " + event.message);
}

void ExpectFailure(Backend& backend, EventLog& log, const Command& command, const char* what)
{
	const CommandEvent event = PostAndWait(backend, log, command);
	if (event.status != CommandStatus::Failed)
		Fail(__FILE__, __LINE__, std::string(what) + " unexpectedly succeeded: " + event.message);
}

void SeedUsbGames(FakeBackend& fake, uint32_t count, const std::string& dir = "games")
{
	for (uint32_t i = 0; i < count; i++)
	{
		fake.AddFile("fake-usb0", dir + "/Game " + std::to_string(i) + " (USA).iso", 700ull * 1024ull * 1024ull, false,
			"Game " + std::to_string(i), "SLUS-200" + std::to_string(10 + i), "USA");
	}
}

void ScanUsb(Backend& backend, EventLog& log)
{
	ExpectSuccess(backend, log, MakeCommand(CommandType::ScanSource, "fake:usb0"), "USB scan");
}

void TestOrderedCommandsAndRevisions()
{
	FakeBackend fake;
	EventLog log;
	const CommandId first = fake.Post(MakeCommand(CommandType::SetAccessibility, "high_contrast", {}, "true"));
	const CommandId second = fake.Post(MakeCommand(CommandType::SetSetting, "system.sound", {}, "false"));
	const CommandId third = fake.Post(MakeCommand(CommandType::SetAccessibility, "text_scale", {}, {}, 150));
	const CommandEvent* first_done_ptr = log.WaitTerminal(fake, first);
	CHECK_TRUE(first_done_ptr != nullptr);
	if (!first_done_ptr)
		return;
	const CommandEvent first_done = *first_done_ptr;
	const CommandEvent* second_done_ptr = log.WaitTerminal(fake, second);
	CHECK_TRUE(second_done_ptr != nullptr);
	if (!second_done_ptr)
		return;
	const CommandEvent second_done = *second_done_ptr;
	const CommandEvent* third_done_ptr = log.WaitTerminal(fake, third);
	CHECK_TRUE(third_done_ptr != nullptr);
	if (!third_done_ptr)
		return;
	const CommandEvent third_done = *third_done_ptr;
	CHECK_EQ(static_cast<int>(first_done.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_EQ(static_cast<int>(second_done.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_EQ(static_cast<int>(third_done.status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_TRUE(first_done.revision < second_done.revision);
	CHECK_TRUE(second_done.revision < third_done.revision);
	const Snapshot snapshot = fake.GetSnapshot();
	CHECK_TRUE(snapshot.revision >= third_done.revision);
	CHECK_TRUE(snapshot.accessibility.high_contrast);
	CHECK_EQ(snapshot.accessibility.text_scale_percent, 150u);
	bool sound_found = false;
	for (const SettingCategory& category : snapshot.settings)
		for (const SettingItem& item : category.items)
			if (item.id == "system.sound")
			{
				sound_found = true;
				CHECK_EQ(item.value, std::string("false"));
				CHECK_TRUE(item.modified);
			}
	CHECK_TRUE(sound_found);
}

void TestFolderBrowsingBreadcrumbsFilterPagination()
{
	FakeBackend fake;
	fake.AddDirectory("fake-usb0", "games/empty");
	fake.AddFile("fake-usb0", "games/sub/Gamma (Europe).chd", 500000000, false, "Gamma", "SLES-12345", "Europe");
	fake.AddFile("fake-usb0", "games/Alpha (USA).iso", 700000000, false, "Alpha", "SLUS-11111", "USA");
	fake.AddFile("fake-usb0", "games/Beta (Japan).iso", 700000000, false, "Beta", "SLPS-22222", "Japan");
	fake.AddFile("fake-usb0", "games/notes.txt", 20);
	fake.AddFile("fake-usb0", "games/" + std::string(180, 'x') + " (USA).iso", 700000000, false, "Long", "SLUS-33333", "USA");

	const FolderListing first_page = fake.BrowseFolder("/mnt/usb0/games", "", 0, 2);
	CHECK_TRUE(first_page.error.empty());
	CHECK_EQ(first_page.entries.size(), 2u);
	CHECK_TRUE(first_page.truncated);
	CHECK_TRUE(first_page.entries[0].directory);
	CHECK_TRUE(first_page.entries[1].directory);
	CHECK_EQ(first_page.breadcrumbs.size(), 2u);
	if (first_page.breadcrumbs.size() == 2)
	{
		CHECK_EQ(first_page.breadcrumbs[0], std::string("USB drive"));
		CHECK_EQ(first_page.breadcrumbs[1], std::string("games"));
	}
	CHECK_EQ(first_page.parent, std::string("/mnt/usb0"));
	CHECK_EQ(first_page.volume_id, std::string("fake-usb0"));
	bool saw_sub = false;
	for (const FolderEntry& entry : first_page.entries)
		if (entry.name == "sub")
		{
			saw_sub = true;
			CHECK_EQ(entry.compatible_count, 1u);
		}
	CHECK_TRUE(saw_sub);

	const FolderListing filtered = fake.BrowseFolder("/mnt/usb0/games", "alpha", 0, 0);
	CHECK_EQ(filtered.entries.size(), 1u);
	CHECK_FALSE(filtered.truncated);
	if (!filtered.entries.empty())
	{
		CHECK_EQ(filtered.entries[0].name, std::string("Alpha (USA).iso"));
		CHECK_TRUE(filtered.entries[0].image);
	}
	const FolderListing second_page = fake.BrowseFolder("/mnt/usb0/games", "", 2, 2);
	CHECK_EQ(second_page.entries.size(), 2u);
	CHECK_TRUE(second_page.truncated);
	bool long_name_seen = false;
	for (uint32_t offset = 0; offset < 16; offset++)
	{
		const FolderListing page = fake.BrowseFolder("/mnt/usb0/games", "", offset, 1);
		if (!page.entries.empty() && page.entries[0].name.size() > 150)
			long_name_seen = true;
	}
	CHECK_TRUE(long_name_seen);
	const FolderListing missing = fake.BrowseFolder("/mnt/usb0/not-there", "", 0, 0);
	CHECK_FALSE(missing.error.empty());
}

void TestScanLibrarySourceLifecycle()
{
	FakeBackend fake;
	EventLog log;
	fake.AddFile("fake-usb0", "games/Valid Game (USA).iso", 700000000, false, "Valid Game", "SLUS-10001", "USA");
	fake.AddFile("fake-usb0", "games/Damaged Game (USA).iso", 123, true, "Damaged Game", "SLUS-10002", "USA");
	ScanUsb(fake, log);

	Snapshot snapshot = fake.GetSnapshot();
	CHECK_EQ(snapshot.games.size(), 2u);
	CHECK_EQ(static_cast<int>(snapshot.scan.state), static_cast<int>(ScanState::Complete));
	bool damaged_seen = false;
	for (const GameEntry& game : snapshot.games)
	{
		if (game.file.find("Damaged") != std::string::npos)
		{
			damaged_seen = true;
			CHECK_TRUE(game.damaged);
			CHECK_FALSE(game.can_launch);
			CHECK_FALSE(game.problem.empty());
		}
	}
	CHECK_TRUE(damaged_seen);

	fake.AddDirectory("fake-usb0", "custom");
	fake.AddFile("fake-usb0", "custom/Custom Game (Europe).iso", 600000000, false, "Custom Game", "SLES-10003", "Europe");
	ExpectSuccess(fake, log, MakeCommand(CommandType::AddLibraryFolder, {}, "/mnt/usb0/custom", "Custom Games"), "add library folder");
	snapshot = fake.GetSnapshot();
	std::string saved_id;
	for (const LibrarySource& source : snapshot.sources)
		if (source.label == "Custom Games")
			saved_id = source.id;
	CHECK_FALSE(saved_id.empty());
	if (!saved_id.empty())
	{
		ExpectSuccess(fake, log, MakeCommand(CommandType::ScanSource, saved_id), "scan saved folder");
		snapshot = fake.GetSnapshot();
		bool custom_game_seen = false;
		for (const GameEntry& game : snapshot.games)
			custom_game_seen = custom_game_seen || game.title == "Custom Game";
		CHECK_TRUE(custom_game_seen);

		fake.AddDirectory("fake-usb0", "custom-relocated");
		ExpectSuccess(fake, log, MakeCommand(CommandType::RelocateLibraryFolder, saved_id, "/mnt/usb0/custom-relocated"), "relocate saved link");
		snapshot = fake.GetSnapshot();
		bool relocated_seen = false;
		for (const LibrarySource& source : snapshot.sources)
			relocated_seen = relocated_seen || source.path == "/mnt/usb0/custom-relocated";
		CHECK_TRUE(relocated_seen);
		ExpectSuccess(fake, log, MakeCommand(CommandType::RemoveLibraryFolder, saved_id), "remove saved link");
		const FolderListing still_there = fake.BrowseFolder("/mnt/usb0/custom", "", 0, 0);
		CHECK_TRUE(still_there.error.empty());
		CHECK_FALSE(still_there.entries.empty());
	}

	ExpectSuccess(fake, log, MakeCommand(CommandType::RescanAll), "rescan all");
}

void TestScanCancellation()
{
	FakeBackendOptions options;
	options.scan_step_delay_microseconds = 8000;
	FakeBackend fake(options);
	SeedUsbGames(fake, 30);
	EventLog log;
	const CommandId scan_id = fake.Post(MakeCommand(CommandType::ScanSource, "fake:usb0"));
	CHECK_TRUE(WaitUntil([&fake] {
		const Snapshot snapshot = fake.GetSnapshot();
		return snapshot.scan.state == ScanState::Scanning && snapshot.scan.files_seen > 0;
	}));
	const CommandId cancel_id = fake.Post(MakeCommand(CommandType::CancelScan, "fake:usb0"));
	const CommandEvent* cancel_done = log.WaitTerminal(fake, cancel_id);
	const CommandEvent* scan_done = log.WaitTerminal(fake, scan_id);
	CHECK_TRUE(cancel_done != nullptr);
	CHECK_TRUE(scan_done != nullptr);
	if (!cancel_done || !scan_done)
		return;
	CHECK_EQ(static_cast<int>(cancel_done->status), static_cast<int>(CommandStatus::Succeeded));
	CHECK_EQ(static_cast<int>(scan_done->status), static_cast<int>(CommandStatus::Cancelled));
	const Snapshot snapshot = fake.GetSnapshot();
	CHECK_EQ(static_cast<int>(snapshot.scan.state), static_cast<int>(ScanState::Cancelled));
	CHECK_TRUE(scan_done->revision >= snapshot.revision);
	CHECK_TRUE(snapshot.scan.files_seen < 30u);
}

void TestDisconnectDuringScanAndReconnect()
{
	FakeBackendOptions options;
	options.scan_step_delay_microseconds = 5000;
	FakeBackend fake(options);
	SeedUsbGames(fake, 24);
	EventLog log;
	const CommandId scan_id = fake.Post(MakeCommand(CommandType::ScanSource, "fake:usb0"));
	CHECK_TRUE(WaitUntil([&fake] {
		const Snapshot snapshot = fake.GetSnapshot();
		return snapshot.scan.state == ScanState::Scanning && snapshot.scan.files_seen > 0;
	}));
	fake.SetVolumePresent("fake-usb0", false);
	const CommandEvent* scan_done = log.WaitTerminal(fake, scan_id);
	CHECK_TRUE(scan_done != nullptr);
	if (!scan_done)
		return;
	CHECK_EQ(static_cast<int>(scan_done->status), static_cast<int>(CommandStatus::Failed));
	const Snapshot disconnected = fake.GetSnapshot();
	CHECK_EQ(static_cast<int>(disconnected.scan.state), static_cast<int>(ScanState::Failed));
	CHECK_TRUE(disconnected.scan.error.find("disconnect") != std::string::npos);
	CHECK_FALSE(disconnected.games.empty());

	fake.SetVolumePresent("fake-usb0", true);
	const Snapshot reconnected = fake.GetSnapshot();
	bool connected_source_seen = false;
	for (const LibrarySource& source : reconnected.sources)
		if (source.id == "fake:usb0")
			connected_source_seen = source.state == SourceState::Connected;
	CHECK_TRUE(connected_source_seen);
	CHECK_EQ(reconnected.games.size(), disconnected.games.size());
}

void TestEveryNonScanCommandFamily()
{
	FakeBackend fake;
	EventLog log;
	fake.AddFile("fake-usb0", "games/Valid Game (USA).iso", 700000000, false, "Valid Game", "SLUS-10001", "USA");
	fake.AddFile("fake-usb0", "games/Damaged Game (USA).iso", 123, true, "Damaged Game", "SLUS-10002", "USA");
	fake.AddFile("fake-usb0", "games/Second Disc (USA).iso", 700000000, false, "Second Disc", "SLUS-10004", "USA");
	ScanUsb(fake, log);
	const Snapshot scanned = fake.GetSnapshot();
	CHECK_EQ(scanned.games.size(), 3u);
	const std::string valid_id = scanned.games.front().id;
	std::string damaged_id;
	std::string real_valid_id;
	for (const GameEntry& game : scanned.games)
	{
		if (game.damaged)
			damaged_id = game.id;
		else if (game.title == "Valid Game")
			real_valid_id = game.id;
	}
	CHECK_FALSE(damaged_id.empty());
	CHECK_FALSE(real_valid_id.empty());

	ExpectFailure(fake, log, MakeCommand(CommandType::LaunchGame, damaged_id), "damaged launch");
	fake.SetBiosPresent(false);
	ExpectFailure(fake, log, MakeCommand(CommandType::LaunchGame, real_valid_id), "launch without BIOS");
	ExpectFailure(fake, log, MakeCommand(CommandType::LaunchSystemMenu), "system menu without BIOS");
	fake.SetBiosPresent(true);
	ExpectSuccess(fake, log, MakeCommand(CommandType::LaunchGame, real_valid_id), "launch valid game");
	ExpectSuccess(fake, log, MakeCommand(CommandType::PauseVm), "pause");
	ExpectSuccess(fake, log, MakeCommand(CommandType::SaveState, {}, {}, {}, 2), "save state");
	ExpectSuccess(fake, log, MakeCommand(CommandType::LoadState, {}, {}, {}, 2), "load state");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ResumeVm), "resume");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ChangeDisc, {}, "/mnt/usb0/games/Second Disc (USA).iso"), "change disc");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ReturnToLibrary), "return to library");
	ExpectSuccess(fake, log, MakeCommand(CommandType::LaunchSystemMenu), "launch system menu");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ReturnToLibrary), "return from system menu");
	ExpectSuccess(fake, log, MakeCommand(CommandType::SetBiosFolder, {}, "/mnt/usb0"), "set BIOS folder");

	ExpectFailure(fake, log, MakeCommand(CommandType::SetSetting, "emulation.preset", {}, "not-a-preset"), "invalid setting choice");
	ExpectSuccess(fake, log, MakeCommand(CommandType::SetSetting, "emulation.preset", {}, "performance"), "set setting");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ResetSetting, "emulation.preset"), "reset setting");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ApplyRecommendedSettings), "apply recommended settings");

	ExpectSuccess(fake, log, MakeCommand(CommandType::CreateMemoryCard, "card", {}, {}, 8), "create memory card");
	ExpectFailure(fake, log, MakeCommand(CommandType::CreateMemoryCard, "card", {}, {}, 8), "duplicate memory card");
	ExpectSuccess(fake, log, MakeCommand(CommandType::AssignMemoryCard, "card.ps2", {}, {}, 1), "assign memory card");
	Snapshot snapshot = fake.GetSnapshot();
	CHECK_TRUE(snapshot.memory_cards.size() >= 2);
	if (snapshot.memory_cards.size() >= 2)
	{
		CHECK_TRUE(snapshot.memory_cards[1].present);
		CHECK_TRUE(snapshot.memory_cards[1].enabled);
		CHECK_TRUE(snapshot.memory_cards[1].file.find("card.ps2") != std::string::npos);
	}

	ExpectSuccess(fake, log, MakeCommand(CommandType::SetControlBinding, "confirm", {}, "circle"), "conflicting control binding");
	snapshot = fake.GetSnapshot();
	bool conflict_seen = false;
	for (const ControlBinding& control : snapshot.controls)
		conflict_seen = conflict_seen || control.conflict;
	CHECK_TRUE(conflict_seen);
	ExpectSuccess(fake, log, MakeCommand(CommandType::ResetControlBindings), "reset control bindings");
	snapshot = fake.GetSnapshot();
	for (const ControlBinding& control : snapshot.controls)
		CHECK_FALSE(control.conflict);

	ExpectFailure(fake, log, MakeCommand(CommandType::SetAccessibility, "text_scale", {}, {}, 300), "invalid accessibility scale");
	ExpectSuccess(fake, log, MakeCommand(CommandType::SetAccessibility, "high_contrast", {}, "true"), "accessibility");

	// The expanded contract's storage/settings/service/runtime commands are explicit fake state transitions too.
	fake.AddFile("fake-internal", "bios/scph10000.bin", 4 * 1024 * 1024);
	ExpectFailure(fake, log, MakeCommand(CommandType::SelectBios, {}, "/data/caliza/bios/missing.bin"), "missing BIOS selection");
	ExpectSuccess(fake, log, MakeCommand(CommandType::SelectBios, {}, "/data/caliza/bios/scph10000.bin"), "select BIOS");
	ExpectSuccess(fake, log, MakeCommand(CommandType::SetSetting, "system.sound", {}, "false"), "set before unset");
	ExpectSuccess(fake, log, MakeCommand(CommandType::UnsetSetting, "system.sound"), "unset setting");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ResetSettingsScope, {}, {}, "settings"), "reset settings scope");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ResetSettingsScope, {}, {}, "controls"), "reset controls scope");
	ExpectSuccess(fake, log, MakeCommand(CommandType::FetchOnlinePatches, real_valid_id), "fetch online patches");
	ExpectSuccess(fake, log, MakeCommand(CommandType::RetryTexturePackCatalog, real_valid_id), "retry texture catalog");
	ExpectSuccess(fake, log, MakeCommand(CommandType::DownloadTexturePack, real_valid_id, {}, {}, 0), "download texture pack");
	ExpectSuccess(fake, log, MakeCommand(CommandType::CancelTexturePack, real_valid_id), "cancel/forget texture pack");
	ExpectFailure(fake, log, MakeCommand(CommandType::CancelTexturePack, real_valid_id), "missing texture operation");
	ExpectSuccess(fake, log, MakeCommand(CommandType::DownloadTexturePack, real_valid_id, {}, {}, 0), "download texture pack again");
	ExpectSuccess(fake, log, MakeCommand(CommandType::DeleteTexturePack, real_valid_id), "delete texture pack");
	ExpectFailure(fake, log, MakeCommand(CommandType::DeleteTexturePack, real_valid_id), "delete missing texture pack");
	ExpectSuccess(fake, log, MakeCommand(CommandType::LaunchSmartTune), "launch Smart Tune");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ToggleFastForward), "toggle fast forward");
	ExpectSuccess(fake, log, MakeCommand(CommandType::OpenCompanionPanel), "open companion panel");
	ExpectSuccess(fake, log, MakeCommand(CommandType::ReturnToLibrary), "return from Smart Tune");
	ExpectFailure(fake, log, MakeCommand(CommandType::SignInAchievements), "empty achievement sign-in");
	ExpectSuccess(fake, log, MakeCommand(CommandType::SignInAchievements, "tester"), "achievement sign-in");
	ExpectSuccess(fake, log, MakeCommand(CommandType::RefreshAchievements, real_valid_id), "refresh achievements");
	ExpectFailure(fake, log, MakeCommand(CommandType::CancelAchievementLoad), "cancel inactive achievement load");
	ExpectSuccess(fake, log, MakeCommand(CommandType::SignOutAchievements), "achievement sign-out");
	snapshot = fake.GetSnapshot();
	CHECK_TRUE(snapshot.diagnostics.web_panel_connected);
	CHECK_FALSE(snapshot.achievements.signed_in);

	ExpectSuccess(fake, log, MakeCommand(CommandType::GenerateReport), "generate report");
	snapshot = fake.GetSnapshot();
	CHECK_TRUE(snapshot.diagnostics.report_available);

	fake.FailNextCommand(CommandType::GenerateReport, "injected failure");
	ExpectFailure(fake, log, MakeCommand(CommandType::GenerateReport), "injected command failure");
	(void)valid_id;
}
} // namespace

int main()
{
	TestOrderedCommandsAndRevisions();
	TestFolderBrowsingBreadcrumbsFilterPagination();
	TestScanLibrarySourceLifecycle();
	TestScanCancellation();
	TestDisconnectDuringScanAndReconnect();
	TestEveryNonScanCommandFamily();
	if (Failures() == 0)
	{
		std::cout << "test_contract: PASS\n";
		return 0;
	}
	std::cerr << "test_contract: " << Failures() << " failure(s)\n";
	return 1;
}
