// Host tests for ps5/frontend/fe_covers.cpp: the pieces of the cover-art pipeline that can be exercised
// without a PS4 -- URL building from the ${serial} template, cache-folder creation before any write, the
// manual/beside/drive/cache/art lookup order, the 404 "don't ask again for two weeks" marker, and Prefetch's
// retry/stop-on-network-failure behavior. CoverService::Decode is exercised with a real, tiny JPEG (not a
// stub) so a successful "download" actually has to survive stb_image.
//
// Run (see tests/caliza/run-all.sh): g++ -std=c++17 -Ips5/frontend tests/caliza/test_covers.cpp
// ps5/frontend/fe_covers.cpp ps5/frontend/fe_text.cpp ps5/frontend/fe_i18n.cpp -pthread -o /tmp/test_covers
// && /tmp/test_covers
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_covers.h"
#include "fe_games.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <utime.h>

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
	for (size_t i = 0; i < path.size(); i++)
	{
		cur += path[i];
		if ((path[i] == '/' && i > 0) || i + 1 == path.size())
			mkdir(cur.c_str(), 0777);
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
	std::snprintf(tmpl, sizeof(tmpl), "/tmp/covers-test-%s-XXXXXX", suffix);
	return mkdtemp(tmpl);
}

// A real, tiny (64x96) JPEG -- ffmpeg's `color=c=blue` test pattern -- so a successful download has to
// survive the same stb_image decode a real cover does, not just a byte-count check.
const unsigned char kTinyJpeg[] = {
	0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x02, 0x00, 0x00,
	0x01, 0x00, 0x01, 0x00, 0x00, 0xff, 0xfe, 0x00, 0x10, 0x4c, 0x61, 0x76, 0x63, 0x36, 0x30,
	0x2e, 0x33, 0x31, 0x2e, 0x31, 0x30, 0x32, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x08, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x07,
	0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x08, 0x08,
	0x08, 0x08, 0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0c, 0x0c, 0x0b, 0x0b, 0x0e, 0x0e, 0x0e, 0x11,
	0x11, 0x14, 0xff, 0xc4, 0x00, 0x4d, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x01, 0x01, 0x01, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x07,
	0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x60, 0x00, 0x40,
	0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00, 0xff, 0xda, 0x00, 0x0c, 0x03,
	0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0x8e, 0x00, 0xdf, 0xd2, 0xc0, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x01, 0xff, 0xd9};

std::vector<uint8_t> TinyJpegBytes()
{
	return std::vector<uint8_t>(kTinyJpeg, kTinyJpeg + sizeof(kTinyJpeg));
}

fe::GameInfo MakeGame(const std::string& serial, const std::string& path = "/data/caliza/games/Example Game.iso")
{
	fe::GameInfo g;
	g.path = path;
	g.file = path.substr(path.rfind('/') + 1);
	const size_t dot = g.file.find_last_of('.');
	g.stem = dot == std::string::npos ? g.file : g.file.substr(0, dot);
	g.title = g.stem;
	g.serial = serial;
	return g;
}

// The ${serial} placeholder in the production URL template (fe_ps5.cpp's kCoverUrl) is replaced with the
// game's exact serial, and only the first occurrence (there is only one in the real template, but
// Prefetch and DownloadCover both use std::string::replace on the first match found).
void TestUrlBuilding()
{
	const std::string root = TempDir("url");
	fe::CoverConfig cc;
	cc.cache_dir = root + "/cache";
	cc.url_template = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/${serial}.jpg";

	std::vector<std::string> urls_seen;
	const fe::DownloadFn capture = [&](const std::string& url, std::vector<uint8_t>& out) {
		urls_seen.push_back(url);
		out.clear();
		return 404; // the URL is what this test cares about; keep it simple
	};
	const fe::GameInfo g = MakeGame("SLUS-21351");
	fe::CoverService::Prefetch({g}, {0}, cc, capture, 10.0);

	Check(urls_seen.size() == 1, "one request was made");
	Check(!urls_seen.empty() && urls_seen[0] == "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/SLUS-21351.jpg",
		"the ${serial} placeholder became the game's exact serial: " + (urls_seen.empty() ? std::string("<none>") : urls_seen[0]));
}

