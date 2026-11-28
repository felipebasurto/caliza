// SPDX-FileCopyrightText: 2026 Caliza contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_games.h"
#include "fe_web.h"

#include "libchdr/chd.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace
{
std::atomic<int> g_chd_open_calls{0};
std::atomic<int> g_chd_open_finished{0};
int g_failures = 0;

void Check(bool ok, const char* msg)
{
	if (ok)
		std::cout << "  ok    " << msg << "\n";
	else
	{
		std::cerr << "  FAIL  " << msg << "\n";
		++g_failures;
	}
}

void WriteFile(const std::string& path, const std::string& content)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string HttpGet(uint16_t port, const std::string& target)
{
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return {};
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
	{
		close(fd);
		return {};
	}
	const std::string req =
		"GET " + target + " HTTP/1.1\r\n"
		"Host: 127.0.0.1:" + std::to_string(port) + "\r\n"
		"Connection: close\r\n\r\n";
	(void)send(fd, req.data(), req.size(), 0);
	std::string resp;
	char buf[4096];
	for (;;)
	{
		const ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n <= 0)
			break;
		resp.append(buf, static_cast<size_t>(n));
	}
	close(fd);
	return resp;
}
} // namespace

// Slow libchdr stub simulating a slow network/USB first CHD open (350 ms).
extern "C" {
chd_error chd_open(const char*, int, chd_file*, chd_file** chd)
{
	g_chd_open_calls.fetch_add(1, std::memory_order_relaxed);
	std::this_thread::sleep_for(std::chrono::milliseconds(350));
	if (chd)
		*chd = nullptr;
	g_chd_open_finished.fetch_add(1, std::memory_order_relaxed);
	return CHDERR_INVALID_DATA;
}
void chd_close(chd_file*) {}
const chd_header* chd_get_header(chd_file*) { return nullptr; }
chd_error chd_read_header(const char*, chd_header*) { return CHDERR_INVALID_DATA; }
chd_error chd_read(chd_file*, uint32_t, void*) { return CHDERR_INVALID_DATA; }
chd_error chd_get_metadata(chd_file*, uint32_t, uint32_t, void*, uint32_t, uint32_t*, uint32_t*, uint8_t*)
{
	return CHDERR_METADATA_NOT_FOUND;
}
const char* chd_error_string(chd_error) { return "invalid data"; }
}

int main()
{
	char tmpl[] = "/tmp/caliza-web-nonblock-XXXXXX";
	char* dir = mkdtemp(tmpl);
	Check(dir != nullptr, "created temp dir");
	if (!dir)
		return 1;
	const std::string root(dir);
	const std::string games = root + "/games";
	const std::string settings = root + "/settings";
	const std::string covers = root + "/covers";
	const std::string cache = root + "/cache";
	(void)mkdir(games.c_str(), 0755);
	(void)mkdir(settings.c_str(), 0755);
	(void)mkdir(covers.c_str(), 0755);
	(void)mkdir(cache.c_str(), 0755);

	const std::string chd = games + "/Slow Share Game (USA).chd";
	WriteFile(chd, "MComprHD");
	fe::SetSerialCacheFile(cache + "/chd-serials.txt");

	fe::WebConfig cfg;
	cfg.game_dirs = {games};
	cfg.settings_dir = settings;
	cfg.gs_ini = root + "/gs.ini";
	cfg.covers_dir = covers;
	cfg.cache_dir = cache;
	cfg.port = 18844;

	fe::WebServer server;
	Check(server.Start(cfg), "WebServer started on loopback");
	if (server.Port() == 0)
		return 1;

	const auto t0 = std::chrono::steady_clock::now();
	const std::string resp = HttpGet(server.Port(), "/api/games");
	const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - t0).count();

	Check(resp.find("200 OK") != std::string::npos, "GET /api/games returned HTTP 200");
	Check(resp.find("Slow Share Game") != std::string::npos, "GET /api/games immediately returned filename title");
	Check(elapsed_ms < 150, ("GET /api/games did not block on 350ms CHD open (" + std::to_string(elapsed_ms) + " ms)").c_str());

	// Wait for the background serial worker to finish resolving the slow CHD off the request thread.
	for (int i = 0; i < 40 && g_chd_open_finished.load() == 0; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	Check(g_chd_open_calls.load() == 1 && g_chd_open_finished.load() == 1,
		"background serial worker resolved the uncached CHD off the request thread");

	server.Stop();
	(void)unlink(chd.c_str());
	(void)unlink((cache + "/chd-serials.txt").c_str());
	(void)rmdir(cache.c_str());
	(void)rmdir(covers.c_str());
	(void)rmdir(settings.c_str());
	(void)rmdir(games.c_str());
	(void)rmdir(root.c_str());

	if (g_failures != 0)
	{
		std::cerr << "test_web_nonblocking: " << g_failures << " failure(s)\n";
		return 1;
	}
	std::cout << "test_web_nonblocking: PASS\n";
	return 0;
}
