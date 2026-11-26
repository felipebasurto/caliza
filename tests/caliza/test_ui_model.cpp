// Focused host tests for Caliza's UI-model boundary: navigation, focus restoration,
// destructive confirmations, folder picking and command posting. Uses a fake Backend;
// no persistence, scanning or VM implementation is tested here.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../caliza/core/ui_model.h"

#include <cstdio>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

using namespace caliza;

namespace
{
int g_failures = 0;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			g_failures++; \
		} \
	} while (0)

struct FakeBackend : Backend
{
	Snapshot snap;
	std::vector<Command> posted;
	std::deque<Command> queue;
	std::vector<CommandEvent> events;
	CommandId next_command = 1;
	std::vector<std::string> browsed;

	Snapshot GetSnapshot() override { return snap; }

	CommandId Post(const Command& command) override
	{
		posted.push_back(command);
		queue.push_back(command);
		return next_command++;
	}

	std::vector<CommandEvent> PollEvents() override
	{
		std::vector<CommandEvent> out = events;
		events.clear();
		return out;
	}

	void Bump()
	{
		snap.revision++;
	}

	void Complete(const Command& command, const std::string& message = "done")
	{
		Bump();
		events.push_back({next_command++, command.type, CommandStatus::Succeeded, snap.revision, message});
	}

	void PumpAll()
	{
		while (!queue.empty())
		{
			Command command = queue.front();
			queue.pop_front();
			switch (command.type)
			{
				case CommandType::AddLibraryFolder:
				{
					LibrarySource source;
					source.id = "source-new";
					source.path = command.path;
					source.label = "Demo USB";
					source.volume_id = "usb0";
					source.state = SourceState::Connected;
					snap.sources.push_back(source);
					Complete(command, "folder added");
					break;
				}
				case CommandType::ScanSource:
					snap.scan.state = ScanState::Complete;
					snap.scan.source_id = command.id;
					Complete(command, "scan complete");
					break;
				case CommandType::SetBiosFolder:
					snap.bios.path = command.path;
					snap.bios.present = true;
					snap.bios.problem.clear();
					Complete(command, "BIOS found");
					break;
				case CommandType::SetAccessibility:
					if (command.id == "text_scale")
						snap.accessibility.text_scale_percent = static_cast<uint32_t>(command.number);
					else if (command.id == "high_contrast")
						snap.accessibility.high_contrast = command.number != 0;
					else if (command.id == "reduce_motion")
						snap.accessibility.reduce_motion = command.number != 0;
					else if (command.id == "large_focus")
						snap.accessibility.large_focus = command.number != 0;
					Complete(command);
					break;
				case CommandType::SetSetting:
					for (SettingCategory& category : snap.settings)
						for (SettingItem& item : category.items)
							if (item.id == command.id)
							{
								item.value = command.value;
								item.modified = true;
							}
					Complete(command);
					break;
				case CommandType::ResetControlBindings:
					for (ControlBinding& binding : snap.controls)
						binding.button = "Default " + binding.action;
					Complete(command, "controls reset");
					break;
				case CommandType::ReturnToLibrary:
					snap.vm.state = VmState::Idle;
					Complete(command, "returned");
					break;
				case CommandType::LaunchGame:
					snap.vm.state = VmState::Running;
					snap.vm.game_id = command.id;
					Complete(command, "launched");
					break;
				default:
					Complete(command);
					break;
			}
		}
	}