// Prefetch creates cache_dir before writing anything into it (a fresh console with no cache/covers/ yet
// must not fail the first download).
void TestCacheFolderCreatedBeforeWrite()
{
	const std::string root = TempDir("mkdir");
	const std::string cache_dir = root + "/cache/covers"; // nested: the real path is OrbisDir("cache") + "/covers"
	Check(!IsDir(cache_dir), "the cache folder does not exist yet");

	fe::CoverConfig cc;
	cc.cache_dir = cache_dir;
	cc.url_template = "https://example.invalid/${serial}.jpg";
	const fe::DownloadFn ok_download = [](const std::string&, std::vector<uint8_t>& out) {
		out = TinyJpegBytes();
		return 200;
	};
	const fe::GameInfo g = MakeGame("SLUS-20010");
	const int saved = fe::CoverService::Prefetch({g}, {0}, cc, ok_download, 10.0);

	Check(IsDir(cache_dir), "the cache folder was created before the write");
	Check(saved == 1, "Prefetch reports one saved cover");
	Check(FileExists(cache_dir + "/SLUS-20010.jpg"), "the cover landed at <cache>/<serial>.jpg");
	Check(!FileExists(cache_dir + "/SLUS-20010.jpg.part"), "the atomic-write temp file is not left behind");
	Check(ReadWhole(cache_dir + "/SLUS-20010.jpg").size() == sizeof(kTinyJpeg), "the saved file has the downloaded bytes");
}

// Once a cover is in the cache, CoverFinder reports it (so the catalog's cover_path resolves) and
// MissingCovers no longer asks for it again.
void TestCacheHitSatisfiesLookupAndMissingCovers()
{
	const std::string root = TempDir("cachehit");
	const std::string cache_dir = root + "/cache";
	MakeDirs(cache_dir);
	Check(WriteWhole(cache_dir + "/SLUS-20011.jpg", std::string(reinterpret_cast<const char*>(kTinyJpeg), sizeof(kTinyJpeg))),
		"wrote a pre-existing cached cover");

	fe::CoverFinder finder("", cache_dir);
	const fe::GameInfo g = MakeGame("SLUS-20011");
	const std::vector<fe::CoverFile> found = finder.Find(g);
	Check(!found.empty() && found.front().path == cache_dir + "/SLUS-20011.jpg", "CoverFinder returns the cached file");
	Check(!found.empty() && std::string(found.front().source) == "cache", "its source is \"cache\"");

	fe::CoverConfig cc;
	cc.cache_dir = cache_dir;
	const std::vector<int> missing = fe::CoverService::MissingCovers({g}, cc);
	Check(missing.empty(), "a game whose cover is already cached is not reported as missing");
}

// A manual cover (the user's own file in the covers/ folder) wins over a cached download, matching
// fe_covers.h's documented lookup order (manual, beside, drive, cache, art).
void TestManualCoverBeatsCache()
{
	const std::string root = TempDir("manual");
	const std::string manual_dir = root + "/covers", cache_dir = root + "/cache";
	MakeDirs(manual_dir);
	MakeDirs(cache_dir);
	Check(WriteWhole(manual_dir + "/SLUS-20012.png", "not a real png, just needs to be non-empty"), "wrote a manual cover");
	Check(WriteWhole(cache_dir + "/SLUS-20012.jpg", "cached bytes"), "wrote a cached cover for the same serial");

	fe::CoverFinder finder(manual_dir, cache_dir);
	const fe::GameInfo g = MakeGame("SLUS-20012");
	const fe::CoverFile best = finder.Best(g);
	Check(best.path == manual_dir + "/SLUS-20012.png", "the manual cover is picked over the cached one");
	Check(std::string(best.source) == "manual", "its source is \"manual\"");
}

