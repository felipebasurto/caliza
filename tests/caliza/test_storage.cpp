// Caliza storage tests: POSIX discovery, browsing, saved-source persistence and real scan cancellation.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test_contract_common.h"

using namespace caliza;
using namespace caliza_test;

namespace
{
Command ScanCommand(const std::string& source)
{
	Command command;
	command.type = CommandType::ScanSource;
	command.id = source;
	return command;
}

Command CancelCommand(const std::string& source)
{
	Command command;
	command.type = CommandType::CancelScan;
	command.id = source;
	return command;
}

VolumeInfo MakeVolume(const std::string& root, VolumeKind kind, int slot, const std::string& id, const std::string& fingerprint)
{
	VolumeInfo volume;
	volume.id = id;
	volume.root_path = root;
	volume.label = root;
	volume.kind = kind;
	volume.slot = slot;
	volume.present = true;
	volume.readable = true;
	volume.fingerprint = fingerprint;
	return volume;
}

void TestDiscoveryAndBrowse()
{
	TempDir temp;
	const std::string data = temp.path + "/data";
	const std::string usb0 = temp.path + "/usb0";
	const std::string usb1 = temp.path + "/usb1";
	const std::string ext0 = temp.path + "/ext0";
	std::string error;
	CHECK_TRUE(storage::fs::MakeDirs(data + "/games", &error));
	CHECK_TRUE(storage::fs::MakeDirs(usb0 + "/games/sub", &error));
	CHECK_TRUE(storage::fs::MakeDirs(usb0 + "/games/empty", &error));
	CHECK_TRUE(storage::fs::MakeDirs(usb0 + "/games/unreadable", &error));
	CHECK_TRUE(storage::fs::MakeDirs(usb1, &error));
	CHECK_TRUE(storage::fs::MakeDirs(ext0 + "/system_ex", &error));
	chmod((usb0 + "/games/unreadable").c_str(), 0000);
	chmod(usb1.c_str(), 0000);

	storage::StorageDiscoveryOptions options;
	options.internal_root = data;
	options.usb_roots = {usb0, usb1};
	options.extended_roots = {ext0};
	const std::vector<VolumeInfo> volumes = storage::DiscoverVolumes(options);
	CHECK_EQ(volumes.size(), 4u);
	if (volumes.size() == 4)
	{
		CHECK_TRUE(volumes[0].present);
		CHECK_TRUE(volumes[0].readable);
		CHECK_EQ(static_cast<int>(volumes[0].kind), static_cast<int>(VolumeKind::Internal));
		CHECK_TRUE(volumes[1].present);
		CHECK_TRUE(volumes[1].readable);
		CHECK_TRUE(volumes[2].present);
		if (geteuid() != 0)
			CHECK_FALSE(volumes[2].readable);
		CHECK_EQ(static_cast<int>(volumes[3].kind), static_cast<int>(VolumeKind::Extended));
		CHECK_TRUE(volumes[3].extended_managed);
		CHECK_TRUE(volumes[3].id.empty());
		CHECK_FALSE(volumes[3].fingerprint.empty());
	}
	chmod(usb1.c_str(), 0700);

	MakeMinimalIso(usb0 + "/games/Alpha (USA).iso");
	MakeMinimalIso(usb0 + "/games/sub/Beta (Europe).iso");
	MakeMinimalIso(usb0 + "/games/sub/Duplicate Name (USA).iso");
	MakeMinimalIso(usb0 + "/games/Duplicate Name (USA).iso");
	MakeMinimalIso(usb0 + "/games/" + std::string(170, 'n') + ".iso");
	CHECK_TRUE(WriteFile(usb0 + "/games/readme.txt", "notes"));
	const FolderListing page = storage::BrowseFolderPosix(usb0 + "/games", "", 0, 2, volumes);
	CHECK_TRUE(page.error.empty());
	CHECK_EQ(page.entries.size(), 2u);
	CHECK_TRUE(page.truncated);
	CHECK_TRUE(page.entries[0].directory);
	CHECK_TRUE(page.entries[1].directory);
	CHECK_EQ(page.breadcrumbs.size(), 2u);
	if (page.breadcrumbs.size() == 2)
		CHECK_EQ(page.breadcrumbs[1], std::string("games"));
	const FolderListing filtered = storage::BrowseFolderPosix(usb0 + "/games", "alpha", 0, 0, volumes);
	CHECK_EQ(filtered.entries.size(), 1u);
	const FolderListing second = storage::BrowseFolderPosix(usb0 + "/games", "", 2, 2, volumes);
	CHECK_EQ(second.entries.size(), 2u);
	bool long_name = false;
	bool duplicate_seen = false;
	for (uint32_t offset = 0; offset < 20; offset++)
	{
		const FolderListing scan = storage::BrowseFolderPosix(usb0 + "/games", "", offset, 1, volumes);
		if (!scan.entries.empty() && scan.entries[0].name.size() > 150)
			long_name = true;
		if (!scan.entries.empty() && scan.entries[0].name == "Duplicate Name (USA).iso")
			duplicate_seen = true;
	}
	CHECK_TRUE(long_name);
	CHECK_TRUE(duplicate_seen);
	const FolderListing empty = storage::BrowseFolderPosix(usb0 + "/games/empty", "", 0, 0, volumes);
	CHECK_TRUE(empty.error.empty());
	CHECK_TRUE(empty.entries.empty());
	CHECK_FALSE(empty.contains_compatible);
	const FolderListing unreadable = storage::BrowseFolderPosix(usb0 + "/games/unreadable", "", 0, 0, volumes);
	if (geteuid() != 0)
		CHECK_FALSE(unreadable.error.empty());
	chmod((usb0 + "/games/unreadable").c_str(), 0700);
}

void TestSavedSourceStore()
{
	TempDir temp;
	const std::string usb0 = temp.path + "/usb0";
	const std::string usb1 = temp.path + "/usb1";
	std::string error;
	CHECK_TRUE(storage::fs::MakeDirs(usb0 + "/games", &error));
	CHECK_TRUE(storage::fs::MakeDirs(usb0 + "/relocated", &error));
	CHECK_TRUE(storage::fs::MakeDirs(usb1 + "/games", &error));
	const std::string fingerprint = storage::VolumeFingerprint(usb0);
	CHECK_FALSE(fingerprint.empty());
	VolumeInfo usb = MakeVolume(usb0, VolumeKind::USB, 0, "", fingerprint);
	const std::string store_path = temp.path + "/data/library/sources.ini";

	storage::SavedSourceStore store(store_path);
	CHECK_TRUE(store.Load(&error));
	const storage::AddSavedSourceResult added = store.Add(usb0 + "/games", usb, "USB games");
	CHECK_EQ(static_cast<int>(added.status), static_cast<int>(storage::AddSavedSourceStatus::Added));
	CHECK_FALSE(added.source_id.empty());
	const storage::AddSavedSourceResult duplicate = store.Add(usb0 + "/games", usb, "duplicate");
	CHECK_EQ(static_cast<int>(duplicate.status), static_cast<int>(storage::AddSavedSourceStatus::DuplicatePath));
	CHECK_EQ(symlink("games", (usb0 + "/linked-games").c_str()), 0);
	const storage::AddSavedSourceResult symlinked = store.Add(usb0 + "/linked-games", usb, "symlink");
	CHECK_EQ(static_cast<int>(symlinked.status), static_cast<int>(storage::AddSavedSourceStatus::InvalidPath));
	CHECK_TRUE(store.UpdateScan(added.source_id, 7, 12345).status == storage::SavedSourceMutationStatus::Changed);

	storage::SavedSourceStore reloaded(store_path);
	CHECK_TRUE(reloaded.Load(&error));
	CHECK_EQ(reloaded.records().size(), 1u);
	if (!reloaded.records().empty())
	{
		CHECK_EQ(reloaded.records()[0].game_count, 7u);
		CHECK_EQ(reloaded.records()[0].last_scan, 12345ull);
	}
	std::vector<LibrarySource> resolved = reloaded.Resolve({usb});
	CHECK_EQ(resolved.size(), 1u);
	if (!resolved.empty())
		CHECK_EQ(static_cast<int>(resolved[0].state), static_cast<int>(SourceState::Connected));

	CHECK_TRUE(reloaded.Relocate(added.source_id, usb0 + "/relocated", usb).status == storage::SavedSourceMutationStatus::Changed);
	CHECK_TRUE(storage::fs::IsDirectory(usb0 + "/games"));
	resolved = reloaded.Resolve({usb});
	CHECK_EQ(resolved.size(), 1u);
	if (!resolved.empty())
		CHECK_EQ(resolved[0].path, usb0 + "/relocated");

	// Missing and ambiguous non-authoritative evidence never reassociates the source to another slot.
	std::vector<LibrarySource> missing = reloaded.Resolve({});
	CHECK_EQ(missing.size(), 1u);
	if (!missing.empty())
		CHECK_EQ(static_cast<int>(missing[0].state), static_cast<int>(SourceState::Disconnected));
	VolumeInfo wrong_at_original = MakeVolume(usb0, VolumeKind::USB, 0, "", "different-fingerprint");
	VolumeInfo matching_elsewhere = MakeVolume(usb1, VolumeKind::USB, 1, "", fingerprint);
	std::vector<LibrarySource> ambiguous = reloaded.Resolve({wrong_at_original, matching_elsewhere});
	CHECK_EQ(ambiguous.size(), 1u);
	if (!ambiguous.empty())
	{
		CHECK_EQ(static_cast<int>(ambiguous[0].state), static_cast<int>(SourceState::Disconnected));
		CHECK_EQ(ambiguous[0].path, usb0 + "/relocated");
		CHECK_TRUE(ambiguous[0].problem.find("not used automatically") != std::string::npos);
	}

	CHECK_TRUE(reloaded.Remove(added.source_id).status == storage::SavedSourceMutationStatus::Changed);
	CHECK_TRUE(storage::fs::IsDirectory(usb0 + "/relocated"));
	storage::SavedSourceStore after_remove(store_path);
	CHECK_TRUE(after_remove.Load(&error));
	CHECK_TRUE(after_remove.records().empty());

	const std::string corrupt_path = temp.path + "/data/library/corrupt-sources.ini";
	CHECK_TRUE(WriteFile(corrupt_path, "not-a-caliza-store\n"));
	storage::SavedSourceStore corrupt(corrupt_path);
	CHECK_FALSE(corrupt.Load(&error));
	const storage::AddSavedSourceResult rejected_add = corrupt.Add(usb0 + "/games", usb, "must not overwrite");
	CHECK_EQ(static_cast<int>(rejected_add.status), static_cast<int>(storage::AddSavedSourceStatus::StoreWriteFailed));
	CHECK_EQ(ReadFile(corrupt_path), std::string("not-a-caliza-store\n"));

	// Durable identity lets a disk move slots without orphaning the link, and duplicate detection must use
	// that identity rather than only the old absolute mount path.
	VolumeInfo stable_at_usb0 = MakeVolume(usb0, VolumeKind::USB, 0, "stable-usb-id", "stable-fingerprint-a");
	storage::SavedSourceStore stable_store(temp.path + "/data/library/stable-sources.ini");
	CHECK_TRUE(stable_store.Load(&error));
	const storage::AddSavedSourceResult stable_added = stable_store.Add(usb0 + "/games", stable_at_usb0, "Stable games");
	CHECK_EQ(static_cast<int>(stable_added.status), static_cast<int>(storage::AddSavedSourceStatus::Added));
	VolumeInfo stable_at_usb1 = MakeVolume(usb1, VolumeKind::USB, 1, "stable-usb-id", "stable-fingerprint-a");
	const storage::AddSavedSourceResult moved_duplicate = stable_store.Add(usb1 + "/games", stable_at_usb1, "Duplicate moved games");
	CHECK_EQ(static_cast<int>(moved_duplicate.status), static_cast<int>(storage::AddSavedSourceStatus::DuplicatePath));
	std::vector<LibrarySource> stable_resolved = stable_store.Resolve({stable_at_usb1});
	CHECK_EQ(stable_resolved.size(), 1u);
	if (!stable_resolved.empty())
		CHECK_EQ(stable_resolved[0].path, usb1 + "/games");
}

BackendOptions HostBackendOptions(const std::string& data, const std::string& usb0, uint32_t delay_us)
{
	BackendOptions options;
	options.data_root = data;
	options.legacy_data_root = data + "-legacy";
	options.discovery.internal_root = data;
	options.discovery.usb_roots = {usb0};
	options.discovery.extended_roots.clear();
	options.scan_step_delay_microseconds = delay_us;
	options.scan_progress_interval_files = 1;
	return options;
}

void TestRealScanCancellationAndDisconnect()
{
	TempDir temp;
	const std::string data = temp.path + "/data";
	const std::string usb0 = temp.path + "/usb0";
	std::string error;
	CHECK_TRUE(storage::fs::MakeDirs(usb0 + "/games", &error));
	for (int i = 0; i < 36; i++)
		MakeMinimalIso(usb0 + "/games/Game " + std::to_string(i) + " (USA).iso");

	{
		StorageBackend backend(HostBackendOptions(data, usb0, 5000));
		EventLog log;
		const CommandId scan_id = backend.Post(ScanCommand("volume:usb0"));
		CHECK_TRUE(WaitUntil([&backend] {
			const Snapshot snapshot = backend.GetSnapshot();
			return snapshot.scan.state == ScanState::Scanning && snapshot.scan.files_seen > 0;
		}));
		const CommandId cancel_id = backend.Post(CancelCommand("volume:usb0"));
		const CommandEvent* cancel_done = log.WaitTerminal(backend, cancel_id);
		const CommandEvent* scan_done = log.WaitTerminal(backend, scan_id);
		CHECK_TRUE(cancel_done != nullptr);
		CHECK_TRUE(scan_done != nullptr);
		if (cancel_done && scan_done)
		{
			CHECK_EQ(static_cast<int>(cancel_done->status), static_cast<int>(CommandStatus::Succeeded));
			CHECK_EQ(static_cast<int>(scan_done->status), static_cast<int>(CommandStatus::Cancelled));
		}
		CHECK_EQ(static_cast<int>(backend.GetSnapshot().scan.state), static_cast<int>(ScanState::Cancelled));
	}

	{
		StorageBackend backend(HostBackendOptions(data, usb0, 5000));
		EventLog log;
		const CommandId scan_id = backend.Post(ScanCommand("volume:usb0"));
		CHECK_TRUE(WaitUntil([&backend] {
			const Snapshot snapshot = backend.GetSnapshot();
			return snapshot.scan.state == ScanState::Scanning && snapshot.scan.files_seen > 0;
		}));
		const std::string offline = temp.path + "/usb0-offline";
		CHECK_EQ(rename(usb0.c_str(), offline.c_str()), 0);
		const CommandEvent* scan_done = log.WaitTerminal(backend, scan_id);
		CHECK_TRUE(scan_done != nullptr);
		if (scan_done)
			CHECK_EQ(static_cast<int>(scan_done->status), static_cast<int>(CommandStatus::Failed));
		const Snapshot snapshot = backend.GetSnapshot();
		CHECK_EQ(static_cast<int>(snapshot.scan.state), static_cast<int>(ScanState::Failed));
		CHECK_TRUE(snapshot.scan.error.find("disappeared") != std::string::npos);
		CHECK_FALSE(snapshot.games.empty());
		CHECK_EQ(rename(offline.c_str(), usb0.c_str()), 0);
	}
}

void TestBackendSavedSourceRestartPersistence()
{
	TempDir temp;
	const std::string data = temp.path + "/data";
	const std::string usb0 = temp.path + "/usb0";
	std::string error;
	CHECK_TRUE(storage::fs::MakeDirs(usb0 + "/custom", &error));
	MakeMinimalIso(usb0 + "/custom/Persistent Game (USA).iso");

	std::string saved_id;
	{
		StorageBackend backend(HostBackendOptions(data, usb0, 0));
		EventLog log;
		Command add;
		add.type = CommandType::AddLibraryFolder;
		add.path = usb0 + "/custom";
		add.value = "Persistent custom folder";
		const CommandId add_id = backend.Post(add);
		const CommandEvent* added = log.WaitTerminal(backend, add_id);
		CHECK_TRUE(added != nullptr);
		if (!added || added->status != CommandStatus::Succeeded)
			return;
		for (const LibrarySource& source : backend.GetSnapshot().sources)
			if (source.label == "Persistent custom folder")
				saved_id = source.id;
		CHECK_FALSE(saved_id.empty());
		if (saved_id.empty())
			return;
		const CommandId scan_id = backend.Post(ScanCommand(saved_id));
		const CommandEvent* scanned = log.WaitTerminal(backend, scan_id);
		CHECK_TRUE(scanned != nullptr);
		if (scanned)
			CHECK_EQ(static_cast<int>(scanned->status), static_cast<int>(CommandStatus::Succeeded));
		bool serial_seen = false;
		for (const GameEntry& game : backend.GetSnapshot().games)
			serial_seen = serial_seen || game.serial == "SLUS-20010";
		CHECK_TRUE(serial_seen);
	}

	{
		StorageBackend restarted(HostBackendOptions(data, usb0, 0));
		bool restored = false;
		for (const LibrarySource& source : restarted.GetSnapshot().sources)
		{
			if (source.id != saved_id)
				continue;
			restored = true;
			CHECK_EQ(static_cast<int>(source.state), static_cast<int>(SourceState::Connected));
			CHECK_EQ(source.path, usb0 + "/custom");
			CHECK_EQ(source.game_count, 1u);
			CHECK_TRUE(source.last_scan != 0);
		}
		CHECK_TRUE(restored);
	}
}

} // namespace

int main()
{
	TestDiscoveryAndBrowse();
	TestSavedSourceStore();
	TestRealScanCancellationAndDisconnect();
	TestBackendSavedSourceRestartPersistence();
	if (Failures() == 0)
	{
		std::cout << "test_storage: PASS\n";
		return 0;
	}
	std::cerr << "test_storage: " << Failures() << " failure(s)\n";
	return 1;
}