	FolderListing BrowseFolder(const std::string& path, const std::string& filter, uint32_t, uint32_t) override
	{
		browsed.push_back(path + "|" + filter);
		FolderListing listing;
		listing.path = path;
		auto add_dir = [&](const std::string& name, uint32_t count) {
			FolderEntry entry;
			entry.name = name;
			entry.path = path + "/" + name;
			entry.directory = true;
			entry.compatible_count = count;
			listing.entries.push_back(entry);
		};
		auto add_image = [&](const std::string& name) {
			FolderEntry entry;
			entry.name = name;
			entry.path = path + "/" + name;
			entry.image = true;
			listing.entries.push_back(entry);
		};
		if (path == "/data/caliza")
		{
			listing.breadcrumbs = {"Internal Storage", "caliza"};
			add_dir("Games", 3);
			add_dir("BIOS", 0);
			add_dir("A very long folder name that still has to remain readable", 1);
		}
		else if (path == "/mnt/usb0")
		{
			listing.breadcrumbs = {"USB Storage"};
			add_dir("PS2", 8);
		}
		else if (path == "/data/caliza/Games")
		{
			listing.parent = "/data/caliza";
			listing.breadcrumbs = {"Internal Storage", "caliza", "Games"};
			add_dir("Racing", 2);
			add_image("Alpha One.iso");
			add_image("Beta Field.chd");
			listing.contains_compatible = true;
		}
		else if (path == "/data/caliza/BIOS")
		{
			listing.parent = "/data/caliza";
			listing.breadcrumbs = {"Internal Storage", "caliza", "BIOS"};
		}
		else
		{
			listing.parent = "/data/caliza";
			listing.error = "The folder is unavailable";
		}
		if (!filter.empty())
		{
			std::vector<FolderEntry> kept;
			for (const FolderEntry& entry : listing.entries)
				if (entry.name.find(filter) != std::string::npos)
					kept.push_back(entry);
			listing.entries = kept;
		}
		return listing;
	}
};

struct Ctx
{
	FakeBackend backend;
	UiModel model;

	Ctx() : model(&backend) {}
};

void Step(Ctx& ctx, const Input& input = Input(), double dt = 1.0 / 60.0)
{
	ctx.backend.snap = ctx.backend.GetSnapshot();
	ctx.model.Tick(ctx.backend.snap, ctx.backend.PollEvents(), input, dt);
}

void Run(Ctx& ctx, int frames)
{
	for (int i = 0; i < frames; i++)
		Step(ctx);
}

void Tap(Ctx& ctx, bool Input::*button)
{
	Input in;
	in.*button = true;
	Step(ctx);
	Step(ctx, in);
	Step(ctx);
}

void SkipBoot(Ctx& ctx)
{
	Run(ctx, 80);
}

GameEntry MakeGame(const std::string& id, const char* title, const char* source, uint64_t played = 0)
{
	GameEntry game;
	game.id = id;
	game.path = std::string("/games/") + id + ".iso";
	game.file = game.path;
	game.title = title;
	game.region = "USA";
	game.serial = std::string("SCUS-000") + id.back();
	game.source_id = source;
	game.bytes = 1024 * 1024 * 900;
	game.last_played = played;
	return game;
}

Snapshot MakeSnapshot(int game_count = 8, bool bios = true)
{
	Snapshot snap;
	snap.revision = 1;
	snap.build_tag = "host-test";
	snap.data_root = "/data/caliza";
	snap.bios.present = bios;
	snap.bios.path = bios ? "/data/caliza/BIOS" : "";
	if (!bios)
		snap.bios.problem = "No compatible BIOS image was found";

	VolumeInfo internal;
	internal.id = "internal";
	internal.root_path = "/data/caliza";
	internal.label = "Internal Storage";
	internal.kind = VolumeKind::Internal;
	internal.present = true;
	internal.readable = true;
	internal.total_bytes = 512ull << 30;
	internal.free_bytes = 220ull << 30;
	snap.volumes.push_back(internal);
	VolumeInfo usb;
	usb.id = "usb0";
	usb.root_path = "/mnt/usb0";
	usb.label = "Demo USB";
	usb.kind = VolumeKind::USB;
	usb.present = true;
	usb.readable = true;
	usb.total_bytes = 128ull << 30;
	usb.free_bytes = 80ull << 30;
	snap.volumes.push_back(usb);

	LibrarySource internal_source;
	internal_source.id = "source-internal";
	internal_source.path = "/data/caliza/Games";
	internal_source.volume_id = "internal";
	internal_source.label = "Internal Games";
	internal_source.state = SourceState::Connected;
	LibrarySource usb_source;
	usb_source.id = "source-usb";
	usb_source.path = "/mnt/usb0/PS2";
	usb_source.volume_id = "usb0";
	usb_source.label = "USB Games";
	usb_source.state = SourceState::Connected;
	snap.sources = {internal_source, usb_source};

	static const char* names[] = {"Alpha One", "Beta Field", "Gamma Coast", "Delta Quarry", "Epsilon Run", "Zeta Hollow", "Eta Relay", "Theta Meridian"};
	for (int i = 0; i < game_count; i++)
	{
		GameEntry game = MakeGame(std::string("g") + char('0' + i), names[i % 8], i % 2 ? "source-usb" : "source-internal", 1000 + i);
		snap.games.push_back(game);
	}

	SettingCategory display{"display", "Display", {}};
	SettingItem resolution;
	resolution.id = "resolution";
	resolution.label = "Resolution";
	resolution.value = "native";
	resolution.kind = SettingKind::Choice;
	resolution.choices = {{"native", "Native"}, {"2x", "2x"}, {"3x", "3x"}};
	display.items.push_back(resolution);
	SettingItem frames;
	frames.id = "frame_limit";
	frames.label = "Frame Limit";
	frames.value = "true";
	frames.kind = SettingKind::Toggle;
	display.items.push_back(frames);
	snap.settings.push_back(display);

	snap.controls = {
		{"confirm", "Confirm", "Cross"},
		{"back", "Back", "Circle"},
		{"pause", "Pause Menu", "Triangle"},
		{"fast-forward", "Fast Forward", "Square"},
	};

	for (int slot = 0; slot < 2; slot++)
	{
		MemoryCardInfo card;
		card.slot = slot;
		card.present = true;
		card.enabled = true;
		card.file = "Mcd00" + std::to_string(slot + 1) + ".ps2";
		card.bytes = 8ull << 20;
		card.used_bytes = (slot + 1) * 128ull * 1024;
		snap.memory_cards.push_back(card);
	}
	snap.diagnostics.build_id = "host-test";
	snap.diagnostics.console_status = "Library Active";
	snap.diagnostics.web_panel_url = "http://ps4.local:8080";
	return snap;
}

