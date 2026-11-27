// Focused host tests for the runtime pause menu model: five actions, 10-slot picker for
// save/load, multi-disc swap, destructive confirmation with cancel first, input re-synchronisation
// (including L1/R1/L2/R2 release gating), command events, early-close ResumeVm safety, and the
// pause-timeout failure mode. Uses a fake command sink; no VM is tested here.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../caliza/core/pause_menu.h"

#include <cstdio>
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

struct FakeSink : PauseMenuBackend
{
	std::vector<Command> posted;
	CommandId next = 1;

	CommandId Post(const Command& command) override
	{
		posted.push_back(command);
		return next++;
	}

	bool LastIs(CommandType type) const
	{
		return !posted.empty() && posted.back().type == type;
	}
};

VmStatus RunningVm()
{
	VmStatus vm;
	vm.state = VmState::Running;
	vm.title = "Demo Game";
	vm.can_save = true;
	vm.can_load = true;
	vm.occupied_slots = (1u << 1);
	vm.disc_index = 1;
	vm.disc_count = 1;
	return vm;
}

VmStatus PausedVm()
{
	VmStatus vm = RunningVm();
	vm.state = VmState::Paused;
	return vm;
}

CommandEvent Event(CommandType type, CommandStatus status, const std::string& message = "")
{
	CommandEvent event;
	event.type = type;
	event.status = status;
	event.message = message;
	return event;
}

void Tick(PauseMenu& menu, const VmStatus& vm, const Input& input, double dt = 0.016,
	const std::vector<CommandEvent>& events = {})
{
	menu.Tick(vm, events, input, dt);
}

void Press(PauseMenu& menu, const VmStatus& vm, bool Input::* button)
{
	Input down;
	down.*button = true;
	Tick(menu, vm, down);
	Tick(menu, vm, Input());
}

// The real flow: the menu always opens while the vm is running, then the pause lands. `vm` is
// the status after that transition.
bool Opened(PauseMenu& menu, FakeSink& sink, const VmStatus& vm)
{
	menu.Open(vm.title);
	// The opening combo buttons (e.g. L1 + combo options) are still held, then released: no edge may fire.
	Input held;
	held.options = true;
	held.l1 = true;
	const VmStatus running = RunningVm();
	Tick(menu, running, held);
	Tick(menu, running, Input());
	Tick(menu, vm, Input());
	return menu.IsOpen() && !sink.posted.empty();
}
} // namespace

