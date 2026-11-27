// Caliza Estratos host preview: deterministic fictional backend data.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host_demo.h"

#include "settings_projection.h"

#include <algorithm>

namespace caliza
{
namespace
{
GameEntry Game(const char* id, const char* title, const char* region, uint64_t bytes, uint64_t played, bool damaged = false)
{
	GameEntry game;
	game.id = id;
	game.path = std::string("/demo/usb0/") + id + ".iso";
	game.file = game.path.substr(game.path.find_last_of('/') + 1);
	game.title = title;
	game.region = region;
	game.serial = std::string("DEMO-") + id;
	game.source_id = "usb0";
	game.bytes = bytes;
	game.last_played = played;
	game.damaged = damaged;
	game.can_launch = !damaged;
	if (damaged)
		game.problem = "The image checksum changed since the last scan.";
	return game;
}

// A handful of fictional per-patch/per-cheat rows for the preview's one game with downloaded files, in the same
// Toggle shape BuildPatchAndCheatRows (console_backend.cpp) builds from a real game's patches/cheats folders,
// including the one case where a group's name (ConsolePatchGroup::modern_controls, fe::IsModernControlsGroup)
// moves it into its own "Modern controls" category instead. The global-scope projection MakeDemoSnapshot starts
// from never includes these (all are ScopeMask::Game), so this adds the whole category, not just its items; it
// is idempotent, so selecting a different game clears them again.
void ApplyDemoPatchRows(const std::string& game_id, std::vector<SettingCategory>& settings)
{
	settings.erase(std::remove_if(settings.begin(), settings.end(),
						[](const SettingCategory& category) {
							return category.id == "patches" || category.id == "cheats" || category.id == "modern-controls";
						}),
		settings.end());
	if (game_id != "aurora")
		return;
	SettingItem fps;
	fps.id = "demo-patch:60-fps";
	fps.label = "60 FPS";
	fps.value = "true";
	fps.description = "Removes the frame cap";
	fps.kind = SettingKind::Toggle;
	fps.modified = true;
	SettingItem widescreen;
	widescreen.id = "demo-patch:widescreen";
	widescreen.label = "Widescreen (Gabominated)";
	widescreen.value = "false";
	widescreen.description = "aurora_demo_gabominated.pnach";
	widescreen.kind = SettingKind::Toggle;
	SettingCategory patches;
	patches.id = "patches";
	patches.label = "Patches";
	patches.items = {fps, widescreen};
	SettingItem infinite_hp;
	infinite_hp.id = "demo-cheat:infinite-hp";
	infinite_hp.label = "Infinite HP";
	infinite_hp.value = "false";
	infinite_hp.description = "aurora_demo.pnach";
	infinite_hp.kind = SettingKind::Toggle;
	SettingCategory cheats;
	cheats.id = "cheats";
	cheats.label = "Cheats";
	cheats.items = {infinite_hp};
	SettingItem modern_camera;
	modern_camera.id = "demo-patch:modern-controls";
	modern_camera.label = "Modern analog controls";
	modern_camera.value = "false";
	modern_camera.description = "aurora_demo_modern_controls.pnach";
	modern_camera.kind = SettingKind::Toggle;
	SettingCategory modern_controls;
	modern_controls.id = "modern-controls";
	modern_controls.label = "Modern controls";
	modern_controls.items = {modern_camera};
	settings.push_back(std::move(patches));
	settings.push_back(std::move(cheats));
	settings.push_back(std::move(modern_controls));
}

} // namespace

Snapshot MakeDemoSnapshot()
{
	Snapshot s;
	s.revision = 1;
	s.build_tag = "host-preview";
	s.data_root = "/data/caliza";
	s.bios.present = true;
	s.bios.path = "/data/caliza/bios";
	s.vm.state = VmState::Idle;
	s.scan.state = ScanState::Idle;
	s.games = {
		Game("aurora", "Aurora Meridian", "USA", 2432696320, 20),
		Game("basalt", "Basalt Runner", "Europe", 1543503872, 19),
		Game("fjord", "Neon Fjord", "Japan", 3288334336, 18),
		Game("harbor", "Glass Harbor", "USA", 2013265920, 17),
		Game("relic", "Winter Relic", "Europe", 2841640960, 16),
		Game("circuit", "Quiet Circuit", "USA", 1321205760, 15),
		Game("tundra", "Tundra Signal", "Japan", 2516582400, 14),
		Game("ember", "Ember Atlas", "Europe", 1879048192, 13, true),
	};
	VolumeInfo internal;
	internal.id = "internal";
	internal.root_path = "/data/caliza";
	internal.label = "Internal storage";
	internal.kind = VolumeKind::Internal;
	internal.present = internal.readable = true;
	internal.total_bytes = 420000000000ull;
	internal.free_bytes = 180000000000ull;
	VolumeInfo usb;
	usb.id = "usb0";
	usb.root_path = "/mnt/usb0";
	usb.label = "USB drive";
	usb.kind = VolumeKind::USB;
	usb.present = usb.readable = true;
	usb.total_bytes = 1000000000000ull;
	usb.free_bytes = 440000000000ull;
	s.volumes = {internal, usb};
	LibrarySource source;
	source.id = "usb0";
	source.path = "/mnt/usb0/games";
	source.volume_id = "usb0";
	source.label = "USB drive / games";
	source.state = SourceState::Connected;
	source.game_count = static_cast<uint32_t>(s.games.size());
	s.sources = {source};
	MemoryCardInfo card;
	card.slot = 0;
	card.present = card.enabled = true;
	card.file = "Caliza Demo Card.ps2";
	card.bytes = 8 * 1024 * 1024;
	card.used_bytes = 3 * 1024 * 1024;
	MemoryCardInfo empty;
	empty.slot = 1;
	s.memory_cards = {card, empty};
	// The real global settings catalog, so the preview shows the same rows and help text as the console.
	settings::ProjectionOptions projection;
	projection.values = {{"upscale_multiplier", "2"}, {"EmuCore/EnableWideScreenPatches", "true"}};
	s.settings = settings::ProjectSettings(projection);
	s.controls = {
		{"cross", "Confirm / Cross", "Cross"}, {"circle", "Back / Circle", "Circle"},
		{"square", "Context / Square", "Square"}, {"triangle", "Search / Triangle", "Triangle"},
		{"up", "D-Pad Up", "D-Pad Up"}, {"down", "D-Pad Down", "D-Pad Down"},
		{"left", "D-Pad Left", "D-Pad Left"}, {"right", "D-Pad Right", "D-Pad Right"},
		{"options", "Pause / Options", "Options"},
	};
	s.accessibility.text_scale_percent = 100;
	s.accessibility.large_focus = true;
	s.diagnostics.build_id = "caliza-host-estratos";
	s.diagnostics.console_status = "Host preview";
	s.diagnostics.web_panel_url = "http://127.0.0.1:48200";
	s.diagnostics.probe_available = true;
	s.continue_game_id = "basalt";
	s.continue_valid = false;
	s.time_unix = 20;
	return s;
}

DemoBackend::DemoBackend() : m_snapshot(MakeDemoSnapshot()) {}

Snapshot DemoBackend::GetSnapshot()
{
	return m_snapshot;
}

void DemoBackend::Bump()
{
	m_snapshot.revision++;
	m_snapshot.time_unix++;
}

CommandId DemoBackend::Post(const Command& command)
{
	m_posted.push_back(command);
	const CommandId id = m_next_command++;
	Bump();
	if (command.type == CommandType::LaunchGame)
	{
		m_snapshot.vm.state = VmState::Running;
		m_snapshot.vm.game_id = command.id;
		for (const GameEntry& game : m_snapshot.games)
			if (game.id == command.id)
				m_snapshot.vm.title = game.title;
		m_snapshot.vm.can_save = m_snapshot.vm.can_load = true;
		m_snapshot.continue_game_id = command.id;
		m_snapshot.continue_valid = true;
	}
	else if (command.type == CommandType::SelectGame)
	{
		m_snapshot.selected_game_id = command.id;
		m_snapshot.texture_packs = {};
		if (!command.id.empty())
		{
			const auto game = std::find_if(m_snapshot.games.begin(), m_snapshot.games.end(), [&](const GameEntry& entry) {
				return entry.id == command.id;
			});
			const std::string title = game == m_snapshot.games.end() ? "Demo game" : game->title;
			auto& packs = m_snapshot.texture_packs;
			packs.catalog = TexturePacks::Catalog::Ready;
			packs.entries = {
				{"sharp", title + " HD", "Elliot Maren", "", "See the original release for terms", 685ull << 20},
				{"faithful", title + " · Faithful", "Jules Sato", "", "See the original release for terms", 312ull << 20},
			};
		}
		ApplyDemoPatchRows(command.id, m_snapshot.settings);
	}
	else if (command.type == CommandType::DownloadTexturePack || command.type == CommandType::ImportTexturePack)
	{
		auto& packs = m_snapshot.texture_packs;
		const auto it = std::find_if(packs.entries.begin(), packs.entries.end(), [&](const TexturePackEntry& entry) {
			return entry.id == command.value;
		});
		packs.installed_name = it != packs.entries.end() ? it->name : command.path.substr(command.path.rfind('/') + 1);
		packs.removable = packs.installed = true;
	}
	else if (command.type == CommandType::DeleteTexturePack)
	{
		m_snapshot.texture_packs.installed_name.clear();
		m_snapshot.texture_packs.removable = m_snapshot.texture_packs.installed = false;
	}
	else if (command.type == CommandType::PauseVm)
		m_snapshot.vm.state = VmState::Paused;
	else if (command.type == CommandType::ResumeVm)
		m_snapshot.vm.state = VmState::Running;
	else if (command.type == CommandType::ReturnToLibrary)
	{
		m_snapshot.vm.state = VmState::Idle;
		m_snapshot.vm.game_id.clear();
		m_snapshot.vm.title.clear();
	}
	else if (command.type == CommandType::SetAccessibility)
	{
		if (command.id == "text_scale")
			m_snapshot.accessibility.text_scale_percent = static_cast<uint32_t>(command.number);
		else if (command.id == "high_contrast")
			m_snapshot.accessibility.high_contrast = command.number != 0;
		else if (command.id == "reduce_motion")
			m_snapshot.accessibility.reduce_motion = command.number != 0;
		else if (command.id == "large_focus")
			m_snapshot.accessibility.large_focus = command.number != 0;
	}
	else if (command.type == CommandType::SetSetting)
	{
		for (SettingCategory& category : m_snapshot.settings)
			for (SettingItem& item : category.items)
				if (item.id == command.id)
				{
					item.value = command.value;
					item.modified = true;
				}
	}
	else if (command.type == CommandType::CreateMemoryCard)
	{
		MemoryCardInfo& card = m_snapshot.memory_cards[static_cast<size_t>(std::max(0, std::min(1, command.number)))];
		card.present = card.enabled = true;
		card.file = "Demo Card " + std::to_string(command.number + 1) + ".ps2";
		card.bytes = 8 * 1024 * 1024;
		card.used_bytes = 0;
	}
	else if (command.type == CommandType::SetBiosFolder)
	{
		m_snapshot.bios.path = command.path;
		m_snapshot.bios.present = true;
	}
	m_events.push_back({id, command.type, CommandStatus::Succeeded, m_snapshot.revision, "Demo command completed"});
	return id;
}

std::vector<CommandEvent> DemoBackend::PollEvents()
{
	std::vector<CommandEvent> out;
	out.swap(m_events);
	return out;
}

FolderListing DemoBackend::BrowseFolder(const std::string& path, const std::string&, uint32_t, uint32_t)
{
	FolderListing listing;
	listing.path = path.empty() ? "/" : path;
	listing.parent = "/";
	listing.contains_compatible = true;
	if (path.empty() || path == "/")
	{
		listing.entries = {
			{"Internal storage", "/data/caliza", true, false, true, 0},
			{"USB drive", "/mnt/usb0", true, false, true, 8},
		};
	}
	else if (path == "/mnt/usb0")
	{
		listing.entries = {
			{"games", "/mnt/usb0/games", true, false, true, 8},
			{"bios", "/mnt/usb0/bios", true, false, true, 0},
			{"aurora-hd.zip", "/mnt/usb0/aurora-hd.zip", false, false, true, 0},
		};
	}
	else
	{
		listing.entries = {
			{"aurora.iso", path + "/aurora.iso", false, true, true, 0},
			{"basalt.iso", path + "/basalt.iso", false, true, true, 0},
			{"winter-relic.chd", path + "/winter-relic.chd", false, true, true, 0},
		};
	}
	return listing;
}

void DemoBackend::SetTextScale(uint32_t percent)
{
	m_snapshot.accessibility.text_scale_percent = percent;
	Bump();
}
void DemoBackend::SetHighContrast(bool enabled)
{
	m_snapshot.accessibility.high_contrast = enabled;
	Bump();
}
void DemoBackend::SetReduceMotion(bool enabled)
{
	m_snapshot.accessibility.reduce_motion = enabled;
	Bump();
}
void DemoBackend::SetBiosPresent(bool present)
{
	m_snapshot.bios.present = present;
	m_snapshot.bios.problem = present ? std::string() : "No compatible BIOS was found in the configured folder.";
	Bump();
}
void DemoBackend::SetLibraryEmpty(bool empty)
{
	if (empty)
	{
		m_snapshot.games.clear();
		m_snapshot.sources.clear();
		m_snapshot.continue_game_id.clear();
		m_snapshot.continue_valid = false;
	}
	else
		m_snapshot = MakeDemoSnapshot();
	Bump();
}
} // namespace caliza
