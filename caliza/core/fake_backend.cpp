// Caliza PS4 core: deterministic in-memory backend for contract tests and host preview.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fake_backend.h"

#include "storage.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace caliza
{
namespace
{
using storage::fs::BaseName;
using storage::fs::DirName;
using storage::fs::IsPathWithin;
using storage::fs::JoinPath;
using storage::fs::NormalizePath;

std::string LowerAscii(std::string text)
{
	for (char& c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
}

std::vector<std::string> SplitComponents(const std::string& path)
{
	std::vector<std::string> components;
	size_t at = 0;
	while (at <= path.size())
	{
		const size_t slash = path.find('/', at);
		const std::string part = path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
		if (!part.empty())
			components.push_back(part);
		if (slash == std::string::npos)
			break;
		at = slash + 1;
	}
	return components;
}

struct FakeFile
{
	std::string path;
	std::string file;
	uint64_t bytes = 0;
	bool damaged = false;
	bool hidden = false;
	std::string title;
	std::string serial;
	std::string region;
};

struct FakeVolume
{
	VolumeInfo info;
	std::set<std::string> directories;
	std::map<std::string, FakeFile> files;
};

struct FakeScanControl
{
	std::atomic<bool> cancel{false};
};

struct FakeScanJob
{
	uint64_t batch = 0;
	std::string source_id;
	std::string root_path;
	std::shared_ptr<FakeScanControl> control;
};

struct FakeScanOutcome
{
	ScanState state = ScanState::Complete;
	std::string error;
	uint32_t files_seen = 0;
	uint32_t games_found = 0;
	std::vector<std::string> seen_ids;
};

struct FakeScanBatch
{
	CommandId command = 0;
	CommandType type = CommandType::None;
	uint32_t remaining = 0;
	bool failed = false;
	bool cancelled = false;
};

struct CommandExecution
{
	CommandStatus status = CommandStatus::Failed;
	std::string message;
	bool asynchronous = false;
};

struct FakeCardRecord
{
	std::string name;
	uint64_t bytes = 0;
};

SettingItem MakeSetting(const std::string& id, const std::string& label, const std::string& value, const std::string& description,
	SettingKind kind, std::vector<SettingChoice> choices = {})
{
	SettingItem item;
	item.id = id;
	item.label = label;
	item.value = value;
	item.description = description;
	item.kind = kind;
	item.choices = std::move(choices);
	return item;
}

std::vector<SettingCategory> MakeDefaultSettings()
{
	SettingCategory system;
	system.id = "system";
	system.label = "System";
	system.items = {
		MakeSetting("system.language", "Interface language", "auto", "Language used by the interface", SettingKind::Choice,
			{{"auto", "System language"}, {"en", "English"}, {"es", "Spanish"}, {"fr", "French"}}),
		MakeSetting("system.sound", "Interface sounds", "true", "Play navigation sounds", SettingKind::Toggle),
	};
	SettingCategory emulation;
	emulation.id = "emulation";
	emulation.label = "Emulation";
	emulation.items = {
		MakeSetting("emulation.fast_boot", "Fast boot", "true", "Skip the BIOS boot animation when possible", SettingKind::Toggle),
		MakeSetting("emulation.preset", "Emulation preset", "balanced", "Compatibility and performance preset", SettingKind::Choice,
			{{"compatibility", "Compatibility"}, {"balanced", "Balanced"}, {"performance", "Performance"}}),
		MakeSetting("emulation.cheats", "Cheats", "false", "Enable approved cheat patches", SettingKind::Toggle),
	};
	SettingCategory video;
	video.id = "video";
	video.label = "Video";
	video.items = {
		MakeSetting("video.aspect_ratio", "Aspect ratio", "auto", "Display aspect ratio", SettingKind::Choice,
			{{"auto", "Auto"}, {"4:3", "4:3"}, {"16:9", "16:9"}}),
		MakeSetting("video.internal_resolution", "Internal resolution", "1", "Renderer scale as an integer multiplier", SettingKind::Number),
	};
	SettingCategory storage;
	storage.id = "storage";
	storage.label = "Storage";
	storage.items = {
		MakeSetting("storage.scan_subfolders", "Scan subfolders", "true", "Scan folders recursively", SettingKind::Toggle),
		MakeSetting("storage.show_hidden", "Show hidden games", "false", "Show games hidden from the library", SettingKind::Toggle),
	};
	return {system, emulation, video, storage};
}

std::vector<ControlBinding> MakeDefaultControls()
{
	const std::vector<std::pair<std::string, std::string>> rows = {
		{"navigate_up", "Navigate up"},
		{"navigate_down", "Navigate down"},
		{"navigate_left", "Navigate left"},
		{"navigate_right", "Navigate right"},
		{"confirm", "Confirm"},
		{"back", "Back"},
		{"options", "Options"},
		{"pause", "Pause"},
	};
	const std::vector<std::string> buttons = {"dpad-up", "dpad-down", "dpad-left", "dpad-right", "cross", "circle", "options", "start"};
	std::vector<ControlBinding> controls;
	for (size_t i = 0; i < rows.size(); i++)
	{
		ControlBinding control;
		control.action = rows[i].first;
		control.label = rows[i].second;
		control.button = buttons[i];
		controls.push_back(std::move(control));
	}
	return controls;
}

bool ValidCardName(const std::string& name)
{
	return !name.empty() && name.size() <= 80 && name[0] != '.' && name.find('/') == std::string::npos &&
		name.find('\\') == std::string::npos && name.find("..") == std::string::npos;
}
} // namespace

class FakeBackend::Impl
{
public:
	explicit Impl(const FakeBackendOptions& input_options)
		: options(input_options)
	{
		snapshot.product = "Caliza PS4";
		snapshot.build_tag = "caliza-fake";
		snapshot.data_root = NormalizePath(options.data_root);
		snapshot.legacy_data_root = NormalizePath(options.legacy_data_root);
		snapshot.diagnostics.build_id = "caliza-fake";
		snapshot.diagnostics.console_status = "in-memory fake backend";
		snapshot.diagnostics.probe_available = true;
		snapshot.scan.state = ScanState::Idle;
		snapshot.vm.state = VmState::Idle;
		snapshot.vm.message = "fake VM is idle";
		snapshot.bios.path = JoinPath(snapshot.data_root, "bios");
		snapshot.bios.present = options.bios_present;
		snapshot.bios.problem = options.bios_present ? std::string() : "fake BIOS is absent";
		snapshot.settings = MakeDefaultSettings();
		snapshot.controls = MakeDefaultControls();
		snapshot.time_unix = options.initial_time_unix;
		for (const SettingCategory& category : snapshot.settings)
			for (const SettingItem& item : category.items)
				default_setting_values[item.id] = item.value;
		default_controls = snapshot.controls;
		for (int slot = 0; slot < 2; slot++)
		{
			MemoryCardInfo card;
			card.slot = slot;
			snapshot.memory_cards.push_back(card);
		}

		if (options.install_default_layout)
		{
			VolumeInfo internal;
			internal.id = "fake-internal";
			internal.root_path = snapshot.data_root;
			internal.label = "Internal storage";
			internal.kind = VolumeKind::Internal;
			internal.present = true;
			internal.readable = true;
			internal.fingerprint = "fake-internal-fingerprint";
			internal.total_bytes = 512ull * 1024ull * 1024ull;
			internal.free_bytes = 256ull * 1024ull * 1024ull;
			AddVolumeNoEvent(internal);
			AddDirectoryNoEvent("fake-internal", JoinPath(snapshot.data_root, "games"));

			VolumeInfo usb;
			usb.id = "fake-usb0";
			usb.root_path = "/mnt/usb0";
			usb.label = "USB drive";
			usb.kind = VolumeKind::USB;
			usb.slot = 0;
			usb.present = true;
			usb.readable = true;
			usb.fingerprint = "fake-usb0-fingerprint";
			usb.total_bytes = 16ull * 1024ull * 1024ull * 1024ull;
			usb.free_bytes = 8ull * 1024ull * 1024ull * 1024ull;
			AddVolumeNoEvent(usb);
		}
		scan_step_delay_microseconds.store(options.scan_step_delay_microseconds, std::memory_order_release);
		RebuildSourcesLocked();
		BumpRevisionLocked();

		scan_thread = std::thread(&Impl::ScanMain, this);
		command_thread = std::thread(&Impl::CommandMain, this);
	}

	~Impl()
	{
		stop_commands.store(true, std::memory_order_release);
		command_cv.notify_all();
		if (command_thread.joinable())
			command_thread.join();
		{
			std::lock_guard<std::mutex> lock(scan_mutex);
			stop_scan = true;
			if (active_scan_control)
				active_scan_control->cancel.store(true, std::memory_order_release);
			for (FakeScanJob& job : scan_queue)
				job.control->cancel.store(true, std::memory_order_release);
		}
		scan_cv.notify_all();
		if (scan_thread.joinable())
			scan_thread.join();
	}

	Snapshot GetSnapshot()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return snapshot;
	}

	CommandId Post(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const CommandId id = next_command++;
		commands.emplace_back(id, command);
		PushEventLocked(id, command.type, CommandStatus::Queued, "queued");
		command_cv.notify_one();
		return id;
	}

	std::vector<CommandEvent> PollEvents()
	{
		std::lock_guard<std::mutex> lock(mutex);
		std::vector<CommandEvent> out;
		out.swap(events);
		return out;
	}

	FolderListing BrowseFolder(const std::string& raw_path, const std::string& filter, uint32_t offset, uint32_t limit)
	{
		std::lock_guard<std::mutex> lock(mutex);
		FolderListing listing;
		listing.path = NormalizePath(raw_path.empty() ? "/" : raw_path);
		FakeVolume* volume = VolumeForPathLocked(listing.path);
		if (!volume)
		{
			listing.error = "path is not on a fake volume";
			return listing;
		}
		listing.volume_id = volume->info.id;
		if (!volume->info.present)
		{
			listing.error = "volume is disconnected";
			return listing;
		}
		if (!volume->info.readable)
		{
			listing.error = "volume is not readable";
			return listing;
		}
		if (!DirectoryExistsLocked(*volume, listing.path))
		{
			listing.error = "folder does not exist";
			return listing;
		}

		std::map<std::string, FolderEntry> entries;
		for (const std::string& dir : volume->directories)
		{
			if (dir == listing.path || DirName(dir) != listing.path)
				continue;
			FolderEntry entry;
			entry.name = BaseName(dir);
			entry.path = dir;
			entry.directory = true;
			entry.readable = true;
			entry.compatible_count = CountImagesUnderLocked(*volume, dir);
			entries[LowerAscii(entry.name) + "\x01" + entry.name] = entry;
		}
		for (const auto& pair : volume->files)
		{
			const FakeFile& file = pair.second;
			if (DirName(file.path) != listing.path)
				continue;
			FolderEntry entry;
			entry.name = file.file;
			entry.path = file.path;
			entry.directory = false;
			entry.image = storage::LooksLikeGameImageName(file.file);
			entry.readable = true;
			entries[LowerAscii(entry.name) + "\x01" + entry.name] = entry;
		}

		std::vector<FolderEntry> ordered;
		for (const auto& pair : entries)
			ordered.push_back(pair.second);
		std::sort(ordered.begin(), ordered.end(), [](const FolderEntry& a, const FolderEntry& b) {
			if (a.directory != b.directory)
				return a.directory > b.directory;
			const std::string la = LowerAscii(a.name);
			const std::string lb = LowerAscii(b.name);
			return la == lb ? a.name < b.name : la < lb;
		});

		const std::string needle = LowerAscii(filter);
		std::vector<FolderEntry> filtered;
		for (const FolderEntry& entry : ordered)
		{
			if (!needle.empty() && LowerAscii(entry.name).find(needle) == std::string::npos)
				continue;
			listing.contains_compatible = listing.contains_compatible || entry.image || entry.compatible_count != 0;
			filtered.push_back(entry);
		}
		const size_t begin = std::min<size_t>(offset, filtered.size());
		const size_t available = filtered.size() - begin;
		const size_t count = limit == 0 ? available : std::min<size_t>(limit, available);
		listing.entries.insert(listing.entries.end(), filtered.begin() + static_cast<ptrdiff_t>(begin),
			filtered.begin() + static_cast<ptrdiff_t>(begin + count));
		listing.truncated = begin + count < filtered.size();
		listing.parent = listing.path == volume->info.root_path ? std::string() : DirName(listing.path);
		listing.breadcrumbs.push_back(volume->info.label);
		if (listing.path != volume->info.root_path)
		{
			const std::string relative = listing.path.substr(volume->info.root_path.size() + 1);
			for (const std::string& component : SplitComponents(relative))
				listing.breadcrumbs.push_back(component);
		}
		return listing;
	}

	void AddVolume(const VolumeInfo& input)
	{
		std::lock_guard<std::mutex> lock(mutex);
		AddVolumeNoEvent(input);
		RebuildSourcesLocked();
		BumpRevisionLocked();
	}

	void AddDirectory(const std::string& volume_id, const std::string& path)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (AddDirectoryNoEvent(volume_id, path))
			BumpRevisionLocked();
	}

	std::string AddFile(const std::string& volume_id, const std::string& path, uint64_t bytes, bool damaged,
		const std::string& title, const std::string& serial, const std::string& region)
	{
		std::lock_guard<std::mutex> lock(mutex);
		FakeVolume* volume = FindVolumeLocked(volume_id);
		if (!volume)
			return std::string();
		const std::string full = FullPathInVolume(*volume, path);
		if (full.empty())
			return std::string();
		AddParentDirectoriesLocked(*volume, DirName(full));
		FakeFile file;
		file.path = full;
		file.file = BaseName(full);
		file.bytes = bytes;
		file.damaged = damaged;
		file.title = title.empty() ? file.file : title;
		file.serial = serial;
		file.region = region;
		volume->files[full] = file;
		BumpRevisionLocked();
		return full;
	}

	void RemoveFile(const std::string& volume_id, const std::string& path)
	{
		std::lock_guard<std::mutex> lock(mutex);
		FakeVolume* volume = FindVolumeLocked(volume_id);
		if (!volume)
			return;
		volume->files.erase(FullPathInVolume(*volume, path));
		BumpRevisionLocked();
	}

	void SetVolumePresent(const std::string& volume_id, bool present)
	{
		std::lock_guard<std::mutex> lock(mutex);
		FakeVolume* volume = FindVolumeLocked(volume_id);
		if (!volume || volume->info.present == present)
			return;
		volume->info.present = present;
		volume->info.status = present ? "readable" : "disconnected by fake host control";
		RebuildSourcesLocked();
		BumpRevisionLocked();
	}

	void SetVolumeReadable(const std::string& volume_id, bool readable)
	{
		std::lock_guard<std::mutex> lock(mutex);
		FakeVolume* volume = FindVolumeLocked(volume_id);
		if (!volume || volume->info.readable == readable)
			return;
		volume->info.readable = readable;
		volume->info.status = readable ? "readable" : "not readable by fake host control";
		RebuildSourcesLocked();
		BumpRevisionLocked();
	}

	void SetBiosPresent(bool present)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (snapshot.bios.present == present)
			return;
		snapshot.bios.present = present;
		snapshot.bios.problem = present ? std::string() : "fake BIOS is absent";
		BumpRevisionLocked();
	}

	void SetScanStepDelayMicroseconds(uint32_t microseconds)
	{
		scan_step_delay_microseconds.store(microseconds, std::memory_order_release);
	}

	void FailNextCommand(CommandType type, const std::string& message)
	{
		std::lock_guard<std::mutex> lock(mutex);
		forced_failures[type] = message;
	}