void OpenLibrary(Ctx& ctx)
{
	Tap(ctx, &Input::cross); // Home's first entry is Library when Continue is absent.
}

void TypeKeyboardText(Ctx& ctx, const std::string& text)
{
	auto position = [](char c, int& row, int& col) {
		if (c >= 'a' && c <= 'j') { row = 0; col = c - 'a'; return true; }
		if (c >= 'k' && c <= 't') { row = 1; col = c - 'k'; return true; }
		if (c >= 'u' && c <= 'z') { row = 2; col = c - 'u'; return true; }
		if (c >= '0' && c <= '9') { row = 3; col = c - '0'; return true; }
		return false;
	};
	int row = 0, col = 0;
	for (char c : text)
	{
		int tr = 0, tc = 0;
		if (!position(c, tr, tc))
			continue;
		while (row < tr) { Tap(ctx, &Input::down); row++; }
		while (row > tr) { Tap(ctx, &Input::up); row--; }
		while (col < tc) { Tap(ctx, &Input::right); col++; }
		while (col > tc) { Tap(ctx, &Input::left); col--; }
		Tap(ctx, &Input::cross);
	}
}

bool Posted(const Ctx& ctx, CommandType type)
{
	for (const Command& command : ctx.backend.posted)
		if (command.type == type)
			return true;
	return false;
}

void TestBootFirstRunAndPickerRestore()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(0, false);
	SkipBoot(ctx);
	CHECK(ctx.model.screen() == Screen::FirstRun);
	CHECK(ctx.model.FocusIdForTest() == "bios-folder");
	Tap(ctx, &Input::up);
	CHECK(ctx.model.FocusIdForTest() == "bios-folder");
	Tap(ctx, &Input::down);
	CHECK(ctx.model.FocusIdForTest() == "check-bios");
	Tap(ctx, &Input::up);
	Tap(ctx, &Input::cross);
	CHECK(ctx.model.screen() == Screen::FolderPicker);
	Tap(ctx, &Input::circle);
	CHECK(ctx.model.screen() == Screen::FirstRun);
	CHECK(ctx.model.FocusIdForTest() == "bios-folder");
}

void TestContinueOnlyWhenValid()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(2, true);
	SkipBoot(ctx);
	CHECK(ctx.model.screen() == Screen::Home);
	UiView view = ctx.model.BuildView();
	CHECK(!view.items.empty() && view.items[0].id == "library");
	ctx.backend.snap.continue_game_id = "g0";
	ctx.backend.snap.continue_valid = true;
	Step(ctx);
	view = ctx.model.BuildView();
	CHECK(!view.items.empty() && view.items[0].id == "continue");
	Tap(ctx, &Input::cross);
	CHECK(Posted(ctx, CommandType::LaunchGame));
	CHECK(ctx.backend.posted.back().id == "g0");
}

