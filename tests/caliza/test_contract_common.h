// Shared helpers for the Caliza contract/storage/migration host tests.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../../caliza/core/backend.h"
#include "../../caliza/core/contract.h"
#include "../../caliza/core/fake_backend.h"

#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace caliza_test
{
inline int& Failures()
{
	static int failures = 0;
	return failures;
}

inline void Fail(const char* file, int line, const std::string& message)
{
	std::cerr << file << ':' << line << ": CHECK failed: " << message << '\n';
	++Failures();
}

#define CHECK_TRUE(expr) \
	do \
	{ \
		if (!(expr)) \
			caliza_test::Fail(__FILE__, __LINE__, #expr); \
	} while (0)
#define CHECK_FALSE(expr) CHECK_TRUE(!(expr))
#define CHECK_EQ(actual, expected) \
	do \
	{ \
		const auto caliza_actual = (actual); \
		const auto caliza_expected = (expected); \
		if (!(caliza_actual == caliza_expected)) \
		{ \
			std::ostringstream caliza_out; \
			caliza_out << #actual " != " #expected " (" << caliza_actual << " vs " << caliza_expected << ")"; \
			caliza_test::Fail(__FILE__, __LINE__, caliza_out.str()); \
		} \
	} while (0)

inline void RemoveTree(const std::string& path)
{
	struct stat st = {};
	if (lstat(path.c_str(), &st) != 0)
		return;
	if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
	{
		unlink(path.c_str());
		return;
	}
	chmod(path.c_str(), 0700);
	if (DIR* dir = opendir(path.c_str()))
	{
		while (const dirent* entry = readdir(dir))
		{
			if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, ".."))
				continue;
			RemoveTree(path + "/" + entry->d_name);
		}
		closedir(dir);
	}
	rmdir(path.c_str());
}

class TempDir
{
public:
	TempDir()
	{
		char pattern[] = "/tmp/caliza-test-XXXXXX";
		char* made = mkdtemp(pattern);
		if (made)
			path = made;
	}
	~TempDir()
	{
		if (!path.empty())
			RemoveTree(path);
	}
	TempDir(const TempDir&) = delete;
	TempDir& operator=(const TempDir&) = delete;
	std::string path;
};

inline bool WriteFile(const std::string& path, const std::string& data)
{
	const std::string parent = path.substr(0, path.rfind('/'));
	std::string error;
	if (!caliza::storage::fs::MakeDirs(parent, &error))
		return false;
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(data.data(), static_cast<std::streamsize>(data.size()));
	return static_cast<bool>(out);
}

inline std::string ReadFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline void WriteLe16(std::vector<char>& image, size_t offset, uint16_t value)
{
	image[offset] = static_cast<char>(value & 0xff);
	image[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

inline void WriteBe16(std::vector<char>& image, size_t offset, uint16_t value)
{
	image[offset] = static_cast<char>((value >> 8) & 0xff);
	image[offset + 1] = static_cast<char>(value & 0xff);
}

inline void WriteLe32(std::vector<char>& image, size_t offset, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++)
		image[offset + i] = static_cast<char>((value >> (i * 8)) & 0xff);
}

inline void WriteBe32(std::vector<char>& image, size_t offset, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++)
		image[offset + i] = static_cast<char>((value >> ((3 - i) * 8)) & 0xff);
}

inline size_t WriteIsoDirRecord(std::vector<char>& image, size_t offset, uint32_t lba, uint32_t size, uint8_t flags,
	const std::string& name)
{
	size_t length = 33 + name.size();
	if (length & 1)
		length++;
	image[offset] = static_cast<char>(length);
	image[offset + 1] = 0;
	WriteLe32(image, offset + 2, lba);
	WriteBe32(image, offset + 6, lba);
	WriteLe32(image, offset + 10, size);
	WriteBe32(image, offset + 14, size);
	image[offset + 18] = 126; // year 2026 in ISO9660's years-since-1900 field; exact time is irrelevant here
	image[offset + 25] = static_cast<char>(flags);
	WriteLe16(image, offset + 28, 1);
	WriteBe16(image, offset + 30, 1);
	image[offset + 32] = static_cast<char>(name.size());
	std::memcpy(image.data() + offset + 33, name.data(), name.size());
	return offset + length;
}

// Creates the smallest ISO9660-like image needed by the portable scanner tests: a primary volume descriptor,
// a root directory, and SYSTEM.CNF naming SLUS_200.10. It is test data only, not a bootable/proprietary game image.
inline void MakeMinimalIso(const std::string& path, size_t sectors = 20)
{
	std::vector<char> image(std::max<size_t>(sectors, 20) * 2048, 0);
	const size_t pvd = 16 * 2048;
	image[pvd] = 1;
	std::memcpy(image.data() + pvd + 1, "CD001", 5);
	image[pvd + 6] = 1;
	WriteIsoDirRecord(image, pvd + 156, 17, 2048, 2, std::string(1, '\0'));

	size_t at = 17 * 2048;
	at = WriteIsoDirRecord(image, at, 17, 2048, 2, std::string(1, '\0'));
	at = WriteIsoDirRecord(image, at, 17, 2048, 2, std::string(1, '\1'));
	const std::string cnf = "BOOT2 = cdrom0:\\SLUS_200.10;1\r\n";
	at = WriteIsoDirRecord(image, at, 18, static_cast<uint32_t>(cnf.size()), 0, "SYSTEM.CNF;1");
	std::memcpy(image.data() + 18 * 2048, cnf.data(), cnf.size());

	std::string error;
	caliza::storage::fs::MakeDirs(path.substr(0, path.rfind('/')), &error);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(image.data(), static_cast<std::streamsize>(image.size()));
}

inline bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms = 5000)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (predicate())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return predicate();
}

struct EventLog
{
	std::vector<caliza::CommandEvent> events;

	void Pump(caliza::Backend& backend)
	{
		std::vector<caliza::CommandEvent> fresh = backend.PollEvents();
		events.insert(events.end(), fresh.begin(), fresh.end());
	}

	const caliza::CommandEvent* Terminal(caliza::CommandId id) const
	{
		for (const caliza::CommandEvent& event : events)
		{
			if (event.command == id &&
				(event.status == caliza::CommandStatus::Succeeded || event.status == caliza::CommandStatus::Failed ||
					event.status == caliza::CommandStatus::Cancelled))
				return &event;
		}
		return nullptr;
	}

	const caliza::CommandEvent* WaitTerminal(caliza::Backend& backend, caliza::CommandId id, int timeout_ms = 5000)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
		while (std::chrono::steady_clock::now() < deadline)
		{
			Pump(backend);
			if (const caliza::CommandEvent* event = Terminal(id))
				return event;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		Pump(backend);
		return Terminal(id);
	}
};

inline size_t CountTerminalEvents(const std::vector<caliza::CommandEvent>& events, caliza::CommandId id)
{
	size_t count = 0;
	for (const caliza::CommandEvent& event : events)
	{
		if (event.command == id &&
			(event.status == caliza::CommandStatus::Succeeded || event.status == caliza::CommandStatus::Failed ||
				event.status == caliza::CommandStatus::Cancelled))
			count++;
	}
	return count;
}
} // namespace caliza_test
