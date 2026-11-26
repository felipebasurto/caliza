// Caliza Estratos host preview: deterministic fictional backend data.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/contract.h"

#include <vector>

namespace caliza
{
class DemoBackend final : public Backend
{
public:
	DemoBackend();
	Snapshot GetSnapshot() override;
	CommandId Post(const Command& command) override;
	std::vector<CommandEvent> PollEvents() override;
	FolderListing BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit) override;

	void SetTextScale(uint32_t percent);
	void SetHighContrast(bool enabled);
	void SetReduceMotion(bool enabled);
	void SetBiosPresent(bool present);
	void SetLibraryEmpty(bool empty);
	const std::vector<Command>& posted() const { return m_posted; }

private:
	void Bump();
	Snapshot m_snapshot;
	std::vector<CommandEvent> m_events;
	std::vector<Command> m_posted;
	CommandId m_next_command = 1;
};

Snapshot MakeDemoSnapshot();
} // namespace caliza
