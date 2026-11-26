// Caliza core: POSIX storage discovery, folder browsing and saved-source persistence.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "contract.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace caliza::storage
{
namespace fs
{
// Small POSIX filesystem surface shared by the storage service, backend and migration code. These functions
// deliberately do not use std::filesystem: the OpenOrbis toolchain's C++ library support is narrower than a
// desktop C++17 library, while the POSIX subset below is available on the console and on host test systems.
bool Exists(const std::string& path);
bool IsDirectory(const std::string& path);
bool IsRegularFile(const std::string& path);
bool IsSymlink(const std::string& path);
bool IsReadableDirectory(const std::string& path);
uint64_t FileSize(const std::string& path);

// Lexically normalizes an absolute or relative POSIX path: duplicate slashes are folded, "." components are
// removed and ".." components are resolved without following symlinks. The result never ends in '/' except
// for the root directory.
std::string NormalizePath(const std::string& path);
std::string JoinPath(const std::string& parent, const std::string& child);
std::string BaseName(const std::string& path);
std::string DirName(const std::string& path);
bool IsPathWithin(const std::string& path, const std::string& root);

// Canonicalizes an existing path with realpath(3). For a missing path, resolves the deepest existing
// prefix and appends the missing components lexically so overlap checks are not defeated by alias prefixes.
// Symlink rejection decisions must use lstat on the original path; this helper is for duplicate/collision evidence.
std::string CanonicalPath(const std::string& path);

bool MakeDirs(const std::string& path, std::string* error);
bool ReadFile(const std::string& path, std::string* out, std::string* error, uint64_t max_bytes = 16u * 1024u * 1024u);
// Writes the complete file beside its destination and atomically renames it over the destination. Use this
// for state files that readers may open at any time. Destination data is intentionally replaced; migration
// uses a stricter no-overwrite path and does not depend on this helper.
bool WriteFileAtomic(const std::string& path, const std::string& data, std::string* error);
} // namespace fs

// A platform may be able to supply a durable volume identity (a filesystem UUID, device serial, or Orbis
// mount identity). Generic POSIX cannot prove that an identity is durable, so the callback is explicit
// platform evidence rather than a guess made by this library.
using VolumeIdentityProvider = std::function<std::string(const std::string& root_path)>;
using ExtendedManagedEvidence = std::function<bool(const std::string& root_path)>;

struct StorageDiscoveryOptions
{
	StorageDiscoveryOptions();

	std::string internal_root = "/data/caliza";
	std::vector<std::string> usb_roots;      // defaults to /mnt/usb0 through /mnt/usb7
	std::vector<std::string> extended_roots; // defaults to /mnt/ext0 through /mnt/ext1
	bool include_absent_slots = true;
	VolumeIdentityProvider stable_identity;
	ExtendedManagedEvidence extended_managed;
};

// Discovers the fixed console storage slots without writing to any volume. Readability is tested as this
// process (not merely inferred from mount-point existence). A slot that cannot be opened remains in the
// result as present but unreadable when its directory exists, so the UI can explain the problem.
std::vector<VolumeInfo> DiscoverVolumes(const StorageDiscoveryOptions& options = {});

// Non-authoritative evidence for a volume root. It is intentionally not exposed as VolumeInfo::id: a POSIX
// device number, inode and root-entry sample can collide or change, so saved sources must not silently use
// it as durable identity.
std::string VolumeFingerprint(const std::string& root_path);

// Longest-volume-root match for path, or -1. Both missing and present volumes may be passed in; callers
// decide which states are acceptable.
int FindVolumeIndexForPath(const std::string& path, const std::vector<VolumeInfo>& volumes);

// Recognized disc/executable image extensions. This intentionally mirrors the existing frontend's public
// format list (.iso, .chd, .cso, .zso, .bin, .img and .elf) without depending on its implementation.
bool LooksLikeGameImageName(const std::string& name);

// Read-only POSIX folder browsing for Backend::BrowseFolder. Entries are sorted directories-first and then
// case-insensitively by name. `filter` is a case-insensitive substring. `offset` and `limit` are applied
// after filtering; limit 0 means "all remaining entries". Breadcrumbs are display names, with the first
// component replaced by the matching volume label when one is known.
FolderListing BrowseFolderPosix(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit,
	const std::vector<VolumeInfo>& volumes);

struct SavedSourceRecord
{
	std::string id;
	std::string path;             // normalized absolute path at the time it was saved
	std::string volume_id;        // authoritative platform identity, empty when unavailable
	std::string volume_root;      // mount root used to derive relative_path
	std::string relative_path;    // path inside volume_root; empty for the root itself
	std::string volume_fingerprint; // non-authoritative evidence, never enough for silent relocation
	std::string label;
	bool user_added = true;
	uint64_t added_order = 0;
	uint32_t game_count = 0;
	uint64_t last_scan = 0;
};

enum class AddSavedSourceStatus
{
	Added,
	DuplicatePath,
	InvalidPath,
	UnavailableVolume,
	AmbiguousVolume,
	StoreWriteFailed,
};

struct AddSavedSourceResult
{
	AddSavedSourceStatus status = AddSavedSourceStatus::InvalidPath;
	std::string source_id;
	std::string message;
};

enum class SavedSourceMutationStatus
{
	Changed,
	NotFound,
	DuplicatePath,
	InvalidPath,
	UnavailableVolume,
	StoreWriteFailed,
};

struct SavedSourceMutationResult
{
	SavedSourceMutationStatus status = SavedSourceMutationStatus::NotFound;
	std::string message;
};

// Persistent saved-folder store. The file is data only; removing a record never touches the folder it names.
// The store's atomic rewrite is safe to read while a scan is in progress, and all mutating operations save
// before reporting success so a restart preserves the accepted configuration.
class SavedSourceStore
{
public:
	explicit SavedSourceStore(std::string file_path);

	const std::string& file_path() const { return m_file_path; }
	const std::vector<SavedSourceRecord>& records() const { return m_records; }

	// Missing files are an empty store, not an error. Malformed lines are ignored conservatively; entries
	// that cannot be parsed are not rewritten as valid data.
	bool Load(std::string* error);
	bool Save(std::string* error) const;

	AddSavedSourceResult Add(const std::string& path, const VolumeInfo& volume, const std::string& label);
	SavedSourceMutationResult Remove(const std::string& source_id);
	SavedSourceMutationResult Relocate(const std::string& source_id, const std::string& new_path, const VolumeInfo& volume);
	SavedSourceMutationResult UpdateScan(const std::string& source_id, uint32_t game_count, uint64_t last_scan);

	const SavedSourceRecord* Find(const std::string& source_id) const;
	const SavedSourceRecord* FindPath(const std::string& path) const;

	// Resolves persisted records against the currently discovered volumes. Missing volumes leave the source in
	// place as Disconnected. Non-authoritative fingerprint matches are accepted only at the originally saved
	// absolute path; a matching fingerprint at another slot is reported as ambiguous evidence and never causes
	// silent reassociation.
	std::vector<LibrarySource> Resolve(const std::vector<VolumeInfo>& volumes) const;

private:
	std::string m_file_path;
	std::vector<SavedSourceRecord> m_records;
	uint64_t m_next_order = 1;
	bool m_load_failed = false;
};
} // namespace caliza::storage
