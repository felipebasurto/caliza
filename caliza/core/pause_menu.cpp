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

constexpr const char* kRowIds[PauseMenu::kRowCount] = {"resume", "save", "load", "disc", "return"};

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

bool VisibleVmChanged(const VmStatus& a, const VmStatus& b)
{
	return a.state != b.state || a.can_save != b.can_save || a.can_load != b.can_load ||
	       a.occupied_slots != b.occupied_slots || a.disc_index != b.disc_index || a.disc_count != b.disc_count;
}
} // namespace

PauseMenu::PauseMenu(PauseMenuBackend* backend) : m_backend(backend) {}

int PauseMenu::MoveClamped(int index, int delta, int count)
{
	if (count <= 0)
		return 0;
	return std::max(0, std::min(count - 1, index + delta));
}

bool PauseMenu::SlotHasState(int slot) const
{
	if (slot < kMinSlot || slot > kMaxSlot)
		return false;
	if (m_vm.occupied_slots != 0)
		return (m_vm.occupied_slots & (1u << static_cast<unsigned>(slot))) != 0;
	return slot == kSlot && m_vm.can_load;
}

void PauseMenu::SyncTargetDisc()
{
	if (m_vm.disc_count < 2)
	{
		m_target_disc = 0;
		return;
	}
	const int cur = (m_vm.disc_index >= 1 && m_vm.disc_index <= m_vm.disc_count) ? m_vm.disc_index : 1;
	if (m_target_disc < 1 || m_target_disc > m_vm.disc_count || m_target_disc == cur)
		m_target_disc = (cur % m_vm.disc_count) + 1;
}

void PauseMenu::StepSlot(int delta)
{
	int next = m_slot + delta;
	if (next < kMinSlot)
		next = kMaxSlot;
	else if (next > kMaxSlot)
		next = kMinSlot;
	if (next != m_slot)
	{
		m_slot = next;
		m_revision++;
	}
}

void PauseMenu::StepDisc(int delta)
{
	if (m_vm.disc_count <= 2)
		return;
	const int cur = (m_vm.disc_index >= 1 && m_vm.disc_index <= m_vm.disc_count) ? m_vm.disc_index : 1;
	SyncTargetDisc();
	int next = m_target_disc;
	for (int tries = 0; tries < m_vm.disc_count; tries++)
	{
		next += delta;
		if (next < 1)
			next = m_vm.disc_count;
		else if (next > m_vm.disc_count)
			next = 1;
		if (next != cur)
			break;
	}
	if (next != m_target_disc)
	{
		m_target_disc = next;
		m_revision++;
	}
}

void PauseMenu::Open(const std::string& title)
{
	if (m_open)
		return;
	m_open = true;
	m_index = 0;
	m_slot = std::max(kMinSlot, std::min(kMaxSlot, m_slot));
	m_dialog_open = false;
	m_dialog_focus = 0;
	m_return_posted = false;
	m_pause_posted = false;
	m_unpaused_seconds = 0.0;
	m_toast.clear();
	m_toast_seconds = 0.0;
	m_close_reason.clear();
	m_title = title;
	m_wait_release = true; // the press that opened the menu is not an action
	m_previous_input = Input();
	for (RepeatState& st : m_repeat)
		st = RepeatState();
	SyncTargetDisc();
	m_revision++;
}

void PauseMenu::Close(bool resume, const char* reason)
{
	if (!m_open)
		return;
	m_close_reason = reason ? reason : "close";
	if (resume && (m_vm.state == VmState::Paused || m_pause_posted))
		PostCommand(CommandType::ResumeVm);
	m_open = false;
	m_dialog_open = false;
	m_pause_posted = false;
	m_revision++;
}

void PauseMenu::Reset()
{
	m_open = false;
	m_wait_release = false;
	m_index = 0;
	m_slot = kMinSlot;
	m_target_disc = 0;
	m_dialog_open = false;
	m_dialog_focus = 0;
	m_title.clear();
	m_close_reason.clear();
	m_vm = VmStatus();
	m_previous_input = Input();
	for (RepeatState& st : m_repeat)
		st = RepeatState();
	m_toast.clear();
	m_toast_seconds = 0.0;
	m_unpaused_seconds = 0.0;
	m_return_posted = false;
	m_pause_posted = false;
	m_revision++;
}

