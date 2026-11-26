// Caliza Estratos: deterministic controller-first UI model.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_model.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace caliza
{
namespace
{
constexpr double kRepeatDelay = 0.42;
constexpr double kRepeatInterval = 0.085;
constexpr double kBootSeconds = 1.15;
constexpr const char* kRootPathKey = "<locations>";

struct HomeAction
{
	std::string id;
	std::string label;
	EntryIcon icon;
};

struct NamedAction
{
	std::string id;
	std::string label;
	std::string value;
	std::string detail;
	EntryIcon icon = EntryIcon::Action;
	bool disabled = false;
	bool destructive = false;
	float progress = -1.0f;
};

std::string FormatBytes(uint64_t bytes)
{
	static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
	double value = static_cast<double>(bytes);
	int unit = 0;
	while (value >= 1024.0 && unit < 4)
	{
		value /= 1024.0;
		unit++;
	}
	char buf[64];
	if (unit == 0)
		std::snprintf(buf, sizeof(buf), "%u B", static_cast<unsigned>(bytes));
	else
		std::snprintf(buf, sizeof(buf), value >= 10.0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
	return buf;
}

std::string BoolText(bool value)
{
	return value ? "On" : "Off";
}

std::string VmStateText(VmState state)
{
	switch (state)
	{
		case VmState::Unavailable: return "Unavailable";
		case VmState::Idle: return "Idle";
		case VmState::Starting: return "Starting";
		case VmState::Running: return "Running";
		case VmState::Paused: return "Paused";
		case VmState::Stopping: return "Stopping";
		case VmState::Failed: return "Failed";
	}
	return "Unknown";
}

std::string ScanStateText(const ScanProgress& scan)
{
	switch (scan.state)
	{
		case ScanState::Idle: return {};
		case ScanState::Scanning: return "Scanning " + scan.current_path;
		case ScanState::Cancelling: return "Cancelling scan";
		case ScanState::Cancelled: return {};
		case ScanState::Complete: return {};
		case ScanState::Failed: return scan.error.empty() ? "Scan failed" : scan.error;
	}
	return {};
}

bool DirectionButton(const Input& in, int index)
{
	switch (index)
	{
		case 0: return in.left;
		case 1: return in.right;
		case 2: return in.up;
		case 3: return in.down;
	}
	return false;
}

int MoveClamped(int index, int delta, int count)
{
	if (count <= 0)
		return 0;
	return std::max(0, std::min(count - 1, index + delta));
}

std::vector<HomeAction> HomeActions(const Snapshot& snapshot)
{
	std::vector<HomeAction> out;
	if (snapshot.continue_valid && !snapshot.continue_game_id.empty())
	{
		std::string title = snapshot.continue_game_id;
		for (const GameEntry& game : snapshot.games)
			if (game.id == snapshot.continue_game_id)
			{
				title = game.title.empty() ? game.file : game.title;
				break;
			}
		out.push_back({"continue", "Continue  ·  " + title, EntryIcon::Continue});
	}
	out.push_back({"library", "Library", EntryIcon::Library});
	out.push_back({"memory", "Memory cards", EntryIcon::MemoryCard});
	out.push_back({"settings", "Settings", EntryIcon::Settings});
	out.push_back({"controls", "Controls", EntryIcon::Controls});
	out.push_back({"accessibility", "Accessibility", EntryIcon::Accessibility});
	out.push_back({"diagnostics", "Diagnostics", EntryIcon::Diagnostics});
	out.push_back({"system-menu", "PS2 system menu", EntryIcon::Game});
	return out;
}

std::vector<NamedAction> FirstRunActions(const Snapshot& snapshot)
{
	return {
		{"bios-folder", "Choose BIOS folder", "", snapshot.bios.path.empty() ? "No BIOS folder selected" : snapshot.bios.path,
			EntryIcon::Folder},
		{"check-bios", "Check again", "", snapshot.bios.checking ? "Checking…" : "Use the selected folder",
			EntryIcon::Action, snapshot.bios.checking},
		{"game-folder", "Add game folder", "", "Internal, USB or extended storage", EntryIcon::Folder},
		{"finish", "Continue to home", "", snapshot.bios.present ? "BIOS found" : "Available after the BIOS check",
			EntryIcon::Action, !snapshot.bios.present},
	};
}

const OperationStatus* FindOperation(const std::vector<OperationStatus>& operations, const char* prefix, const GameEntry* game)
{
	if (!game)
		return nullptr;
	for (const OperationStatus& operation : operations)
	{
		if (operation.id.rfind(prefix, 0) != 0)
			continue;
		if (operation.game_id == game->serial || operation.game_id == game->id)
			return &operation;
	}
	return nullptr;
}

std::string OperationValue(const OperationStatus* operation, const char* idle)
{
	if (!operation)
		return idle;
	switch (operation->state)
	{
		case OperationState::Queued: return "Queued";
		case OperationState::Running: return operation->progress >= 0.0f ? "Running" : "Working";
		case OperationState::Succeeded: return "Installed";
		case OperationState::Failed: return "Failed — try again";
		case OperationState::Cancelled: return "Cancelled";
		case OperationState::Idle: break;
	}
	return idle;
}

std::vector<NamedAction> DetailActions(const GameEntry* game, const BiosStatus& bios, const std::vector<OperationStatus>& operations)
{
	const bool launchable = game && game->can_launch && !game->damaged && bios.present;
	const OperationStatus* patches = FindOperation(operations, "online-patches:", game);
	const OperationStatus* textures = FindOperation(operations, "texture-pack:", game);
	const bool patch_busy = patches && patches->state == OperationState::Running;
	return {
		{"play", "Play", "", launchable ? "" : (bios.present ? "This image cannot launch" : "PS2 BIOS required"),
			EntryIcon::Game, !launchable},
		{"settings", "Game settings", "", "Display, emulation and fixes", EntryIcon::Settings},
		{"memory", "Memory card", "", "Choose cards for this session", EntryIcon::MemoryCard},
		{"smart-tune", "Smart Tune", "Experimental", launchable ? "Start once with performance measurement" : "Requires a launchable image and BIOS",
			EntryIcon::Action, !launchable},
		{"patches", "Get patches and cheats", OperationValue(patches, "Download"),
			patches && !patches->message.empty() ? patches->message : "PCSX2, Gabominated and cheat files", EntryIcon::Action,
			patch_busy, false, patches ? patches->progress : -1.0f},
		{"textures", "HD texture packs", OperationValue(textures, ""),
			"Browse the catalog or import a pack", EntryIcon::Action,
			false, false, textures ? textures->progress : -1.0f},
	};
}

std::vector<NamedAction> TextureActions(const TexturePacks& packs, bool has_serial)
{
	using Catalog = TexturePacks::Catalog;
	const auto& operation = packs.operation;
	const bool busy = operation.state == OperationState::Running || operation.state == OperationState::Queued;
	std::vector<NamedAction> rows;
	if (!has_serial)
		return rows;
	if (busy)
	{
		rows.push_back({"cancel", operation.cancelable ? "Cancel operation" : "Please wait", "", operation.message,
			EntryIcon::Action, !operation.cancelable, false, operation.progress});
		return rows;
	}
	if (packs.installed)
	{
		rows.push_back({"remove", packs.removable ? "Delete installed pack" : "External texture pack", "Installed",
			packs.installed_name, EntryIcon::Action, !packs.removable, packs.removable});
	}
	else
		rows.push_back({"import", "Import from storage", "", "Choose a ZIP or RAR archive", EntryIcon::Folder});
	if (packs.catalog == Catalog::Unavailable)
		rows.push_back({"retry", "Retry catalog", "", packs.catalog_message, EntryIcon::Action});
	if (operation.state == OperationState::Failed && operation.cancelable)
		rows.push_back({"forget", "Clear failed operation", "", operation.message, EntryIcon::Action});
	for (const auto& pack : packs.entries)
		rows.push_back({"pack:" + pack.id, pack.name, FormatBytes(pack.bytes), pack.authors, EntryIcon::Action, packs.installed});
	return rows;
}

std::vector<NamedAction> PauseActions(const VmStatus& vm)
{
	return {
		{"resume", "Resume", "", vm.state == VmState::Paused ? "" : "Resume when paused",
			EntryIcon::Action, vm.state != VmState::Paused},
		{"save", "Save state", "", vm.can_save ? "" : "Unavailable right now", EntryIcon::Action, !vm.can_save},
		{"load", "Load state", "", vm.can_load ? "" : "No state is available", EntryIcon::Action, !vm.can_load},
		{"disc", "Change disc", "", "", EntryIcon::Folder},
		{"return", "Return to library", "", "Unsaved progress is lost", EntryIcon::Warning, false, true},
	};
}

std::vector<NamedAction> EmptyLibraryActions(const std::string& search)
{
	std::vector<NamedAction> out = {
		{"add-folder", "Add folder", "", "", EntryIcon::Folder},
		{"rescan", "Rescan library", "", "", EntryIcon::Action},
	};
	if (!search.empty())
		out.push_back({"clear-search", "Clear search", "", search, EntryIcon::Action});
	return out;
}

std::vector<NamedAction> AccessibilityActions(const AccessibilityState& a11y)
{
	return {
		{"text_scale", "Text size", std::to_string(a11y.text_scale_percent) + "%", "100% to 200%"},
		{"high_contrast", "Contrast", a11y.high_contrast ? "High" : "Standard", "Stronger surfaces and focus"},
		{"reduce_motion", "Reduce motion", BoolText(a11y.reduce_motion), "Stops ambient movement"},
		{"large_focus", "Large focus", BoolText(a11y.large_focus), "Thicker controller focus"},
	};
}

std::string SettingValueForCommand(const SettingItem& item, int direction)
{
	switch (item.kind)
	{
		case SettingKind::Toggle:
			return item.value == "true" || item.value == "1" || item.value == "On" ? "false" : "true";
		case SettingKind::Choice:
		{
			if (item.choices.empty())
				return item.value;
			int at = 0;
			for (size_t i = 0; i < item.choices.size(); i++)
				if (item.choices[i].value == item.value)
				{
					at = static_cast<int>(i);
					break;
				}
			const int count = static_cast<int>(item.choices.size());
			at = (at + direction + count) % count;
			return item.choices[static_cast<size_t>(at)].value;
		}
		case SettingKind::Number:
		{
			char* end = nullptr;
			long value = std::strtol(item.value.c_str(), &end, 10);
			if (end == item.value.c_str())
				value = 0;
			const long step = std::labs(value) >= 100 ? 5 : 1;
			return std::to_string(std::max<long>(0, value + direction * step));
		}
		case SettingKind::Text:
		case SettingKind::Action:
			return item.value;
	}
	return item.value;
}

const char* CaptureButtonName(const UiModel::LogicalInput& in)
{
	if (in.cross) return "Cross";
	if (in.circle) return "Circle";
	if (in.square) return "Square";
	if (in.triangle) return "Triangle";
	if (in.options) return "Options";
	if (in.l1) return "L1";
	if (in.r1) return "R1";
	if (in.l2) return "L2";
	if (in.r2) return "R2";
	if (in.up) return "Up";
	if (in.down) return "Down";
	if (in.left) return "Left";
	if (in.right) return "Right";
	return nullptr;
}

std::string EncodeGameSettingId(const std::string& setting_id, const std::string& game_key)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string encoded;
	for (const unsigned char ch : game_key)
	{
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')
			encoded.push_back(static_cast<char>(ch));
		else
		{
			encoded.push_back('%');
			encoded.push_back(kHex[ch >> 4]);
			encoded.push_back(kHex[ch & 15]);
		}
	}
	return "game:" + setting_id + ":" + encoded;
}

std::string ScopedSettingCommandId(const SettingItem& item, const std::string& game_key)
{
	return !game_key.empty() && item.game_scoped ? EncodeGameSettingId(item.id, game_key) : item.id;
}

std::string JoinBreadcrumbs(const std::vector<std::string>& crumbs)
{
	std::string out;
	for (const std::string& crumb : crumbs)
	{
		if (!out.empty())
			out += "  ›  ";
		out += crumb;
	}
	return out;
}

} // namespace

UiModel::UiModel(Backend* backend) : m_backend(backend) {}

UiModel::LogicalInput UiModel::EvaluateInput(const Input& input, double dt)
{
	LogicalInput out;
	out.cross = input.cross && !m_previous_input.cross;
	out.circle = input.circle && !m_previous_input.circle;
	out.square = input.square && !m_previous_input.square;
	out.triangle = input.triangle && !m_previous_input.triangle;
	out.options = input.options && !m_previous_input.options;
	out.l1 = input.l1 && !m_previous_input.l1;
	out.r1 = input.r1 && !m_previous_input.r1;
	out.l2 = input.l2 && !m_previous_input.l2;
	out.r2 = input.r2 && !m_previous_input.r2;

	for (int i = 0; i < 4; i++)
	{
		const bool now = DirectionButton(input, i);
		RepeatState& st = m_repeat[i];
		bool emit = false;
		if (now && !st.held)
		{
			emit = true;
			st.held = true;
			st.held_seconds = 0.0;
			st.since_last = 0.0;
		}
		else if (now)
		{
			st.held_seconds += dt;
			st.since_last += dt;
			if (st.held_seconds >= kRepeatDelay && st.since_last >= kRepeatInterval)
			{
				emit = true;
				st.since_last = 0.0;
			}
		}
		else
		{
			st = RepeatState();
		}
		switch (i)
		{
			case 0: out.left = emit; break;
			case 1: out.right = emit; break;
			case 2: out.up = emit; break;
			case 3: out.down = emit; break;
		}
	}
	return out;
}

void UiModel::Tick(const Snapshot& snapshot, const std::vector<CommandEvent>& events, const Input& input, double dt)
{
	m_snapshot = snapshot;
	if (m_toast_seconds > 0.0)
	{
		m_toast_seconds = std::max(0.0, m_toast_seconds - dt);
		if (m_toast_seconds == 0.0)
			m_toast.clear();
	}
	for (const CommandEvent& event : events)
	{
		if (event.type == CommandType::SelectGame && event.status == CommandStatus::Succeeded)
			continue;
		if (event.status == CommandStatus::Failed || event.status == CommandStatus::Cancelled ||
			event.status == CommandStatus::Succeeded)
		{
			m_toast = event.message;
			if (m_toast.empty())
				m_toast = event.status == CommandStatus::Failed ? "Command failed" :
					event.status == CommandStatus::Cancelled ? "Command cancelled" : "Done";
			m_toast_revision = event.revision;
			m_toast_seconds = 4.0;
		}
	}

	if (m_screen == Screen::Boot)
		m_boot_seconds += dt;
	LogicalInput in = EvaluateInput(input, dt);
	if (m_dialog.open)
		HandleDialog(in);
	else if (m_keyboard.open)
		HandleKeyboard(in);
	else
	{
		const bool vm_active = m_snapshot.vm.state == VmState::Running || m_snapshot.vm.state == VmState::Paused;
		if (in.options && vm_active && m_screen != Screen::PauseOverlay && m_screen != Screen::Boot &&
			m_screen != Screen::FolderPicker)
		{
			OpenPause();
		}
		else
		{
			Dispatch(in);
		}
	}

	if (m_screen == Screen::PauseOverlay && !m_dialog.open &&
		(m_snapshot.vm.state == VmState::Idle || m_snapshot.vm.state == VmState::Unavailable ||
			m_snapshot.vm.state == VmState::Failed))
	{
		OpenLibrary();
	}

	SyncAfterSnapshot();
	m_previous_input = input;
}

void UiModel::Dispatch(const LogicalInput& in)
{
	switch (m_screen)
	{
		case Screen::Boot: HandleBoot(in); break;
		case Screen::FirstRun: HandleFirstRun(in); break;
		case Screen::Home: HandleHome(in); break;
		case Screen::Library: HandleLibrary(in); break;
		case Screen::GameDetail: HandleGameDetail(in); break;
		case Screen::TexturePacks: HandleTexturePacks(in); break;
		case Screen::PauseOverlay: HandlePause(in); break;
		case Screen::MemoryCards: HandleMemoryCards(in); break;
		case Screen::Settings: HandleSettings(in); break;
		case Screen::Controls: HandleControls(in); break;
		case Screen::Accessibility: HandleAccessibility(in); break;
		case Screen::Diagnostics: HandleDiagnostics(in); break;
		case Screen::FolderPicker: HandleFolderPicker(in); break;
	}
}

void UiModel::SyncAfterSnapshot()
{
	const std::vector<HomeAction> home = HomeActions(m_snapshot);
	m_home_index = ClampIndex(m_home_index, static_cast<int>(home.size()));
	m_first_run_index = ClampIndex(m_first_run_index, static_cast<int>(FirstRunActions(m_snapshot).size()));
	m_filter_index = ClampIndex(m_filter_index, static_cast<int>(Filters().size()));
	SyncLibraryFocus();
	m_detail_index = ClampIndex(m_detail_index, static_cast<int>(DetailActions(FindGame(m_detail_game_id), m_snapshot.bios, m_snapshot.operations).size()));
	m_pause_index = ClampIndex(m_pause_index, 5);
	m_memory_slot = std::max(0, std::min(1, m_memory_slot));
	m_memory_index = std::max(0, m_memory_index);
	SyncSettingsFocus();
	m_controls_index = ClampIndex(m_controls_index, static_cast<int>(m_snapshot.controls.size()));
	m_accessibility_index = ClampIndex(m_accessibility_index, 4);
	m_diagnostics_index = ClampIndex(m_diagnostics_index, 1);
	if (m_screen == Screen::FolderPicker)
		m_picker.row_index = ClampIndex(m_picker.row_index, static_cast<int>(m_picker.rows.size()));

	if (!m_pending_scan_added_path.empty())
	{
		for (const LibrarySource& source : m_snapshot.sources)
		{
			if (source.path == m_pending_scan_added_path)
			{
				if (m_snapshot.scan.state != ScanState::Scanning || m_snapshot.scan.source_id != source.id)
					Post(CommandType::ScanSource, source.id);
				m_pending_scan_added_path.clear();
				break;
			}
		}
	}
}

int UiModel::ClampIndex(int index, int count) const
{
	if (count <= 0)
		return 0;
	return std::max(0, std::min(count - 1, index));
}

void UiModel::Post(CommandType type, const std::string& id, const std::string& path, const std::string& value, int number)
{
	if (!m_backend)
		return;
	Command command;
	command.type = type;
	command.id = id;
	command.path = path;
	command.value = value;
	command.number = number;
	m_backend->Post(command);
}

void UiModel::SetToast(std::string text)
{
	m_toast = std::move(text);
	m_toast_revision = m_snapshot.revision;
	m_toast_seconds = 4.0;
}

std::string UiModel::Lower(std::string text)
{
	for (char& c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
}

bool UiModel::ContainsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
	return Lower(haystack).find(Lower(needle)) != std::string::npos;
}

bool UiModel::HasTag(const GameEntry& game, const char* tag)
{
	return std::find(game.badges.begin(), game.badges.end(), tag) != game.badges.end();
}

std::vector<UiModel::FilterOption> UiModel::Filters() const
{
	return {
		{"all", "All games", FilterKind::All, VolumeKind::Internal},
		{"recent", "Recent", FilterKind::Recent, VolumeKind::Internal},
		{"internal", "Internal", FilterKind::Volume, VolumeKind::Internal},
		{"usb", "USB", FilterKind::Volume, VolumeKind::USB},
		{"extended", "Extended", FilterKind::Volume, VolumeKind::Extended},
		{"attention", "Needs attention", FilterKind::Damaged, VolumeKind::Internal},
		{"add-folder", "Add folder", FilterKind::AddFolder, VolumeKind::Internal},
	};
}

std::vector<const GameEntry*> UiModel::VisibleGames() const
{
	std::vector<FilterOption> filters = Filters();
	const FilterOption& filter = filters[static_cast<size_t>(ClampIndex(m_filter_index, static_cast<int>(filters.size())))];
	bool show_hidden = false;
	for (const SettingCategory& category : m_snapshot.settings)
		for (const SettingItem& item : category.items)
			if (item.id == "shelf-global.show-hidden-games")
				show_hidden = item.value == "true";
	std::vector<const GameEntry*> out;
	for (const GameEntry& game : m_snapshot.games)
	{
		if (game.hidden && !show_hidden)
			continue;
		bool include = true;
		if (filter.kind == FilterKind::Recent)
			include = game.last_played != 0;
		else if (filter.kind == FilterKind::Damaged)
			include = game.damaged || !game.problem.empty() || !game.can_launch;
		else if (filter.kind == FilterKind::Volume)
		{
			const LibrarySource* source = nullptr;
			for (const LibrarySource& candidate : m_snapshot.sources)
				if (candidate.id == game.source_id)
				{
					source = &candidate;
					break;
				}
			// A USB slot can have a readable mount path and no durable platform ID.
			const std::string& path = source && !source->path.empty() ? source->path : game.path;
			const VolumeInfo* volume = nullptr;
			for (const VolumeInfo& candidate : m_snapshot.volumes)
			{
				const std::string& root = candidate.root_path;
				if (root.empty() || (volume && root.size() <= volume->root_path.size()))
					continue;
				if (path == root || (path.compare(0, root.size(), root) == 0 &&
					(root.back() == '/' || (path.size() > root.size() && path[root.size()] == '/'))))
					volume = &candidate;
			}
			if (!volume && source && !source->volume_id.empty())
				for (const VolumeInfo& candidate : m_snapshot.volumes)
					if (candidate.id == source->volume_id)
					{
						volume = &candidate;
						break;
					}
			include = volume ? volume->kind == filter.volume_kind : filter.volume_kind == VolumeKind::Internal;
		}

		if (include && !m_library_search.empty())
			include = ContainsCaseInsensitive(game.title, m_library_search) ||
				ContainsCaseInsensitive(game.serial, m_library_search) || ContainsCaseInsensitive(game.file, m_library_search);
		if (include)
			out.push_back(&game);
	}
	if (filter.kind == FilterKind::Recent)
		std::stable_sort(out.begin(), out.end(), [](const GameEntry* a, const GameEntry* b) {
			return a->last_played > b->last_played;
		});
	return out;
}

const GameEntry* UiModel::FindGame(const std::string& id) const
{
	for (const GameEntry& game : m_snapshot.games)
		if (game.id == id)
			return &game;
	return nullptr;
}

bool UiModel::GameLaunchable(const GameEntry& game) const
{
	return game.can_launch && !game.damaged && m_snapshot.bios.present;
}

void UiModel::SyncLibraryFocus()
{
	const std::vector<const GameEntry*> games = VisibleGames();
	if (games.empty())
	{
		m_grid_index = 0;
		m_library_focus_game_id.clear();
		m_empty_index = ClampIndex(m_empty_index, static_cast<int>(EmptyLibraryActions(m_library_search).size()));
		return;
	}
	if (!m_library_focus_game_id.empty())
	{
		for (size_t i = 0; i < games.size(); i++)
			if (games[i]->id == m_library_focus_game_id)
			{
				m_grid_index = static_cast<int>(i);
				return;
			}
	}
	m_grid_index = ClampIndex(m_grid_index, static_cast<int>(games.size()));
	m_library_focus_game_id = games[static_cast<size_t>(m_grid_index)]->id;
}

void UiModel::SyncSettingsFocus()
{
	const auto& categories = m_snapshot.settings;
	m_settings_category = ClampIndex(m_settings_category, static_cast<int>(categories.size()));
	if (!categories.empty())
	{
		const SettingCategory& category = categories[static_cast<size_t>(m_settings_category)];
		int& item = m_settings_item_by_category[category.id];
		item = ClampIndex(item, static_cast<int>(category.items.size()));
	}
}

void UiModel::LeaveBoot()
{
	if (m_snapshot.bios.present)
		OpenHome();
	else
		m_screen = Screen::FirstRun;
}

void UiModel::OpenHome()
{
	m_screen = Screen::Home;
}

void UiModel::OpenLibrary()
{
	m_screen = Screen::Library;
	SyncLibraryFocus();
}

void UiModel::OpenGameDetail(const std::string& game_id)
{
	m_detail_game_id = game_id;
	m_detail_index = 0;
	m_screen = Screen::GameDetail;
}

void UiModel::OpenPause()
{
	if (m_snapshot.vm.state == VmState::Running)
		Post(CommandType::PauseVm);
	m_pause_index = 0;
	m_pause_return_screen = m_screen;
	m_screen = Screen::PauseOverlay;
}

void UiModel::OpenSettings(Screen return_screen, const std::string& game_id)
{
	m_settings_return_screen = return_screen;
	m_settings_game_id = game_id;
	m_settings_zone = 0;
	Post(CommandType::SelectGame, game_id);
	m_screen = Screen::Settings;
	SyncSettingsFocus();
}

void UiModel::HandleBoot(const LogicalInput& in)
{
	const bool skip = in.cross || in.options || in.circle;
	if ((m_boot_seconds >= kBootSeconds && !m_snapshot.bios.checking) || skip)
		LeaveBoot();
}

void UiModel::HandleFirstRun(const LogicalInput& in)
{
	const int count = static_cast<int>(FirstRunActions(m_snapshot).size());
	if (in.up)
		m_first_run_index = MoveClamped(m_first_run_index, -1, count);
	if (in.down)
		m_first_run_index = MoveClamped(m_first_run_index, 1, count);
	if (in.circle)
	{
		OpenHome();
		return;
	}
	if (!in.cross)
		return;
	const std::string id = FirstRunActions(m_snapshot)[static_cast<size_t>(m_first_run_index)].id;
	if (id == "bios-folder")
		OpenPicker(PickerPurpose::BiosFolder, Screen::FirstRun);
	else if (id == "check-bios")
	{
		if (m_snapshot.bios.path.empty())
			SetToast("Choose a BIOS folder first");
		else
			Post(CommandType::SetBiosFolder, "", m_snapshot.bios.path);
	}
	else if (id == "game-folder")
		OpenPicker(PickerPurpose::AddLibraryFolder, Screen::FirstRun);
	else if (id == "finish" && m_snapshot.bios.present)
		OpenHome();
}

void UiModel::HandleHome(const LogicalInput& in)
{
	const std::vector<HomeAction> actions = HomeActions(m_snapshot);
	const int count = static_cast<int>(actions.size());
	if (in.up)
		m_home_index = MoveClamped(m_home_index, -1, count);
	if (in.down)
		m_home_index = MoveClamped(m_home_index, 1, count);
	if (in.square && count > 0 && actions[static_cast<size_t>(m_home_index)].id == "continue")
		OpenGameDetail(m_snapshot.continue_game_id);
	else if (in.cross && count > 0)
		ActivateHome(m_home_index);
}

void UiModel::ActivateHome(int index)
{
	const std::vector<HomeAction> actions = HomeActions(m_snapshot);
	if (actions.empty())
		return;
	const std::string& id = actions[static_cast<size_t>(ClampIndex(index, static_cast<int>(actions.size())))].id;
	if (id == "continue")
		Post(CommandType::LaunchGame, m_snapshot.continue_game_id);
	else if (id == "library")
		OpenLibrary();
	else if (id == "system-menu")
		Post(CommandType::LaunchSystemMenu);
	else if (id == "memory")
	{
		m_memory_return_screen = Screen::Home;
		m_screen = Screen::MemoryCards;
	}
	else if (id == "settings")
		OpenSettings(Screen::Home);
	else if (id == "controls")
		m_screen = Screen::Controls;
	else if (id == "accessibility")
		m_screen = Screen::Accessibility;
	else if (id == "diagnostics")
		m_screen = Screen::Diagnostics;
}

void UiModel::HandleLibrary(const LogicalInput& in)
{
	const std::vector<FilterOption> filters = Filters();
	const int filter_count = static_cast<int>(filters.size());
	const std::vector<const GameEntry*> games = VisibleGames();
	const bool empty = games.empty();
	const int content_count = empty ? static_cast<int>(EmptyLibraryActions(m_library_search).size()) :
		static_cast<int>(games.size());

	if (in.triangle)
	{
		OpenKeyboard(KeyboardTarget::LibrarySearch, "Search library", m_library_search);
		return;
	}
	if (in.circle)
	{
		OpenHome();
		return;
	}
	if (in.l1 || in.r1)
	{
		const int delta = in.l1 ? -1 : 1;
		m_filter_index = (m_filter_index + delta + filter_count) % filter_count;
		m_grid_index = 0;
		m_library_focus_game_id.clear();
		SyncLibraryFocus();
		return;
	}

	if (m_library_zone == 0)
	{
		if (in.up || in.down)
		{
			m_filter_index = MoveClamped(m_filter_index, in.up ? -1 : 1, filter_count);
			m_grid_index = 0;
			m_library_focus_game_id.clear();
			SyncLibraryFocus();
		}
		if (in.right)
			m_library_zone = 1;
		if (in.cross)
		{
			const FilterOption& filter = filters[static_cast<size_t>(m_filter_index)];
			if (filter.kind == FilterKind::AddFolder)
				OpenPicker(PickerPurpose::AddLibraryFolder, Screen::Library);
			else
				m_library_zone = 1;
		}
		return;
	}

	// Content zone. Movement clamps at every edge, so focus always remains on a real tile/action.
	if (in.left)
	{
		if (empty || (m_grid_index % kLibraryColumns) == 0)
			m_library_zone = 0;
		else
			m_grid_index = MoveClamped(m_grid_index, -1, content_count);
	}
	if (in.right)
	{
		if (!empty && (m_grid_index % kLibraryColumns) < kLibraryColumns - 1)
			m_grid_index = MoveClamped(m_grid_index, 1, content_count);
	}
	if (in.up)
	{
		if (empty)
			m_empty_index = MoveClamped(m_empty_index, -1, content_count);
		else if (m_grid_index >= kLibraryColumns)
			m_grid_index = MoveClamped(m_grid_index, -kLibraryColumns, content_count);
	}
	if (in.down)
	{
		if (empty)
			m_empty_index = MoveClamped(m_empty_index, 1, content_count);
		else if (m_grid_index + kLibraryColumns < content_count)
			m_grid_index = MoveClamped(m_grid_index, kLibraryColumns, content_count);
	}

	if (!empty)
	{
		m_grid_index = ClampIndex(m_grid_index, content_count);
		m_library_focus_game_id = games[static_cast<size_t>(m_grid_index)]->id;
		if (in.square)
		{
			OpenGameDetail(m_library_focus_game_id);
			return;
		}
		if (in.cross)
			ActivateLibraryTile();
		return;
	}

	if (in.cross && content_count > 0)
	{
		const std::vector<NamedAction> actions = EmptyLibraryActions(m_library_search);
		const std::string& id = actions[static_cast<size_t>(ClampIndex(m_empty_index, content_count))].id;
		if (id == "add-folder")
			OpenPicker(PickerPurpose::AddLibraryFolder, Screen::Library);
		else if (id == "rescan")
			Post(CommandType::RescanAll);
		else if (id == "clear-search")
			m_library_search.clear();
	}
}

void UiModel::ActivateLibraryTile()
{
	const std::vector<const GameEntry*> games = VisibleGames();
	if (games.empty())
		return;
	const GameEntry& game = *games[static_cast<size_t>(ClampIndex(m_grid_index, static_cast<int>(games.size())))];
	m_library_focus_game_id = game.id;
	if (GameLaunchable(game))
		Post(CommandType::LaunchGame, game.id);
	else
		OpenGameDetail(game.id);
}

void UiModel::HandleGameDetail(const LogicalInput& in)
{
	const GameEntry* game = FindGame(m_detail_game_id);
	if (!game)
	{
		OpenLibrary();
		return;
	}
	const std::vector<NamedAction> actions = DetailActions(game, m_snapshot.bios, m_snapshot.operations);
	const int count = static_cast<int>(actions.size());
	if (in.up)
		m_detail_index = MoveClamped(m_detail_index, -1, count);
	if (in.down)
		m_detail_index = MoveClamped(m_detail_index, 1, count);
	if (in.circle)
	{
		OpenLibrary();
		return;
	}
	if (in.cross)
		ActivateGameDetail(m_detail_index);
}

void UiModel::ActivateGameDetail(int index)
{
	const GameEntry* game = FindGame(m_detail_game_id);
	if (!game)
		return;
	const std::vector<NamedAction> actions = DetailActions(game, m_snapshot.bios, m_snapshot.operations);
	const std::string& id = actions[static_cast<size_t>(ClampIndex(index, static_cast<int>(actions.size())))].id;
	if (id == "play" && GameLaunchable(*game))
		Post(CommandType::LaunchGame, game->id);
	else if (id == "smart-tune" && GameLaunchable(*game))
		Post(CommandType::LaunchSmartTune, game->id);
	else if (id == "patches")
		Post(CommandType::FetchOnlinePatches, game->id);
	else if (id == "textures")
	{
		m_texture_index = 0;
		Post(CommandType::SelectGame, game->id);
		m_screen = Screen::TexturePacks;
	}
	else if (id == "settings")
		OpenSettings(Screen::GameDetail, m_detail_game_id);
	else if (id == "memory")
	{
		m_memory_return_screen = Screen::GameDetail;
		m_screen = Screen::MemoryCards;
	}
}

void UiModel::HandleTexturePacks(const LogicalInput& in)
{
	const GameEntry* game = FindGame(m_detail_game_id);
	if (!game)
	{
		OpenLibrary();
		return;
	}
	if (in.circle)
	{
		Post(CommandType::SelectGame);
		m_screen = Screen::GameDetail;
		return;
	}
	if (m_snapshot.selected_game_id != game->id)
		return;
	const auto& packs = m_snapshot.texture_packs;
	const auto rows = TextureActions(packs, !game->serial.empty());
	const int count = static_cast<int>(rows.size());
	if (in.up)
		m_texture_index = MoveClamped(m_texture_index, -1, count);
	if (in.down)
		m_texture_index = MoveClamped(m_texture_index, 1, count);
	if (!in.cross || rows.empty())
		return;
	const auto& row = rows[static_cast<size_t>(ClampIndex(m_texture_index, count))];
	if (row.disabled)
		return;
	if (row.id == "cancel")
		OpenDialog(PendingAction::CancelTexturePack, "Cancel texture operation?",
			"Any partial download for this operation will be removed.", "Cancel operation", true, game->id);
	else if (row.id == "forget")
		OpenDialog(PendingAction::CancelTexturePack, "Clear failed operation?",
			"Any partial download for this operation will be removed.", "Clear operation", true, game->id);
	else if (row.id == "retry")
		Post(CommandType::RetryTexturePackCatalog, game->id);
	else if (row.id == "import")
		OpenPicker(PickerPurpose::ImportTexturePack, Screen::TexturePacks);
	else if (row.id == "remove")
		OpenDialog(PendingAction::DeleteTexturePack, "Delete installed pack?",
			"The installed textures for this game will be removed.", "Delete pack", true, game->id);
	else
	{
		const auto pack = std::find_if(packs.entries.begin(), packs.entries.end(),
			[&](const TexturePackEntry& entry) { return "pack:" + entry.id == row.id; });
		if (pack == packs.entries.end())
			return;
		std::string detail = FormatBytes(pack->bytes);
		if (!pack->authors.empty())
			detail += " · " + pack->authors;
		if (!pack->license.empty())
			detail += "\n" + pack->license;
		if (!pack->source.empty())
			detail += "\n" + pack->source;
		OpenDialog(PendingAction::DownloadTexturePack, pack->name, detail, "Download", false, game->id, "", pack->id);
	}
}

void UiModel::HandlePause(const LogicalInput& in)
{
	const std::vector<NamedAction> actions = PauseActions(m_snapshot.vm);
	const int count = static_cast<int>(actions.size());
	if (in.up)
		m_pause_index = MoveClamped(m_pause_index, -1, count);
	if (in.down)
		m_pause_index = MoveClamped(m_pause_index, 1, count);
	if (in.circle || in.options)
	{
		if (m_snapshot.vm.state == VmState::Paused)
			Post(CommandType::ResumeVm);
		m_screen = m_pause_return_screen;
		return;
	}
	if (in.cross)
		ActivatePause(m_pause_index);
}

void UiModel::ActivatePause(int index)
{
	const std::vector<NamedAction> actions = PauseActions(m_snapshot.vm);
	const NamedAction& action = actions[static_cast<size_t>(ClampIndex(index, static_cast<int>(actions.size())))];
	if (action.disabled)
		return;
	if (action.id == "resume")
	{
		Post(CommandType::ResumeVm);
		m_screen = m_pause_return_screen;
	}
	else if (action.id == "save")
		Post(CommandType::SaveState, "", "", "", 1); // the runtime schema and the legacy combos both use slot 1
	else if (action.id == "load")
		Post(CommandType::LoadState, "", "", "", 1);
	else if (action.id == "disc")
		OpenPicker(PickerPurpose::ChangeDisc, Screen::PauseOverlay);
	else if (action.id == "return")
		OpenDialog(PendingAction::ReturnToLibrary, "Return to library?",
			"The current game session closes. Unsaved progress in memory is lost.", "Return", true);
}

static std::vector<NamedAction> MemoryActionsForSlot(const Snapshot& snapshot, int slot)
{
	std::vector<NamedAction> out;
	for (const MemoryCardInfo& card : snapshot.memory_cards)
	{
		if (!card.present || card.file.empty())
			continue;
		NamedAction action;
		action.id = card.file;
		action.label = card.file;
		action.value = card.slot == slot ? "In slot " + std::to_string(slot + 1) : "Available";
		action.detail = FormatBytes(card.bytes) + " card";
		action.icon = EntryIcon::MemoryCard;
		out.push_back(action);
	}
	out.push_back({"create", "Create memory card", "", "For slot " + std::to_string(slot + 1), EntryIcon::Action});
	return out;
}

void UiModel::HandleMemoryCards(const LogicalInput& in)
{
	const std::vector<NamedAction> actions = MemoryActionsForSlot(m_snapshot, m_memory_slot);
	const int count = static_cast<int>(actions.size());
	if (in.circle)
	{
		m_screen = m_memory_return_screen;
		if (m_screen == Screen::GameDetail && !FindGame(m_detail_game_id))
			OpenLibrary();
		return;
	}
	if (m_memory_zone == 0)
	{
		if (in.left)
			m_memory_slot = MoveClamped(m_memory_slot, -1, 2);
		if (in.right)
			m_memory_slot = MoveClamped(m_memory_slot, 1, 2);
		if (in.down)
		{
			m_memory_zone = 1;
			m_memory_index = ClampIndex(m_memory_index, count);
		}
		if (in.cross)
		{
			bool present = false;
			for (const MemoryCardInfo& card : m_snapshot.memory_cards)
				if (card.slot == m_memory_slot && card.present)
					present = true;
			if (!present)
				Post(CommandType::CreateMemoryCard, "", "", "", m_memory_slot);
		}
		return;
	}
	if (in.up)
	{
		if (m_memory_index == 0)
			m_memory_zone = 0;
		else
			m_memory_index = MoveClamped(m_memory_index, -1, count);
	}
	if (in.down)
		m_memory_index = MoveClamped(m_memory_index, 1, count);
	if (in.cross)
		ActivateMemoryCard();
}

void UiModel::ActivateMemoryCard()
{
	const std::vector<NamedAction> actions = MemoryActionsForSlot(m_snapshot, m_memory_slot);
	if (actions.empty())
		return;
	const NamedAction& action = actions[static_cast<size_t>(ClampIndex(m_memory_index, static_cast<int>(actions.size())))];
	if (action.id == "create")
		Post(CommandType::CreateMemoryCard, "", "", "", m_memory_slot);
	else
		Post(CommandType::AssignMemoryCard, action.id, action.id, "", m_memory_slot);
}

void UiModel::HandleSettings(const LogicalInput& in)
{
	if (m_snapshot.settings.empty())
	{
		if (in.circle)
			m_screen = m_settings_return_screen;
		return;
	}
	const int category_count = static_cast<int>(m_snapshot.settings.size());
	m_settings_category = ClampIndex(m_settings_category, category_count);
	const SettingCategory& category = m_snapshot.settings[static_cast<size_t>(m_settings_category)];
	int& item_index = m_settings_item_by_category[category.id];
	const int item_count = static_cast<int>(category.items.size());

	if (m_settings_zone == 0)
	{
		if (in.up)
			m_settings_category = MoveClamped(m_settings_category, -1, category_count);
		if (in.down)
			m_settings_category = MoveClamped(m_settings_category, 1, category_count);
		if ((in.right || in.cross) && item_count > 0)
			m_settings_zone = 1;
		if (in.circle)
		{
			m_screen = m_settings_return_screen;
			if (m_screen == Screen::GameDetail && !FindGame(m_detail_game_id))
				OpenLibrary();
		}
		return;
	}

	if (in.up)
		item_index = MoveClamped(item_index, -1, item_count);
	if (in.down)
		item_index = MoveClamped(item_index, 1, item_count);
	if (in.circle)
	{
		m_settings_zone = 0;
		return;
	}
	if (item_count == 0)
		return;
	const SettingItem& item = category.items[static_cast<size_t>(ClampIndex(item_index, item_count))];
	if (in.square && item.modified)
	{
		OpenDialog(PendingAction::ResetSetting, "Reset setting?", item.label + " returns to its default value.",
			"Reset", item.destructive, ScopedSettingCommandId(item, m_settings_game_id));
		return;
	}
	if (in.left)
		ActivateSettingsItem(-1);
	else if (in.right || in.cross)
		ActivateSettingsItem(1);
}

void UiModel::ActivateSettingsItem(int direction)
{
	if (m_snapshot.settings.empty())
		return;
	const SettingCategory& category = m_snapshot.settings[static_cast<size_t>(ClampIndex(m_settings_category,
		static_cast<int>(m_snapshot.settings.size())))];
	if (category.items.empty())
		return;
	int item_index = 0;
	if (const auto it = m_settings_item_by_category.find(category.id); it != m_settings_item_by_category.end())
		item_index = it->second;
	const SettingItem& item = category.items[static_cast<size_t>(ClampIndex(item_index, static_cast<int>(category.items.size())))];
	ChangeSetting(item, direction);
}

void UiModel::ChangeSetting(const SettingItem& item, int direction)
{
	if (item.kind == SettingKind::Text)
	{
		OpenKeyboard(KeyboardTarget::SettingText, item.label, item.value, ScopedSettingCommandId(item, m_settings_game_id));
		return;
	}
	if (item.kind == SettingKind::Action)
	{
		if (item.destructive)
			OpenDialog(PendingAction::ApplyRecommendedSettings, item.label, item.description, "Apply", true, item.id,
				"", item.value);
		else if (item.id.find("recommended") != std::string::npos)
			Post(CommandType::ApplyRecommendedSettings);
		else
			Post(CommandType::SetSetting, ScopedSettingCommandId(item, m_settings_game_id), "", "activate");
		return;
	}
	Post(CommandType::SetSetting, ScopedSettingCommandId(item, m_settings_game_id), "", SettingValueForCommand(item, direction));
}

void UiModel::HandleControls(const LogicalInput& in)
{
	const int count = static_cast<int>(m_snapshot.controls.size());
	if (m_controls_capture)
	{
		if (const char* button = CaptureButtonName(in))
		{
			if (count > 0)
			{
				const ControlBinding& binding = m_snapshot.controls[static_cast<size_t>(ClampIndex(m_controls_index, count))];
				Post(CommandType::SetControlBinding, binding.action, "", button);
			}
			m_controls_capture = false;
		}
		else if (in.options)
		{
			m_controls_capture = false;
			SetToast("Remap cancelled");
		}
		return;
	}
	if (in.up)
		m_controls_index = MoveClamped(m_controls_index, -1, count);
	if (in.down)
		m_controls_index = MoveClamped(m_controls_index, 1, count);
	if (in.circle)
	{
		OpenHome();
		return;
	}
	if (in.square)
	{
		OpenDialog(PendingAction::ResetControlBindings, "Reset controls?",
			"Every action returns to the default controller layout.", "Reset", true);
		return;
	}
	if (in.cross && count > 0)
	{
		const ControlBinding& binding = m_snapshot.controls[static_cast<size_t>(ClampIndex(m_controls_index, count))];
		if (binding.editable)
			m_controls_capture = true;
	}
}

void UiModel::HandleAccessibility(const LogicalInput& in)
{
	const int count = 4;
	if (in.up)
		m_accessibility_index = MoveClamped(m_accessibility_index, -1, count);
	if (in.down)
		m_accessibility_index = MoveClamped(m_accessibility_index, 1, count);
	if (in.circle)
	{
		OpenHome();
		return;
	}
	if (in.left)
		ActivateAccessibility(-1);
	else if (in.right || in.cross)
		ActivateAccessibility(1);
}

void UiModel::ActivateAccessibility(int direction)
{
	const AccessibilityState& a11y = m_snapshot.accessibility;
	std::string id;
	int value = 0;
	switch (ClampIndex(m_accessibility_index, 4))
	{
		case 0:
			id = "text_scale";
			value = static_cast<int>(a11y.text_scale_percent) + direction * 25;
			value = std::max(100, std::min(200, value));
			break;
		case 1:
			id = "high_contrast";
			value = a11y.high_contrast ? 0 : 1;
			break;
		case 2:
			id = "reduce_motion";
			value = a11y.reduce_motion ? 0 : 1;
			break;
		case 3:
			id = "large_focus";
			value = a11y.large_focus ? 0 : 1;
			break;
	}
	Post(CommandType::SetAccessibility, id, "", std::to_string(value), value);
}

void UiModel::HandleDiagnostics(const LogicalInput& in)
{
	if (in.circle)
	{
		OpenHome();
		return;
	}
	// Only the report action is focusable; metadata rows are informational and never swallow focus.
	if (in.cross)
		SetToast("Native report collection is not available yet; use the web panel");
}

void UiModel::ActivateDiagnostics()
{
	Post(CommandType::GenerateReport);
}

void UiModel::OpenPicker(PickerPurpose purpose, Screen return_screen, const std::string& initial_path,
	const std::string& source_id)
{
	m_picker = PickerState();
	m_picker.purpose = purpose;
	m_picker.return_screen = return_screen;
	m_picker.source_id = source_id;
	m_screen = Screen::FolderPicker;
	if (initial_path.empty())
	{
		m_picker.at_root = true;
		RebuildPickerRows();
	}
	else
	{
		BrowsePickerPath(initial_path);
	}
}

void UiModel::ClosePicker(bool restore)
{
	if (restore)
		m_screen = m_picker.return_screen;
	else
		OpenHome();
}

void UiModel::RebuildPickerRows()
{
	m_picker.rows.clear();
	if (m_picker.at_root)
	{
		bool any_volume = false;
		for (const VolumeInfo& volume : m_snapshot.volumes)
		{
			any_volume = true;
			PickerRowData row;
			row.id = "volume:" + (volume.id.empty() ? volume.root_path : volume.id);
			row.label = volume.label.empty() ? volume.root_path : volume.label;
			row.path = volume.root_path;
			row.directory = true;
			row.disabled = !volume.present || !volume.readable;
			row.detail = !volume.present ? "Disconnected" : !volume.readable ? "Not readable" :
				(volume.total_bytes ? FormatBytes(volume.free_bytes) + " free" : volume.root_path);
			if (!volume.status.empty() && row.disabled)
				row.detail += " · " + volume.status;
			m_picker.rows.push_back(row);
		}
		if (!any_volume)
		{
			PickerRowData row;
			row.id = "volume:internal";
			row.label = "Internal storage";
			row.path = m_snapshot.data_root.empty() ? "/data/caliza" : m_snapshot.data_root;
			row.directory = true;
			row.detail = "Caliza data";
			m_picker.rows.push_back(row);
		}
		for (const LibrarySource& source : m_snapshot.sources)
		{
			if (source.state == SourceState::Connected)
				continue;
			PickerRowData row;
			row.id = "source:" + source.id;
			row.label = source.label.empty() ? source.path : source.label;
			row.path = source.path;
			row.directory = true;
			row.disabled = true;
			row.detail = source.state == SourceState::Disconnected ? "Saved folder · disconnected" :
				source.state == SourceState::Unreadable ? "Saved folder · unreadable" : "Saved folder";
			m_picker.rows.push_back(row);
		}
		m_picker.row_index = ClampIndex(m_picker.row_index, static_cast<int>(m_picker.rows.size()));
		return;
	}

	PickerRowData use;
	use.id = "use-current";
	use.label = m_picker.purpose == PickerPurpose::BiosFolder ? "Use this BIOS folder" : "Use this folder";
	use.detail = m_picker.current_path;
	use.path = m_picker.current_path;
	use.use_current = true;
	const bool importing = m_picker.purpose == PickerPurpose::ImportTexturePack;
	if (!importing)
		m_picker.rows.push_back(use);

	PickerRowData parent;
	parent.id = "parent";
	parent.label = "Up one level";
	parent.detail = m_picker.listing.parent.empty() ? "Locations" : m_picker.listing.parent;
	parent.path = m_picker.listing.parent;
	parent.parent = true;
	m_picker.rows.push_back(parent);

	for (const FolderEntry& entry : m_picker.listing.entries)
	{
		const std::string name = importing ? Lower(entry.name) : "";
		const std::string ext = name.size() >= 4 ? name.substr(name.size() - 4) : "";
		const bool archive = ext == ".zip" || ext == ".rar";
		if (importing && !entry.directory && !archive)
			continue;
		PickerRowData row;
		row.id = "entry:" + entry.path;
		row.label = entry.name;
		row.path = entry.path;
		row.directory = entry.directory;
		row.image = importing ? !entry.directory && archive : entry.image;
		row.disabled = !entry.readable || (!entry.directory && !row.image);
		if (!entry.readable)
			row.detail = "Not readable";
		else if (entry.directory)
			row.detail = entry.compatible_count ? std::to_string(entry.compatible_count) + " compatible" : "Folder";
		else if (row.image)
			row.detail = importing ? "Texture archive" : "Compatible image";
		else
			row.detail = "File";
		m_picker.rows.push_back(row);
	}
	m_picker.row_index = ClampIndex(m_picker.row_index, static_cast<int>(m_picker.rows.size()));
}

void UiModel::BrowsePickerPath(const std::string& path, const std::string& focus_path)
{
	const std::string key = m_picker.at_root ? kRootPathKey : m_picker.current_path;
	if (!key.empty() || m_picker.at_root)
		m_picker.focus_by_path[key] = m_picker.row_index;

	m_picker.current_path = path;
	m_picker.at_root = false;
	m_picker.listing = FolderListing();
	if (m_backend)
		m_picker.listing = m_backend->BrowseFolder(path, m_picker.filter, 0, 128);
	else
		m_picker.listing.error = "No backend connected";
	if (m_picker.listing.path.empty())
		m_picker.listing.path = path;
	RebuildPickerRows();

	int focus = 0;
	if (!focus_path.empty())
	{
		for (size_t i = 0; i < m_picker.rows.size(); i++)
			if (m_picker.rows[i].path == focus_path)
			{
				focus = static_cast<int>(i);
				break;
			}
	}
	else if (const auto it = m_picker.focus_by_path.find(path); it != m_picker.focus_by_path.end())
		focus = it->second;
	m_picker.row_index = ClampIndex(focus, static_cast<int>(m_picker.rows.size()));
}

void UiModel::RefreshPicker()
{
	const int focus = m_picker.row_index;
	if (m_picker.at_root)
		RebuildPickerRows();
	else
	{
		BrowsePickerPath(m_picker.current_path);
		m_picker.row_index = ClampIndex(focus, static_cast<int>(m_picker.rows.size()));
	}
}

void UiModel::HandleFolderPicker(const LogicalInput& in)
{
	if (in.triangle)
	{
		OpenKeyboard(KeyboardTarget::FolderFilter, "Filter folder", m_picker.filter);
		return;
	}
	if (in.circle)
	{
		if (m_picker.at_root)
		{
			ClosePicker(true);
			return;
		}
		const std::string child = m_picker.current_path;
		const std::string parent = m_picker.listing.parent;
		if (parent.empty())
		{
			m_picker.at_root = true;
			m_picker.current_path.clear();
			RebuildPickerRows();
			for (size_t i = 0; i < m_picker.rows.size(); i++)
				if (m_picker.rows[i].path == child)
				{
					m_picker.row_index = static_cast<int>(i);
					break;
				}
		}
		else
			BrowsePickerPath(parent, child);
		return;
	}
	const int count = static_cast<int>(m_picker.rows.size());
	if (in.up)
		m_picker.row_index = MoveClamped(m_picker.row_index, -1, count);
	if (in.down)
		m_picker.row_index = MoveClamped(m_picker.row_index, 1, count);
	if (in.square && count > 0)
	{
		const PickerRowData& row = m_picker.rows[static_cast<size_t>(ClampIndex(m_picker.row_index, count))];
		if (row.id.rfind("source:", 0) == 0)
		{
			OpenDialog(PendingAction::RemoveLibraryFolder, "Remove saved folder?",
				"Only the library link is removed. Game images stay where they are.", "Remove link", true,
				row.id.substr(7));
			return;
		}
		if (!row.detail.empty())
			SetToast(row.detail);
	}
	if (in.cross)
		ActivatePickerRow();
}

void UiModel::ActivatePickerRow()
{
	if (m_picker.rows.empty())
		return;
	const PickerRowData row = m_picker.rows[static_cast<size_t>(ClampIndex(m_picker.row_index,
		static_cast<int>(m_picker.rows.size())))];
	if (row.disabled)
	{
		SetToast(row.detail.empty() ? "Unavailable" : row.detail);
		return;
	}
	if (row.use_current)
	{
		FinishPicker(m_picker.current_path, false);
		return;
	}
	if (row.parent)
	{
		const std::string child = m_picker.current_path;
		if (row.path.empty())
		{
			m_picker.at_root = true;
			m_picker.current_path.clear();
			RebuildPickerRows();
		}
		else
			BrowsePickerPath(row.path, child);
		return;
	}
	if (row.directory)
	{
		BrowsePickerPath(row.path);
		return;
	}
	if (row.image)
	{
		if (m_picker.purpose == PickerPurpose::ChangeDisc || m_picker.purpose == PickerPurpose::ImportTexturePack)
			FinishPicker(row.path, true);
		else
			SetToast("Use the folder containing this image");
	}
}

void UiModel::FinishPicker(const std::string& selected_path, bool image_file)
{
	switch (m_picker.purpose)
	{
		case PickerPurpose::AddLibraryFolder:
			if (m_picker.return_screen == Screen::Library)
			{
				m_filter_index = 0;
				m_library_zone = 1;
				m_empty_index = 0;
				m_grid_index = 0;
				m_library_focus_game_id.clear();
				m_library_search.clear();
			}
			m_pending_scan_added_path = selected_path;
			Post(CommandType::AddLibraryFolder, "", selected_path);
			break;
		case PickerPurpose::BiosFolder:
			Post(CommandType::SetBiosFolder, "", selected_path);
			break;
		case PickerPurpose::RelocateLibraryFolder:
			Post(CommandType::RelocateLibraryFolder, m_picker.source_id, selected_path);
			break;
		case PickerPurpose::ChangeDisc:
			Post(CommandType::ChangeDisc, "", selected_path);
			break;
		case PickerPurpose::ImportTexturePack:
			if (image_file)
				Post(CommandType::ImportTexturePack, m_detail_game_id, selected_path);
			break;
	}
	ClosePicker(true);
}

void UiModel::OpenDialog(PendingAction action, const std::string& title, const std::string& body,
	const std::string& confirm_label, bool destructive, const std::string& id, const std::string& path,
	const std::string& value, int number)
{
	m_dialog = DialogState();
	m_dialog.open = true;
	m_dialog.destructive = destructive;
	m_dialog.title = title;
	m_dialog.body = body;
	m_dialog.confirm_label = confirm_label;
	m_dialog.action = action;
	m_dialog.id = id;
	m_dialog.path = path;
	m_dialog.value = value;
	m_dialog.number = number;
	m_dialog.option = 0; // Cancel is always the initial focus, most importantly for destructive actions.
}

void UiModel::HandleDialog(const LogicalInput& in)
{
	if (in.left || in.right || in.up || in.down)
		m_dialog.option = m_dialog.option == 0 ? 1 : 0;
	if (in.circle)
	{
		m_dialog = DialogState();
		return;
	}
	if (!in.cross)
		return;
	const DialogState closing = m_dialog;
	m_dialog = DialogState();
	if (closing.option == 1)
		ExecuteDialogAction(closing);
}

void UiModel::ExecuteDialogAction(const DialogState& dialog)
{
	switch (dialog.action)
	{
		case PendingAction::ResetControlBindings:
			Post(CommandType::ResetControlBindings);
			break;
		case PendingAction::ResetSetting:
			Post(CommandType::ResetSetting, dialog.id);
			break;
		case PendingAction::ReturnToLibrary:
			Post(CommandType::ReturnToLibrary);
			break;
		case PendingAction::RemoveLibraryFolder:
			Post(CommandType::RemoveLibraryFolder, dialog.id);
			break;
		case PendingAction::ApplyRecommendedSettings:
			if (dialog.id.find("recommended") != std::string::npos)
				Post(CommandType::ApplyRecommendedSettings);
			else
				Post(CommandType::SetSetting, dialog.id, dialog.path, "activate", dialog.number);
			break;
		case PendingAction::DownloadTexturePack:
			Post(CommandType::DownloadTexturePack, dialog.id, "", dialog.value);
			break;
		case PendingAction::CancelTexturePack:
			Post(CommandType::CancelTexturePack, dialog.id);
			break;
		case PendingAction::DeleteTexturePack:
			Post(CommandType::DeleteTexturePack, dialog.id);
			break;
		case PendingAction::None:
			break;
	}
}

static const std::vector<std::vector<std::pair<std::string, std::string>>>& KeyboardLayout()
{
	static const std::vector<std::vector<std::pair<std::string, std::string>>> rows = {
		{{"a", "a"}, {"b", "b"}, {"c", "c"}, {"d", "d"}, {"e", "e"}, {"f", "f"}, {"g", "g"}, {"h", "h"}, {"i", "i"}, {"j", "j"}},
		{{"k", "k"}, {"l", "l"}, {"m", "m"}, {"n", "n"}, {"o", "o"}, {"p", "p"}, {"q", "q"}, {"r", "r"}, {"s", "s"}, {"t", "t"}},
		{{"u", "u"}, {"v", "v"}, {"w", "w"}, {"x", "x"}, {"y", "y"}, {"z", "z"}, {".", "."}, {"-", "-"}, {"_", "_"}, {"'", "'"}},
		{{"0", "0"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"5", "5"}, {"6", "6"}, {"7", "7"}, {"8", "8"}, {"9", "9"}},
		{{"space", "Space"}, {"back", "Back"}, {"clear", "Clear"}, {"done", "Done"}},
	};
	return rows;
}

void UiModel::OpenKeyboard(KeyboardTarget target, const std::string& title, const std::string& initial,
	const std::string& setting_id)
{
	m_keyboard = KeyboardState();
	m_keyboard.open = true;
	m_keyboard.target = target;
	m_keyboard.title = title;
	m_keyboard.text = initial;
	m_keyboard.setting_id = setting_id;
}

void UiModel::HandleKeyboard(const LogicalInput& in)
{
	const auto& layout = KeyboardLayout();
	const int rows = static_cast<int>(layout.size());
	int& row = m_keyboard.row;
	int& col = m_keyboard.col;
	row = ClampIndex(row, rows);
	col = ClampIndex(col, static_cast<int>(layout[static_cast<size_t>(row)].size()));
	if (in.left)
		col = MoveClamped(col, -1, static_cast<int>(layout[static_cast<size_t>(row)].size()));
	if (in.right)
		col = MoveClamped(col, 1, static_cast<int>(layout[static_cast<size_t>(row)].size()));
	if (in.up)
	{
		row = MoveClamped(row, -1, rows);
		col = ClampIndex(col, static_cast<int>(layout[static_cast<size_t>(row)].size()));
	}
	if (in.down)
	{
		row = MoveClamped(row, 1, rows);
		col = ClampIndex(col, static_cast<int>(layout[static_cast<size_t>(row)].size()));
	}
	if (in.circle)
	{
		CloseKeyboard(false);
		return;
	}
	if (in.triangle)
	{
		CloseKeyboard(true);
		return;
	}
	if (!in.cross)
		return;
	const std::string& key = layout[static_cast<size_t>(row)][static_cast<size_t>(col)].first;
	if (key.size() == 1)
		m_keyboard.text += key;
	else if (key == "space")
		m_keyboard.text += ' ';
	else if (key == "back")
	{
		if (!m_keyboard.text.empty())
			m_keyboard.text.pop_back();
	}
	else if (key == "clear")
		m_keyboard.text.clear();
	else if (key == "done")
		CloseKeyboard(true);
}

void UiModel::CloseKeyboard(bool apply)
{
	const KeyboardState closing = m_keyboard;
	m_keyboard = KeyboardState();
	if (!apply)
		return;
	if (closing.target == KeyboardTarget::LibrarySearch)
	{
		m_library_search = closing.text;
		m_library_focus_game_id.clear();
		m_grid_index = 0;
		SyncLibraryFocus();
	}
	else if (closing.target == KeyboardTarget::FolderFilter)
	{
		m_picker.filter = closing.text;
		RefreshPicker();
	}
	else if (closing.target == KeyboardTarget::SettingText)
	{
		Post(CommandType::SetSetting, closing.setting_id, "", closing.text);
	}
}

static UiEntry EntryFromAction(const NamedAction& action, bool focused)
{
	UiEntry entry;
	entry.id = action.id;
	entry.label = action.label;
	entry.value = action.value;
	entry.detail = action.detail;
	entry.icon = action.icon;
	entry.focused = focused;
	entry.disabled = action.disabled;
	entry.destructive = action.destructive;
	entry.progress = action.progress;
	return entry;
}

static const char* ScreenName(Screen screen)
{
	switch (screen)
	{
		case Screen::Boot: return "boot";
		case Screen::FirstRun: return "first-run";
		case Screen::Home: return "home";
		case Screen::Library: return "library";
		case Screen::GameDetail: return "game-detail";
		case Screen::TexturePacks: return "texture-packs";
		case Screen::PauseOverlay: return "pause";
		case Screen::MemoryCards: return "memory-cards";
		case Screen::Settings: return "settings";
		case Screen::Controls: return "controls";
		case Screen::Accessibility: return "accessibility";
		case Screen::Diagnostics: return "diagnostics";
		case Screen::FolderPicker: return "folder-picker";
	}
	return "unknown";
}

UiView UiModel::BuildView() const
{
	UiView view;
	view.screen = m_screen;
	view.screen_name = ScreenName(m_screen);
	view.accessibility = m_snapshot.accessibility;
	view.scan = m_snapshot.scan;
	view.bios = m_snapshot.bios;
	view.vm = m_snapshot.vm;
	view.revision = m_snapshot.revision;
	view.reduce_motion = m_snapshot.accessibility.reduce_motion;
	view.high_contrast = m_snapshot.accessibility.high_contrast;
	view.toast = m_toast_seconds > 0.0 ? m_toast : std::string();
	view.status = ScanStateText(m_snapshot.scan);

	auto prompts = [&](std::initializer_list<UiPrompt> list) { view.prompts.assign(list.begin(), list.end()); };

	switch (m_screen)
	{
		case Screen::Boot:
		{
			view.title = m_snapshot.product.empty() ? "Caliza" : m_snapshot.product;
			if (m_snapshot.bios.checking)
				view.status = "Checking for a PS2 BIOS…";
			view.focus_id = "boot";
			prompts({{PromptGlyph::Cross, "Skip"}});
			break;
		}
		case Screen::FirstRun:
		{
			view.title = "Set up Caliza";
			view.subtitle = "Use a backup from your own PS2. Caliza never includes BIOS data.";
			UiEntry bios;
			bios.id = "step-bios";
			bios.label = "1  PS2 BIOS";
			bios.detail = m_snapshot.bios.present ? "Found" :
				m_snapshot.bios.problem.empty() ? "Required" : m_snapshot.bios.problem;
			bios.selected = m_snapshot.bios.present;
			bios.icon = m_snapshot.bios.present ? EntryIcon::Action : EntryIcon::Warning;
			UiEntry folder;
			folder.id = "step-folder";
			folder.label = "2  Game folder";
			folder.detail = m_snapshot.sources.empty() ? "Choose where games live" :
				std::to_string(m_snapshot.sources.size()) + " folder" + (m_snapshot.sources.size() == 1 ? "" : "s");
			folder.selected = !m_snapshot.sources.empty();
			folder.icon = EntryIcon::Folder;
			view.rail = {bios, folder};
			const std::vector<NamedAction> actions = FirstRunActions(m_snapshot);
			for (size_t i = 0; i < actions.size(); i++)
			{
				view.items.push_back(EntryFromAction(actions[i], static_cast<int>(i) == m_first_run_index));
				if (static_cast<int>(i) == m_first_run_index)
					view.focus_id = actions[i].id;
			}
			if (!m_snapshot.bios.problem.empty())
				view.status = m_snapshot.bios.problem;
			prompts({{PromptGlyph::Cross, "Select"}, {PromptGlyph::Circle, "Back"}});
			break;
		}
		case Screen::Home:
		{
			view.title = "PS2 emulator for PS4";
			view.subtitle = m_snapshot.build_tag;
			const std::vector<HomeAction> actions = HomeActions(m_snapshot);
			for (size_t i = 0; i < actions.size(); i++)
			{
				UiEntry entry;
				entry.id = actions[i].id;
				entry.label = actions[i].label;
				entry.icon = actions[i].icon;
				entry.focused = static_cast<int>(i) == m_home_index;
				if (entry.focused)
					view.focus_id = entry.id;
				if (entry.id == "library")
				{
					const size_t count = m_snapshot.games.size();
					entry.detail = count == 0 ? "Add a folder to find your games" :
						std::to_string(count) + (count == 1 ? " game" : " games");
				}
				else if (entry.id == "memory")
					entry.detail = "Create cards and choose what goes in each slot";
				else if (entry.id == "settings")
					entry.detail = "Display, graphics, performance and folders";
				else if (entry.id == "controls")
					entry.detail = "Save, load and fast-forward shortcuts";
				else if (entry.id == "accessibility")
					entry.detail = "Text size, contrast, motion and focus";
				else if (entry.id == "diagnostics")
					entry.detail = "System status and the companion panel";
				else if (entry.id == "system-menu")
					entry.detail = m_snapshot.bios.present ? "Start the PS2 without a disc" : "Needs a PS2 BIOS first";
				if (entry.id == "continue")
				{
					const GameEntry* game = FindGame(m_snapshot.continue_game_id);
					entry.detail = game && game->last_played ? "Resume your last session" : "Ready";
					if (game)
					{
						entry.label = "Continue";
						entry.detail = (game->title.empty() ? game->file : game->title) + "  ·  " + entry.detail;
						view.hero_cover_key = game->cover_path.empty() ? "proc:" + game->id : game->cover_path;
						view.hero_title = game->title.empty() ? game->file : game->title;
					}
				}
				view.items.push_back(entry);
			}
			view.prompts.push_back({PromptGlyph::Cross, "Select"});
			if (!m_snapshot.continue_game_id.empty() && m_snapshot.continue_valid)
				view.prompts.push_back({PromptGlyph::Square, "Details"});
			break;
		}
		case Screen::Library:
		{
			view.title = "Library";
			const std::vector<FilterOption> filters = Filters();
			for (size_t i = 0; i < filters.size(); i++)
			{
				UiEntry entry;
				entry.id = filters[i].id;
				entry.label = filters[i].label;
				entry.icon = filters[i].kind == FilterKind::AddFolder ? EntryIcon::Folder : EntryIcon::Library;
				entry.selected = static_cast<int>(i) == m_filter_index;
				entry.focused = m_library_zone == 0 && static_cast<int>(i) == m_filter_index;
				if (entry.focused)
					view.focus_id = "filter:" + entry.id;
				view.rail.push_back(entry);
			}
			const std::vector<const GameEntry*> games = VisibleGames();
			for (size_t i = 0; i < games.size(); i++)
			{
				const GameEntry& game = *games[i];
				UiTile tile;
				tile.id = game.id;
				tile.title = game.title.empty() ? game.file : game.title;
				tile.subtitle = game.serial.empty() ? game.region : game.serial;
				tile.cover_key = game.cover_path.empty() ? "proc:" + game.id : game.cover_path;
				tile.badges = game.badges;
				tile.disabled = !GameLaunchable(game);
				tile.damaged = game.damaged;
				tile.hidden = game.hidden;
				tile.focused = m_library_zone == 1 && static_cast<int>(i) == m_grid_index;
				if (tile.focused)
					view.focus_id = "game:" + tile.id;
				view.tiles.push_back(tile);
			}
			if (games.empty())
			{
				const std::vector<NamedAction> actions = EmptyLibraryActions(m_library_search);
				for (size_t i = 0; i < actions.size(); i++)
				{
					view.items.push_back(EntryFromAction(actions[i], m_library_zone == 1 && static_cast<int>(i) == m_empty_index));
					if (view.items.back().focused)
						view.focus_id = "empty:" + actions[i].id;
				}
				if (m_library_zone == 1 && actions.empty())
					view.focus_id = "library-empty";
			}
			view.subtitle = std::to_string(games.size()) + (games.size() == 1 ? " game" : " games");
			if (!m_library_search.empty())
				view.subtitle += " · search “" + m_library_search + "”";
			if (games.empty() && m_snapshot.games.empty() && view.status.empty())
				view.status = "No games yet. Add a folder without moving or copying your images.";
			prompts({{PromptGlyph::Cross, games.empty() ? "Select" : "Play"}, {PromptGlyph::Square, "Details"},
				{PromptGlyph::Triangle, "Search"}, {PromptGlyph::Circle, "Back"}});
			break;
		}
		case Screen::GameDetail:
		{
			const GameEntry* game = FindGame(m_detail_game_id);
			view.title = game ? (game->title.empty() ? game->file : game->title) : "Game";
			if (game)
			{
				std::ostringstream sub;
				sub << (game->region.empty() ? "PS2" : game->region);
				if (!game->serial.empty())
					sub << " · " << game->serial;
				if (game->bytes)
					sub << " · " << FormatBytes(game->bytes);
				view.subtitle = sub.str();
				if (!game->problem.empty())
					view.status = game->problem;
				view.hero_cover_key = game->cover_path.empty() ? "proc:" + game->id : game->cover_path;
				view.hero_title = game->title.empty() ? game->file : game->title;
			}
			const std::vector<NamedAction> actions = DetailActions(game, m_snapshot.bios, m_snapshot.operations);
			for (size_t i = 0; i < actions.size(); i++)
			{
				view.items.push_back(EntryFromAction(actions[i], static_cast<int>(i) == m_detail_index));
				if (static_cast<int>(i) == m_detail_index)
					view.focus_id = "detail:" + actions[i].id;
			}
			prompts({{PromptGlyph::Cross, "Select"}, {PromptGlyph::Circle, "Back"}});
			break;
		}
		case Screen::TexturePacks:
		{
			view.title = "HD texture packs";
			const GameEntry* game = FindGame(m_detail_game_id);
			if (game)
				view.subtitle = game->title + (game->serial.empty() ? "" : " · " + game->serial);
			if (!game || game->serial.empty())
				view.status = "This game has no recognized serial";
			else if (m_snapshot.selected_game_id != game->id)
				view.status = "Loading packs";
			else
			{
				using Catalog = TexturePacks::Catalog;
				const auto& packs = m_snapshot.texture_packs;
				if (packs.catalog == Catalog::Loading)
					view.status = "Loading catalog";
				else if (packs.catalog == Catalog::Ready && packs.entries.empty())
					view.status = "No catalog packs for this game";
				const auto rows = TextureActions(packs, true);
				const int focus = ClampIndex(m_texture_index, static_cast<int>(rows.size()));
				for (size_t i = 0; i < rows.size(); i++)
				{
					const bool focused = static_cast<int>(i) == focus;
					view.items.push_back(EntryFromAction(rows[i], focused));
					if (focused)
						view.focus_id = "texture:" + rows[i].id;
				}
			}
			prompts({{PromptGlyph::Cross, "Select"}, {PromptGlyph::Circle, "Back"}});
			break;
		}
		case Screen::PauseOverlay:
		{
			view.title = "Paused";
			view.subtitle = m_snapshot.vm.title.empty() ? VmStateText(m_snapshot.vm.state) : m_snapshot.vm.title;
			if (!m_snapshot.vm.game_id.empty())
			{
				view.hero_cover_key = "proc:" + m_snapshot.vm.game_id;
				view.hero_title = m_snapshot.vm.title;
			}
			const std::vector<NamedAction> actions = PauseActions(m_snapshot.vm);
			for (size_t i = 0; i < actions.size(); i++)
			{
				view.items.push_back(EntryFromAction(actions[i], static_cast<int>(i) == m_pause_index));
				if (static_cast<int>(i) == m_pause_index)
					view.focus_id = "pause:" + actions[i].id;
			}
			if (!m_snapshot.vm.message.empty())
				view.status = m_snapshot.vm.message;
			prompts({{PromptGlyph::Cross, "Select"}, {PromptGlyph::Circle, "Resume"}});
			break;
		}
		case Screen::MemoryCards:
		{
			view.title = "Memory cards";
			for (int slot = 0; slot < 2; slot++)
			{
				UiEntry entry;
				entry.id = "slot:" + std::to_string(slot);
				entry.label = "Slot " + std::to_string(slot + 1);
				entry.icon = EntryIcon::Slot;
				entry.selected = slot == m_memory_slot;
				entry.focused = m_memory_zone == 0 && slot == m_memory_slot;
				bool found = false;
				for (const MemoryCardInfo& card : m_snapshot.memory_cards)
					if (card.slot == slot && card.present)
					{
						found = true;
						entry.value = card.file;
						entry.detail = card.enabled ? "Enabled" : "Disabled";
						break;
					}
				if (!found)
					entry.detail = "Empty";
				if (entry.focused)
					view.focus_id = entry.id;
				view.rail.push_back(entry);
			}
			const std::vector<NamedAction> actions = MemoryActionsForSlot(m_snapshot, m_memory_slot);
			for (size_t i = 0; i < actions.size(); i++)
			{
				view.items.push_back(EntryFromAction(actions[i], m_memory_zone == 1 && static_cast<int>(i) == m_memory_index));
				if (view.items.back().focused)
					view.focus_id = "memory:" + actions[i].id;
			}
			view.subtitle = "Choose a slot, then assign or create a card";
			prompts({{PromptGlyph::Cross, "Assign"}, {PromptGlyph::Circle, "Back"}});
			break;
		}
		case Screen::Settings:
		{
			view.title = "Settings";
			for (size_t i = 0; i < m_snapshot.settings.size(); i++)
			{
				const SettingCategory& category = m_snapshot.settings[i];
				UiEntry entry;
				entry.id = category.id;
				entry.label = category.label;
				entry.icon = EntryIcon::Settings;
				entry.selected = static_cast<int>(i) == m_settings_category;
				entry.focused = m_settings_zone == 0 && entry.selected;
				if (entry.focused)
					view.focus_id = "setting-category:" + entry.id;
				view.rail.push_back(entry);
			}
			if (!m_snapshot.settings.empty())
			{
				const SettingCategory& category = m_snapshot.settings[static_cast<size_t>(ClampIndex(m_settings_category,
					static_cast<int>(m_snapshot.settings.size())))];
				int item_index = 0;
				if (const auto it = m_settings_item_by_category.find(category.id); it != m_settings_item_by_category.end())
					item_index = it->second;
				for (size_t i = 0; i < category.items.size(); i++)
				{
					const SettingItem& setting = category.items[i];
					UiEntry entry;
					entry.id = setting.id;
					entry.label = setting.label;
					entry.value = setting.value;
					for (size_t c = 0; c < setting.choices.size(); c++)
					{
						entry.options.push_back(setting.choices[c].label);
						if (setting.choices[c].value == setting.value)
						{
							entry.value = setting.choices[c].label;
							entry.option_index = static_cast<int>(c);
						}
					}
					entry.detail = setting.description;
					entry.needs_restart = setting.restart_required;
					entry.game_scoped = setting.game_scoped;
					entry.modified = setting.modified;
					entry.destructive = setting.destructive;
					entry.focused = m_settings_zone == 1 && static_cast<int>(i) == item_index;
					if (entry.focused)
						view.focus_id = "setting:" + entry.id;
					view.items.push_back(entry);
				}
			}
			else
				view.status = "No settings are exposed by the backend yet";
			if (m_settings_zone == 1)
				prompts({{PromptGlyph::DpadLeftRight, "Change"}, {PromptGlyph::Square, "Reset"}, {PromptGlyph::Circle, "Back"}});
			else
				prompts({{PromptGlyph::Cross, "Open"}, {PromptGlyph::Circle, "Back"}});
			break;
		}
		case Screen::Controls:
		{
			view.title = "Controls";
			if (m_controls_capture)
				view.subtitle = "Press the new button. Options cancels.";
			for (size_t i = 0; i < m_snapshot.controls.size(); i++)
			{
				const ControlBinding& binding = m_snapshot.controls[i];
				UiEntry entry;
				entry.id = binding.action;
				entry.label = binding.label.empty() ? binding.action : binding.label;
				entry.value = binding.button;
				entry.disabled = !binding.editable;
				entry.detail = binding.conflict ? "Conflicts with another action" : "";
				entry.focused = static_cast<int>(i) == m_controls_index;
				if (entry.focused)
					view.focus_id = "control:" + entry.id;
				view.items.push_back(entry);
			}
			prompts({{PromptGlyph::Cross, "Remap"}, {PromptGlyph::Square, "Reset"}, {PromptGlyph::Circle, "Back"}});
			break;
		}
		case Screen::Accessibility:
		{
			view.title = "Accessibility";
			view.subtitle = "Changes apply right away";
			const std::vector<NamedAction> actions = AccessibilityActions(m_snapshot.accessibility);
			for (size_t i = 0; i < actions.size(); i++)
			{
				view.items.push_back(EntryFromAction(actions[i], static_cast<int>(i) == m_accessibility_index));
				if (static_cast<int>(i) == m_accessibility_index)
					view.focus_id = "accessibility:" + actions[i].id;
			}
			prompts({{PromptGlyph::Cross, "Change"}, {PromptGlyph::Circle, "Back"}});
			break;
		}
		case Screen::Diagnostics:
		{
			view.title = "Diagnostics";
			UiEntry report;
			report.id = "report";
			report.label = "Generate report";
			report.value = "Web panel";
			report.detail = "Reports are collected from the companion panel on another device";
			report.disabled = true;
			report.icon = EntryIcon::Diagnostics;
			report.focused = true;
			view.focus_id = "diagnostics:report";
			view.items.push_back(report);
			auto info = [&](const std::string& id, const std::string& label, const std::string& value) {
				UiEntry entry;
				entry.id = id;
				entry.label = label;
				entry.value = value;
				entry.disabled = true;
				view.items.push_back(entry);
			};
			info("build", "Build ID", m_snapshot.diagnostics.build_id.empty() ? m_snapshot.build_tag : m_snapshot.diagnostics.build_id);
			info("console", "Console status", m_snapshot.diagnostics.console_status.empty() ? "Unknown" : m_snapshot.diagnostics.console_status);
			info("panel", "Web panel", m_snapshot.diagnostics.web_panel_connected ? "Connected" : "Not connected");
			view.subtitle = m_snapshot.diagnostics.web_panel_url;
			prompts({{PromptGlyph::Cross, "Open"}, {PromptGlyph::Circle, "Back"}});
			break;
		}
		case Screen::FolderPicker:
		{
			view.title = m_picker.purpose == PickerPurpose::BiosFolder ? "Choose BIOS folder" :
				m_picker.purpose == PickerPurpose::ChangeDisc ? "Change disc" :
				m_picker.purpose == PickerPurpose::ImportTexturePack ? "Import texture pack" : "Choose game folder";
			if (!m_picker.at_root)
				view.subtitle = JoinBreadcrumbs(m_picker.listing.breadcrumbs);
			view.picker.purpose = m_picker.purpose;
			view.picker.title = view.title;
			view.picker.current_path = m_picker.current_path;
			view.picker.filter = m_picker.filter;
			view.picker.breadcrumbs = m_picker.listing.breadcrumbs;
			view.picker.at_root = m_picker.at_root;
			for (size_t i = 0; i < m_picker.rows.size(); i++)
			{
				const PickerRowData& row = m_picker.rows[i];
				UiPickerRow out;
				out.id = row.id;
				out.label = row.label;
				out.detail = row.detail;
				out.path = row.path;
				out.directory = row.directory;
				out.image = row.image;
				out.use_current = row.use_current;
				out.parent = row.parent;
				out.disabled = row.disabled;
				out.focused = static_cast<int>(i) == m_picker.row_index;
				if (out.focused)
					view.focus_id = "picker:" + row.id;
				view.picker.rows.push_back(out);
			}
			if (!m_picker.listing.error.empty())
				view.picker.status = m_picker.listing.error;
			else if (!m_picker.at_root && (m_picker.listing.entries.empty() || (m_picker.purpose == PickerPurpose::ImportTexturePack && m_picker.rows.size() == 1)))
				view.picker.status = m_picker.purpose == PickerPurpose::ImportTexturePack ? "No folders or ZIP/RAR archives here" :
					"No readable folders or compatible images here";
			else if (m_picker.listing.truncated)
				view.picker.status = "Showing the first entries; use the filter to narrow this folder";
			prompts({{PromptGlyph::Cross, "Open"}, {PromptGlyph::Triangle, "Filter"}, {PromptGlyph::Square, "Actions"},
				{PromptGlyph::Circle, "Back"}});
			break;
		}
	}

	if (m_keyboard.open)
	{
		view.keyboard.open = true;
		view.keyboard.title = m_keyboard.title;
		view.keyboard.text = m_keyboard.text;
		view.keyboard.focus_row = m_keyboard.row;
		view.keyboard.focus_col = m_keyboard.col;
		const auto& layout = KeyboardLayout();
		for (size_t r = 0; r < layout.size(); r++)
		{
			std::vector<UiKeyboardKey> row;
			for (size_t c = 0; c < layout[r].size(); c++)
			{
				UiKeyboardKey key;
				key.id = layout[r][c].first;
				key.label = layout[r][c].second;
				key.focused = static_cast<int>(r) == m_keyboard.row && static_cast<int>(c) == m_keyboard.col;
				row.push_back(key);
			}
			view.keyboard.rows.push_back(row);
		}
		view.prompts = {{PromptGlyph::Cross, "Type"}, {PromptGlyph::Triangle, "Done"}, {PromptGlyph::Circle, "Cancel"}};
	}

	if (m_dialog.open)
	{
		view.dialog.open = true;
		view.dialog.destructive = m_dialog.destructive;
		view.dialog.title = m_dialog.title;
		view.dialog.body = m_dialog.body;
		view.dialog.cancel_label = m_dialog.cancel_label;
		view.dialog.confirm_label = m_dialog.confirm_label;
		view.dialog.focused_option = m_dialog.option;
		view.focus_id = m_dialog.option == 0 ? "dialog:cancel" : "dialog:confirm";
		view.prompts = {{PromptGlyph::Cross, "Select"}, {PromptGlyph::Circle, "Cancel"}};
	}
	return view;
}

std::string UiModel::FocusIdForTest() const
{
	return BuildView().focus_id;
}
} // namespace caliza