// A 404 writes a <serial>.missing marker (Prefetch and DownloadCover both do this), and MissingCovers
// honors it for two weeks: no retry storm against a source that does not have that game's cover.
void TestFourOhFourMarkerSuppressesRetryForTwoWeeks()
{
	const std::string root = TempDir("404");
	const std::string cache_dir = root + "/cache";
	fe::CoverConfig cc;
	cc.cache_dir = cache_dir;
	cc.url_template = "https://example.invalid/${serial}.jpg";
	const fe::DownloadFn not_found = [](const std::string&, std::vector<uint8_t>& out) {
		out.clear();
		return 404;
	};
	const fe::GameInfo g = MakeGame("SLUS-20013");
	const int saved = fe::CoverService::Prefetch({g}, {0}, cc, not_found, 10.0);
	Check(saved == 0, "a 404 saves nothing");
	Check(FileExists(cache_dir + "/SLUS-20013.missing"), "a .missing marker was written");

	const std::vector<int> missing_now = fe::CoverService::MissingCovers({g}, cc);
	Check(missing_now.empty(), "the same game is not asked for again right after a fresh 404");

	// Back-date the marker past the 14-day window (fe_covers.cpp's RecentlyMissing) and confirm it is
	// retried once that window has passed.
	const std::string marker = cache_dir + "/SLUS-20013.missing";
	struct utimbuf old_time;
	old_time.actime = old_time.modtime = std::time(nullptr) - 15 * 24 * 3600;
	Check(utime(marker.c_str(), &old_time) == 0, "back-dated the .missing marker to 15 days ago");
	const std::vector<int> missing_later = fe::CoverService::MissingCovers({g}, cc);
	Check(missing_later.size() == 1, "a stale (15-day-old) .missing marker no longer suppresses the retry");
}

// An HTTP 200 whose body is not a decodable image (a redirect page, an error HTML body, truncated bytes)
// must not be written to disk as if it were a cover.
void TestUndecodableBodyIsNotSaved()
{
	const std::string root = TempDir("baddecode");
	const std::string cache_dir = root + "/cache";
	fe::CoverConfig cc;
	cc.cache_dir = cache_dir;
	cc.url_template = "https://example.invalid/${serial}.jpg";
	const fe::DownloadFn html_body = [](const std::string&, std::vector<uint8_t>& out) {
		const std::string html = "<html>not an image</html>";
		out.assign(html.begin(), html.end());
		return 200;
	};
	const fe::GameInfo g = MakeGame("SLUS-20014");
	const int saved = fe::CoverService::Prefetch({g}, {0}, cc, html_body, 10.0);
	Check(saved == 0, "an undecodable 200 body is not counted as saved");
	Check(!FileExists(cache_dir + "/SLUS-20014.jpg"), "nothing was written for it");
}

// The network going down (download() returning a negative status, as fe_https.cpp's client does on a
// connect/TLS failure) stops the whole prefetch pass immediately instead of retrying every other missing
// game against a server that is unreachable.
void TestNetworkDownStopsFurtherAttempts()
{
	const std::string root = TempDir("netdown");
	fe::CoverConfig cc;
	cc.cache_dir = root + "/cache";
	cc.url_template = "https://example.invalid/${serial}.jpg";
	int calls = 0;
	const fe::DownloadFn offline = [&](const std::string&, std::vector<uint8_t>& out) {
		calls++;
		out.clear();
		return -1;
	};
	const std::vector<fe::GameInfo> games = {MakeGame("SLUS-20015"), MakeGame("SLUS-20016"), MakeGame("SLUS-20017")};
	const int saved = fe::CoverService::Prefetch(games, {0, 1, 2}, cc, offline, 10.0);
	Check(saved == 0, "nothing is saved while offline");
	Check(calls == 1, "only the first game is attempted once the network is reported down");
}

