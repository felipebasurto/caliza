// Caliza core: console-facing backend adapter over storage, settings and platform seams.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "backend.h"
#include "settings_schema.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace caliza
{
// One group ("[60 FPS]") found in a game's patch or cheat file, as fe_settings.cpp's PatchGroups reads it.
struct ConsolePatchGroup
{
	std::string name;
	std::string file;
	std::string description;
};

// Production services owned outside the presentation layer. console_backend.cpp validates and serializes
// every contract command, but these hooks are the only place that performs platform/settings mutations. The
// UI never edits ini files, memory cards, BIOS state or boot state itself.
struct ConsoleSettingsHooks
{
	using EditSettingsFile = std::function<bool(const std::string& path, const std::vector<settings::Mutation>& mutations,
		std::string* summary, std::string* error)>;
	using ApplyRecommended = std::function<bool(const std::string& path, const std::string& preset_id,
		std::string* summary, std::string* error)>;
	using CreateMemoryCard = std::function<bool(const std::string& directory, uint64_t megabytes, const std::string& name,
		std::string* created_name, std::string* error)>;
	using ListMemoryCardFiles = std::function<std::vector<std::pair<std::string, uint64_t>>()>;
	using ReadTextFile = std::function<bool(const std::string& path, std::string* text, std::string* error)>;
	using BiosStatus = std::function<caliza::BiosStatus()>;
	using DynamicChoices = std::function<std::vector<settings::Choice>(const settings::SettingDef& definition)>;
	using WebPanelUrl = std::function<std::string()>;
	// The patch or cheat groups a game's downloaded file(s) actually contain, read live so the settings
	// projection never shows a stale or empty row. `cheats` selects the cheats folder over the patches one.
	using ListPatchGroups = std::function<std::vector<ConsolePatchGroup>(const GameEntry& game, bool cheats)>;
	// Production long-running services. Begin/cancel/delete hooks return whether the service accepted the
	// request; completion is reported through ServiceOperations rather than pretending the command itself
	// downloaded or installed anything.
	using FetchOnlinePatches = std::function<bool(const GameEntry& game, std::string* error)>;
	using DownloadTexturePack = std::function<bool(const GameEntry& game, int pack_index, const std::string& pack_id, std::string* error)>;
	using CancelTexturePack = std::function<bool(const GameEntry& game, std::string* error)>;
	using DeleteTexturePack = std::function<bool(const GameEntry& game, std::string* error)>;
	using RetryTexturePackCatalog = std::function<bool(std::string* error)>;
	using ServiceOperations = std::function<std::vector<OperationStatus>()>;
	// Future VM transport seam. It is deliberately unbound in the menu shell today; unbound runtime commands
	// therefore fail as unsupported instead of pretending that the re-exec menu process can pause the VM.
	using VmCommand = std::function<bool(const Command& command, std::string* error)>;

	EditSettingsFile edit_settings_file;
	ApplyRecommended apply_recommended;
	CreateMemoryCard create_memory_card;
	ListMemoryCardFiles list_memory_card_files;
	ReadTextFile read_text_file;
	BiosStatus bios_status;
	DynamicChoices dynamic_choices;
	WebPanelUrl web_panel_url;
	ListPatchGroups list_patch_groups;
	FetchOnlinePatches fetch_online_patches;
	DownloadTexturePack download_texture_pack;
	std::function<bool(const GameEntry&, const std::string&, std::string*)> import_texture_pack;
	std::function<TexturePacks(const GameEntry&)> texture_packs;
	CancelTexturePack cancel_texture_pack;
	DeleteTexturePack delete_texture_pack;
	RetryTexturePackCatalog retry_texture_pack_catalog;
	ServiceOperations service_operations;
	VmCommand vm_command;
};

struct ConsoleBackendOptions
{
	BackendOptions storage;
	std::string global_settings_path;
	std::string game_settings_dir;
	std::string memory_cards_dir;
	std::string patches_dir;
	std::string cheats_dir;
	std::string bios_dir;
	std::string last_game_path;
	std::string build_tag;
	ConsoleSettingsHooks hooks;
	// A game settings stem is normally the image file stem. The shell can replace this when the existing
	// frontend's serial/database compatibility rules provide a better ini stem.
	std::function<std::string(const GameEntry& game)> game_settings_stem;
	// Projected settings refresh no faster than this many milliseconds outside mutation-forced refreshes.
	uint32_t projection_refresh_ms = 1000;
	bool scan_on_create = false;
};

enum class ConsoleSettingScope
{
	Global,
	Game,
};

struct ConsoleLaunchRequest
{
	enum class Kind
	{
		None,
		Game,
		SystemMenu,
	};
	Kind kind = Kind::None;
	CommandId command = 0;
	std::string game_id;
	std::string path;
	std::string title;
	std::string serial;
	bool smart_tune = false;
};

// Console adapter boundary. It owns one serialized command queue. Storage commands are delegated to the
// storage backend and re-published under the outer command id; settings/card/control/accessibility commands
// go through the injected production mutation hooks. This preserves the contract's post-order mutation rule
// without making the Estratos UI a second owner of platform state.
class ConsoleBackend final : public Backend
{
public:
	explicit ConsoleBackend(const ConsoleBackendOptions& options, std::unique_ptr<Backend> storage_backend = nullptr);
	~ConsoleBackend() override;

	ConsoleBackend(const ConsoleBackend&) = delete;
	ConsoleBackend& operator=(const ConsoleBackend&) = delete;

	Snapshot GetSnapshot() override;
	CommandId Post(const Command& command) override;
	std::vector<CommandEvent> PollEvents() override;
	FolderListing BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit) override;

	// Future scoped ids: "global:<schema-id>" and "game:<schema-id>:<percent-encoded-game-key>". A bare schema
	// id is the global layer when that layer exists; game-only rows deliberately require an explicit game scope.
	static std::string EncodeSettingId(ConsoleSettingScope scope, const std::string& schema_id, const std::string& game_key = {});
	static bool DecodeSettingId(const std::string& encoded, ConsoleSettingScope* scope, std::string* schema_id,
		std::string* game_key, std::string* error);

	// The shell consumes a validated launch selection exactly once. Success means only that the menu accepted
	// and validated the shell handoff; it is not a claim that the VM has already booted or presented a frame.
	bool ConsumeLaunchRequest(ConsoleLaunchRequest* out);

	// Stops and joins the serialized command worker before service hooks are torn down. The destructor also
	// performs this shutdown; shell code calls it explicitly so service managers cannot be deleted under the worker.
	void Shutdown();

private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
} // namespace caliza