void TestLibraryGridEdgesAndFocusRestore()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(8, true);
	SkipBoot(ctx);
	OpenLibrary(ctx);
	CHECK(ctx.model.screen() == Screen::Library);
	Tap(ctx, &Input::right);
	CHECK(ctx.model.FocusIdForTest() == "game:g0");
	Tap(ctx, &Input::left);
	CHECK(ctx.model.FocusIdForTest() == "filter:all");
	Tap(ctx, &Input::right);
	for (int i = 0; i < 3; i++)
		Tap(ctx, &Input::right);
	CHECK(ctx.model.FocusIdForTest() == "game:g3");
	Tap(ctx, &Input::right);
	CHECK(ctx.model.FocusIdForTest() == "game:g3");
	Tap(ctx, &Input::down);
	CHECK(ctx.model.FocusIdForTest() == "game:g7");
	Tap(ctx, &Input::down);
	CHECK(ctx.model.FocusIdForTest() == "game:g7");
	Tap(ctx, &Input::up);
	CHECK(ctx.model.FocusIdForTest() == "game:g3");
	Tap(ctx, &Input::square);
	CHECK(ctx.model.screen() == Screen::GameDetail);
	Tap(ctx, &Input::circle);
	CHECK(ctx.model.screen() == Screen::Library);
	CHECK(ctx.model.FocusIdForTest() == "game:g3");
}

void TestKeyboardSearchAndFocusRestore()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(8, true);
	SkipBoot(ctx);
	OpenLibrary(ctx);
	Tap(ctx, &Input::right);
	Tap(ctx, &Input::triangle);
	CHECK(ctx.model.BuildView().keyboard.open);
	TypeKeyboardText(ctx, "beta");
	Tap(ctx, &Input::triangle);
	UiView view = ctx.model.BuildView();
	CHECK(!view.keyboard.open);
	CHECK(view.tiles.size() == 1);
	CHECK(ctx.model.FocusIdForTest() == "game:g1");
	Tap(ctx, &Input::circle);
	CHECK(ctx.model.screen() == Screen::Home);
	Tap(ctx, &Input::cross);
	CHECK(ctx.model.screen() == Screen::Library);
	CHECK(ctx.model.FocusIdForTest() == "game:g1");
}

void TestDestructiveResetDefaultsToCancel()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(2, true);
	SkipBoot(ctx);
	for (int i = 0; i < 3; i++)
		Tap(ctx, &Input::down);
	Tap(ctx, &Input::cross);
	CHECK(ctx.model.screen() == Screen::Controls);
	Tap(ctx, &Input::square);
	CHECK(ctx.model.BuildView().dialog.open);
	CHECK(ctx.model.FocusIdForTest() == "dialog:cancel");
	Tap(ctx, &Input::cross);
	CHECK(!Posted(ctx, CommandType::ResetControlBindings));
	Tap(ctx, &Input::square);
	Tap(ctx, &Input::right);
	CHECK(ctx.model.FocusIdForTest() == "dialog:confirm");
	Tap(ctx, &Input::cross);
	CHECK(Posted(ctx, CommandType::ResetControlBindings));
}

void TestFolderPickerPostsAddThenScan()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(0, true);
	SkipBoot(ctx);
	OpenLibrary(ctx);
	Tap(ctx, &Input::right);
	CHECK(ctx.model.FocusIdForTest() == "empty:add-folder");
	Tap(ctx, &Input::cross);
	CHECK(ctx.model.screen() == Screen::FolderPicker);
	Tap(ctx, &Input::cross); // Internal Storage.
	CHECK(!ctx.backend.browsed.empty() && ctx.backend.browsed.back().find("/data/caliza") == 0);
	CHECK(ctx.model.FocusIdForTest() == "picker:use-current");
	Tap(ctx, &Input::cross);
	CHECK(ctx.model.screen() == Screen::Library);
	CHECK(Posted(ctx, CommandType::AddLibraryFolder));
	ctx.backend.PumpAll();
	Step(ctx);
	CHECK(Posted(ctx, CommandType::ScanSource));
}

