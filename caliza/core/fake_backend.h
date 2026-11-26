// Caliza PS4 core: deterministic in-memory backend for contract tests and host preview.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "contract.h"
#include "data_root.h"

#include <cstdint>
#include <memory>
#include <string>

namespace caliza
{
struct FakeBackendOptions
{
	std::string data_root = "/data/caliza";
	std::string legacy_data_root = dataroot::LegacyDataRoot();
	bool install_default_layout = true;
	bool bios_present = true;
	uint64_t initial_time_unix = 1700000000;
	// Zero is as fast as the worker can cooperatively scan. Tests can add microseconds to make cancellation
	// and disconnect windows easy to reach without sleeping in the caller thread.
	uint32_t scan_step_delay_microseconds = 0;
};

// A thread-safe in-memory implementation of every command in contract.h. It is deliberately honest about being
// a fake: VM transitions, memory cards, settings, controls, reports and storage are contract-level state only.
// It exists so presentation/host tests can exercise success, failure, cancellation, empty, damaged and missing-
// volume flows before the console adapter exists.
class FakeBackend final : public Backend
{
public:
	explicit FakeBackend(const FakeBackendOptions& options = {});
	~FakeBackend() override;

	FakeBackend(const FakeBackend&) = delete;
	FakeBackend& operator=(const FakeBackend&) = delete;

	Snapshot GetSnapshot() override;
	CommandId Post(const Command& command) override;
	std::vector<CommandEvent> PollEvents() override;
	FolderListing BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit) override;

	// Fake-environment controls. These are thread-safe and publish a new revision when they change visible state.
	// Paths inside a volume are POSIX-style relative paths ("games/Foo (USA).iso"); full paths are accepted too.
	void AddVolume(const VolumeInfo& volume);
	void AddDirectory(const std::string& volume_id, const std::string& path);
	std::string AddFile(const std::string& volume_id, const std::string& path, uint64_t bytes, bool damaged = false,
		const std::string& title = std::string(), const std::string& serial = std::string(), const std::string& region = std::string());
	void RemoveFile(const std::string& volume_id, const std::string& path);
	void SetVolumePresent(const std::string& volume_id, bool present);
	void SetVolumeReadable(const std::string& volume_id, bool readable);
	void SetBiosPresent(bool present);
	void SetScanStepDelayMicroseconds(uint32_t microseconds);
	void FailNextCommand(CommandType type, const std::string& message);

private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};

std::unique_ptr<Backend> CreateFakeBackend(const FakeBackendOptions& options = {});
} // namespace caliza
