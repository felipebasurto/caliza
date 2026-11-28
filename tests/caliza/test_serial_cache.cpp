// Host test for fe::ReadSerial negative/positive caching and single-open inspect behavior.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5/frontend/fe_games.h"
#include "libchdr/chd.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

namespace
{
int g_chd_open_calls = 0;
int g_failures = 0;

void Check(bool ok, const std::string& msg)
{
	if (!ok)
	{
		std::cerr << "FAIL: " << msg << "\n";
		g_failures++;
	}
	else
	{
		std::cout << "  ok    " << msg << "\n";
	}
}

void WriteBytes(const std::string& path, const std::string& data)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(data.data(), static_cast<std::streamsize>(data.size()));
}
} // namespace

// Counting libchdr stub (replaces chd_stub.c for this test binary).
extern "C" {
chd_error chd_open(const char*, int, chd_file*, chd_file** chd)
{
	++g_chd_open_calls;
	*chd = nullptr;
	return CHDERR_INVALID_DATA;
}
void chd_close(chd_file*) {}
const char* chd_error_string(chd_error err)
{
	return err == CHDERR_INVALID_DATA ? "invalid data" : "error";
}
const chd_header* chd_get_header(chd_file*) { return nullptr; }
chd_error chd_read_header(const char*, chd_header*) { return CHDERR_INVALID_DATA; }
chd_error chd_read(chd_file*, uint32_t, void*) { return CHDERR_INVALID_FILE; }
chd_error chd_get_metadata(chd_file*, uint32_t, uint32_t, void*, uint32_t, uint32_t*, uint32_t*, uint8_t*)
{
	return CHDERR_METADATA_NOT_FOUND;
}
}

int main()
{
	char tmpl[] = "/tmp/caliza-serial-cache-XXXXXX";
	const char* dir = mkdtemp(tmpl);
	Check(dir != nullptr, "created temp dir");
	if (!dir)
		return 1;
	const std::string root(dir);
	const std::string cache = root + "/chd-serials.txt";
	const std::string chd = root + "/Truncated.chd";
	const std::string cso = root + "/Broken.cso";
	const std::string zso = root + "/Legacy4Col.zso";

	WriteBytes(chd, "MComprHD");
	WriteBytes(cso, "CISO1234");
	WriteBytes(zso, "ZISO");

	// Seed a legacy 4-column cache entry for Legacy4Col.zso to verify backward compatibility.
	struct stat zst = {};
	Check(stat(zso.c_str(), &zst) == 0, "stat legacy fixture");
	WriteBytes(cache, zso + "\t" + std::to_string(static_cast<long long>(zst.st_size)) + "\t" +
	                  std::to_string(static_cast<long long>(zst.st_mtime)) + "\tSLUS-20099\n");

	fe::SetSerialCacheFile(cache);

	// Legacy 4-column entry loads cleanly.
	std::string reason = "sentinel";
	Check(fe::ReadSerial(zso, &reason) == "SLUS-20099", "legacy 4-column cache line is read");
	Check(reason.empty(), "legacy 4-column cache line has empty reason");

	// First failed CHD open populates cache and returns the reason in a single open.
	g_chd_open_calls = 0;
	reason.clear();
	Check(fe::ReadSerial(chd, &reason).empty(), "failed CHD returns empty serial");
	Check(g_chd_open_calls == 1, "first ReadSerial opens CHD once");
	Check(reason.find("invalid data") != std::string::npos, "first ReadSerial returns libchdr reason");

	// Second call hits in-memory negative cache without re-opening.
	std::string reason2;
	Check(fe::ReadSerial(chd, &reason2).empty(), "second ReadSerial returns empty serial");
	Check(g_chd_open_calls == 1, "second ReadSerial does not re-open CHD");
	Check(reason2 == reason, "second ReadSerial returns cached reason");

	// Reload from disk (simulates process restart): negative CHD entry still hits cache.
	fe::SetSerialCacheFile("");
	fe::SetSerialCacheFile(cache);
	std::string reason3;
	Check(fe::ReadSerial(chd, &reason3).empty(), "reloaded ReadSerial returns empty serial");
	Check(g_chd_open_calls == 1, "reloaded ReadSerial hits on-disk negative cache without re-opening");
	Check(reason3 == reason, "reloaded ReadSerial preserves cached reason");

	// Changing mtime invalidates the cache and triggers a fresh open.
	struct stat cst = {};
	Check(stat(chd.c_str(), &cst) == 0, "stat CHD");
	struct utimbuf ut = {cst.st_atime, cst.st_mtime + 10};
	Check(utime(chd.c_str(), &ut) == 0, "bump CHD mtime");
	std::string reason4;
	Check(fe::ReadSerial(chd, &reason4).empty(), "ReadSerial after mtime change runs again");
	Check(g_chd_open_calls == 2, "mtime change invalidates negative cache entry");

	// Changing size also invalidates the cache.
	WriteBytes(chd, "MComprHD12345678");
	std::string reason5;
	(void)fe::ReadSerial(chd, &reason5);
	Check(g_chd_open_calls == 3, "size change invalidates negative cache entry");

	// CSO negative cache persists across reload (verify by revoking read permission before reload).
	std::string cso_reason;
	Check(fe::ReadSerial(cso, &cso_reason).empty(), "truncated CSO returns empty serial");
	Check(!cso_reason.empty(), "truncated CSO returns failure reason");
	Check(chmod(cso.c_str(), 0000) == 0, "make CSO unreadable to prove cache hit avoids open");
	fe::SetSerialCacheFile("");
	fe::SetSerialCacheFile(cache);
	std::string cso_reason2;
	Check(fe::ReadSerial(cso, &cso_reason2).empty(), "reloaded CSO hits negative cache");
	Check(cso_reason2 == cso_reason, "reloaded CSO returns cached reason without re-opening");
	(void)chmod(cso.c_str(), 0644);

	// Verify user-facing error translation for CHD/CSO/ZSO failure reasons.
	bool can_launch = true;
	Check(fe::FormatImageProblem("libchdr can't open it: invalid data", &can_launch) == "This CHD is cut short or damaged." && !can_launch,
		"invalid data maps to clear damaged CHD sentence");
	Check(fe::FormatImageProblem("libchdr can't open it: requires parent", &can_launch) == "This CHD needs its parent file in the same folder." && !can_launch,
		"requires parent maps to clear missing-parent sentence");
	Check(fe::FormatImageProblem("libchdr can't open it: unsupported format", &can_launch) == "This CHD uses a compression or version Caliza cannot read." && !can_launch,
		"unsupported format maps to clear unsupported-compression sentence");
	Check(fe::FormatImageProblem("unsupported CSO version", &can_launch).find("unsupported version") != std::string::npos && !can_launch,
		"unsupported CSO version maps to clear sentence");
	Check(fe::FormatImageProblem("v5, 2048-byte units, 16384-byte hunks", &can_launch).empty() && can_launch,
		"non-error CHD description is not treated as damaged");

	(void)unlink(chd.c_str());
	(void)unlink(cso.c_str());
	(void)unlink(zso.c_str());
	(void)unlink(cache.c_str());
	(void)rmdir(root.c_str());

	if (g_failures != 0)
	{
		std::cerr << "test_serial_cache: " << g_failures << " failure(s)\n";
		return 1;
	}
	std::cout << "test_serial_cache: PASS\n";
	return 0;
}