int main()
{
	// Opening posts PauseVm once, and the still-held opening buttons neither close the menu nor act.
	// Transitioning Running -> Paused bumps the view revision so the overlay recomposes.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		VmStatus vm = RunningVm();
		CHECK(Opened(menu, sink, vm));
		CHECK(sink.posted.size() == 1);
		CHECK(sink.posted[0].type == CommandType::PauseVm);
		CHECK(menu.IsOpen());
		PauseMenuView view = menu.BuildView();
		CHECK(view.open);
		CHECK(view.rows.size() == 5);
		CHECK(view.rows[0].id == "resume" && view.rows[0].focused);
		CHECK(view.rows[1].id == "save" && !view.rows[1].disabled);
		CHECK(view.rows[2].id == "load" && !view.rows[2].disabled);
		CHECK(view.rows[3].id == "disc" && view.rows[3].disabled);
		CHECK(view.rows[4].id == "return" && view.rows[4].destructive);
		CHECK(view.subtitle == "Pausing");
		const uint64_t rev_before = view.revision;
		vm = PausedVm();
		Tick(menu, vm, Input());
		view = menu.BuildView();
		CHECK(view.subtitle == "Paused");
		CHECK(view.revision > rev_before);
	}

	// Navigation clamps at both ends across all 5 rows.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::up);
		CHECK(menu.FocusIndexForTest() == 0);
		Press(menu, vm, &Input::down);
		CHECK(menu.FocusIndexForTest() == 1);
		Press(menu, vm, &Input::down);
		Press(menu, vm, &Input::down);
		Press(menu, vm, &Input::down);
		CHECK(menu.FocusIndexForTest() == 4);
		Press(menu, vm, &Input::down);
		CHECK(menu.FocusIndexForTest() == 4);
	}

	// Slot picker on save and load: Left/Right and L1/R1 wrap across slots 1..10 and share state.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		VmStatus vm = PausedVm();
		vm.occupied_slots = (1u << 3); // only slot 3 is occupied
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::down); // save row
		CHECK(menu.SelectedSlot() == 1);
		CHECK(menu.BuildView().picker_hint == "< > Slot");
		CHECK(menu.BuildView().rows[1].detail == "< Slot 1 - Empty >");
		// Wrap backwards 1 -> 10
		Press(menu, vm, &Input::left);
		CHECK(menu.SelectedSlot() == 10);
		// Wrap forwards 10 -> 1, then step to slot 4 with Right and R1
		Press(menu, vm, &Input::right);
		CHECK(menu.SelectedSlot() == 1);
		Press(menu, vm, &Input::right);
		Press(menu, vm, &Input::r1);
		Press(menu, vm, &Input::right);
		CHECK(menu.SelectedSlot() == 4);
		// Save posts slot 4 and stays open; completion event becomes a toast.
		Press(menu, vm, &Input::cross);
		CHECK(sink.LastIs(CommandType::SaveState));
		CHECK(sink.posted.back().number == 4);
		CHECK(menu.IsOpen());
		Tick(menu, vm, Input(), 0.016, {Event(CommandType::SaveState, CommandStatus::Succeeded, "Saved slot 4")});
		CHECK(menu.BuildView().toast == "Saved slot 4");

		// Move to Load row: still on slot 4 (empty until status updates), so Cross is ignored.
		Press(menu, vm, &Input::down);
		CHECK(menu.BuildView().rows[2].disabled);
		const size_t before_load = sink.posted.size();
		Press(menu, vm, &Input::cross);
		CHECK(sink.posted.size() == before_load);

		// Step left with L1 to slot 3 (occupied): Load row enables and posts slot 3.
		Press(menu, vm, &Input::l1);
		CHECK(menu.SelectedSlot() == 3);
		CHECK(!menu.BuildView().rows[2].disabled);
		CHECK(menu.BuildView().rows[2].detail == "< Slot 3 - Saved >");
		Press(menu, vm, &Input::cross);
		CHECK(sink.LastIs(CommandType::LoadState));
		CHECK(sink.posted.back().number == 3);
		// Failure keeps overlay open with toast.
		Tick(menu, vm, Input(), 0.016, {Event(CommandType::LoadState, CommandStatus::Failed, "Load slot 3 failed")});
		CHECK(menu.IsOpen());
		CHECK(menu.BuildView().toast == "Load slot 3 failed");
		// Success closes overlay and resumes.
		Tick(menu, vm, Input(), 0.016, {Event(CommandType::LoadState, CommandStatus::Succeeded, "Loaded slot 3")});
		CHECK(!menu.IsOpen());
		CHECK(menu.LastCloseReason() == "load state");
		CHECK(sink.LastIs(CommandType::ResumeVm));

		// Re-opening preserves selected slot (3); Reset() returns it to slot 1.
		menu.Open(vm.title);
		CHECK(menu.SelectedSlot() == 3);
		menu.Reset();
		CHECK(!menu.IsOpen());
		CHECK(menu.SelectedSlot() == 1);
	}

	// Save is disabled without can_save.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		VmStatus vm = PausedVm();
		vm.can_save = false;
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::down);
		const size_t before = sink.posted.size();
		Press(menu, vm, &Input::cross);
		CHECK(sink.posted.size() == before);
		CHECK(menu.BuildView().rows[1].disabled);
		CHECK(menu.BuildView().rows[1].detail == "Unavailable right now");
	}

	// Disc swap: single-disc disabled, two-disc posts disc 2, three-disc allows picking disc 2 or 3.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		VmStatus vm = PausedVm();
		vm.disc_index = 1;
		vm.disc_count = 1;
		CHECK(Opened(menu, sink, vm));
		for (int i = 0; i < 3; i++)
			Press(menu, vm, &Input::down); // disc row
		CHECK(menu.BuildView().rows[3].disabled);
		CHECK(menu.BuildView().rows[3].detail == "Single-disc game");
		const size_t before = sink.posted.size();
		Press(menu, vm, &Input::cross);
		CHECK(sink.posted.size() == before);

		// Multi-disc (3 discs, currently on disc 1): defaults to target 2; Right picks 3; Right skips current 1 -> 2.
		vm.disc_count = 3;
		vm.disc_index = 1;
		Tick(menu, vm, Input());
		CHECK(!menu.BuildView().rows[3].disabled);
		CHECK(menu.TargetDisc() == 2);
		CHECK(menu.BuildView().picker_hint == "< > Disc");
		Press(menu, vm, &Input::right);
		CHECK(menu.TargetDisc() == 3);
		Press(menu, vm, &Input::right);
		CHECK(menu.TargetDisc() == 2);
		Press(menu, vm, &Input::left);
		CHECK(menu.TargetDisc() == 3);
		Press(menu, vm, &Input::cross);
		CHECK(sink.LastIs(CommandType::ChangeDisc));
		CHECK(sink.posted.back().number == 3);
		Tick(menu, vm, Input(), 0.016, {Event(CommandType::ChangeDisc, CommandStatus::Succeeded, "Switched to Disc 3 of 3")});
		CHECK(menu.IsOpen());
		CHECK(menu.BuildView().toast == "Switched to Disc 3 of 3");
	}

	// Resume posts ResumeVm and closes; circle and pause combo close with resume as well.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::cross);
		CHECK(!menu.IsOpen());
		CHECK(menu.LastCloseReason() == "resume");
		CHECK(sink.LastIs(CommandType::ResumeVm));
	}
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::circle);
		CHECK(!menu.IsOpen());
		CHECK(menu.LastCloseReason() == "circle");
		CHECK(sink.LastIs(CommandType::ResumeVm));
	}
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::options);
		CHECK(!menu.IsOpen());
		CHECK(menu.LastCloseReason() == "combo");
		CHECK(sink.LastIs(CommandType::ResumeVm));
	}

	// Closing before PauseVm lands still posts ResumeVm so a queued PauseVm cannot leave the VM stuck paused.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = RunningVm();
		CHECK(Opened(menu, sink, vm));
		CHECK(sink.posted.size() == 1 && sink.posted[0].type == CommandType::PauseVm);
		Press(menu, vm, &Input::circle);
		CHECK(!menu.IsOpen());
		CHECK(sink.posted.size() == 2 && sink.posted[1].type == CommandType::ResumeVm);
	}

	// Return to library: the dialog opens on cancel, cross there only cancels.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		for (int i = 0; i < 4; i++)
			Press(menu, vm, &Input::down);
		Press(menu, vm, &Input::cross);
		PauseMenuView view = menu.BuildView();
		CHECK(view.dialog_open);
		CHECK(view.dialog_focus == 0);
		CHECK(view.cancel_label == "Cancel");
		// Cross on cancel closes the dialog without posting.
		const size_t before = sink.posted.size();
		Press(menu, vm, &Input::cross);
		CHECK(!menu.BuildView().dialog_open);
		CHECK(sink.posted.size() == before);
		// Reopen, move to confirm, cross posts ReturnToLibrary and closes the overlay without resuming.
		Press(menu, vm, &Input::cross);
		CHECK(menu.BuildView().dialog_open);
		Press(menu, vm, &Input::right);
		CHECK(menu.BuildView().dialog_focus == 1);
		Press(menu, vm, &Input::cross);
		CHECK(sink.LastIs(CommandType::ReturnToLibrary));
		CHECK(!menu.IsOpen());
		CHECK(menu.LastCloseReason() == "return to library");
		CHECK(sink.posted.back().type != CommandType::ResumeVm);
	}

	// A pause that never lands closes the overlay instead of capturing input forever.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = RunningVm(); // never becomes paused
		CHECK(Opened(menu, sink, vm));
		for (int i = 0; i < 200 && menu.IsOpen(); i++)
			Tick(menu, vm, Input(), 0.016);
		CHECK(!menu.IsOpen());
		CHECK(menu.LastCloseReason() == "pause timeout");
		CHECK(sink.posted.size() == 1); // only the PauseVm; no resume is posted for a pause that never happened
	}

	// A failed PauseVm command event closes the overlay at once.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = RunningVm();
		CHECK(Opened(menu, sink, vm));
		Tick(menu, vm, Input(), 0.016, {Event(CommandType::PauseVm, CommandStatus::Failed, "no vm")});
		CHECK(!menu.IsOpen());
		CHECK(menu.LastCloseReason() == "pause failed");
	}

	// The vm stopping underneath the overlay closes it without a resume.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		vm.state = VmState::Stopping;
		const size_t before = sink.posted.size();
		Tick(menu, vm, Input());
		CHECK(!menu.IsOpen());
		CHECK(menu.LastCloseReason() == "vm stopped");
		CHECK(sink.posted.size() == before);
	}

	// Direction repeat: held down eventually moves again, but not on the first repeat interval.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		Input down;
		down.down = true;
		Tick(menu, vm, down, 0.016); // the press edge: row 1
		CHECK(menu.FocusIndexForTest() == 1);
		for (int i = 0; i < 20; i++) // ~0.32 s: past the edge, before the 0.42 s repeat delay
			Tick(menu, vm, down, 0.016);
		CHECK(menu.FocusIndexForTest() == 1);
		for (int i = 0; i < 20; i++) // ~0.64 s total: repeat has fired
			Tick(menu, vm, down, 0.016);
		CHECK(menu.FocusIndexForTest() > 1);
	}

	if (g_failures != 0)
	{
		std::fprintf(stderr, "%d checks failed\n", g_failures);
		return 1;
	}
	std::printf("pause menu tests passed\n");
	return 0;
}
