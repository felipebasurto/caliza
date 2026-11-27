// Caliza interface contract: snapshots in, commands out.
// The native interface, host preview, tests and console adapter share this boundary.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace caliza
{
// A monotonically increasing snapshot revision. A command completion event must never be published
// before GetSnapshot() can return a revision containing that command's visible effect.
using Revision = uint64_t;
using CommandId = uint64_t;

enum class VolumeKind
{
	Internal,
	USB,
	Extended,
	Network,
	Saved,
};

enum class SourceState
{
	Connected,
	Disconnected,
	Unreadable,
	Scanning,
	Error,
};

enum class ScanState
{
	Idle,
	Scanning,
	Cancelling,
	Cancelled,
	Complete,
	Failed,
};

enum class VmState
{
	Unavailable,
	Idle,
	Starting,
	Running,
	Paused,
	Stopping,
	Failed,
};

enum class SettingKind
{
	Toggle,
	Choice,
	Number,
	Text,
	Action,
};

enum class CommandType
{
	None,
	RescanAll,
	ScanSource,
	CancelScan,
	AddLibraryFolder,
	RemoveLibraryFolder,   // unlink only; never deletes game images
	RelocateLibraryFolder,
	LaunchGame,
	LaunchSystemMenu,
	SetBiosFolder,
	SelectBios,
	SetSetting,
	UnsetSetting,
	ResetSetting,
	ResetSettingsScope,
	ApplyRecommendedSettings,
	CreateMemoryCard,
	AssignMemoryCard,
	SetControlBinding,
	ResetControlBindings,
	SetAccessibility,
	FetchOnlinePatches,
	RetryTexturePackCatalog,
	DownloadTexturePack,
	ImportTexturePack,
	CancelTexturePack,
	DeleteTexturePack,
	LaunchSmartTune,
	SignInAchievements,
	SignOutAchievements,
	RefreshAchievements,
	CancelAchievementLoad,
	SaveState,
	LoadState,
	ToggleFastForward,
	PauseVm,
	ResumeVm,
	ReturnToLibrary,
	ChangeDisc,
	OpenCompanionPanel,
	GenerateReport,
	SelectGame, // selects the game scope used by the settings projection; empty clears it
};

struct GameEntry
{
	std::string id;       // stable for this scan: normally the full image path
	std::string path;     // original location; external images are never copied
	std::string file;
	std::string title;
	std::string region;
	std::string serial;
	std::string source_id;
	std::string cover_path;
	std::vector<std::string> badges;
	uint64_t bytes = 0;
	uint64_t last_played = 0;
	bool damaged = false;
	bool hidden = false;
	bool can_launch = true;
	std::string problem;
};

struct VolumeInfo
{
	std::string id;          // stable evidence key when the platform exposes one; otherwise empty
	std::string root_path;   // /mnt/usb0, /mnt/ext0, /data/caliza, /net, ...
	std::string label;       // user-facing; never require the technical root in normal flow
	VolumeKind kind = VolumeKind::Internal;
	int slot = -1;
	bool present = false;
	bool readable = false;
	bool extended_managed = false;
	uint64_t total_bytes = 0;
	uint64_t free_bytes = 0;
	std::string fingerprint; // non-authoritative evidence when no stable id exists
	std::string status;
};

struct LibrarySource
{
	std::string id;
	std::string path;
	std::string volume_id;
	std::string label;
	SourceState state = SourceState::Connected;
	uint32_t game_count = 0;
	uint64_t last_scan = 0;
	bool user_added = true;
	std::string problem;
};

struct ScanProgress
{
	ScanState state = ScanState::Idle;
	std::string source_id;
	std::string current_path;
	uint32_t files_seen = 0;
	uint32_t games_found = 0;
	bool cancel_requested = false;
	std::string error;
};

struct BiosStatus
{
	bool present = false;
	bool checking = false;
	std::string path;
	std::string problem;
};

struct VmStatus
{
	VmState state = VmState::Unavailable;
	std::string game_id;
	std::string title;
	bool can_save = false;
	bool can_load = false;
	uint32_t occupied_slots = 0; // bit s (1u << s) set when slot s (1..10) has a save state or backup
	int disc_index = 0;          // 1-based current disc in multi-disc set (0 when single/unknown)
	int disc_count = 0;          // total discs in set (>=2 enables disc swap)
	std::string disc_label;      // short current disc name when known
	bool progress_seen = false;
	std::string message;
};

struct MemoryCardInfo
{
	int slot = 0;
	bool present = false;
	bool enabled = false;
	std::string file;
	uint64_t bytes = 0;
	uint64_t used_bytes = 0;
};

struct SettingChoice
{
	std::string value;
	std::string label;
};

struct SettingItem
{
	std::string id;
	std::string label;
	std::string value;
	std::string description;
	SettingKind kind = SettingKind::Text;
	std::vector<SettingChoice> choices;
	bool modified = false;
	bool restart_required = false;
	bool destructive = false;
	bool game_scoped = false;
};

struct SettingCategory
{
	std::string id;
	std::string label;
	std::vector<SettingItem> items;
};

struct ControlBinding
{
	std::string action;
	std::string label;
	std::string button;
	bool editable = true;
	bool conflict = false;
};

struct AccessibilityState
{
	uint32_t text_scale_percent = 100;
	bool high_contrast = false;
	bool reduce_motion = false;
	bool large_focus = true;
};

struct DiagnosticsStatus
{
	std::string build_id;
	std::string console_status;
	std::string web_panel_url;
	bool web_panel_connected = false;
	bool probe_available = false;
	bool report_available = false;
	std::vector<std::string> log_paths;
};


enum class OperationState
{
	Idle,
	Queued,
	Running,
	Succeeded,
	Failed,
	Cancelled,
};

// Generic service-operation state for texture packs, online patches, achievement loads and diagnostics.
// Long operations expose progress here; action rows must not pretend success before this state reaches Succeeded.
struct OperationStatus
{
	std::string id;
	std::string label;
	std::string game_id;
	OperationState state = OperationState::Idle;
	float progress = -1.0f; // 0..1 when deterministic
	bool cancelable = false;
	std::string message;
};

struct TexturePackEntry
{
	std::string id, name, authors, source, license;
	uint64_t bytes = 0;
};

struct TexturePacks
{
	enum class Catalog { Unavailable, Loading, Ready };
	Catalog catalog = Catalog::Unavailable;
	std::string catalog_message;
	std::vector<TexturePackEntry> entries;
	OperationStatus operation;
	std::string installed_name;
	bool installed = false;
	bool removable = false;
};

struct AchievementEntry
{
	std::string id;
	std::string title;
	std::string description;
	std::string badge_path;
	uint32_t points = 0;
	bool unlocked = false;
};

struct AchievementsStatus
{
	bool signed_in = false;
	bool loading = false;
	std::string username;
	std::string game_id;
	uint32_t unlocked = 0;
	uint32_t total = 0;
	std::vector<AchievementEntry> entries;
	std::string problem;
};

struct Snapshot
{
	Revision revision = 0;
	std::string product = "Caliza";
	std::string build_tag;
	std::string data_root;
	std::string legacy_data_root;
	BiosStatus bios;
	VmStatus vm;
	ScanProgress scan;
	std::vector<GameEntry> games;
	std::vector<VolumeInfo> volumes;
	std::vector<LibrarySource> sources;
	std::vector<MemoryCardInfo> memory_cards;
	std::vector<SettingCategory> settings;
	std::vector<ControlBinding> controls;
	AccessibilityState accessibility;
	DiagnosticsStatus diagnostics;
	AchievementsStatus achievements;
	std::vector<OperationStatus> operations;
	TexturePacks texture_packs; // for selected_game_id
	std::string continue_game_id;
	bool continue_valid = false;
	bool pause_trigger_chosen = false;
	std::string selected_game_id;
	uint64_t time_unix = 0;
};

struct Command
{
	CommandType type = CommandType::None;
	std::string id;     // game/source/setting/control id, depending on type
	std::string path;   // folder/disc/card path
	std::string value;  // setting value or action payload
	int number = 0;     // slot, size, or numeric value
};

enum class CommandStatus
{
	Queued,
	Running,
	Succeeded,
	Failed,
	Cancelled,
};

struct CommandEvent
{
	CommandId command = 0;
	CommandType type = CommandType::None;
	CommandStatus status = CommandStatus::Queued;
	Revision revision = 0;
	std::string message;
};

struct FolderEntry
{
	std::string name;
	std::string path;
	bool directory = false;
	bool image = false;
	bool readable = true;
	uint32_t compatible_count = 0; // recursive compatible images already counted for this folder, when known
};

struct FolderListing
{
	std::string path;
	std::string parent;
	std::string volume_id;
	std::vector<std::string> breadcrumbs;
	std::vector<FolderEntry> entries;
	bool truncated = false;
	bool contains_compatible = false;
	std::string error;
};

// Backend contract:
// - Post() only queues work; it never performs presentation work and never blocks the input/render thread.
// - Mutating commands execute in post order. Removing a library source only removes its link.
// - Scanning is incremental and cooperatively cancelable. Partially discovered games may appear in
//   newer snapshots, but a source's scan completion event must follow its final scan snapshot revision.
// - A volume disappearing never invalidates or deletes its saved sources or library entries.
// - The UI owns no second copy of save/settings/import/boot logic; it navigates snapshots and posts commands.
class Backend
{
public:
	virtual ~Backend() = default;
	virtual Snapshot GetSnapshot() = 0;
	virtual CommandId Post(const Command& command) = 0;
	virtual std::vector<CommandEvent> PollEvents() = 0;
	virtual FolderListing BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset,
		uint32_t limit) = 0;
};

// Input edges as the interface consumes them. Repeated movement is generated by the UI model, so the
// same semantics apply to a DualShock/DualSense, the host keyboard and scripted host tests.
struct Input
{
	bool left = false, right = false, up = false, down = false;
	bool cross = false, circle = false, square = false, triangle = false;
	bool options = false, l1 = false, r1 = false, l2 = false, r2 = false;
};
} // namespace caliza
