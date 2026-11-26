// Caliza Estratos: deterministic controller-first UI model.
// Snapshots and command events in; navigation state, view data and backend commands out.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "contract.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace caliza
{
enum class Screen
{
	Boot,
	FirstRun,
	Home,
	Library,
	GameDetail,
	TexturePacks,
	PauseOverlay,
	MemoryCards,
	Settings,
	Controls,
	Accessibility,
	Diagnostics,
	FolderPicker,
};

enum class PickerPurpose
{
	AddLibraryFolder,
	BiosFolder,
	RelocateLibraryFolder,
	ChangeDisc,
	ImportTexturePack,
};

enum class PromptGlyph
{
	Cross,
	Circle,
	Square,
	Triangle,
	Options,
	Dpad,
	DpadLeftRight,
};

enum class EntryIcon
{
	None,
	Continue,
	Library,
	Settings,
	MemoryCard,
	Controls,
	Accessibility,
	Diagnostics,
	Folder,
	Game,
	Action,
	Warning,
	Slot,
};

struct UiPrompt
{
	PromptGlyph glyph = PromptGlyph::Cross;
	std::string label;
};

struct UiEntry
{
	std::string id;
	std::string label;
	std::string value;
	std::string detail;
	EntryIcon icon = EntryIcon::None;
	bool focused = false;
	bool selected = false;
	bool disabled = false;
	bool destructive = false;
	bool modified = false;
	float progress = -1.0f; // 0..1 when nonnegative
	// Settings rows: the choice labels in order and the current one, for the explanation panel.
	std::vector<std::string> options;
	int option_index = -1;
	bool needs_restart = false;
	bool game_scoped = false;
};

struct UiTile
{
	std::string id;
	std::string title;
	std::string subtitle;
	std::string cover_key;
	std::vector<std::string> badges;
	bool focused = false;
	bool disabled = false;
	bool damaged = false;
	bool hidden = false;
};

struct UiPickerRow
{
	std::string id;
	std::string label;
	std::string detail;
	std::string path;
	bool focused = false;
	bool disabled = false;
	bool directory = false;
	bool image = false;
	bool use_current = false;
	bool parent = false;
};

struct UiPickerView
{
	PickerPurpose purpose = PickerPurpose::AddLibraryFolder;
	std::string title;
	std::string current_path;
	std::string status;
	std::string filter;
	std::vector<std::string> breadcrumbs;
	std::vector<UiPickerRow> rows;
	bool at_root = true;
};

struct UiDialogView
{
	bool open = false;
	bool destructive = false;
	std::string title;
	std::string body;
	std::string cancel_label;
	std::string confirm_label;
	int focused_option = 0; // 0 cancel, 1 confirm; destructive dialogs always start on cancel
};

struct UiKeyboardKey
{
	std::string id;
	std::string label;
	bool focused = false;
};

struct UiKeyboardView
{
	bool open = false;
	std::string title;
	std::string text;
	std::vector<std::vector<UiKeyboardKey>> rows;
	int focus_row = 0;
	int focus_col = 0;
};

struct UiView
{
	Screen screen = Screen::Boot;
	std::string screen_name;
	std::string title;
	std::string subtitle;
	std::string status;
	std::string toast;
	std::string hero_cover_key;
	std::string hero_title;
	std::string focus_id;
	std::vector<UiEntry> rail;
	std::vector<UiEntry> items;
	std::vector<UiTile> tiles;
	UiPickerView picker;
	UiDialogView dialog;
	UiKeyboardView keyboard;
	std::vector<UiPrompt> prompts;
	AccessibilityState accessibility;
	ScanProgress scan;
	BiosStatus bios;
	VmStatus vm;
	Revision revision = 0;
	bool reduce_motion = false;
	bool high_contrast = false;
};

// The model is deterministic: identical snapshot/event/input/dt sequences produce identical
// navigation, view data and posted commands. It owns no persistence, scanning, boot or save logic;
// mutations are posted to the Backend contract as soon as the confirming input edge is consumed.
class UiModel
{
public:
	explicit UiModel(Backend* backend = nullptr);

	void SetBackend(Backend* backend) { m_backend = backend; }
	void Tick(const Snapshot& snapshot, const std::vector<CommandEvent>& events, const Input& input, double dt);
	UiView BuildView() const;

	Screen screen() const { return m_screen; }
	const Snapshot& snapshot() const { return m_snapshot; }
	std::string FocusIdForTest() const;
	std::string LibraryFocusGameForTest() const { return m_library_focus_game_id; }

	static constexpr int kLibraryColumns = 4;

public:
	struct LogicalInput
	{
		bool left = false, right = false, up = false, down = false;
		bool cross = false, circle = false, square = false, triangle = false;
		bool options = false, l1 = false, r1 = false, l2 = false, r2 = false;
	};

private:
	struct RepeatState
	{
		bool held = false;
		double held_seconds = 0.0;
		double since_last = 0.0;
	};

	enum class FilterKind
	{
		All,
		Recent,
		Volume,
		Damaged,
		AddFolder,
	};

	struct FilterOption
	{
		std::string id;
		std::string label;
		FilterKind kind = FilterKind::All;
		VolumeKind volume_kind = VolumeKind::Internal;
	};

	enum class PendingAction
	{
		None,
		ResetControlBindings,
		ResetSetting,
		ReturnToLibrary,
		RemoveLibraryFolder,
		DownloadTexturePack,
		CancelTexturePack,
		DeleteTexturePack,
		ApplyRecommendedSettings,
	};

	struct DialogState
	{
		bool open = false;
		bool destructive = false;
		std::string title;
		std::string body;
		std::string cancel_label = "Cancel";
		std::string confirm_label = "Confirm";
		int option = 0;
		PendingAction action = PendingAction::None;
		std::string id;
		std::string path;
		std::string value;
		int number = 0;
	};

	enum class KeyboardTarget
	{
		LibrarySearch,
		FolderFilter,
		SettingText,
	};

	struct KeyboardState
	{
		bool open = false;
		KeyboardTarget target = KeyboardTarget::LibrarySearch;
		std::string title;
		std::string text;
		std::string setting_id;
		int row = 0;
		int col = 0;
	};

	struct PickerRowData
	{
		std::string id;
		std::string label;
		std::string detail;
		std::string path;
		bool disabled = false;
		bool directory = false;
		bool image = false;
		bool use_current = false;
		bool parent = false;
	};

	struct PickerState
	{
		PickerPurpose purpose = PickerPurpose::AddLibraryFolder;
		Screen return_screen = Screen::Home;
		std::string source_id;
		std::string current_path;
		std::string filter;
		FolderListing listing;
		std::vector<PickerRowData> rows;
		int row_index = 0;
		std::map<std::string, int> focus_by_path;
		bool at_root = true;
	};

	LogicalInput EvaluateInput(const Input& input, double dt);
	void Dispatch(const LogicalInput& in);
	void SyncAfterSnapshot();

	void HandleBoot(const LogicalInput& in);
	void HandleFirstRun(const LogicalInput& in);
	void HandleHome(const LogicalInput& in);
	void HandleLibrary(const LogicalInput& in);
	void HandleGameDetail(const LogicalInput& in);
	void HandleTexturePacks(const LogicalInput& in);
	void HandlePause(const LogicalInput& in);
	void HandleMemoryCards(const LogicalInput& in);
	void HandleSettings(const LogicalInput& in);
	void HandleControls(const LogicalInput& in);
	void HandleAccessibility(const LogicalInput& in);
	void HandleDiagnostics(const LogicalInput& in);
	void HandleFolderPicker(const LogicalInput& in);
	void HandleDialog(const LogicalInput& in);
	void HandleKeyboard(const LogicalInput& in);

	void LeaveBoot();
	void OpenHome();
	void OpenLibrary();
	void OpenGameDetail(const std::string& game_id);
	void OpenPause();
	void OpenSettings(Screen return_screen, const std::string& game_id = std::string());
	void OpenPicker(PickerPurpose purpose, Screen return_screen, const std::string& initial_path = std::string(),
		const std::string& source_id = std::string());
	void ClosePicker(bool restore);
	void OpenDialog(PendingAction action, const std::string& title, const std::string& body,
		const std::string& confirm_label, bool destructive, const std::string& id = std::string(),
		const std::string& path = std::string(), const std::string& value = std::string(), int number = 0);
	void ExecuteDialogAction(const DialogState& dialog);
	void OpenKeyboard(KeyboardTarget target, const std::string& title, const std::string& initial,
		const std::string& setting_id = std::string());
	void CloseKeyboard(bool apply);

	void ActivateHome(int index);
	void ActivateLibraryTile();
	void ActivateGameDetail(int index);
	void ActivatePause(int index);
	void ActivateMemoryCard();
	void ActivateSettingsItem(int direction);
	void ActivateAccessibility(int direction);
	void ActivateDiagnostics();
	void ActivatePickerRow();

	void ChangeSetting(const SettingItem& item, int direction);
	void Post(CommandType type, const std::string& id = std::string(), const std::string& path = std::string(),
		const std::string& value = std::string(), int number = 0);
	void SetToast(std::string text);

	std::vector<FilterOption> Filters() const;
	std::vector<const GameEntry*> VisibleGames() const;
	const GameEntry* FindGame(const std::string& id) const;
	bool GameLaunchable(const GameEntry& game) const;
	void SyncLibraryFocus();
	void SyncSettingsFocus();
	void RebuildPickerRows();
	void BrowsePickerPath(const std::string& path, const std::string& focus_path = std::string());
	void FinishPicker(const std::string& selected_path, bool image_file);
	void RefreshPicker();
	int ClampIndex(int index, int count) const;

	static bool HasTag(const GameEntry& game, const char* tag);
	static std::string Lower(std::string text);
	static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle);

	Backend* m_backend = nullptr;
	Snapshot m_snapshot;
	Screen m_screen = Screen::Boot;
	Input m_previous_input;
	RepeatState m_repeat[4]; // left, right, up, down
	double m_boot_seconds = 0.0;
	std::string m_toast;
	Revision m_toast_revision = 0;
	double m_toast_seconds = 0.0;

	int m_first_run_index = 0;
	int m_home_index = 0;
	int m_library_zone = 0; // 0 filters, 1 grid or empty-state actions
	int m_filter_index = 0;
	int m_grid_index = 0;
	int m_empty_index = 0;
	std::string m_library_focus_game_id;
	std::string m_library_search;
	std::string m_pending_scan_added_path;
	std::string m_detail_game_id;
	int m_detail_index = 0;
	int m_texture_index = 0;
	int m_pause_index = 0;
	Screen m_pause_return_screen = Screen::Home;
	int m_memory_zone = 0; // 0 slots, 1 cards/actions
	int m_memory_slot = 0;
	int m_memory_index = 0;
	Screen m_memory_return_screen = Screen::Home;
	Screen m_settings_return_screen = Screen::Home;
	std::string m_settings_game_id;
	int m_settings_zone = 0; // 0 categories, 1 items
	int m_settings_category = 0;
	std::map<std::string, int> m_settings_item_by_category;
	int m_controls_index = 0;
	bool m_controls_capture = false;
	int m_accessibility_index = 0;
	int m_diagnostics_index = 0;
	DialogState m_dialog;
	KeyboardState m_keyboard;
	PickerState m_picker;
};
} // namespace caliza