// A zero time budget (main-boot.cpp passes 30.0 s normally, but the mechanism must not download at all
// once the budget has already elapsed) makes no requests.
void TestBudgetExhaustedMakesNoRequests()
{
	fe::CoverConfig cc;
	cc.cache_dir = TempDir("budget") + "/cache";
	cc.url_template = "https://example.invalid/${serial}.jpg";
	int calls = 0;
	const fe::DownloadFn counting = [&](const std::string&, std::vector<uint8_t>& out) {
		calls++;
		out.clear();
		return 200;
	};
	const fe::GameInfo g = MakeGame("SLUS-20018");
	const int saved = fe::CoverService::Prefetch({g}, {0}, cc, counting, /*budget_s=*/0.0);
	Check(saved == 0, "nothing is saved with no time budget");
	Check(calls == 0, "no request was made with no time budget");
}

// Prefetch's PrefetchStats out-param (what fe_ps5.cpp's boot.log "found/downloaded/failed/missing"
// summary line is built from) must categorize each outcome correctly: a save, a 404, an undecodable
// 200 body, and -- because the network goes down after the first attempt -- the untried rest.
void TestPrefetchStatsSummary()
{
	fe::CoverConfig cc;
	cc.cache_dir = TempDir("stats") + "/cache";
	cc.url_template = "https://example.invalid/${serial}.jpg";
	int call = 0;
	const fe::DownloadFn mixed = [&](const std::string&, std::vector<uint8_t>& out) {
		switch (call++)
		{
		case 0:
			out = TinyJpegBytes();
			return 200; // downloaded
		case 1:
			out.clear();
			return 404; // not_found
		case 2:
		{
			const std::string html = "<html></html>";
			out.assign(html.begin(), html.end());
			return 200; // failed (200 but not an image)
		}
		default:
			out.clear();
			return -1; // network down: this and the rest stay missing
		}
	};
	const std::vector<fe::GameInfo> games = {
		MakeGame("SLUS-30001"), MakeGame("SLUS-30002"), MakeGame("SLUS-30003"), MakeGame("SLUS-30004"), MakeGame("SLUS-30005")};
	fe::CoverService::PrefetchStats stats;
	const int saved = fe::CoverService::Prefetch(games, {0, 1, 2, 3, 4}, cc, mixed, 10.0, &stats);

	Check(saved == 1, "Prefetch's return value counts only the one real save");
	Check(stats.downloaded == 1, "stats.downloaded == 1");
	Check(stats.not_found == 1, "stats.not_found == 1 (the 404)");
	Check(stats.failed == 1, "stats.failed == 1 (the 200 that wasn't an image)");
	Check(stats.missing == 2, "stats.missing == 2 (the network-down attempt plus the untried game after it)");
}

// CoverService::Decode (the same decoder DownloadCover/Prefetch call before writing to the cache) must
// actually decode a real cover image, not just check it is non-empty.
void TestDecodeRealImage()
{
	fe::CoverImage image;
	const bool ok = fe::CoverService::Decode(TinyJpegBytes(), 1024, image);
	Check(ok, "a real small JPEG decodes");
	Check(image.width == 64 && image.height == 96, "decoded dimensions match the source image (64x96)");
	Check(image.rgba.size() == static_cast<size_t>(image.width) * image.height * 4, "decoded RGBA buffer is width*height*4 bytes");
}
} // namespace

int main()
{
	TestUrlBuilding();
	TestCacheFolderCreatedBeforeWrite();
	TestCacheHitSatisfiesLookupAndMissingCovers();
	TestManualCoverBeatsCache();
	TestFourOhFourMarkerSuppressesRetryForTwoWeeks();
	TestUndecodableBodyIsNotSaved();
	TestNetworkDownStopsFurtherAttempts();
	TestBudgetExhaustedMakesNoRequests();
	TestPrefetchStatsSummary();
	TestDecodeRealImage();
	std::printf(g_fails ? "%d FAILED\n" : "all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
