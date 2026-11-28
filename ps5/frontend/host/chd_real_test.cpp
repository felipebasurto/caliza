// Real libchdr host verification: links 3rdparty/libchdr (not chd_stub.c) against
// synthetic CHD images built locally by chdman from patterned/zero data.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "libchdr/chd.h"
#include "ps5/frontend/fe_games.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <malloc.h>
#include <string>
#include <vector>

namespace
{
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

size_t HeapBytes()
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
	const struct mallinfo2 mi = mallinfo2();
	return mi.uordblks + mi.hblkhd;
#else
	return 0;
#endif
}

std::vector<uint8_t> ReadFileBytes(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void TestLzmaDictionaryAndLazyInit(const std::string& chd_path, const std::string& raw_path, const std::string& label)
{
	const std::vector<uint8_t> expected = ReadFileBytes(raw_path);
	Check(!expected.empty(), label + ": loaded synthetic raw fixture");

	const size_t before_open = HeapBytes();
	chd_file* chd = nullptr;
	const chd_error open_err = chd_open(chd_path.c_str(), CHD_OPEN_READ, nullptr, &chd);
	Check(open_err == CHDERR_NONE && chd != nullptr, label + ": chd_open succeeded");
	if (!chd)
		return;

	const chd_header* hdr = chd_get_header(chd);
	Check(hdr != nullptr && hdr->hunkbytes > 0, label + ": valid header");
	const size_t after_open = HeapBytes();
	const size_t open_delta = after_open > before_open ? after_open - before_open : 0;

	// With lazy codec init + fixed dictionary size, opening a small-hunk LZMA/CDLZ CHD
	// must stay far below the old ~256 MiB dictionary reservation.
	constexpr size_t kMaxExpectedDelta = 16u * 1024u * 1024u;
	Check(open_delta < kMaxExpectedDelta,
		label + ": open allocation stayed small (" + std::to_string(open_delta) + " bytes)");

	std::vector<uint8_t> hunk(hdr->hunkbytes, 0);
	const uint32_t hunks_to_check = std::min<uint32_t>(hdr->totalhunks, 4);
	bool data_matches = true;
	for (uint32_t h = 0; h < hunks_to_check; ++h)
	{
		const chd_error read_err = chd_read(chd, h, hunk.data());
		if (read_err != CHDERR_NONE)
		{
			Check(false, label + ": chd_read hunk " + std::to_string(h) + " failed: " + chd_error_string(read_err));
			data_matches = false;
			break;
		}
		const size_t base = static_cast<size_t>(h) * hdr->hunkbytes;
		if (base < expected.size())
		{
			const size_t cmp = std::min<size_t>(hdr->hunkbytes, expected.size() - base);
			if (std::memcmp(hunk.data(), expected.data() + base, cmp) != 0)
			{
				data_matches = false;
				break;
			}
		}
	}
	Check(data_matches, label + ": decoded hunk bytes match synthetic input");

	const size_t after_read = HeapBytes();
	const size_t read_delta = after_read > before_open ? after_read - before_open : 0;
	Check(read_delta < kMaxExpectedDelta,
		label + ": post-read LZMA allocation stayed bounded (" + std::to_string(read_delta) + " bytes, not 256 MiB)");

	chd_close(chd);
}

void TestCdCdlzAllocationAndBytes(const std::string& chd_path, const std::string& raw_2352_path, const std::string& label)
{
	const std::vector<uint8_t> raw = ReadFileBytes(raw_2352_path);
	Check(!raw.empty() && raw.size() % 2352 == 0, label + ": loaded 2352-byte CD fixture");

	const size_t before_open = HeapBytes();
	chd_file* chd = nullptr;
	const chd_error open_err = chd_open(chd_path.c_str(), CHD_OPEN_READ, nullptr, &chd);
	Check(open_err == CHDERR_NONE && chd != nullptr, label + ": chd_open succeeded");
	if (!chd)
		return;

	const chd_header* hdr = chd_get_header(chd);
	Check(hdr != nullptr && hdr->unitbytes == 2448 && hdr->hunkbytes % 2448 == 0, label + ": CD frame geometry");
	const size_t after_open = HeapBytes();
	const size_t open_delta = after_open > before_open ? after_open - before_open : 0;
	constexpr size_t kMaxExpectedDelta = 16u * 1024u * 1024u;
	Check(open_delta < kMaxExpectedDelta,
		label + ": open allocation stayed small (" + std::to_string(open_delta) + " bytes)");

	std::vector<uint8_t> hunk(hdr->hunkbytes, 0);
	const uint32_t frames_per_hunk = hdr->hunkbytes / 2448;
	bool ok = true;
	const chd_error read_err = chd_read(chd, 0, hunk.data());
	Check(read_err == CHDERR_NONE, label + ": chd_read hunk 0 succeeded");
	if (read_err == CHDERR_NONE)
	{
		for (uint32_t f = 0; f < frames_per_hunk && (f + 1) * 2352 <= raw.size(); ++f)
		{
			// Compare user data payload (bytes 24..2071 of Mode 2 sector) since chdman rebuilds sync/ECC.
			if (std::memcmp(hunk.data() + f * 2448 + 24, raw.data() + f * 2352 + 24, 2048) != 0)
			{
				ok = false;
				break;
			}
		}
		Check(ok, label + ": decoded CD sector payloads match synthetic input");
	}
	const size_t after_read = HeapBytes();
	const size_t read_delta = after_read > before_open ? after_read - before_open : 0;
	Check(read_delta < kMaxExpectedDelta,
		label + ": post-read CDLZ allocation stayed bounded (" + std::to_string(read_delta) + " bytes, not 256 MiB)");

	chd_close(chd);
}

void TestUnsupportedCodecRejectedAtOpen(const std::string& chd_path, const std::string& work_dir)
{
	std::vector<uint8_t> bytes = ReadFileBytes(chd_path);
	Check(bytes.size() >= 124, "unsupported codec fixture: source CHD header present");
	if (bytes.size() < 124)
		return;
	// v5 header stores compression[0..3] at bytes 16..31 (4 big-endian uint32 tags).
	bytes[16] = 'X';
	bytes[17] = 'X';
	bytes[18] = 'X';
	bytes[19] = 'X';
	const std::string bad_path = work_dir + "/bad-codec.chd";
	{
		std::ofstream out(bad_path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	chd_file* chd = nullptr;
	const chd_error err = chd_open(bad_path.c_str(), CHD_OPEN_READ, nullptr, &chd);
	Check(err == CHDERR_UNSUPPORTED_FORMAT && chd == nullptr,
		"unsupported codec tag is still rejected at chd_open with CHDERR_UNSUPPORTED_FORMAT");
	if (chd)
		chd_close(chd);
}

void TestTruncatedChdRejected(const std::string& chd_path, const std::string& work_dir)
{
	std::vector<uint8_t> bytes = ReadFileBytes(chd_path);
	if (bytes.size() <= 140)
		return;
	bytes.resize(bytes.size() / 2);
	const std::string trunc_path = work_dir + "/truncated.chd";
	{
		std::ofstream out(trunc_path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	chd_file* chd = nullptr;
	const chd_error err = chd_open(trunc_path.c_str(), CHD_OPEN_READ, nullptr, &chd);
	bool failed = (err != CHDERR_NONE);
	if (!failed && chd)
	{
		const chd_header* hdr = chd_get_header(chd);
		std::vector<uint8_t> hunk(hdr ? hdr->hunkbytes : 4096, 0);
		for (uint32_t h = 0; hdr && h < hdr->totalhunks; ++h)
		{
			if (chd_read(chd, h, hunk.data()) != CHDERR_NONE)
			{
				failed = true;
				break;
			}
		}
		chd_close(chd);
	}
	Check(failed, "truncated CHD fails open or read cleanly");
}
void TestParentChildAndErrorSentences(const std::string& parent_chd, const std::string& child_with_parent,
	const std::string& child_orphan, const std::string& work_dir)
{
	fe::SetSerialCacheFile("");
	std::string reason;
	const std::string parent_serial = fe::ReadSerial(parent_chd, &reason);
	Check(parent_serial == "SLUS-20010", "real CHD parent serial read via libchdr (" + parent_serial + ")");

	reason.clear();
	const std::string child_serial = fe::ReadSerial(child_with_parent, &reason);
	Check(child_serial == "SLUS-20010", "real child CHD resolves sibling parent and reads serial (" + child_serial + ")");

	reason.clear();
	const std::string orphan_serial = fe::ReadSerial(child_orphan, &reason);
	bool orphan_launchable = true;
	const std::string orphan_problem = fe::FormatImageProblem(reason, &orphan_launchable);
	Check(orphan_serial.empty(), "parent-less child CHD has no serial");
	Check(!orphan_launchable && orphan_problem == "This CHD needs its parent file in the same folder.",
		"parent-less child CHD gets clear missing-parent sentence: " + orphan_problem);

	reason.clear();
	bool bad_codec_launchable = true;
	(void)fe::ReadSerial(work_dir + "/bad-codec.chd", &reason);
	const std::string bad_codec_problem = fe::FormatImageProblem(reason, &bad_codec_launchable);
	Check(!bad_codec_launchable && bad_codec_problem == "This CHD uses a compression or version Caliza cannot read.",
		"unsupported codec CHD gets clear user-facing sentence: " + bad_codec_problem);
}
} // namespace

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		std::cerr << "usage: chd_real_test <work_dir> <dvd_lzma.chd> <dvd_raw.bin> <cd_cdlz.chd> [cd_raw.bin] [iso_parent.chd] [iso_child.chd] [orphan_child.chd]\n";
		return 2;
	}
	const std::string work_dir = argv[1];
	const std::string dvd_chd = argv[2];
	const std::string dvd_raw = argv[3];
	const std::string cd_chd = argv[4];
	const std::string cd_raw = argc >= 6 ? argv[5] : std::string();

	TestLzmaDictionaryAndLazyInit(dvd_chd, dvd_raw, "DVD lzma");
	if (!cd_raw.empty())
		TestCdCdlzAllocationAndBytes(cd_chd, cd_raw, "CD cdlz");
	TestUnsupportedCodecRejectedAtOpen(dvd_chd, work_dir);
	TestTruncatedChdRejected(dvd_chd, work_dir);
	if (argc >= 9)
		TestParentChildAndErrorSentences(argv[6], argv[7], argv[8], work_dir);

	if (g_failures != 0)
	{
		std::cerr << "chd_real_test: " << g_failures << " failure(s)\n";
		return 1;
	}
	std::cout << "chd_real_test: PASS\n";
	return 0;
}
