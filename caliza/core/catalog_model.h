// Caliza PS4: portable catalog identity and scan-model definitions.
// This is a schema/model layer, not a scanner: host, PS4 and web adapters own filesystem IO.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace caliza::catalog
{
constexpr uint32_t kCatalogSchemaVersion = 1;

enum class MediaKind
{
	DiscImage,
	ElfExecutable,
	SystemMenu,
};

enum class RootKind
{
	DataGames,
	DataRoot,
	UsbDrive,
	ExtendedStorage,
	CustomFolder,
	NfsShare,
};

struct ScanRoot
{
	std::string id;
	std::string label;
	std::string path;
	RootKind kind = RootKind::DataRoot;
	bool removable = false;
	bool user_configured = false;
	bool network = false;
	int order = 0;
	bool legacy_compatibility = false;
};

struct GameIdentity
{
	std::string stable_id;     // volume-relative identity when volume identity is known
	std::string legacy_web_id; // exact legacy id: the full file basename
	std::string settings_stem; // exact legacy settings file stem, without extension
	std::string content_key;   // serial+size+media key, useful only for collision diagnostics
};

struct GameEntry
{
	uint32_t schema_version = kCatalogSchemaVersion;
	GameIdentity identity;
	std::string path;
	std::string file;
	std::string title;
	std::string region;
	std::string extra;
	std::string serial;
	std::string source_root_id;
	uint64_t bytes = 0;
	MediaKind media = MediaKind::DiscImage;
	bool hidden = false;
	bool damaged = false;
	std::string damage_reason;
	std::vector<std::string> badges;
	std::string cover_path; // selected cover art, empty until the cover adapter resolves one
	int disc_number = 0;
	std::string disc_set_key;
	uint64_t scan_revision = 0;
};

struct ScanRootState
{
	std::string root_id;
	bool present = false;
	bool scanning = false;
	bool cancelled = false;
	std::string error;
	uint32_t entries = 0;
	uint64_t revision = 0;
};

// Legacy-compatible file-name classification. Content checks (ELF magic, ISO9660 shape and the 16 MiB
// .bin/.img floor) deliberately stay with the platform scanner and are represented in ScanRootState/GameEntry.
bool IsDiscImageName(const std::string& name);
bool IsElfName(const std::string& name);
bool IsCatalogMediaName(const std::string& name);
bool IsRawImageCandidateName(const std::string& name); // .bin/.img, subject to size+ISO validation
bool ShouldSkipScanSubfolder(const std::string& name);
constexpr uint32_t kMaxScanSubfoldersPerRoot = 64;
constexpr uint64_t kRawImageMinimumBytes = 16ull * 1024ull * 1024ull;

// The static part of the legacy scan model. USB/extended entries describe mount slots; platform scanning
// decides whether they are present and expands their games/, DVD/, CD/, caliza/games/ and PCSX2/games/ layouts.
std::vector<ScanRoot> DefaultScanRoots();
// Internal roots used by installations that have not completed data-root migration. External-drive layouts
// are included in ExpandDriveLayouts because both retired and canonical folder names can coexist there.
std::vector<ScanRoot> LegacyCompatibilityScanRoots();
std::vector<ScanRoot> ExpandDriveLayouts(const ScanRoot& root);

std::string NormalizePath(std::string path);
std::string FileName(const std::string& path);
std::string FileStem(const std::string& file_or_path);
std::string JoinPath(const std::string& dir, const std::string& file);
bool PathWithin(const std::string& path, const std::string& root);

// Identity compatibility:
// - legacy_web_id is exactly GameInfo::file because the old web API uses file basenames;
// - settings_stem is exactly GameInfo::stem because settings are settings/<stem>.ini;
// - stable_id is volume:<volume>:<relative path> when possible, otherwise path:<normalized absolute path>.
GameIdentity MakeIdentity(const std::string& path, const std::string& volume_id, const std::string& root_path,
	const std::string& serial, uint64_t bytes, MediaKind media);
std::string MakeStableId(const std::string& path, const std::string& volume_id, const std::string& root_path);
std::string MakeContentKey(const std::string& serial, uint64_t bytes, MediaKind media);

// Redump-style presentation and multi-disc grouping compatible with fe_games.cpp.
void ParseDisplayTitle(const std::string& stem, std::string& title, std::string& region, std::string& extra);
int DiscNumber(const std::string& name, std::string* rest);
std::string DiscSetKey(const std::string& name);
bool CatalogLess(const GameEntry& a, const GameEntry& b);

std::string MediaKindName(MediaKind media);
bool MediaKindFromName(const std::string& name, MediaKind& media);

// Schema-shaped JSONL cache helpers for immutable catalog snapshots. GameEntryFromJson accepts the object
// shape emitted by GameEntryToJson; it is not a general-purpose JSON parser.
std::string GameEntryToJson(const GameEntry& game);
bool GameEntryFromJson(const std::string& json, GameEntry& game, std::string* error = nullptr);
std::string CatalogToJsonLines(const std::vector<GameEntry>& games);
bool CatalogFromJsonLines(const std::string& lines, std::vector<GameEntry>& games, std::string* error = nullptr);
} // namespace caliza::catalog
