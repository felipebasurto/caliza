// Host test for ps5/frontend/fe_patchdl.cpp: the patches/cheats folder-separation fix and the one-time migration
// of files a pre-fix build left in the top data folder. Run:
//   g++ -std=c++17 -I ps5/frontend ps5/coreorbis/tests/patchdl/test_patchdl.cpp ps5/frontend/fe_patchdl.cpp -o /tmp/a.out
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_patchdl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

// fe_patchdl.cpp's OnlinePatches::DiscCrc() calls this (fe_games.cpp, not linked here); FetchWithCrc() bypasses it,
// which is what every test below uses, so a stub that is never actually exercised is enough to satisfy the linker.
namespace fe
{
bool ReadAchievementExecutable(const std::string&, std::string&, std::vector<uint8_t>&)
{
	return false;
}
} // namespace fe

namespace
{
int g_fails = 0;

void Check(bool ok, const std::string& what)
{
	std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok)
		g_fails++;
}

void MakeDirs(const std::string& path)
{
	std::string cur;
	size_t pos = 0;
	while (pos <= path.size())
	{
		const size_t slash = path.find('/', pos);
		const std::string part = path.substr(0, slash == std::string::npos ? path.size() : slash);
		if (!part.empty())
			mkdir(part.c_str(), 0777);
		if (slash == std::string::npos)
			break;
		pos = slash + 1;
	}
}

