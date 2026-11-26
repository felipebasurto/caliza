// Focused host tests for the runtime pause menu model: four actions, slot-1 save/load,
// destructive confirmation with cancel first, input re-synchronisation, command events and
// the pause-timeout failure mode. Uses a fake command sink; no VM is tested here.
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

// One tick with the given input levels held (and released afterwards when releases is set).
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
	// The opening press is still held, then released: no edge may fire from it.
	Input held;
	held.options = true;
	const VmStatus running = RunningVm();
	Tick(menu, running, held);
	Tick(menu, running, Input());
	Tick(menu, vm, Input());
	return menu.IsOpen() && !sink.posted.empty();
}
} // namespace

int main()
{
	// Opening posts PauseVm once, and the still-held opening button neither closes the menu nor acts.
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
		CHECK(view.rows.size() == 4);
		CHECK(view.rows[0].id == "resume" && view.rows[0].focused);
		CHECK(view.rows[1].id == "save" && !view.rows[1].disabled);
		CHECK(view.rows[2].id == "load" && !view.rows[2].disabled);
		CHECK(view.rows[3].id == "return" && view.rows[3].destructive);
		CHECK(view.subtitle == "Pausing");
		vm = PausedVm();
		Tick(menu, vm, Input());
		view = menu.BuildView();
		CHECK(view.subtitle == "Paused");
	}

	// Navigation clamps at both ends.
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
		CHECK(menu.FocusIndexForTest() == 3);
		Press(menu, vm, &Input::down);
		CHECK(menu.FocusIndexForTest() == 3);
	}

	// Save posts slot 1 and keeps the overlay open; its completion event becomes a toast.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::down);
		Press(menu, vm, &Input::cross);
		CHECK(sink.LastIs(CommandType::SaveState));
		CHECK(sink.posted.back().number == 1);
		CHECK(menu.IsOpen());
		Tick(menu, vm, Input(), 0.016, {Event(CommandType::SaveState, CommandStatus::Succeeded, "State saved")});
		CHECK(menu.BuildView().toast == "State saved");
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

	// Load posts slot 1; success closes the overlay and resumes, failure stays open.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::down);
		Press(menu, vm, &Input::down);
		Press(menu, vm, &Input::cross);
		CHECK(sink.LastIs(CommandType::LoadState));
		CHECK(sink.posted.back().number == 1);
		Tick(menu, vm, Input(), 0.016, {Event(CommandType::LoadState, CommandStatus::Failed, "No state in slot 1")});
		CHECK(menu.IsOpen());
		CHECK(menu.BuildView().toast == "No state in slot 1");
		Tick(menu, vm, Input(), 0.016, {Event(CommandType::LoadState, CommandStatus::Succeeded, "State loaded")});
		CHECK(!menu.IsOpen());
		CHECK(sink.LastIs(CommandType::ResumeVm));
	}

	// Resume posts ResumeVm and closes; circle closes with a resume as well.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::cross);
		CHECK(!menu.IsOpen());
		CHECK(sink.LastIs(CommandType::ResumeVm));
	}
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		Press(menu, vm, &Input::circle);
		CHECK(!menu.IsOpen());
		CHECK(sink.LastIs(CommandType::ResumeVm));
	}

	// Return to library: the dialog opens on cancel, cross there only cancels.
	{
		FakeSink sink;
		PauseMenu menu(&sink);
		const VmStatus vm = PausedVm();
		CHECK(Opened(menu, sink, vm));
		for (int i = 0; i < 3; i++)
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
