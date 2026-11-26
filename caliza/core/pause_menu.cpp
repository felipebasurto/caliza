// Caliza runtime pause menu model (see pause_menu.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pause_menu.h"

#include <algorithm>

namespace caliza
{
namespace
{
constexpr double kRepeatDelay = 0.42;
constexpr double kRepeatInterval = 0.085;
// A pause command that never lands (the vm keeps running) must not hold the game's input
// captured forever: the overlay closes and the game keeps control.
constexpr double kPauseTimeoutSeconds = 2.5;
constexpr double kToastSeconds = 4.0;

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
} // namespace

PauseMenu::PauseMenu(PauseMenuBackend* backend) : m_backend(backend) {}

int PauseMenu::MoveClamped(int index, int delta, int count)
{
	if (count <= 0)
		return 0;
	return std::max(0, std::min(count - 1, index + delta));
}

void PauseMenu::Open(const std::string& title)
{
	if (m_open)
		return;
	m_open = true;
	m_index = 0;
	m_dialog_open = false;
	m_dialog_focus = 0;
	m_return_posted = false;
	m_pause_posted = false;
	m_unpaused_seconds = 0.0;
	m_toast.clear();
	m_toast_seconds = 0.0;
	m_title = title;
	m_wait_release = true; // the press that opened the menu is not an action
	m_previous_input = Input();
	for (RepeatState& st : m_repeat)
		st = RepeatState();
	m_revision++;
}

void PauseMenu::Close(bool resume)
{
	if (!m_open)
		return;
	if (resume && m_vm.state == VmState::Paused)
		PostCommand(CommandType::ResumeVm);
	m_open = false;
	m_dialog_open = false;
	m_revision++;
}

void PauseMenu::PostCommand(CommandType type)
{
	if (!m_backend)
		return;
	Command command;
	command.type = type;
	if (type == CommandType::SaveState || type == CommandType::LoadState)
		command.number = kSlot;
	m_backend->Post(command);
}

void PauseMenu::SetToast(std::string text)
{
	m_toast = std::move(text);
	m_toast_seconds = kToastSeconds;
	m_revision++;
}

PauseMenu::LogicalInput PauseMenu::EvaluateInput(const Input& input, double dt)
{
	LogicalInput out;
	out.cross = input.cross && !m_previous_input.cross;
	out.circle = input.circle && !m_previous_input.circle;
	out.options = input.options && !m_previous_input.options;

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

void PauseMenu::ActivateRow()
{
	static const char* const ids[] = {"resume", "save", "load", "return"};
	const std::string id = ids[std::max(0, std::min(3, m_index))];
	if (id == "resume")
	{
		Close(true);
		return;
	}
	if (id == "save")
	{
		if (m_vm.can_save)
			PostCommand(CommandType::SaveState);
		return;
	}
	if (id == "load")
	{
		if (m_vm.can_load)
			PostCommand(CommandType::LoadState);
		return;
	}
	// return: destructive, the dialog opens on cancel.
	m_dialog_open = true;
	m_dialog_focus = 0;
	m_revision++;
}

void PauseMenu::ExecuteDialog()
{
	if (m_dialog_focus == 0)
	{
		m_dialog_open = false;
		m_revision++;
		return;
	}
	PostCommand(CommandType::ReturnToLibrary);
	m_return_posted = true;
	// The vm stops and the process re-executes into the library. The overlay closes at once:
	// the state leaving Paused/Running would close it anyway a beat later.
	m_dialog_open = false;
	m_open = false;
	m_revision++;
}

void PauseMenu::Tick(const VmStatus& vm, const std::vector<CommandEvent>& events, const Input& input, double dt)
{
	m_vm = vm;

	if (m_toast_seconds > 0.0)
	{
		m_toast_seconds = std::max(0.0, m_toast_seconds - dt);
		if (m_toast_seconds == 0.0)
		{
			m_toast.clear();
			m_revision++;
		}
	}

	for (const CommandEvent& event : events)
	{
		if (event.status != CommandStatus::Succeeded && event.status != CommandStatus::Failed &&
			event.status != CommandStatus::Cancelled)
			continue;
		const bool failed = event.status != CommandStatus::Succeeded;
		std::string message = event.message;
		if (message.empty())
			message = failed ? "Command failed" : "Done";
		switch (event.type)
		{
			case CommandType::SaveState:
				SetToast(message);
				break;
			case CommandType::LoadState:
				if (failed)
				{
					SetToast(message);
				}
				else
				{
					// A state was loaded: play continues from it. A failure keeps the overlay open.
					Close(true);
				}
				break;
			case CommandType::PauseVm:
				// The vm stays interactive; holding its input captured is the dangerous failure mode.
				if (failed)
					Close(false);
				break;
			case CommandType::ResumeVm:
				break;
			default:
				SetToast(message);
				break;
		}
	}

	if (!m_open)
	{
		m_previous_input = input;
		return;
	}

	// The vm left the session behind (stopped, failed, shut down): nothing to resume.
	if (!m_return_posted &&
		(vm.state == VmState::Idle || vm.state == VmState::Unavailable || vm.state == VmState::Failed ||
			vm.state == VmState::Stopping))
	{
		m_open = false;
		m_dialog_open = false;
		m_revision++;
		m_previous_input = input;
		return;
	}

	// PauseVm is posted here, not in Open(): the model learns the vm state with the first
	// tick, and Open() may run before any status has arrived.
	if (!m_pause_posted && !m_return_posted && vm.state == VmState::Running)
	{
		m_pause_posted = true;
		PostCommand(CommandType::PauseVm);
	}

	// A pause that never lands: close rather than capture input forever.
	if (!m_return_posted && vm.state == VmState::Running)
	{
		m_unpaused_seconds += dt;
		if (m_unpaused_seconds >= kPauseTimeoutSeconds)
		{
			Close(false);
			m_previous_input = input;
			return;
		}
	}
	else
	{
		m_unpaused_seconds = 0.0;
	}

	if (m_wait_release)
	{
		const bool any = input.cross || input.circle || input.options || input.up || input.down || input.left ||
			input.right || input.square || input.triangle;
		m_previous_input = input;
		if (any)
		{
			for (RepeatState& st : m_repeat)
				st = RepeatState();
			return;
		}
		m_wait_release = false;
	}

	const LogicalInput in = EvaluateInput(input, dt);

	if (m_dialog_open)
	{
		if (in.left || in.right || in.up || in.down)
		{
			m_dialog_focus = 1 - m_dialog_focus;
			m_revision++;
		}
		if (in.cross)
			ExecuteDialog();
		else if (in.circle || in.options)
		{
			m_dialog_open = false;
			m_revision++;
		}
	}
	else
	{
		if (in.up)
		{
			const int moved = MoveClamped(m_index, -1, 4);
			if (moved != m_index)
			{
				m_index = moved;
				m_revision++;
			}
		}
		if (in.down)
		{
			const int moved = MoveClamped(m_index, 1, 4);
			if (moved != m_index)
			{
				m_index = moved;
				m_revision++;
			}
		}
		if (in.circle || in.options)
			Close(true);
		else if (in.cross)
			ActivateRow();
	}

	m_previous_input = input;
}

PauseMenuView PauseMenu::BuildView() const
{
	PauseMenuView view;
	view.open = m_open;
	view.revision = m_revision;
	if (!m_open)
		return view;

	view.title = m_title;
	view.subtitle = m_vm.state == VmState::Paused ? "Paused" : "Pausing";

	struct RowSpec
	{
		const char* id;
		const char* label;
		bool disabled;
		const char* detail_disabled;
		const char* detail;
		bool destructive;
	};
	const RowSpec specs[] = {
		{"resume", "Resume", false, "", "Back to the game", false},
		{"save", "Save state", !m_vm.can_save, "Unavailable right now", "Slot 1", false},
		{"load", "Load state", !m_vm.can_load, "No state is available", "Slot 1", false},
		{"return", "Return to library", false, "", "Unsaved progress is lost", true},
	};
	for (int i = 0; i < 4; i++)
	{
		PauseMenuRow row;
		row.id = specs[i].id;
		row.label = specs[i].label;
		row.detail = specs[i].disabled ? specs[i].detail_disabled : specs[i].detail;
		row.focused = i == m_index && !m_dialog_open;
		row.disabled = specs[i].disabled;
		row.destructive = specs[i].destructive;
		view.rows.push_back(std::move(row));
	}

	view.dialog_open = m_dialog_open;
	if (m_dialog_open)
	{
		view.dialog_title = "Return to library?";
		view.dialog_body = "The current game session closes. Unsaved progress in memory is lost.";
		view.cancel_label = "Cancel";
		view.confirm_label = "Return";
		view.dialog_focus = m_dialog_focus;
	}
	view.toast = m_toast;
	return view;
}
} // namespace caliza
