// Caliza migration tests: fresh copy, interruption/resume, collision preservation, symlink rejection and verified hardlinks.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test_contract_common.h"

#include <atomic>

using namespace caliza;
using namespace caliza_test;
using namespace caliza::dataroot;

namespace
{
MigrationOptions OptionsFor(const std::string& source, const std::string& destination)
{
	MigrationOptions options;
	options.source_root = source;
	options.destination_root = destination;
	options.copy_buffer_bytes = 4096;
	return options;
}

void SeedLegacyTree(const std::string& source)
{
	CHECK_TRUE(WriteFile(source + "/bios/scph10000.bin", std::string(1024 * 1024, 'b')));
	CHECK_TRUE(WriteFile(source + "/memcards/card.ps2", "memory-card"));
	CHECK_TRUE(WriteFile(source + "/savestates/game.slot", "state"));
	CHECK_TRUE(WriteFile(source + "/settings/emu.ini", "[emu]\n"));
	CHECK_TRUE(WriteFile(source + "/settings/.hidden-state", "hidden"));
	CHECK_TRUE(WriteFile(source + "/patches/SLUS-12345.pnach", "patch"));
	CHECK_TRUE(WriteFile(source + "/cheats/cheat.txt", "cheat"));
	CHECK_TRUE(WriteFile(source + "/covers/SLUS-12345.jpg", "cover"));
	CHECK_TRUE(WriteFile(source + "/lang/en.ini", "language"));
	CHECK_TRUE(WriteFile(source + "/flags/eu.png", "flag"));
	CHECK_TRUE(WriteFile(source + "/inputprofiles/pad.ini", "input"));
	CHECK_TRUE(WriteFile(source + "/gs.ini", "gs"));
	CHECK_TRUE(WriteFile(source + "/live.ini", "live"));
	CHECK_TRUE(WriteFile(source + "/playtime.dat", "playtime"));
	CHECK_TRUE(WriteFile(source + "/lastgame.txt", "lastgame"));
	CHECK_TRUE(WriteFile(source + "/games/Big Game.iso", "large-game"));
	CHECK_TRUE(WriteFile(source + "/textures/pack/data.bin", "texture-data"));
	CHECK_TRUE(WriteFile(source + "/cache/generated.bin", "cache"));
	CHECK_TRUE(WriteFile(source + "/logs/old.log", "log"));
	CHECK_TRUE(WriteFile(source + "/unpreserved.txt", "unknown"));
}

const MigrationItem* FindItem(const MigrationResult& result, const std::string& relative)
{
	for (const MigrationItem& item : result.items)
	{
		if (item.relative_path == relative)
			return &item;
	}
	return nullptr;
}

void TestFreshMigrationAndInventory()
{
	TempDir temp;
	const std::string source = temp.path + "/legacy";
	const std::string destination = temp.path + "/caliza";
	SeedLegacyTree(source);
	const std::string legacy_ini = legacy::RetiredIniNamespace() + "GameFolders=" + legacy::RetiredDataRoot() + "/games\n" +
		legacy::RetiredIniNamespace() + "Renderer=Vulkan\n";
	CHECK_TRUE(WriteFile(source + "/settings/legacy.ini", legacy_ini));
	const MigrationResult result = MigrateDataRoot(OptionsFor(source, destination));
	CHECK_EQ(static_cast<int>(result.status), static_cast<int>(MigrationStatus::Completed));
	CHECK_TRUE(result.source_preserved);
	CHECK_TRUE(result.resumable);
	CHECK_TRUE(ReadFile(source + "/bios/scph10000.bin") == std::string(1024 * 1024, 'b'));
	CHECK_EQ(ReadFile(destination + "/memcards/card.ps2"), std::string("memory-card"));
	CHECK_EQ(ReadFile(destination + "/settings/emu.ini"), std::string("[emu]\n"));
	CHECK_EQ(ReadFile(destination + "/settings/.hidden-state"), std::string("hidden"));
	CHECK_EQ(ReadFile(destination + "/inputprofiles/pad.ini"), std::string("input"));
	CHECK_EQ(ReadFile(destination + "/gs.ini"), std::string("gs"));
	const std::string migrated_ini = ReadFile(destination + "/settings/legacy.ini");
	CHECK_TRUE(migrated_ini.find("Caliza/GameFolders=/data/caliza/games") != std::string::npos);
	CHECK_TRUE(migrated_ini.find("Caliza/Renderer=Vulkan") != std::string::npos);
	CHECK_TRUE(migrated_ini.find(legacy::RetiredIniNamespace()) == std::string::npos);
	CHECK_TRUE(ReadFile(source + "/settings/legacy.ini") == legacy_ini);
	CHECK_EQ(ReadFile(destination + "/live.ini"), std::string("live"));
	CHECK_EQ(ReadFile(destination + "/playtime.dat"), std::string("playtime"));
	CHECK_EQ(ReadFile(destination + "/lastgame.txt"), std::string("lastgame"));
	CHECK_FALSE(storage::fs::Exists(destination + "/games/Big Game.iso"));
	CHECK_FALSE(storage::fs::Exists(destination + "/textures/pack/data.bin"));
	CHECK_FALSE(storage::fs::Exists(destination + "/cache/generated.bin"));
	CHECK_TRUE(storage::fs::Exists(result.manifest_path));
	CHECK_TRUE(storage::fs::Exists(result.journal_path));
	CHECK_EQ(result.large_data_exceptions.size(), 2u);
	CHECK_TRUE(result.skipped_exceptions.size() >= 3u);
	bool large_game_item_seen = false;
	for (const MigrationItem& item : result.items)
	{
		if (item.relative_path == "games")
		{
			large_game_item_seen = true;
			CHECK_EQ(static_cast<int>(item.status), static_cast<int>(MigrationItemStatus::SkippedLargeData));
			CHECK_EQ(static_cast<int>(item.action), static_cast<int>(MigrationItemAction::Reference));
		}
	}
	CHECK_TRUE(large_game_item_seen);
	const std::string manifest = ReadFile(result.manifest_path);
	CHECK_TRUE(manifest.find("games") != std::string::npos);
	CHECK_TRUE(manifest.find("textures") != std::string::npos);

	const MigrationResult repeated = MigrateDataRoot(OptionsFor(source, destination));
	CHECK_EQ(static_cast<int>(repeated.status), static_cast<int>(MigrationStatus::Completed));
	const MigrationItem* repeated_ini = FindItem(repeated, "settings/legacy.ini");
	CHECK_TRUE(repeated_ini != nullptr);
	if (repeated_ini)
		CHECK_EQ(static_cast<int>(repeated_ini->status), static_cast<int>(MigrationItemStatus::AlreadyIdentical));
}

void TestCollisionSymlinkAndNoOverwrite()
{
	TempDir temp;
	const std::string source = temp.path + "/legacy";
	const std::string destination = temp.path + "/caliza";
	CHECK_TRUE(WriteFile(source + "/settings/emu.ini", "new-source"));
	CHECK_TRUE(WriteFile(source + "/settings/same.ini", "same"));
	CHECK_TRUE(WriteFile(source + "/settings/link-target.ini", "target"));
	CHECK_TRUE(WriteFile(destination + "/settings/emu.ini", "existing-different"));
	CHECK_TRUE(WriteFile(destination + "/settings/same.ini", "same"));
	CHECK_EQ(symlink("link-target.ini", (source + "/settings/link.ini").c_str()), 0);
	const MigrationResult result = MigrateDataRoot(OptionsFor(source, destination));
	CHECK_EQ(static_cast<int>(result.status), static_cast<int>(MigrationStatus::CompletedWithIssues));
	CHECK_EQ(ReadFile(destination + "/settings/emu.ini"), std::string("existing-different"));
	CHECK_EQ(ReadFile(source + "/settings/emu.ini"), std::string("new-source"));
	CHECK_EQ(result.collisions, 1u);
	CHECK_TRUE(result.rejected >= 1);
	const MigrationItem* collision = FindItem(result, "settings/emu.ini");
	const MigrationItem* same = FindItem(result, "settings/same.ini");
	const MigrationItem* link_item = FindItem(result, "settings/link.ini");
	CHECK_TRUE(collision != nullptr);
	CHECK_TRUE(same != nullptr);
	CHECK_TRUE(link_item != nullptr);
	if (collision)
		CHECK_EQ(static_cast<int>(collision->status), static_cast<int>(MigrationItemStatus::Collision));
	if (same)
		CHECK_EQ(static_cast<int>(same->status), static_cast<int>(MigrationItemStatus::AlreadyIdentical));
	if (link_item)
		CHECK_EQ(static_cast<int>(link_item->status), static_cast<int>(MigrationItemStatus::RejectedSymlink));
}


void TestDestinationSymlinkParentIsNotFollowed()
{
	TempDir temp;
	const std::string source = temp.path + "/legacy";
	const std::string destination = temp.path + "/caliza";
	const std::string outside = temp.path + "/outside";
	std::string error;
	CHECK_TRUE(storage::fs::MakeDirs(outside, &error));
	CHECK_TRUE(WriteFile(source + "/settings/emu.ini", "must-not-escape"));
	CHECK_TRUE(storage::fs::MakeDirs(destination, &error));
	CHECK_EQ(symlink(outside.c_str(), (destination + "/settings").c_str()), 0);
	const MigrationResult result = MigrateDataRoot(OptionsFor(source, destination));
	CHECK_EQ(static_cast<int>(result.status), static_cast<int>(MigrationStatus::CompletedWithIssues));
	CHECK_FALSE(storage::fs::Exists(outside + "/emu.ini"));
	CHECK_TRUE(result.collisions >= 2);
	CHECK_TRUE(ReadFile(source + "/settings/emu.ini") == "must-not-escape");
}

void TestInterruptionAndRetry()
{
	TempDir temp;
	const std::string source = temp.path + "/legacy";
	const std::string destination = temp.path + "/caliza";
	CHECK_TRUE(WriteFile(source + "/bios/first.bin", "first"));
	CHECK_TRUE(WriteFile(source + "/memcards/second.ps2", "second"));
	CHECK_TRUE(WriteFile(source + "/settings/third.ini", "third"));
	std::atomic<uint32_t> copied_progress{0};
	const MigrationResult interrupted = MigrateDataRoot(OptionsFor(source, destination),
		[&copied_progress] { return copied_progress.load(std::memory_order_acquire) != 0; },
		[&copied_progress](const MigrationItem& item) {
			if (!item.directory && item.status == MigrationItemStatus::Copied)
				copied_progress.fetch_add(1, std::memory_order_acq_rel);
		});
	CHECK_EQ(static_cast<int>(interrupted.status), static_cast<int>(MigrationStatus::Interrupted));
	CHECK_TRUE(interrupted.resumable);
	CHECK_TRUE(storage::fs::Exists(source + "/bios/first.bin"));
	const bool any_file_copied = storage::fs::Exists(destination + "/bios/first.bin") ||
		storage::fs::Exists(destination + "/memcards/second.ps2") || storage::fs::Exists(destination + "/settings/third.ini");
	CHECK_TRUE(any_file_copied);

	const MigrationResult resumed = MigrateDataRoot(OptionsFor(source, destination));
	CHECK_EQ(static_cast<int>(resumed.status), static_cast<int>(MigrationStatus::Completed));
	CHECK_EQ(ReadFile(destination + "/bios/first.bin"), std::string("first"));
	CHECK_EQ(ReadFile(destination + "/memcards/second.ps2"), std::string("second"));
	CHECK_EQ(ReadFile(destination + "/settings/third.ini"), std::string("third"));
	CHECK_TRUE(storage::fs::Exists(source + "/settings/third.ini"));
	CHECK_TRUE(resumed.files_already_identical >= 1);
}

void TestVerifiedHardlinkLargeData()
{
	TempDir temp;
	const std::string source = temp.path + "/legacy";
	const std::string destination = temp.path + "/caliza";
	CHECK_TRUE(WriteFile(source + "/games/Big Game.iso", "large-game"));
	CHECK_TRUE(WriteFile(source + "/textures/pack/data.bin", "texture-data"));
	MigrationOptions options = OptionsFor(source, destination);
	options.large_data_strategy = MigrationLargeDataStrategy::HardlinkVerified;
	const MigrationResult result = MigrateDataRoot(options);
	CHECK_EQ(static_cast<int>(result.status), static_cast<int>(MigrationStatus::Completed));
	CHECK_TRUE(result.files_hardlinked >= 2);
	struct stat source_game = {};
	struct stat destination_game = {};
	CHECK_EQ(lstat((source + "/games/Big Game.iso").c_str(), &source_game), 0);
	CHECK_EQ(lstat((destination + "/games/Big Game.iso").c_str(), &destination_game), 0);
	CHECK_TRUE(source_game.st_dev == destination_game.st_dev);
	CHECK_TRUE(source_game.st_ino == destination_game.st_ino);
	CHECK_TRUE(storage::fs::Exists(source + "/games/Big Game.iso"));
	CHECK_EQ(result.large_data_exceptions.size(), 2u);
}

void TestNoSourceAndInvalidOverlap()
{
	TempDir temp;
	const MigrationResult missing = MigrateDataRoot(OptionsFor(temp.path + "/missing", temp.path + "/dest"));
	CHECK_EQ(static_cast<int>(missing.status), static_cast<int>(MigrationStatus::NoSource));
	CHECK_TRUE(missing.source_preserved);
	const std::string source = temp.path + "/legacy";
	CHECK_TRUE(WriteFile(source + "/settings/a.ini", "a"));
	const MigrationResult overlap = MigrateDataRoot(OptionsFor(source, source + "/inside"));
	CHECK_EQ(static_cast<int>(overlap.status), static_cast<int>(MigrationStatus::InvalidRequest));
}
} // namespace

int main()
{
	TestFreshMigrationAndInventory();
	TestCollisionSymlinkAndNoOverwrite();
	TestDestinationSymlinkParentIsNotFollowed();
	TestInterruptionAndRetry();
	TestVerifiedHardlinkLargeData();
	TestNoSourceAndInvalidOverlap();
	if (Failures() == 0)
	{
		std::cout << "test_migration: PASS\n";
		return 0;
	}
	std::cerr << "test_migration: " << Failures() << " failure(s)\n";
	return 1;
}