private:
	void BumpRevisionLocked()
	{
		snapshot.revision++;
		snapshot.time_unix = options.initial_time_unix + snapshot.revision;
	}

	void PushEventLocked(CommandId id, CommandType type, CommandStatus status, const std::string& message)
	{
		CommandEvent event;
		event.command = id;
		event.type = type;
		event.status = status;
		event.revision = snapshot.revision;
		event.message = message;
		events.push_back(std::move(event));
	}

	void AddVolumeNoEvent(const VolumeInfo& input)
	{
		FakeVolume volume;
		volume.info = input;
		volume.info.root_path = NormalizePath(input.root_path);
		volume.directories.insert(volume.info.root_path);
		for (FakeVolume& existing : volumes)
		{
			if (existing.info.id == volume.info.id)
			{
				volume.directories.insert(existing.directories.begin(), existing.directories.end());
				volume.files = existing.files;
				existing = std::move(volume);
				return;
			}
		}
		volumes.push_back(std::move(volume));
	}

	bool AddDirectoryNoEvent(const std::string& volume_id, const std::string& path)
	{
		FakeVolume* volume = FindVolumeLocked(volume_id);
		if (!volume)
			return false;
		const std::string full = FullPathInVolume(*volume, path);
		if (full.empty())
			return false;
		AddParentDirectoriesLocked(*volume, full);
		volume->directories.insert(full);
		return true;
	}

	FakeVolume* FindVolumeLocked(const std::string& volume_id)
	{
		for (FakeVolume& volume : volumes)
		{
			if (volume.info.id == volume_id)
				return &volume;
		}
		return nullptr;
	}

	FakeVolume* VolumeForPathLocked(const std::string& path)
	{
		FakeVolume* best = nullptr;
		size_t best_size = 0;
		for (FakeVolume& volume : volumes)
		{
			const std::string root = volume.info.root_path;
			if (root.size() > best_size && IsPathWithin(path, root))
			{
				best = &volume;
				best_size = root.size();
			}
		}
		return best;
	}

	std::string FullPathInVolume(const FakeVolume& volume, const std::string& path) const
	{
		std::string full = NormalizePath(path);
		if (full.empty() || full[0] != '/')
			full = JoinPath(volume.info.root_path, path);
		if (!IsPathWithin(full, volume.info.root_path))
			return std::string();
		return full;
	}

	void AddParentDirectoriesLocked(FakeVolume& volume, const std::string& directory)
	{
		if (!IsPathWithin(directory, volume.info.root_path))
			return;
		std::vector<std::string> chain;
		for (std::string current = directory; current != volume.info.root_path && IsPathWithin(current, volume.info.root_path);
			current = DirName(current))
		{
			chain.push_back(current);
			if (current == "/")
				break;
		}
		for (auto it = chain.rbegin(); it != chain.rend(); ++it)
			volume.directories.insert(*it);
		volume.directories.insert(volume.info.root_path);
	}

	bool DirectoryExistsLocked(const FakeVolume& volume, const std::string& path) const
	{
		if (volume.directories.count(path))
			return true;
		const std::string prefix = path == "/" ? "/" : path + "/";
		for (const auto& file : volume.files)
		{
			if (file.first.compare(0, prefix.size(), prefix) == 0)
				return true;
		}
		return false;
	}

	uint32_t CountImagesUnderLocked(const FakeVolume& volume, const std::string& root) const
	{
		uint32_t count = 0;
		const std::string prefix = root == "/" ? "/" : root + "/";
		for (const auto& pair : volume.files)
		{
			if (pair.first.compare(0, prefix.size(), prefix) == 0 && storage::LooksLikeGameImageName(pair.second.file))
				count++;
		}
		return count;
	}

	LibrarySource MakeAutoSource(const std::string& id, const std::string& path, const std::string& label, const VolumeInfo& volume) const
	{
		LibrarySource source;
		source.id = id;
		source.path = path;
		source.volume_id = volume.id;
		source.label = label;
		source.user_added = false;
		if (!volume.present)
		{
			source.state = SourceState::Disconnected;
			source.problem = "volume is disconnected";
		}
		else if (!volume.readable)
		{
			source.state = SourceState::Unreadable;
			source.problem = "volume is not readable";
		}
		else
			source.state = SourceState::Connected;
		return source;
	}

	void RebuildSourcesLocked()
	{
		std::map<std::string, LibrarySource> previous;
		for (const LibrarySource& source : snapshot.sources)
			previous[source.id] = source;
		std::vector<LibrarySource> sources;
		for (const FakeVolume& volume : volumes)
		{
			if (volume.info.kind == VolumeKind::Internal)
			{
				sources.push_back(MakeAutoSource("internal:games", JoinPath(volume.info.root_path, "games"), "Internal games", volume.info));
			}
			else if (volume.info.kind == VolumeKind::USB || volume.info.kind == VolumeKind::Extended)
			{
				const std::string prefix = volume.info.kind == VolumeKind::USB ? "fake:usb" : "fake:ext";
				sources.push_back(MakeAutoSource(prefix + std::to_string(volume.info.slot), volume.info.root_path, volume.info.label,
					volume.info));
			}
		}
		for (LibrarySource saved : saved_sources)
		{
			FakeVolume* volume = VolumeForPathLocked(saved.path);
			if (!volume || !volume->info.present)
			{
				saved.state = SourceState::Disconnected;
				saved.problem = "volume is disconnected";
			}
			else if (!volume->info.readable)
			{
				saved.state = SourceState::Unreadable;
				saved.problem = "volume is not readable";
			}
			else if (!DirectoryExistsLocked(*volume, saved.path))
			{
				saved.state = SourceState::Error;
				saved.problem = "saved folder does not exist";
			}
			else
			{
				saved.state = SourceState::Connected;
				saved.problem.clear();
			}
			sources.push_back(std::move(saved));
		}
		for (LibrarySource& source : sources)
		{
			const auto old = previous.find(source.id);
			if (old != previous.end())
			{
				source.game_count = old->second.game_count;
				source.last_scan = old->second.last_scan;
			}
			if (scanning_sources.count(source.id))
				source.state = SourceState::Scanning;
		}
		snapshot.sources = std::move(sources);
	}

	void CommandMain()
	{
		for (;;)
		{
			std::pair<CommandId, Command> work;
			{
				std::unique_lock<std::mutex> lock(mutex);
				command_cv.wait(lock, [this] { return stop_commands.load(std::memory_order_acquire) || !commands.empty(); });
				if (stop_commands.load(std::memory_order_acquire) && commands.empty())
					return;
				work = std::move(commands.front());
				commands.pop_front();
				PushEventLocked(work.first, work.second.type, CommandStatus::Running, "running");
			}
			CommandExecution execution = ExecuteCommand(work.first, work.second);
			if (!execution.asynchronous)
			{
				std::lock_guard<std::mutex> lock(mutex);
				PushEventLocked(work.first, work.second.type, execution.status, execution.message);
			}
		}
	}

	std::string ConsumeForcedFailure(CommandType type)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const auto it = forced_failures.find(type);
		if (it == forced_failures.end())
			return std::string();
		std::string message = it->second;
		forced_failures.erase(it);
		return message;
	}

	CommandExecution ExecuteCommand(CommandId command_id, const Command& command)
	{
		const std::string forced = ConsumeForcedFailure(command.type);
		if (!forced.empty())
			return {CommandStatus::Failed, forced, false};
		switch (command.type)
		{
		case CommandType::None: return {CommandStatus::Failed, "empty command", false};
		case CommandType::RescanAll: return ExecuteRescanAll(command_id);
		case CommandType::ScanSource: return ExecuteScanSource(command_id, command);
		case CommandType::CancelScan: return ExecuteCancelScan(command);
		case CommandType::AddLibraryFolder: return ExecuteAddLibraryFolder(command);
		case CommandType::RemoveLibraryFolder: return ExecuteRemoveLibraryFolder(command);
		case CommandType::RelocateLibraryFolder: return ExecuteRelocateLibraryFolder(command);
		case CommandType::LaunchGame: return ExecuteLaunchGame(command);
		case CommandType::LaunchSystemMenu: return ExecuteLaunchSystemMenu();
		case CommandType::SetBiosFolder: return ExecuteSetBiosFolder(command);
		case CommandType::SelectBios: return ExecuteSelectBios(command);
		case CommandType::SetSetting: return ExecuteSetSetting(command);
		case CommandType::UnsetSetting: return ExecuteUnsetSetting(command);
		case CommandType::ResetSetting: return ExecuteResetSetting(command);
		case CommandType::ResetSettingsScope: return ExecuteResetSettingsScope(command);
		case CommandType::ApplyRecommendedSettings: return ExecuteApplyRecommendedSettings();
		case CommandType::CreateMemoryCard: return ExecuteCreateMemoryCard(command);
		case CommandType::AssignMemoryCard: return ExecuteAssignMemoryCard(command);
		case CommandType::SetControlBinding: return ExecuteSetControlBinding(command);
		case CommandType::ResetControlBindings: return ExecuteResetControlBindings();
		case CommandType::SetAccessibility: return ExecuteSetAccessibility(command);
		case CommandType::FetchOnlinePatches: return ExecuteFetchOnlinePatches(command);
		case CommandType::RetryTexturePackCatalog: return ExecuteRetryTexturePackCatalog(command);
		case CommandType::DownloadTexturePack: return ExecuteDownloadTexturePack(command);
		case CommandType::CancelTexturePack: return ExecuteCancelTexturePack(command);
		case CommandType::DeleteTexturePack: return ExecuteDeleteTexturePack(command);
		case CommandType::LaunchSmartTune: return ExecuteLaunchSmartTune();
		case CommandType::SignInAchievements: return ExecuteSignInAchievements(command);
		case CommandType::SignOutAchievements: return ExecuteSignOutAchievements();
		case CommandType::RefreshAchievements: return ExecuteRefreshAchievements(command);
		case CommandType::CancelAchievementLoad: return ExecuteCancelAchievementLoad();
		case CommandType::SaveState: return ExecuteSaveState(command);
		case CommandType::LoadState: return ExecuteLoadState(command);
		case CommandType::ToggleFastForward: return ExecuteToggleFastForward();
		case CommandType::PauseVm: return ExecutePauseVm();
		case CommandType::ResumeVm: return ExecuteResumeVm();
		case CommandType::ReturnToLibrary: return ExecuteReturnToLibrary();
		case CommandType::ChangeDisc: return ExecuteChangeDisc(command);
		case CommandType::OpenCompanionPanel: return ExecuteOpenCompanionPanel();
		case CommandType::GenerateReport: return ExecuteGenerateReport();
		case CommandType::SelectGame: return ExecuteSelectGame(command);
		}
		return {CommandStatus::Failed, "unknown command", false};
	}

	CommandExecution ExecuteRescanAll(CommandId command_id)
	{
		std::vector<LibrarySource> sources;
		{
			std::lock_guard<std::mutex> lock(mutex);
			for (const LibrarySource& source : snapshot.sources)
			{
				if (source.state == SourceState::Connected)
					sources.push_back(source);
			}
			for (const LibrarySource& source : sources)
			{
				if (scanning_sources.count(source.id))
					return {CommandStatus::Failed, "a scan is already active", false};
			}
			if (sources.empty())
				return {CommandStatus::Succeeded, "no connected sources to scan", false};
			std::vector<std::string> ids;
			for (const LibrarySource& source : sources)
				ids.push_back(source.id);
			const uint64_t batch = RegisterScanBatchLocked(command_id, CommandType::RescanAll, ids);
			for (const LibrarySource& source : sources)
				EnqueueScanJob(batch, source);
		}
		return {CommandStatus::Running, "scanning all connected fake sources", true};
	}

	CommandExecution ExecuteScanSource(CommandId command_id, const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const auto it = std::find_if(snapshot.sources.begin(), snapshot.sources.end(), [&command](const LibrarySource& source) {
			return source.id == command.id;
		});
		if (it == snapshot.sources.end())
			return {CommandStatus::Failed, "source is not known", false};
		if (it->state != SourceState::Connected)
			return {CommandStatus::Failed, it->problem.empty() ? "source is not connected" : it->problem, false};
		if (scanning_sources.count(it->id))
			return {CommandStatus::Failed, "a scan is already active for this source", false};
		const uint64_t batch = RegisterScanBatchLocked(command_id, CommandType::ScanSource, {it->id});
		EnqueueScanJob(batch, *it);
		return {CommandStatus::Running, "scanning " + it->label, true};
	}

	CommandExecution ExecuteCancelScan(const Command& command)
	{
		const uint32_t count = CancelScanJobs(command.id);
		if (!count)
			return {CommandStatus::Failed, "no matching scan is active or queued", false};
		std::lock_guard<std::mutex> lock(mutex);
		if ((snapshot.scan.state == ScanState::Scanning || snapshot.scan.state == ScanState::Cancelling) &&
			(command.id.empty() || snapshot.scan.source_id == command.id))
		{
			snapshot.scan.cancel_requested = true;
			snapshot.scan.state = ScanState::Cancelling;
			BumpRevisionLocked();
		}
		return {CommandStatus::Succeeded, "cancellation requested for " + std::to_string(count) + " scan job(s)", false};
	}

	CommandExecution ExecuteAddLibraryFolder(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string path = NormalizePath(command.path);
		FakeVolume* volume = VolumeForPathLocked(path);
		if (!volume || !volume->info.present || !volume->info.readable)
			return {CommandStatus::Failed, "folder is not on a connected readable fake volume", false};
		if (!DirectoryExistsLocked(*volume, path))
			return {CommandStatus::Failed, "folder does not exist in the fake volume", false};
		for (const LibrarySource& source : saved_sources)
		{
			if (source.path == path)
				return {CommandStatus::Failed, "this folder is already saved", false};
		}
		LibrarySource source;
		source.id = "saved-" + std::to_string(next_saved_source++);
		source.path = path;
		source.volume_id = volume->info.id;
		source.label = command.value.empty() ? (path == volume->info.root_path ? volume->info.label : BaseName(path)) : command.value;
		source.state = SourceState::Connected;
		source.user_added = true;
		saved_sources.push_back(source);
		RebuildSourcesLocked();
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "saved folder link added as " + source.id, false};
	}

	CommandExecution ExecuteRemoveLibraryFolder(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const auto it = std::find_if(saved_sources.begin(), saved_sources.end(), [&command](const LibrarySource& source) {
			return source.id == command.id;
		});
		if (it == saved_sources.end())
			return {CommandStatus::Failed, "saved folder link is not known", false};
		saved_sources.erase(it);
		RebuildSourcesLocked();
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "saved folder link removed; no files were deleted", false};
	}

	CommandExecution ExecuteRelocateLibraryFolder(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const auto it = std::find_if(saved_sources.begin(), saved_sources.end(), [&command](const LibrarySource& source) {
			return source.id == command.id;
		});
		if (it == saved_sources.end())
			return {CommandStatus::Failed, "saved folder link is not known", false};
		const std::string path = NormalizePath(command.path);
		FakeVolume* volume = VolumeForPathLocked(path);
		if (!volume || !volume->info.present || !volume->info.readable || !DirectoryExistsLocked(*volume, path))
			return {CommandStatus::Failed, "replacement folder is not available", false};
		for (const LibrarySource& source : saved_sources)
		{
			if (source.id != command.id && source.path == path)
				return {CommandStatus::Failed, "another saved folder already uses this path", false};
		}
		it->path = path;
		it->volume_id = volume->info.id;
		RebuildSourcesLocked();
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "saved folder link relocated; no files were moved", false};
	}

	bool VolumeAvailableForPathLocked(const std::string& path) const
	{
		for (const FakeVolume& volume : volumes)
		{
			if (!IsPathWithin(path, volume.info.root_path))
				continue;
			return volume.info.present && volume.info.readable;
		}
		return false;
	}

	CommandExecution ExecuteLaunchGame(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const auto it = std::find_if(snapshot.games.begin(), snapshot.games.end(), [&command](const GameEntry& game) {
			return game.id == command.id || game.path == command.id;
		});
		if (it == snapshot.games.end())
			return {CommandStatus::Failed, "game is not in the catalog", false};
		if (it->damaged || !it->can_launch)
			return {CommandStatus::Failed, it->problem.empty() ? "game image is damaged" : it->problem, false};
		if (!snapshot.bios.present)
			return {CommandStatus::Failed, "no BIOS is available", false};
		if (!VolumeAvailableForPathLocked(it->path))
			return {CommandStatus::Failed, "the game's volume is disconnected or unreadable", false};
		snapshot.vm.state = VmState::Running;
		snapshot.vm.game_id = it->id;
		snapshot.vm.title = it->title;
		snapshot.vm.can_save = true;
		snapshot.vm.can_load = false;
		snapshot.vm.progress_seen = false;
		snapshot.vm.message = "fake VM is running";
		snapshot.continue_game_id = it->id;
		snapshot.continue_valid = true;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake game launched", false};
	}

	CommandExecution ExecuteLaunchSystemMenu()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!snapshot.bios.present)
			return {CommandStatus::Failed, "no BIOS is available", false};
		snapshot.vm.state = VmState::Running;
		snapshot.vm.game_id.clear();
		snapshot.vm.title = "PlayStation 2 system menu";
		snapshot.vm.can_save = false;
		snapshot.vm.can_load = false;
		snapshot.vm.progress_seen = false;
		snapshot.vm.message = "fake system menu is running";
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake system menu launched", false};
	}

	CommandExecution ExecuteSetBiosFolder(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string path = command.path.empty() ? command.id : command.path;
		if (path.empty())
			return {CommandStatus::Failed, "BIOS folder path is empty", false};
		snapshot.bios.path = NormalizePath(path);
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, snapshot.bios.present ? "BIOS folder configured" : "BIOS folder configured; BIOS remains absent", false};
	}

	SettingItem* FindSettingLocked(const std::string& id)
	{
		for (SettingCategory& category : snapshot.settings)
		{
			for (SettingItem& item : category.items)
			{
				if (item.id == id)
					return &item;
			}
		}
		return nullptr;
	}

	CommandExecution ExecuteSetSetting(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		SettingItem* item = FindSettingLocked(command.id);
		if (!item)
			return {CommandStatus::Failed, "setting is not known", false};
		const std::string value = command.value;
		switch (item->kind)
		{
		case SettingKind::Toggle:
			if (LowerAscii(value) != "true" && LowerAscii(value) != "false" && value != "1" && value != "0")
				return {CommandStatus::Failed, "toggle settings accept true/false or 1/0", false};
			item->value = LowerAscii(value) == "true" || value == "1" ? "true" : "false";
			break;
		case SettingKind::Choice:
		{
			const bool valid = std::any_of(item->choices.begin(), item->choices.end(), [&value](const SettingChoice& choice) {
				return choice.value == value;
			});
			if (!valid)
				return {CommandStatus::Failed, "choice is not valid for this setting", false};
			item->value = value;
			break;
		}
		case SettingKind::Number:
			if (value.empty() || std::strtol(value.c_str(), nullptr, 10) < 0)
				return {CommandStatus::Failed, "number setting requires a non-negative integer", false};
			item->value = value;
			break;
		case SettingKind::Text:
			if (value.size() > 1024)
				return {CommandStatus::Failed, "text setting is too long", false};
			item->value = value;
			break;
		case SettingKind::Action:
			break;
		}
		item->modified = default_setting_values[item->id] != item->value;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "setting updated", false};
	}

	CommandExecution ExecuteResetSetting(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		SettingItem* item = FindSettingLocked(command.id);
		const auto default_value = default_setting_values.find(command.id);
		if (!item || default_value == default_setting_values.end())
			return {CommandStatus::Failed, "setting is not known", false};
		item->value = default_value->second;
		item->modified = false;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "setting reset", false};
	}

	CommandExecution ExecuteApplyRecommendedSettings()
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::vector<std::pair<std::string, std::string>> recommended = {
			{"emulation.fast_boot", "true"},
			{"emulation.preset", "balanced"},
			{"video.aspect_ratio", "auto"},
			{"video.internal_resolution", "1"},
		};
		for (const auto& pair : recommended)
		{
			if (SettingItem* item = FindSettingLocked(pair.first))
			{
				item->value = pair.second;
				item->modified = default_setting_values[item->id] != item->value;
			}
		}
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "recommended settings applied", false};
	}

	CommandExecution ExecuteCreateMemoryCard(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		std::string name = !command.path.empty() ? command.path : (!command.id.empty() ? command.id : command.value);
		if (!ValidCardName(name))
			return {CommandStatus::Failed, "memory-card name is empty, too long, or contains a path", false};
		const uint64_t mb = command.number == 0 ? 8 : static_cast<uint64_t>(command.number);
		if (mb != 8 && mb != 16 && mb != 32 && mb != 64)
			return {CommandStatus::Failed, "memory-card size must be 8, 16, 32 or 64 MB", false};
		if (name.size() < 4 || LowerAscii(name.substr(name.size() - 4)) != ".ps2")
			name += ".ps2";
		for (const FakeCardRecord& card : cards)
		{
			if (card.name == name)
				return {CommandStatus::Failed, "a memory card with this name already exists", false};
		}
		FakeCardRecord record;
		record.name = name;
		record.bytes = mb * 1024ull * 1024ull;
		cards.push_back(record);
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake memory card created", false};
	}

	CommandExecution ExecuteAssignMemoryCard(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const int slot = command.number;
		if (slot < 0 || slot > 1)
			return {CommandStatus::Failed, "memory-card slot must be 0 or 1", false};
		std::string name = !command.path.empty() ? command.path : (!command.id.empty() ? command.id : command.value);
		name = BaseName(name);
		if (name.empty())
		{
			snapshot.memory_cards[static_cast<size_t>(slot)] = MemoryCardInfo{};
			snapshot.memory_cards[static_cast<size_t>(slot)].slot = slot;
			BumpRevisionLocked();
			return {CommandStatus::Succeeded, "memory-card slot cleared", false};
		}
		const auto it = std::find_if(cards.begin(), cards.end(), [&name](const FakeCardRecord& card) { return card.name == name; });
		if (it == cards.end())
			return {CommandStatus::Failed, "memory card does not exist in the fake backend", false};
		MemoryCardInfo info;
		info.slot = slot;
		info.present = true;
		info.enabled = true;
		info.file = JoinPath(snapshot.data_root + "/memcards", it->name);
		info.bytes = it->bytes;
		info.used_bytes = it->bytes / 3;
		snapshot.memory_cards[static_cast<size_t>(slot)] = info;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "memory card assigned", false};
	}

	CommandExecution ExecuteSetControlBinding(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string button = command.value.empty() ? command.path : command.value;
		if (button.empty())
			return {CommandStatus::Failed, "control binding button is empty", false};
		const auto it = std::find_if(snapshot.controls.begin(), snapshot.controls.end(), [&command](const ControlBinding& control) {
			return control.action == command.id;
		});
		if (it == snapshot.controls.end())
			return {CommandStatus::Failed, "control action is not known", false};
		if (!it->editable)
			return {CommandStatus::Failed, "control action is not editable", false};
		it->button = button;
		for (ControlBinding& control : snapshot.controls)
		{
			control.conflict = std::any_of(snapshot.controls.begin(), snapshot.controls.end(), [&control](const ControlBinding& other) {
				return other.action != control.action && other.button == control.button;
			});
		}
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "control binding updated", false};
	}

	CommandExecution ExecuteResetControlBindings()
	{
		std::lock_guard<std::mutex> lock(mutex);
		snapshot.controls = default_controls;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "control bindings reset", false};
	}

	CommandExecution ExecuteSetAccessibility(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string id = command.id.empty() ? command.path : command.id;
		const std::string lower = LowerAscii(command.value);
		const bool truthy = lower == "true" || lower == "1" || lower == "on" || command.number != 0;
		if (id == "text_scale" || id == "text_scale_percent")
		{
			const uint32_t scale = command.number != 0 ? static_cast<uint32_t>(command.number) :
				static_cast<uint32_t>(std::strtoul(command.value.c_str(), nullptr, 10));
			if (scale < 50 || scale > 200)
				return {CommandStatus::Failed, "text scale must be between 50 and 200 percent", false};
			snapshot.accessibility.text_scale_percent = scale;
		}
		else if (id == "high_contrast")
			snapshot.accessibility.high_contrast = truthy;
		else if (id == "reduce_motion")
			snapshot.accessibility.reduce_motion = truthy;
		else if (id == "large_focus")
			snapshot.accessibility.large_focus = truthy;
		else
			return {CommandStatus::Failed, "unknown accessibility setting", false};
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "accessibility updated", false};
	}

	CommandExecution ExecuteSaveState(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (snapshot.vm.state != VmState::Running && snapshot.vm.state != VmState::Paused)
			return {CommandStatus::Failed, "no fake VM is running", false};
		if (!snapshot.vm.can_save)
			return {CommandStatus::Failed, "this fake VM state cannot save", false};
		if (command.number < 0 || command.number > 9)
			return {CommandStatus::Failed, "save-state slot must be 0 through 9", false};
		snapshot.vm.can_load = true;
		snapshot.vm.progress_seen = true;
		snapshot.vm.message = "fake state saved in slot " + std::to_string(command.number);
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake state saved", false};
	}

	CommandExecution ExecuteLoadState(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (snapshot.vm.state != VmState::Running && snapshot.vm.state != VmState::Paused)
			return {CommandStatus::Failed, "no fake VM is running", false};
		if (!snapshot.vm.can_load)
			return {CommandStatus::Failed, "no fake state has been saved", false};
		if (command.number < 0 || command.number > 9)
			return {CommandStatus::Failed, "save-state slot must be 0 through 9", false};
		snapshot.vm.progress_seen = true;
		snapshot.vm.message = "fake state loaded from slot " + std::to_string(command.number);
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake state loaded", false};
	}

	CommandExecution ExecutePauseVm()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (snapshot.vm.state != VmState::Running)
			return {CommandStatus::Failed, "fake VM is not running", false};
		snapshot.vm.state = VmState::Paused;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake VM paused", false};
	}

	CommandExecution ExecuteResumeVm()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (snapshot.vm.state != VmState::Paused)
			return {CommandStatus::Failed, "fake VM is not paused", false};
		snapshot.vm.state = VmState::Running;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake VM resumed", false};
	}

	CommandExecution ExecuteReturnToLibrary()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (snapshot.vm.state != VmState::Running && snapshot.vm.state != VmState::Paused)
			return {CommandStatus::Failed, "no fake VM is running", false};
		snapshot.vm.state = VmState::Idle;
		snapshot.vm.game_id.clear();
		snapshot.vm.title.clear();
		snapshot.vm.can_save = false;
		snapshot.vm.can_load = false;
		snapshot.vm.message = "fake VM returned to the library";
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "returned to library", false};
	}

	CommandExecution ExecuteChangeDisc(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (snapshot.vm.state != VmState::Running && snapshot.vm.state != VmState::Paused)
			return {CommandStatus::Failed, "no fake VM is running", false};
		const std::string wanted = NormalizePath(command.path.empty() ? command.id : command.path);
		for (const FakeVolume& volume : volumes)
		{
			const auto file = volume.files.find(wanted);
			if (file == volume.files.end())
				continue;
			if (!volume.info.present || !volume.info.readable)
				return {CommandStatus::Failed, "the disc's volume is disconnected or unreadable", false};
			if (file->second.damaged)
				return {CommandStatus::Failed, "replacement disc image is damaged", false};
			snapshot.vm.game_id = file->second.path;
			snapshot.vm.title = file->second.title;
			snapshot.vm.message = "fake disc changed";
			snapshot.continue_game_id = file->second.path;
			snapshot.continue_valid = true;
			BumpRevisionLocked();
			return {CommandStatus::Succeeded, "fake disc changed", false};
		}
		return {CommandStatus::Failed, "replacement disc image is not in the fake volumes", false};
	}

	GameEntry* FindGameLocked(const std::string& key)
	{
		for (GameEntry& game : snapshot.games)
		{
			if (game.id == key || game.path == key)
				return &game;
		}
		return nullptr;
	}

	OperationStatus* FindOperationLocked(const std::string& id)
	{
		for (OperationStatus& operation : snapshot.operations)
		{
			if (operation.id == id)
				return &operation;
		}
		return nullptr;
	}

	OperationStatus& UpsertOperationLocked(const std::string& id, const std::string& label, const std::string& game_id,
		OperationState state, float progress, bool cancelable, const std::string& message)
	{
		OperationStatus* operation = FindOperationLocked(id);
		if (!operation)
		{
			OperationStatus created;
			created.id = id;
			snapshot.operations.push_back(std::move(created));
			operation = &snapshot.operations.back();
		}
		operation->label = label;
		operation->game_id = game_id;
		operation->state = state;
		operation->progress = progress;
		operation->cancelable = cancelable;
		operation->message = message;
		return *operation;
	}

	std::string ServiceGameKey(const Command& command) const
	{
		return !command.id.empty() ? command.id : (!command.path.empty() ? command.path : command.value);
	}

	CommandExecution ExecuteSelectBios(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string selected = NormalizePath(!command.path.empty() ? command.path : (!command.id.empty() ? command.id : command.value));
		if (selected.empty() || selected == ".")
		{
			BumpRevisionLocked();
			return {CommandStatus::Succeeded, "fake BIOS selection inherited", false};
		}
		for (const FakeVolume& volume : volumes)
		{
			const auto file = volume.files.find(selected);
			if (file == volume.files.end())
				continue;
			if (!volume.info.present || !volume.info.readable)
				return {CommandStatus::Failed, "the BIOS file's volume is disconnected or unreadable", false};
			snapshot.bios.path = DirName(selected);
			snapshot.bios.present = true;
			snapshot.bios.problem.clear();
			BumpRevisionLocked();
			return {CommandStatus::Succeeded, "fake BIOS file selected", false};
		}
		return {CommandStatus::Failed, "BIOS file is not present in the fake volumes", false};
	}

	CommandExecution ExecuteUnsetSetting(const Command& command)
	{
		// The fake has a single global layer, so unsetting and resetting to the default are the same visible operation.
		return ExecuteResetSetting(command);
	}

	CommandExecution ExecuteResetSettingsScope(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string scope = LowerAscii(command.id + " " + command.path + " " + command.value);
		if (scope.find("controls") != std::string::npos)
		{
			snapshot.controls = default_controls;
			BumpRevisionLocked();
			return {CommandStatus::Succeeded, "fake control scope reset", false};
		}
		for (SettingCategory& category : snapshot.settings)
		{
			for (SettingItem& item : category.items)
			{
				const auto default_value = default_setting_values.find(item.id);
				if (default_value != default_setting_values.end())
				{
					item.value = default_value->second;
					item.modified = false;
				}
			}
		}
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake settings scope reset", false};
	}

	CommandExecution ExecuteFetchOnlinePatches(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string key = ServiceGameKey(command);
		GameEntry* game = FindGameLocked(key);
		if (!game)
			return {CommandStatus::Failed, "game is not in the catalog", false};
		const std::string operation_id = "online-patches:" + game->id;
		UpsertOperationLocked(operation_id, "Online patches", game->id, OperationState::Succeeded, 1.0f, false,
			"fake patch and cheat inventory installed without replacing user files");
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake online patches fetched", false};
	}

	CommandExecution ExecuteRetryTexturePackCatalog(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string key = ServiceGameKey(command);
		GameEntry* game = FindGameLocked(key);
		if (!game)
			return {CommandStatus::Failed, "game is not in the catalog", false};
		UpsertOperationLocked("texture-catalog:" + game->id, "Texture pack catalog", game->id, OperationState::Succeeded, 1.0f,
			false, "one fake texture pack is available");
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake texture catalog refreshed", false};
	}

	CommandExecution ExecuteDownloadTexturePack(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string key = ServiceGameKey(command);
		GameEntry* game = FindGameLocked(key);
		if (!game)
			return {CommandStatus::Failed, "game is not in the catalog", false};
		if (command.number < 0 || command.number > 8)
			return {CommandStatus::Failed, "fake texture pack index must be 0 through 8", false};
		const std::string operation_id = "texture-pack:" + game->id + ":" + std::to_string(command.number);
		UpsertOperationLocked(operation_id, "Texture pack download", game->id, OperationState::Succeeded, 1.0f, false,
			"fake texture pack installed");
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake texture pack downloaded", false};
	}

	CommandExecution ExecuteCancelTexturePack(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string key = ServiceGameKey(command);
		GameEntry* game = FindGameLocked(key);
		if (!game)
			return {CommandStatus::Failed, "game is not in the catalog", false};
		const std::string prefix = "texture-pack:" + game->id + ":";
		for (auto it = snapshot.operations.begin(); it != snapshot.operations.end(); ++it)
		{
			if (it->id.compare(0, prefix.size(), prefix) != 0)
				continue;
			if (it->state == OperationState::Running || it->state == OperationState::Queued)
			{
				it->state = OperationState::Cancelled;
				it->cancelable = false;
				it->message = "fake texture pack download cancelled";
			}
			else
				snapshot.operations.erase(it);
			BumpRevisionLocked();
			return {CommandStatus::Succeeded, "fake texture pack operation cancelled or forgotten", false};
		}
		return {CommandStatus::Failed, "no fake texture pack operation exists for this game", false};
	}

	CommandExecution ExecuteDeleteTexturePack(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string key = ServiceGameKey(command);
		GameEntry* game = FindGameLocked(key);
		if (!game)
			return {CommandStatus::Failed, "game is not in the catalog", false};
		const std::string prefix = "texture-pack:" + game->id + ":";
		for (auto it = snapshot.operations.begin(); it != snapshot.operations.end(); ++it)
		{
			if (it->id.compare(0, prefix.size(), prefix) != 0)
				continue;
			if (it->state != OperationState::Succeeded)
				return {CommandStatus::Failed, "only a completed fake texture pack installation can be deleted", false};
			snapshot.operations.erase(it);
			BumpRevisionLocked();
			return {CommandStatus::Succeeded, "fake texture pack installation deleted", false};
		}
		return {CommandStatus::Failed, "no fake-installed texture pack exists for this game", false};
	}

	CommandExecution ExecuteLaunchSmartTune()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!snapshot.bios.present)
			return {CommandStatus::Failed, "no BIOS is available", false};
		snapshot.vm.state = VmState::Running;
		snapshot.vm.game_id = "smart-tune";
		snapshot.vm.title = "Smart Tune";
		snapshot.vm.can_save = false;
		snapshot.vm.can_load = false;
		snapshot.vm.progress_seen = false;
		snapshot.vm.message = "fake Smart Tune is running";
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake Smart Tune launched", false};
	}

	CommandExecution ExecuteSignInAchievements(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		std::string username = !command.id.empty() ? command.id : command.value;
		const size_t separator = username.find_first_of(" \t\n\r:+");
		if (separator != std::string::npos)
			username.resize(separator);
		if (username.empty())
			return {CommandStatus::Failed, "RetroAchievements username is empty", false};
		snapshot.achievements.signed_in = true;
		snapshot.achievements.username = username;
		snapshot.achievements.problem.clear();
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake RetroAchievements account signed in", false};
	}

	CommandExecution ExecuteSignOutAchievements()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!snapshot.achievements.signed_in)
			return {CommandStatus::Failed, "no fake RetroAchievements account is signed in", false};
		snapshot.achievements = AchievementsStatus{};
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake RetroAchievements account signed out", false};
	}

	CommandExecution ExecuteRefreshAchievements(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!snapshot.achievements.signed_in)
			return {CommandStatus::Failed, "sign in before refreshing achievements", false};
		std::string key = ServiceGameKey(command);
		if (key.empty())
			key = snapshot.vm.game_id;
		GameEntry* game = FindGameLocked(key);
		if (!game)
			return {CommandStatus::Failed, "game is not in the catalog", false};
		snapshot.achievements.loading = false;
		snapshot.achievements.game_id = game->id;
		snapshot.achievements.total = 3;
		snapshot.achievements.unlocked = 1;
		snapshot.achievements.entries.clear();
		for (uint32_t i = 0; i < 3; i++)
		{
			AchievementEntry entry;
			entry.id = "fake-" + std::to_string(i + 1);
			entry.title = "Fake achievement " + std::to_string(i + 1);
			entry.description = "Deterministic fake achievement entry";
			entry.points = (i + 1) * 5;
			entry.unlocked = i == 0;
			snapshot.achievements.entries.push_back(std::move(entry));
		}
		UpsertOperationLocked("achievements:" + game->id, "Achievement load", game->id, OperationState::Succeeded, 1.0f, false,
			"fake achievement list loaded");
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake achievements refreshed", false};
	}

	CommandExecution ExecuteCancelAchievementLoad()
	{
		std::lock_guard<std::mutex> lock(mutex);
		bool cancelled = false;
		for (OperationStatus& operation : snapshot.operations)
		{
			if (operation.id.compare(0, 13, "achievements:") == 0 &&
				(operation.state == OperationState::Running || operation.state == OperationState::Queued))
			{
				operation.state = OperationState::Cancelled;
				operation.cancelable = false;
				operation.message = "fake achievement load cancelled";
				cancelled = true;
			}
		}
		if (!snapshot.achievements.loading && !cancelled)
			return {CommandStatus::Failed, "no fake achievement load is active", false};
		snapshot.achievements.loading = false;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake achievement load cancelled", false};
	}

	CommandExecution ExecuteToggleFastForward()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (snapshot.vm.state != VmState::Running && snapshot.vm.state != VmState::Paused)
			return {CommandStatus::Failed, "no fake VM is running", false};
		fast_forward_enabled = !fast_forward_enabled;
		snapshot.vm.message = fast_forward_enabled ? "fake fast forward enabled" : "fake fast forward disabled";
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, fast_forward_enabled ? "fake fast forward enabled" : "fake fast forward disabled", false};
	}

	CommandExecution ExecuteOpenCompanionPanel()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (snapshot.vm.state != VmState::Running && snapshot.vm.state != VmState::Paused)
			return {CommandStatus::Failed, "no fake VM is running", false};
		snapshot.diagnostics.web_panel_url = "fake-companion://panel";
		snapshot.diagnostics.web_panel_connected = true;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake companion panel opened", false};
	}

	CommandExecution ExecuteGenerateReport()
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string path = JoinPath(snapshot.data_root, "logs/caliza-fake-report.txt");
		snapshot.diagnostics.report_available = true;
		if (std::find(snapshot.diagnostics.log_paths.begin(), snapshot.diagnostics.log_paths.end(), path) ==
			snapshot.diagnostics.log_paths.end())
			snapshot.diagnostics.log_paths.push_back(path);
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "fake diagnostics report generated", false};
	}

	CommandExecution ExecuteSelectGame(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string key = !command.id.empty() ? command.id : command.path;
		if (!key.empty())
		{
			const bool found = std::any_of(snapshot.games.begin(), snapshot.games.end(),
				[&](const GameEntry& game) { return game.id == key || game.path == key || game.file == key; });
			if (!found)
				return {CommandStatus::Failed, "the catalog entry is missing", false};
		}
		snapshot.selected_game_id = key;
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, key.empty() ? "global settings scope selected" : "game settings scope selected", false};
	}

	uint64_t RegisterScanBatchLocked(CommandId command, CommandType type, const std::vector<std::string>& ids)
	{
		const uint64_t batch = next_scan_batch++;
		FakeScanBatch state;
		state.command = command;
		state.type = type;
		state.remaining = static_cast<uint32_t>(ids.size());
		scan_batches[batch] = state;
		for (const std::string& id : ids)
			scanning_sources.insert(id);
		return batch;
	}

	void EnqueueScanJob(uint64_t batch, const LibrarySource& source)
	{
		FakeScanJob job;
		job.batch = batch;
		job.source_id = source.id;
		job.root_path = source.path;
		job.control = std::make_shared<FakeScanControl>();
		{
			std::lock_guard<std::mutex> lock(scan_mutex);
			scan_queue.push_back(std::move(job));
		}
		scan_cv.notify_one();
	}

	uint32_t CancelScanJobs(const std::string& source_id)
	{
		std::lock_guard<std::mutex> lock(scan_mutex);
		uint32_t count = 0;
		const bool all = source_id.empty();
		if (!active_scan_source.empty() && (all || active_scan_source == source_id))
		{
			active_scan_control->cancel.store(true, std::memory_order_release);
			count++;
		}
		for (FakeScanJob& job : scan_queue)
		{
			if (all || job.source_id == source_id)
			{
				job.control->cancel.store(true, std::memory_order_release);
				count++;
			}
		}
		return count;
	}

	void ScanMain()
	{
		for (;;)
		{
			FakeScanJob job;
			{
				std::unique_lock<std::mutex> lock(scan_mutex);
				scan_cv.wait(lock, [this] { return stop_scan || !scan_queue.empty(); });
				if (stop_scan && scan_queue.empty())
					return;
				job = std::move(scan_queue.front());
				scan_queue.pop_front();
				active_scan_source = job.source_id;
				active_scan_control = job.control;
			}
			FakeScanOutcome outcome = ScanJob(job);
			{
				std::lock_guard<std::mutex> lock(scan_mutex);
				active_scan_source.clear();
				active_scan_control.reset();
			}
			FinishScan(job, outcome);
		}
	}

	bool ScanCancelled(const FakeScanJob& job) const
	{
		return job.control->cancel.load(std::memory_order_acquire);
	}

	FakeScanOutcome ScanJob(const FakeScanJob& job)
	{
		FakeScanOutcome outcome;
		std::vector<FakeFile> files;
		std::string volume_id;
		{
			std::lock_guard<std::mutex> lock(mutex);
			FakeVolume* volume = VolumeForPathLocked(job.root_path);
			if (!volume)
			{
				outcome.state = ScanState::Failed;
				outcome.error = "scan root is not on a fake volume";
				return outcome;
			}
			volume_id = volume->info.id;
			for (const auto& pair : volume->files)
			{
				if (IsPathWithin(pair.first, job.root_path))
					files.push_back(pair.second);
			}
			snapshot.scan = ScanProgress{};
			snapshot.scan.state = ScanState::Scanning;
			snapshot.scan.source_id = job.source_id;
			snapshot.scan.current_path = job.root_path;
			for (LibrarySource& source : snapshot.sources)
				if (source.id == job.source_id)
					source.state = SourceState::Scanning;
			BumpRevisionLocked();
		}
		std::sort(files.begin(), files.end(), [](const FakeFile& a, const FakeFile& b) { return a.path < b.path; });

		for (const FakeFile& file : files)
		{
			if (ScanCancelled(job))
			{
				outcome.state = ScanState::Cancelled;
				outcome.error = "scan cancelled";
				break;
			}
			bool failed = false;
			{
				std::lock_guard<std::mutex> lock(mutex);
				FakeVolume* volume = FindVolumeLocked(volume_id);
				if (!volume || !volume->info.present)
				{
					failed = true;
					outcome.error = "volume disconnected during scan";
				}
				else if (!volume->info.readable)
				{
					failed = true;
					outcome.error = "volume became unreadable during scan";
				}
				if (failed)
					break;
				outcome.files_seen++;
				snapshot.scan.state = ScanCancelled(job) ? ScanState::Cancelling : ScanState::Scanning;
				snapshot.scan.source_id = job.source_id;
				snapshot.scan.current_path = file.path;
				snapshot.scan.files_seen = outcome.files_seen;
				snapshot.scan.games_found = outcome.games_found;
				snapshot.scan.cancel_requested = ScanCancelled(job);
				if (storage::LooksLikeGameImageName(file.file))
				{
					GameEntry game;
					game.id = file.path;
					game.path = file.path;
					game.file = file.file;
					game.title = file.title;
					game.region = file.region;
					game.serial = file.serial;
					game.source_id = job.source_id;
					game.bytes = file.bytes;
					game.damaged = file.damaged;
					game.hidden = file.hidden;
					game.can_launch = !file.damaged;
					game.problem = file.damaged ? "fake damaged image" : std::string();
					outcome.games_found++;
					outcome.seen_ids.push_back(game.id);
					UpsertGameLocked(game);
					snapshot.scan.games_found = outcome.games_found;
				}
				BumpRevisionLocked();
			}
			const uint32_t delay = scan_step_delay_microseconds.load(std::memory_order_acquire);
			if (delay)
				std::this_thread::sleep_for(std::chrono::microseconds(delay));
		}
		// A cancellation can arrive while the final paced step is sleeping, after the last loop-edge check.
		// Honor it before publishing completion so the fake has the same terminal-state contract as storage scans.
		if (outcome.state != ScanState::Failed && ScanCancelled(job))
		{
			outcome.state = ScanState::Cancelled;
			outcome.error = "scan cancelled";
		}
		else if (outcome.state != ScanState::Cancelled && outcome.error.empty())
			outcome.state = ScanState::Complete;
		else if (outcome.state != ScanState::Cancelled && !outcome.error.empty())
			outcome.state = ScanState::Failed;
		return outcome;
	}

	void UpsertGameLocked(const GameEntry& game)
	{
		const auto it = std::find_if(snapshot.games.begin(), snapshot.games.end(), [&game](const GameEntry& entry) {
			return entry.id == game.id;
		});
		if (it == snapshot.games.end())
			snapshot.games.push_back(game);
		else
		{
			GameEntry merged = game;
			merged.last_played = it->last_played;
			merged.hidden = it->hidden;
			*it = std::move(merged);
		}
	}

	void FinishScan(const FakeScanJob& job, const FakeScanOutcome& outcome)
	{
		std::lock_guard<std::mutex> lock(mutex);
		scanning_sources.erase(job.source_id);
		snapshot.scan.state = outcome.state;
		snapshot.scan.source_id = job.source_id;
		snapshot.scan.files_seen = outcome.files_seen;
		snapshot.scan.games_found = outcome.games_found;
		snapshot.scan.cancel_requested = false;
		snapshot.scan.error = outcome.error;
		if (outcome.state == ScanState::Complete)
		{
			std::set<std::string> seen(outcome.seen_ids.begin(), outcome.seen_ids.end());
			snapshot.games.erase(std::remove_if(snapshot.games.begin(), snapshot.games.end(), [&job, &seen](const GameEntry& game) {
				return game.source_id == job.source_id && !seen.count(game.id);
			}),
				snapshot.games.end());
		}
		uint32_t count = 0;
		for (const GameEntry& game : snapshot.games)
			count += game.source_id == job.source_id ? 1 : 0;
		for (LibrarySource& source : snapshot.sources)
		{
			if (source.id != job.source_id)
				continue;
			source.game_count = count;
			source.last_scan = snapshot.time_unix;
			if (outcome.state == ScanState::Failed)
			{
				source.state = SourceState::Error;
				source.problem = outcome.error;
			}
			else
			{
				source.state = SourceState::Connected;
				source.problem = outcome.error;
			}
		}
		BumpRevisionLocked();

		const auto batch = scan_batches.find(job.batch);
		if (batch != scan_batches.end())
		{
			batch->second.failed = batch->second.failed || outcome.state == ScanState::Failed;
			batch->second.cancelled = batch->second.cancelled || outcome.state == ScanState::Cancelled;
			if (batch->second.remaining)
				batch->second.remaining--;
			if (batch->second.remaining == 0)
			{
				CommandStatus status = CommandStatus::Succeeded;
				std::string message = "scan complete";
				if (batch->second.cancelled)
				{
					status = CommandStatus::Cancelled;
					message = "scan cancelled";
				}
				else if (batch->second.failed)
				{
					status = CommandStatus::Failed;
					message = "scan failed";
				}
				PushEventLocked(batch->second.command, batch->second.type, status, message);
				scan_batches.erase(batch);
			}
		}
	}

	FakeBackendOptions options;
	std::vector<FakeVolume> volumes;
	std::vector<LibrarySource> saved_sources;
	std::vector<FakeCardRecord> cards;
	std::map<std::string, std::string> default_setting_values;
	std::vector<ControlBinding> default_controls;
	std::map<CommandType, std::string> forced_failures;

	mutable std::mutex mutex;
	Snapshot snapshot;
	std::vector<CommandEvent> events;
	std::deque<std::pair<CommandId, Command>> commands;
	std::condition_variable command_cv;
	std::thread command_thread;
	std::atomic<bool> stop_commands{false};
	CommandId next_command = 1;
	uint64_t next_saved_source = 1;

	mutable std::mutex scan_mutex;
	std::deque<FakeScanJob> scan_queue;
	std::condition_variable scan_cv;
	std::thread scan_thread;
	bool stop_scan = false;
	std::string active_scan_source;
	std::shared_ptr<FakeScanControl> active_scan_control;
	uint64_t next_scan_batch = 1;
	std::map<uint64_t, FakeScanBatch> scan_batches;
	std::set<std::string> scanning_sources;
	std::atomic<uint32_t> scan_step_delay_microseconds{0};
	bool fast_forward_enabled = false;
};

