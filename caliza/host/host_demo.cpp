// Caliza Estratos host preview: deterministic fictional backend data.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host_demo.h"

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

SettingItem Setting(const char* id, const char* label, const char* value, SettingKind kind, const char* description)
{
	SettingItem item;
	item.id = id;
	item.label = label;
	item.value = value;
	item.kind = kind;
	item.description = description;
	return item;
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
	internal.label = "Internal Storage";
	internal.kind = VolumeKind::Internal;
	internal.present = internal.readable = true;
	internal.total_bytes = 420000000000ull;
	internal.free_bytes = 180000000000ull;
	VolumeInfo usb;
	usb.id = "usb0";
	usb.root_path = "/mnt/usb0";
	usb.label = "USB Drive";
	usb.kind = VolumeKind::USB;
	usb.present = usb.readable = true;
	usb.total_bytes = 1000000000000ull;
	usb.free_bytes = 440000000000ull;
	s.volumes = {internal, usb};
	LibrarySource source;
	source.id = "usb0";
	source.path = "/mnt/usb0/games";
	source.volume_id = "usb0";
	source.label = "USB Drive / games";
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
	SettingCategory video;
	video.id = "video";
	video.label = "Video";
	video.items = {
		Setting("renderer", "Renderer", "Vulkan", SettingKind::Choice, "Graphics backend"),
		Setting("aspect", "Aspect Ratio", "16:9", SettingKind::Choice, "Display geometry"),
		Setting("vsync", "VSync", "true", SettingKind::Toggle, "Synchronise presentation"),
		Setting("internal_resolution", "Internal Resolution", "2", SettingKind::Number, "Native resolution multiplier"),
	};
	video.items[0].choices = {{"Vulkan", "Vulkan"}, {"Software", "Software"}};
	video.items[1].choices = {{"16:9", "16:9"}, {"4:3", "4:3"}, {"Auto", "Auto"}};
	SettingCategory system;
	system.id = "system";
	system.label = "System";
	system.items = {
		Setting("fast_boot", "Fast Boot", "false", SettingKind::Toggle, "Skip the BIOS splash"),
		Setting("cheats", "Cheats", "false", SettingKind::Toggle, "Enable patched game data"),
		Setting("backup", "Backup Memory Cards", "true", SettingKind::Toggle, "Create dated memory-card copies"),
	};
	s.settings = {video, system};
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
			{"Internal Storage", "/data/caliza", true, false, true, 0},
			{"USB Drive", "/mnt/usb0", true, false, true, 8},
		};
	}
	else if (path == "/mnt/usb0")
	{
		listing.entries = {
			{"games", "/mnt/usb0/games", true, false, true, 8},
			{"bios", "/mnt/usb0/bios", true, false, true, 0},
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
