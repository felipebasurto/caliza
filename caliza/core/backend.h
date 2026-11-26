// Caliza core: asynchronous storage-backed implementation of the interface contract.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "contract.h"
#include "data_root.h"
#include "storage.h"

#include <memory>
#include <string>

namespace caliza
{
// Image metadata adapter boundary. The default POSIX inspector validates common container magic and derives
// display names locally. The console orchestrator can adapt fe_games.h here when its serial/database/CHD
// implementation is available, without moving scanning back onto the caller thread.
class ImageInspector
{
public:
	virtual ~ImageInspector() = default;
	// Fills metadata for one image. `entry` already has path, file, source_id and bytes populated. Returning
	// false means the file should not be catalogued; unreadable or damaged images should normally return true
	// with damaged/can_launch/problem set so the interface can explain them.
	virtual bool Inspect(GameEntry& entry) = 0;
};

class PosixImageInspector final : public ImageInspector
{
public:
	bool Inspect(GameEntry& entry) override;

	// Redump-style filename parsing is exposed for tests and for the orchestrator adapter.
	static void ParseDisplayName(const std::string& stem, std::string* title, std::string* region);
};

struct BackendOptions
{
	std::string data_root = dataroot::kCanonicalDataRoot;
	std::string legacy_data_root = dataroot::LegacyDataRoot();
	storage::StorageDiscoveryOptions discovery;
	bool create_data_root = true;
	bool discover_volume_sources = true;
	std::shared_ptr<ImageInspector> image_inspector;
	// Progress is published at least this often in files seen. Larger values reduce snapshot churn on slow media.
	uint32_t scan_progress_interval_files = 8;
	// Optional cooperative pacing hook for host tests. Production uses zero.
	uint32_t scan_step_delay_microseconds = 0;
};

// Production storage/catalog backend. It implements the contract's storage, browsing, saved-source, scan
// ordering/progress/cancellation and diagnostics-report behavior. Emulator lifecycle commands are answered
// with explicit unsupported-command failures until the orchestrator binds the console adapter; they are never
// faked as successful here.
class StorageBackend final : public Backend
{
public:
	explicit StorageBackend(const BackendOptions& options = {});
	~StorageBackend() override;

	StorageBackend(const StorageBackend&) = delete;
	StorageBackend& operator=(const StorageBackend&) = delete;

	Snapshot GetSnapshot() override;
	CommandId Post(const Command& command) override;
	std::vector<CommandEvent> PollEvents() override;
	FolderListing BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit) override;

	// Rediscover mount slots and rebuild automatic/saved source state without invalidating catalog entries.
	// Platform hotplug code can call this when Orbis reports a mount change; scans also refresh before starting.
	void RefreshStorage();

private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};

std::unique_ptr<Backend> CreateBackend(const BackendOptions& options = {});
} // namespace caliza
