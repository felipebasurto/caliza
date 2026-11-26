// Caliza PS4 core: canonical data-root layout and resumable legacy-to-Caliza migration.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "legacy_compat.h"

namespace caliza::dataroot
{
constexpr const char* kCanonicalDataRoot = "/data/caliza";
inline std::string LegacyDataRoot()
{
	return caliza::legacy::RetiredDataRoot();
}

struct DataRootLayout
{
	std::string root;
	std::string games;
	std::string bios;
	std::string memcards;
	std::string savestates;
	std::string settings;
	std::string patches;
	std::string cheats;
	std::string covers;
	std::string lang;
	std::string flags;
	std::string inputprofiles;
	std::string cache;
	std::string logs;
	std::string library;
	std::string migration_state;

	static DataRootLayout ForRoot(const std::string& root);
	std::vector<std::string> UserDataDirectories() const;
	std::vector<std::string> AllDirectories() const;
};

// Creates the canonical layout under the data root. This function never writes to mounted USB/extended
// volumes; callers pass only the internal data root (or a host-test substitute).
bool EnsureDataRoot(const DataRootLayout& layout, std::string* error);

enum class MigrationLargeDataStrategy
{
	// games/ and textures/ stay in place and are written to the manifest as explicit exceptions. The new
	// interface can add them as read-only scan roots; migration never silently duplicates them.
	ReferenceOnly,
	// Link large read-only data when source and destination are on the same filesystem and the platform
	// supports hardlinks. A link is verified by inode/device before it is accepted. Failure is an exception,
	// never a fallback byte copy, because silently duplicating games or texture packs can consume gigabytes.
	HardlinkVerified,
};

struct MigrationOptions
{
	std::string source_root = LegacyDataRoot();
	std::string destination_root = kCanonicalDataRoot;
	MigrationLargeDataStrategy large_data_strategy = MigrationLargeDataStrategy::ReferenceOnly;
	size_t copy_buffer_bytes = 256u * 1024u;
	uint32_t maximum_depth = 32;
};

enum class MigrationStatus
{
	Completed,
	CompletedWithIssues,
	Interrupted,
	NoSource,
	InvalidRequest,
	Failed,
};

enum class MigrationItemStatus
{
	Planned,
	Copied,
	AlreadyIdentical,
	Hardlinked,
	Collision,
	RejectedSymlink,
	RejectedSpecial,
	ReadError,
	WriteError,
	SkippedLargeData,
};

enum class MigrationItemAction
{
	None,
	Copy,
	Hardlink,
	Reference,
};

struct MigrationItem
{
	std::string relative_path;
	std::string source_path;
	std::string destination_path;
	std::string category;
	uint64_t bytes = 0;
	bool directory = false;
	MigrationItemStatus status = MigrationItemStatus::Planned;
	MigrationItemAction action = MigrationItemAction::None;
	std::string message;
};

struct MigrationException
{
	std::string path;
	std::string kind;
	uint64_t bytes = 0;
	std::string reason;
};

struct MigrationResult
{
	MigrationStatus status = MigrationStatus::Failed;
	std::string message;
	std::string source_root;
	std::string destination_root;
	std::string manifest_path;
	std::string journal_path;
	bool source_preserved = true;
	bool resumable = false;
	uint32_t files_planned = 0;
	uint32_t files_copied = 0;
	uint32_t files_already_identical = 0;
	uint32_t files_hardlinked = 0;
	uint32_t collisions = 0;
	uint32_t rejected = 0;
	uint64_t bytes_copied = 0;
	std::vector<MigrationItem> items;
	std::vector<MigrationException> large_data_exceptions;
	std::vector<MigrationException> skipped_exceptions;
};

// Called before and during file migration. Returning true requests a cooperative interruption before the
// next atomic file publication; an interrupted migration can be retried with the same options.
using MigrationStopCallback = std::function<bool()>;
using MigrationProgressCallback = std::function<void(const MigrationItem&)>;

// Migrates user data from the retired installation root into the canonical Caliza root. The source is never
// deleted, moved, renamed or written. Regular payload files are copied through a temporary file, fsynced,
// and published through atomic no-overwrite hardlink publication. Filesystems that cannot hardlink fail closed
// rather than using an overwrite-capable rename race. Existing differing destination data is never overwritten.
// Every publication is journaled after fsync so a power loss can be retried by simply calling this function again.
//
// INI files are user configuration rather than opaque payload: their retired key namespace and internal-root
// links are translated to canonical Caliza form in the destination copy only. INI files larger than 64 MiB are
// reported as read errors instead of being silently copied untranslated.
//
// Preserved categories: bios/, memcards/, savestates/, settings/, patches/, cheats/, covers/, lang/, flags/,
// inputprofiles/, and top-level INI plus the legacy play-state files gs.ini, live.ini, playtime.dat and
// lastgame.txt. Explicit large-data exceptions are games/ and textures/; cache/, logs/, resources/, pid.txt
// and migration metadata are regenerable or non-user data and are inventoried as skipped.
MigrationResult MigrateDataRoot(const MigrationOptions& options, const MigrationStopCallback& should_stop = {},
	const MigrationProgressCallback& progress = {});

// The fixed inventory of user data categories and large-data exceptions, useful for diagnostics before a
// migration starts. Paths need not exist.
std::vector<std::string> PreservedMigrationCategories();
std::vector<std::string> LargeDataMigrationExceptions();
} // namespace caliza::dataroot
