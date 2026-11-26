// Orbis frontend: HD texture packs from a community catalog (2026-10-05, AI-assisted; catalog switch 2026-11-26).
//
// The catalog is EmuCoreX-Textures' textures.json (kTexturePackCatalogUrl): one entry a pack, with its name, game,
// serials, authors, credits, source page, licence note, the ZIP's size and SHA-256, and for a ZIP of 2 GiB or more its
// ordered parts (each with its own size and SHA-256; concatenated they are the ZIP). Each pack belongs to its creator; the
// catalog keeps the original source and credits, and Caliza shows them beside the pack.
//
// For a game: download the chosen pack in 32 MB ranges that pick up where they stopped (a game starts the app over, so a
// download goes on at the next menu), check each part and the whole file against the catalog's SHA-256, unpack it with
// libarchive (ps5/third_party/libarchive) into textures/.caliza-unpack, move <serial>/ into the textures folder, write a
// marker file there and turn on Texture replacements in the game's settings file. A ZIP or RAR the user already has can be
// installed the same way (Import): it is read where it is and never moved or deleted. Only a pack this app installed (its
// marker) can be removed.
// The platform (HTTP, free space, the popup) is behind TexturePackPlatform: fe_ps5.cpp on the console and
// host/texpacks_test.cpp on a PC. Needs proper testing on the console.
//
// Copyright (C) 2026 swordpdf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fe
{
// The community catalog (schema version 1).
constexpr const char* kTexturePackCatalogUrl = "https://raw.githubusercontent.com/sashkinbro/EmuCoreX-Textures/main/textures.json";
constexpr const char* kTexturePackCatalogPage = "https://github.com/sashkinbro/EmuCoreX-Textures";

struct TexturePackPart
{
	std::string url;
	uint64_t bytes = 0;
	std::string sha256; // lowercase hex
};

struct TexturePack
{
	std::string id;                   // the catalog's id: what a job and the marker file name
	std::string name;                 // "Prince of Persia: The Two Thrones HD Texture Pack"
	std::string game;                 // the game's title
	std::string version;
	std::vector<std::string> authors;
	std::string credits, description, source_url, license;
	std::vector<std::string> serials; // every serial the pack is for
	std::string url;                  // the ZIP (the first part's URL when it comes in parts)
	uint64_t bytes = 0;               // the whole ZIP
	std::string sha256;               // the whole ZIP, lowercase hex
	uint32_t files = 0;               // the archive's file count (0 when the catalog doesn't say)
	std::vector<TexturePackPart> parts; // empty for a one-file pack
};

// textures.json -> its packs: entries with an id, a name, at least one well-formed serial, an https URL, a size and a
// SHA-256 (and, when parts are listed, two or more that add up to the size). False (and `error`) when the text isn't that
// catalog or holds no usable pack.
bool ParseTexturePackCatalog(const std::string& json, std::vector<TexturePack>& out, std::string& error);
// The pieces to download in order: `parts`, or the one file.
std::vector<TexturePackPart> TexturePackPieces(const TexturePack& pack);
std::string PercentEncode(const std::string& text);
std::string PercentDecode(const std::string& text);
// "SLUS-20776" for a well-formed serial (any case), else "".
std::string NormalSerial(const std::string& text);
// Where an archive entry goes within the textures folder. Rejects traversal, absolute paths and control characters.
std::string TexturePackTarget(const std::string& entry, const std::string& serial);
// "1.2 GB", "356 MB", "12 KB".
std::string FormatBytes(uint64_t bytes);
// The SHA-256 of `data`, lowercase hex (the check a download goes through; here for the tests).
std::string TexturePackSha256(const void* data, size_t size);
// "github.com" for "https://github.com/x/y".
std::string UrlHost(const std::string& url);

struct TexturePackStatus
{
	enum class State
	{
		Loading,     // the catalog is on its way
		Unavailable, // no catalog (offline, the host down): `message`; Retry fetches it again
		None,        // the catalog has no pack for this serial
		Available,   // `packs`
		Queued,      // waiting behind another game's pack
		Checking,    // reading back a part downloaded before
		Downloading,
		Verifying, // the SHA-256 of the whole file
		Unpacking,
		Installing,
		Installed, // `installed` names it
		Removing,
		Failed,    // `message`; Cross tries again
		NeedSpace, // `message` ("Needs 12.3 GB free")
	};
	State state = State::Loading;
	bool catalog_loading = false;
	bool catalog_ready = false;
	std::string catalog_message;
	std::vector<TexturePack> packs; // the catalog's packs for this serial
	std::string job_name;           // the one being fetched or imported, by name
	std::string installed;          // Installed: the pack's name, "" for one this app didn't put there
	std::string installed_id;       // Installed: the catalog id ("file:<name>" for an imported archive)
	std::string where;              // Installed: its folder
	bool ours = false;              // Installed by this app (its marker file): Triangle can remove it
	uint64_t done = 0, total = 0;   // bytes so far and in all (checking, downloading, verifying, unpacking)
	double rate = 0;                // download speed, bytes a second
	std::string message;            // Unavailable, Failed, NeedSpace: why
};

// What runs now, for the shelf's line ("HD textures: God of War · 37%").
struct TexturePackActivity
{
	bool active = false;
	std::string title; // the game
	TexturePackStatus::State state = TexturePackStatus::State::Queued;
	double fraction = 0;
	int waiting = 0; // other packs queued behind it
};

// What the app asks of a manager (AppConfig::texture_packs); unset on a build without one.
struct TexturePackService
{
	std::function<TexturePackStatus(const std::string& serial)> status;
	std::function<bool(const std::string& serial, int pack, const std::string& settings_path, const std::string& settings_header,
		const std::string& title, const std::string& id)>
		begin;
	// Installs a ZIP or RAR the user has (read in place, never moved or deleted).
	std::function<bool(const std::string& serial, const std::string& archive_path, const std::string& settings_path,
		const std::string& settings_header, const std::string& title)>
		import;
	std::function<bool(const std::string& serial)> cancel;
	std::function<bool(const std::string& serial)> remove;
	std::function<void()> retry; // fetch the list again
	std::function<TexturePackActivity()> activity;
	explicit operator bool() const { return static_cast<bool>(status); }
};

struct TexturePackPlatform
{
	// GET a small text (the catalog): the HTTP status, or < 0 when the request failed.
	std::function<int(const std::string& url, std::string& body)> get_text;
	// GET bytes [offset, offset + length) of `url`, each block to `sink` (false stops it): the HTTP status (206; 200 when
	// the whole file came from 0), or < 0.
	std::function<int(const std::string& url, uint64_t offset, uint64_t length, const std::function<bool(const void*, size_t)>& sink)>
		get_range;
	std::function<void()> abort; // fails the request in flight (Stop, Cancel), from another thread
	std::function<uint64_t(const std::string& dir)> free_bytes; // UINT64_MAX when unknown
	// A pack for this serial somewhere else the emulator looks first (a USB drive, the settings page's folder): its folder.
	std::function<std::string(const std::string& serial)> existing_pack;
	std::function<void(const std::string& line)> log;
	std::function<void(const std::string& game, bool ok, const std::string& detail)> notify; // the popup when a pack is in or failed
	std::function<double()> now; // seconds, monotonic
	// Called first thing on each thread the manager starts (the worker, "worker"; the unpacker's file writers, "writer"):
	// the console logs where it runs and lets it use all the title's CPUs (a thread made by the shelf's takes its CPU).
	std::function<void(const char* role)> thread_start;
};

class TexturePackManager
{
public:
	// textures_dir: the emulator's textures folder (<serial>/replacements); work_dir: downloads in progress and their
	// job files; catalog_cache: the catalog, used for a day (and when the network is down).
	TexturePackManager(TexturePackPlatform platform, std::string textures_dir, std::string work_dir, std::string catalog_cache);
	~TexturePackManager();
	TexturePackManager(const TexturePackManager&) = delete;
	TexturePackManager& operator=(const TexturePackManager&) = delete;

	// The worker: the list, then the downloads left from last time, then whatever Begin asks for.
	void Start();
	// Stops the worker: a download keeps its part for the next start, an unpack starts over then. True when it ended within
	// `wait_ms`; false leaves it running (and this object must then be left alive).
	bool Stop(int wait_ms);

	TexturePackStatus Status(const std::string& serial) const;
	bool Begin(const std::string& serial, int pack, const std::string& settings_path, const std::string& settings_header,
		const std::string& title, const std::string& id = {});
	bool Import(const std::string& serial, const std::string& archive_path, const std::string& settings_path,
		const std::string& settings_header, const std::string& title);
	bool Cancel(const std::string& serial);
	bool Remove(const std::string& serial);
	void Retry();
	TexturePackActivity Activity() const;

	TexturePackService Service();

	// The marker file in a pack's folder ("caliza-pack.txt"): which pack, from where, when.
	static constexpr const char* kMarker = "caliza-pack.txt";
	static constexpr uint64_t kChunk = 32ull << 20;

private:
	struct Job
	{
		std::string serial, id, name, sha256, title, settings, header;
		std::string source; // an imported archive's path ("" for a download)
		std::string source_url; // the pack's page, for the marker
		uint64_t bytes = 0;
		std::vector<TexturePackPart> parts; // the pieces to download, in order
	};
	struct Progress
	{
		TexturePackStatus::State state = TexturePackStatus::State::Queued;
		uint64_t done = 0, total = 0;
		double rate = 0;
		std::string message;
		std::string id, name; // the pack
		std::string title;
	};
	struct Installed
	{
		double checked = -1;
		bool present = false, ours = false;
		std::string id, name, where;
	};

	void Run();
	void MoveFromParent();
	// pr9l: a folder to delete is renamed out of the way first (.caliza-old-...), and the worker deletes what was set
	// aside when it has nothing else to do: a folder of thousands of files takes the console minutes to delete.
	bool SetAside(const std::string& path);
	void EmptySetAside();
	void LoadCatalog();
	void ResumeJobs();
	void Process(const Job& job);
	bool Download(const Job& job, const std::string& part, const std::string& full);
	bool Unpack(const Job& job, const std::string& full, const std::string& staging, uint32_t& files, bool& no_space, std::string& error);
	bool WriteMarker(const Job& job, uint32_t files, const std::string& folder);
	void Finish(const Job& job);
	void SetState(const std::string& serial, TexturePackStatus::State state, uint64_t done = 0, uint64_t total = 0,
		const std::string& message = {});
	void SetProgress(const std::string& serial, uint64_t done);
	bool Stopping(const std::string& serial) const;
	void Log(const std::string& line) const;
	void DeleteJobFiles(const Job& job);
	void DeleteDownload(const std::string& serial, const std::string& id);
	bool Queue(Job job);
	bool WriteJob(const Job& job);
	Installed LookAtFolder(const std::string& serial) const;
	std::vector<TexturePack> PacksFor(const std::string& serial) const;
	double Now() const;

	TexturePackPlatform m_platform;
	std::string m_textures, m_work, m_cache;
	std::thread m_thread;
	std::shared_ptr<bool> m_finished; // set by the worker as it ends (Stop waits on it)
	mutable std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_stop = false;
	bool m_started = false;
	bool m_retry = false;
	enum class Catalog
	{
		Loading,
		Ready,
		Failed,
	} m_catalog_state = Catalog::Loading;
	std::string m_catalog_error;
	std::vector<TexturePack> m_catalog;
	std::deque<Job> m_queue;
	std::deque<std::string> m_removals;
	std::map<std::string, Progress> m_progress; // by serial: queued, running, failed
	std::string m_current;                      // the serial being worked on
	bool m_cancel_current = false;
	mutable std::map<std::string, Installed> m_installed; // a cache: the folder is looked at every few seconds
	// The download speed: bytes and time at the last measure.
	uint64_t m_rate_bytes = 0;
	double m_rate_time = 0;
};
} // namespace fe