void TestMemoryCardsReturnToGameDetail()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(2, true);
	SkipBoot(ctx);
	OpenLibrary(ctx);
	Tap(ctx, &Input::right);
	Tap(ctx, &Input::square);
	CHECK(ctx.model.screen() == Screen::GameDetail);
	Tap(ctx, &Input::down);
	Tap(ctx, &Input::down);
	CHECK(ctx.model.FocusIdForTest() == "detail:memory");
	Tap(ctx, &Input::cross);
	CHECK(ctx.model.screen() == Screen::MemoryCards);
	Tap(ctx, &Input::circle);
	CHECK(ctx.model.screen() == Screen::GameDetail);
	CHECK(ctx.model.FocusIdForTest() == "detail:memory");
	Tap(ctx, &Input::circle);
	CHECK(ctx.model.screen() == Screen::Library);
	CHECK(ctx.model.FocusIdForTest() == "game:g0");
}

void TestPauseReturnConfirmationDefaultsToCancel()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(2, true);
	ctx.backend.snap.vm.state = VmState::Paused;
	ctx.backend.snap.vm.title = "Alpha One";
	ctx.backend.snap.vm.can_save = true;
	SkipBoot(ctx);
	Tap(ctx, &Input::options);
	CHECK(ctx.model.screen() == Screen::PauseOverlay);
	for (int i = 0; i < 4; i++)
		Tap(ctx, &Input::down);
	CHECK(ctx.model.FocusIdForTest() == "pause:return");
	Tap(ctx, &Input::cross);
	CHECK(ctx.model.FocusIdForTest() == "dialog:cancel");
	Tap(ctx, &Input::cross);
	CHECK(!Posted(ctx, CommandType::ReturnToLibrary));
	Tap(ctx, &Input::cross);
	Tap(ctx, &Input::right);
	Tap(ctx, &Input::cross);
	CHECK(Posted(ctx, CommandType::ReturnToLibrary));
}

void TestAccessibilityCommandsUseContractPayload()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(2, true);
	SkipBoot(ctx);
	for (int i = 0; i < 4; i++)
		Tap(ctx, &Input::down);
	Tap(ctx, &Input::cross);
	CHECK(ctx.model.screen() == Screen::Accessibility);
	Tap(ctx, &Input::cross);
	CHECK(Posted(ctx, CommandType::SetAccessibility));
	CHECK(ctx.backend.posted.back().id == "text_scale");
	CHECK(ctx.backend.posted.back().number == 110);
	ctx.backend.PumpAll();
	Step(ctx);
	CHECK(ctx.backend.snap.accessibility.text_scale_percent == 110);
	Tap(ctx, &Input::down);
	Tap(ctx, &Input::cross);
	CHECK(ctx.backend.posted.back().id == "high_contrast");
	CHECK(ctx.backend.posted.back().number == 1);
}

void TestSettingsChoiceAndNestedBack()
{
	Ctx ctx;
	ctx.backend.snap = MakeSnapshot(2, true);
	SkipBoot(ctx);
	for (int i = 0; i < 2; i++)
		Tap(ctx, &Input::down);
	Tap(ctx, &Input::cross);
	CHECK(ctx.model.screen() == Screen::Settings);
	Tap(ctx, &Input::right);
	CHECK(ctx.model.FocusIdForTest() == "setting:resolution");
	Tap(ctx, &Input::right);
	CHECK(Posted(ctx, CommandType::SetSetting));
	CHECK(ctx.backend.posted.back().id == "resolution");
	CHECK(ctx.backend.posted.back().value == "2x");
	Tap(ctx, &Input::circle);
	CHECK(ctx.model.FocusIdForTest() == "setting-category:display");
	Tap(ctx, &Input::circle);
	CHECK(ctx.model.screen() == Screen::Home);
}

} // namespace

int main()
{
	TestBootFirstRunAndPickerRestore();
	TestContinueOnlyWhenValid();
	TestLibraryGridEdgesAndFocusRestore();
	TestKeyboardSearchAndFocusRestore();
	TestDestructiveResetDefaultsToCancel();
	TestFolderPickerPostsAddThenScan();
	TestMemoryCardsReturnToGameDetail();
	TestPauseReturnConfirmationDefaultsToCancel();
	TestAccessibilityCommandsUseContractPayload();
	TestSettingsChoiceAndNestedBack();
	if (g_failures)
	{
		std::fprintf(stderr, "%d UI model test failure(s)\n", g_failures);
		return 1;
	}
	std::puts("UI model tests passed");
	return 0;
}
