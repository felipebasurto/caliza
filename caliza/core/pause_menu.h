// Caliza runtime pause menu: deterministic controller-first model for the in-game overlay.
// The model owns no VM, save or boot logic: it navigates a small status surface and posts
// contract commands (SaveState/LoadState with slot picker, ChangeDisc, PauseVm, ResumeVm,
// ReturnToLibrary) through the narrow backend the platform runtime implements. Host tests drive
// it exactly as the console pad thread does: raw input levels, vm status snapshots and command
// events.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "contract.h"

#include <string>
#include <vector>

namespace caliza
{
// The runtime command sink the pause menu posts to. The platform implementation queues the
// command for the CPU thread that owns the VM; Post never blocks the input thread.
class PauseMenuBackend
{
public:
	virtual ~PauseMenuBackend() = default;
	virtual CommandId Post(const Command& command) = 0;
};

struct PauseMenuRow
{
	std::string id; // resume, save, load, disc, return
	std::string label;
	std::string detail;
	bool focused = false;
	bool disabled = false;
	bool destructive = false;
};

struct PauseMenuView
{
	bool open = false;
	std::string title;       // the running game's title
	std::string subtitle;    // "Paused", or why an action is unavailable
	std::string picker_hint; // non-empty when Left/Right adjusts a slot or target disc
	std::vector<PauseMenuRow> rows;
	bool dialog_open = false;
	std::string dialog_title;
	std::string dialog_body;
	std::string cancel_label;
	std::string confirm_label;
	int dialog_focus = 0; // 0 cancel, 1 confirm; the destructive dialog always starts on cancel
	std::string toast;
	uint64_t revision = 0; // bumped on every visible change, for the renderer's recomposition
};

// Deterministic: identical vm/event/input/dt sequences produce identical views and posted
// commands. Input is consumed as levels; edges and direction repeat are generated inside,
// with the same semantics as the shell's UI model.
class PauseMenu
{
public:
	explicit PauseMenu(PauseMenuBackend* backend = nullptr);

	void SetBackend(PauseMenuBackend* backend) { m_backend = backend; }

	// Opens the overlay and posts PauseVm when the vm is running. Safe to call when already open.
	void Open(const std::string& title);
	// Closes the overlay, posting ResumeVm when the vm is paused (or a PauseVm was already queued).
	// Used by the platform when the session ends externally (vm stopped, shutdown) with resume = false.
	void Close(bool resume, const char* reason = "close");
	// Resets all session-scoped menu state (including selected slot and disc target).
	void Reset();

	void Tick(const VmStatus& vm, const std::vector<CommandEvent>& events, const Input& input, double dt);
	PauseMenuView BuildView() const;

	bool IsOpen() const { return m_open; }
	int FocusIndexForTest() const { return m_index; }
	int SelectedSlot() const { return m_slot; }
	int TargetDisc() const { return m_target_disc; }
	const std::string& LastCloseReason() const { return m_close_reason; }

	// Save-state slot range (1..10); slot 1 is the default, matching the quick-save shortcut.
	static constexpr int kMinSlot = 1;
	static constexpr int kMaxSlot = 10;
	static constexpr int kSlot = kMinSlot;
	static constexpr int kRowCount = 5;

private:
	struct RepeatState
	{
		bool held = false;
		double held_seconds = 0.0;
		double since_last = 0.0;
	};

	struct LogicalInput
	{
		bool up = false, down = false, left = false, right = false;
		bool cross = false, circle = false, options = false;
		bool l1 = false, r1 = false;
	};

	LogicalInput EvaluateInput(const Input& input, double dt);
	bool SlotHasState(int slot) const;
	void SyncTargetDisc();
	void StepSlot(int delta);
	void StepDisc(int delta);
	void ActivateRow();
	void ExecuteDialog();
	void PostCommand(CommandType type, int number = 0);
	void SetToast(std::string text);
	static int MoveClamped(int index, int delta, int count);

	PauseMenuBackend* m_backend = nullptr;
	bool m_open = false;
	bool m_wait_release = false; // no edges until every button is up: the opening press is not an action
	int m_index = 0;
	int m_slot = kMinSlot;
	int m_target_disc = 0;
	bool m_dialog_open = false;
	int m_dialog_focus = 0; // 0 cancel, 1 confirm
	std::string m_title;
	std::string m_close_reason;
	VmStatus m_vm;
	Input m_previous_input;
	RepeatState m_repeat[4]; // left, right, up, down
	std::string m_toast;
	double m_toast_seconds = 0.0;
	double m_unpaused_seconds = 0.0; // time open while the vm is still running: a pause that never lands closes the overlay
	uint64_t m_revision = 0;
	bool m_return_posted = false;
	bool m_pause_posted = false; // PauseVm is posted on the first tick that sees the vm running
};
} // namespace caliza