FakeBackend::FakeBackend(const FakeBackendOptions& options)
	: m_impl(std::make_unique<Impl>(options))
{
}

FakeBackend::~FakeBackend() = default;

Snapshot FakeBackend::GetSnapshot()
{
	return m_impl->GetSnapshot();
}

CommandId FakeBackend::Post(const Command& command)
{
	return m_impl->Post(command);
}

std::vector<CommandEvent> FakeBackend::PollEvents()
{
	return m_impl->PollEvents();
}

FolderListing FakeBackend::BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit)
{
	return m_impl->BrowseFolder(path, filter, offset, limit);
}

void FakeBackend::AddVolume(const VolumeInfo& volume)
{
	m_impl->AddVolume(volume);
}

void FakeBackend::AddDirectory(const std::string& volume_id, const std::string& path)
{
	m_impl->AddDirectory(volume_id, path);
}

std::string FakeBackend::AddFile(const std::string& volume_id, const std::string& path, uint64_t bytes, bool damaged,
	const std::string& title, const std::string& serial, const std::string& region)
{
	return m_impl->AddFile(volume_id, path, bytes, damaged, title, serial, region);
}

void FakeBackend::RemoveFile(const std::string& volume_id, const std::string& path)
{
	m_impl->RemoveFile(volume_id, path);
}

void FakeBackend::SetVolumePresent(const std::string& volume_id, bool present)
{
	m_impl->SetVolumePresent(volume_id, present);
}

void FakeBackend::SetVolumeReadable(const std::string& volume_id, bool readable)
{
	m_impl->SetVolumeReadable(volume_id, readable);
}

void FakeBackend::SetBiosPresent(bool present)
{
	m_impl->SetBiosPresent(present);
}

void FakeBackend::SetScanStepDelayMicroseconds(uint32_t microseconds)
{
	m_impl->SetScanStepDelayMicroseconds(microseconds);
}

void FakeBackend::FailNextCommand(CommandType type, const std::string& message)
{
	m_impl->FailNextCommand(type, message);
}

std::unique_ptr<Backend> CreateFakeBackend(const FakeBackendOptions& options)
{
	return std::make_unique<FakeBackend>(options);
}
} // namespace caliza