bool IsDir(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool FileExists(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string ReadWhole(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool WriteWhole(const std::string& path, const std::string& data)
{
	const size_t slash = path.rfind('/');
	if (slash != std::string::npos)
		MakeDirs(path.substr(0, slash));
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(data.data(), static_cast<std::streamsize>(data.size()));
	return static_cast<bool>(out);
}

std::string TempDir(const char* suffix)
{
	char tmpl[128];
	std::snprintf(tmpl, sizeof(tmpl), "/tmp/patchdl-test-%s-XXXXXX", suffix);
	return mkdtemp(tmpl);
}

int FakeGetText(const std::string& url, std::string& body)
{
	if (url.find("pcsx2_patches") != std::string::npos)
	{
		body = "[60 FPS]\npatch=1,EE,00000000,extended,00000001\n";
		return 200;
	}
	if (url.find("Gabominated") != std::string::npos)
	{
		body = "[Widescreen]\npatch=1,EE,00000000,extended,00000002\n";
		return 200;
	}
	if (url.find("pcsx2_cheats_collection") != std::string::npos)
	{
		body = "[Infinite HP]\npatch=1,EE,00000000,extended,00000003\n";
		return 200;
	}
	return 404;
}

// The fix: with patches_dir and cheats_dir genuinely different (main-boot.cpp no longer resolves either through
// OrbisDir(), which used to fall back to the same top folder for both while neither subfolder existed), a fetch
// creates each folder before writing and never makes the cheats file collide with the patches file.
void TestSeparateFoldersNoCollision()
{
	const std::string root = TempDir("sep");
	const std::string patches_dir = root + "/patches", cheats_dir = root + "/cheats";
	Check(!IsDir(patches_dir) && !IsDir(cheats_dir), "neither folder exists yet");

	fe::OnlinePatchPlatform platform;
	platform.get_text = FakeGetText;
	fe::OnlinePatches patches(platform, patches_dir, cheats_dir, root + "/cache/online-patches.txt");
	const fe::OnlinePatchStatus result = patches.FetchWithCrc("SLUS-20010", 0xDEADBEEF);
	Check(result.state == fe::OnlinePatchStatus::State::Done, "fetch reports done: " + result.message);

	Check(IsDir(patches_dir), "patches/ was created before writing");
	Check(IsDir(cheats_dir), "cheats/ was created before writing");
	const std::string patch_file = patches_dir + "/SLUS-20010_DEADBEEF.pnach";
	const std::string gabo_file = patches_dir + "/SLUS-20010_DEADBEEF_gabominated.pnach";
	const std::string cheat_file = cheats_dir + "/SLUS-20010_DEADBEEF.pnach";
	Check(FileExists(patch_file), "the PCSX2 patch file is in patches/");
	Check(FileExists(gabo_file), "the Gabominated patch file is in patches/");
	Check(FileExists(cheat_file), "the cheat file is in cheats/");
	Check(!FileExists(root + "/SLUS-20010_DEADBEEF.pnach"), "nothing was written at the top folder");
	const std::string patch_text = ReadWhole(patch_file), cheat_text = ReadWhole(cheat_file);
	Check(patch_text.find("60 FPS") != std::string::npos, "the patches file holds the PCSX2 source's content");
	Check(cheat_text.find("Infinite HP") != std::string::npos, "the cheats file holds the cheats source's content");
	Check(patch_text != cheat_text, "the cheats fetch did not overwrite the patches file");
}

// A Gabominated-suffixed file is unambiguous (no other source ever writes that suffix), so a copy left at the top
// folder by a pre-fix build is moved into patches/ once, and the manifest follows it.
void TestMigratesUnambiguousGabominatedFile()
{
	const std::string root = TempDir("migrate-gabo");
	const std::string patches_dir = root + "/patches", cheats_dir = root + "/cheats";
	const std::string stray = root + "/SLUS-20010_DEADBEEF_gabominated.pnach";
	const std::string manifest_path = root + "/cache/online-patches.txt";
	Check(WriteWhole(stray, "[Widescreen (Gabominated)]\npatch=1,EE,00000000,extended,00000002\n"), "wrote the stray file");
	Check(WriteWhole(manifest_path, stray + "\tsomefingerprint:1\n"), "wrote a manifest naming it");

	fe::OnlinePatchPlatform platform;
	fe::OnlinePatches patches(platform, patches_dir, cheats_dir, manifest_path);
	(void)patches;

	const std::string moved = patches_dir + "/SLUS-20010_DEADBEEF_gabominated.pnach";
	Check(!FileExists(stray), "the stray file is gone from the top folder");
	Check(FileExists(moved), "it is now in patches/");
	Check(ReadWhole(moved).find("Widescreen") != std::string::npos, "its content is unchanged");
	const std::string manifest_text = ReadWhole(manifest_path);
	Check(manifest_text.find(moved) != std::string::npos, "the manifest now names the new path");
	Check(manifest_text.find(stray) == std::string::npos, "the manifest no longer names the old path");
}

// A plain "<serial>_<crc>.pnach" at the top folder could have been written by either the PCSX2 patches source or
// the cheats source (both use that exact name, one just overwriting the other before this fix): not safe to tell
// apart, so it must be left exactly where it is instead of guessed into the wrong folder.
void TestLeavesAmbiguousFileInPlace()
{
	const std::string root = TempDir("ambiguous");
	const std::string patches_dir = root + "/patches", cheats_dir = root + "/cheats";
	const std::string stray = root + "/SLUS-20010_DEADBEEF.pnach";
	const std::string manifest_path = root + "/cache/online-patches.txt";
	Check(WriteWhole(stray, "[60 FPS]\npatch=1,EE,00000000,extended,00000001\n"), "wrote the stray file");
	Check(WriteWhole(manifest_path, stray + "\tsomefingerprint:1\n"), "wrote a manifest naming it");

	fe::OnlinePatchPlatform platform;
	fe::OnlinePatches patches(platform, patches_dir, cheats_dir, manifest_path);
	(void)patches;

	Check(FileExists(stray), "the ambiguous file is left at the top folder");
	Check(!FileExists(patches_dir + "/SLUS-20010_DEADBEEF.pnach"), "it was not guessed into patches/");
	Check(!FileExists(cheats_dir + "/SLUS-20010_DEADBEEF.pnach"), "it was not guessed into cheats/");
}

} // namespace

int main()
{
	TestSeparateFoldersNoCollision();
	TestMigratesUnambiguousGabominatedFile();
	TestLeavesAmbiguousFileInPlace();
	std::printf(g_fails ? "%d FAILED\n" : "all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