void PauseMenu::PostCommand(CommandType type, int number)
{
	if (!m_backend)
		return;
	Command command;
	command.type = type;
	command.number = number;
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
	out.l1 = input.l1 && !m_previous_input.l1;
	out.r1 = input.r1 && !m_previous_input.r1;

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
	const std::string id = kRowIds[std::max(0, std::min(kRowCount - 1, m_index))];
	if (id == "resume")
	{
		Close(true, "resume");
		return;
	}
	if (id == "save")
	{
		if (m_vm.can_save)
			PostCommand(CommandType::SaveState, m_slot);
		return;
	}
	if (id == "load")
	{
		if (SlotHasState(m_slot))
			PostCommand(CommandType::LoadState, m_slot);
		return;
	}
	if (id == "disc")
	{
		if (m_vm.disc_count >= 2)
		{
			SyncTargetDisc();
			PostCommand(CommandType::ChangeDisc, m_target_disc);
		}
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
	m_close_reason = "return to library";
	// The vm stops and the process re-executes into the library. The overlay closes at once:
	// the state leaving Paused/Running would close it anyway a beat later.
	m_dialog_open = false;
	m_open = false;
	m_pause_posted = false;
	m_revision++;
}

void PauseMenu::Tick(const VmStatus& vm, const std::vector<CommandEvent>& events, const Input& input, double dt)
{
	const bool vm_changed = m_open && VisibleVmChanged(m_vm, vm);
	m_vm = vm;
	SyncTargetDisc();
	if (vm_changed)
		m_revision++;

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
					Close(true, "load state");
				}
				break;
			case CommandType::ChangeDisc:
				SetToast(message);
				break;
			case CommandType::PauseVm:
				// The vm stays interactive; holding its input captured is the dangerous failure mode.
				if (failed)
					Close(false, "pause failed");
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
		m_close_reason = "vm stopped";
		m_open = false;
		m_dialog_open = false;
		m_pause_posted = false;
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
			Close(false, "pause timeout");
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
			input.right || input.square || input.triangle || input.l1 || input.r1 || input.l2 || input.r2;
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
			const int moved = MoveClamped(m_index, -1, kRowCount);
			if (moved != m_index)
			{
				m_index = moved;
				m_revision++;
			}
		}
		if (in.down)
		{
			const int moved = MoveClamped(m_index, 1, kRowCount);
			if (moved != m_index)
			{
				m_index = moved;
				m_revision++;
			}
		}
		const std::string focused_id = kRowIds[std::max(0, std::min(kRowCount - 1, m_index))];
		if (focused_id == "save" || focused_id == "load")
		{
			if (in.left || in.l1)
				StepSlot(-1);
			else if (in.right || in.r1)
				StepSlot(1);
		}
		else if (focused_id == "disc" && m_vm.disc_count > 2)
		{
			if (in.left || in.l1)
				StepDisc(-1);
			else if (in.right || in.r1)
				StepDisc(1);
		}
		if (in.circle)
			Close(true, "circle");
		else if (in.options)
			Close(true, "combo");
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

	const bool slot_has_state = SlotHasState(m_slot);
	const auto slot_detail = [&](bool focused) {
		std::string s = "Slot " + std::to_string(m_slot) + (slot_has_state ? " - Saved" : " - Empty");
		return focused ? ("< " + s + " >") : s;
	};

	const int cur_disc = (m_vm.disc_index >= 1 && m_vm.disc_index <= m_vm.disc_count) ? m_vm.disc_index : 1;
	const int next_disc = (m_target_disc >= 1 && m_target_disc <= m_vm.disc_count && m_target_disc != cur_disc)
		? m_target_disc
		: (m_vm.disc_count >= 2 ? (cur_disc % m_vm.disc_count) + 1 : 0);

	std::string disc_detail;
	if (m_vm.disc_count < 2)
	{
		disc_detail = "Single-disc game";
	}
	else if (m_vm.disc_count == 2)
	{
		disc_detail = "Disc " + std::to_string(cur_disc) + " of 2 - Switch to Disc " + std::to_string(next_disc);
	}
	else
	{
		const bool disc_focused = (m_index == 3 && !m_dialog_open);
		std::string body = "Disc " + std::to_string(cur_disc) + " of " + std::to_string(m_vm.disc_count) +
			" - Switch to Disc " + std::to_string(next_disc);
		disc_detail = disc_focused ? ("< " + body + " >") : body;
	}

	const bool save_focused = (m_index == 1 && !m_dialog_open);
	const bool load_focused = (m_index == 2 && !m_dialog_open);
	const bool disc_focused = (m_index == 3 && !m_dialog_open);

	if (save_focused || load_focused)
		view.picker_hint = "< > Slot";
	else if (disc_focused && m_vm.disc_count > 2)
		view.picker_hint = "< > Disc";

	const PauseMenuRow rows[kRowCount] = {
		{"resume", "Resume", "Back to the game", m_index == 0 && !m_dialog_open, false, false},
		{"save", "Save state", m_vm.can_save ? slot_detail(save_focused) : "Unavailable right now", save_focused,
			!m_vm.can_save, false},
		{"load", "Load state", slot_detail(load_focused), load_focused, !slot_has_state, false},
		{"disc", "Change disc", disc_detail, disc_focused, m_vm.disc_count < 2, false},
		{"return", "Return to library", "Unsaved progress is lost", m_index == 4 && !m_dialog_open, false, true},
	};
	view.rows.assign(rows, rows + kRowCount);

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
