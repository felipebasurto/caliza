// Orbis frontend: HD texture packs from the community catalog (see fe_texpacks.h). 2026-10-05, AI-assisted; needs proper testing
// on the console.
//
// Copyright (C) 2026 swordpdf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_texpacks.h"
#include "fe_settings.h"

#include "OrbisTexturePak.h" // pcsx2/: the one-file pack's format, shared with GSTextureReplacements.cpp

#include "archive.h"
#include "archive_entry.h"
#include "rapidjson/document.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fe
{
namespace
{
using State = TexturePackStatus::State;

// ---- SHA-256 (FIPS 180-4), to check a download against the catalog's digests.
class Sha256
{
public:
	void Update(const void* data, size_t n)
	{
		const uint8_t* p = static_cast<const uint8_t*>(data);
		m_bytes += n;
		while (n > 0)
		{
			const size_t take = std::min(n, sizeof(m_block) - m_used);
			std::memcpy(m_block + m_used, p, take);
			m_used += take;
			p += take;
			n -= take;
			if (m_used == sizeof(m_block))
			{
				Block(m_block);
				m_used = 0;
			}
		}
	}
	std::string Hex() const
	{
		Sha256 copy = *this;
		const uint64_t bits = copy.m_bytes * 8;
		const uint8_t one = 0x80, zero = 0;
		copy.Update(&one, 1);
		while (copy.m_used != 56)
			copy.Update(&zero, 1);
		uint8_t len[8];
		for (int i = 0; i < 8; i++)
			len[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
		copy.Update(len, 8);
		static const char* const hex = "0123456789abcdef";
		std::string out;
		for (uint32_t w : copy.m_h)
			for (int i = 3; i >= 0; i--)
			{
				const uint8_t b = static_cast<uint8_t>(w >> (8 * i));
				out += hex[b >> 4];
				out += hex[b & 15];
			}
		return out;
	}

private:
	static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
	void Block(const uint8_t* b)
	{
		static const uint32_t k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
			0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
			0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb,
			0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f,
			0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
		uint32_t w[64];
		for (int i = 0; i < 16; i++)
			w[i] = (uint32_t(b[i * 4]) << 24) | (uint32_t(b[i * 4 + 1]) << 16) | (uint32_t(b[i * 4 + 2]) << 8) | uint32_t(b[i * 4 + 3]);
		for (int i = 16; i < 64; i++)
		{
			const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = m_h[0], bb = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
		for (int i = 0; i < 64; i++)
		{
			const uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = bb;
			bb = a;
			a = t1 + t2;
		}
		m_h[0] += a;
		m_h[1] += bb;
		m_h[2] += c;
		m_h[3] += d;
		m_h[4] += e;
		m_h[5] += f;
		m_h[6] += g;
		m_h[7] += h;
	}
	uint32_t m_h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	uint8_t m_block[64] = {};
	size_t m_used = 0;
	uint64_t m_bytes = 0;
};

// ---- files

bool LocalTextureArchive(const std::string& path, uint64_t& bytes)
{
	if (path.empty() || path.front() != '/' || path.find_first_of("\r\n") != std::string::npos || path.find('\0') != std::string::npos)
		return false;
	const size_t dot = path.rfind('.');
	if (dot == std::string::npos)
		return false;
	std::string ext = path.substr(dot);
	for (char& c : ext)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	struct stat info{};
	if ((ext != ".zip" && ext != ".rar") || lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0)
		return false;
	bytes = static_cast<uint64_t>(info.st_size);
	return bytes <= UINT64_MAX / 4;
}

bool IsDir(const std::string& p)
{
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool Exists(const std::string& p)
{
	struct stat st;
	return stat(p.c_str(), &st) == 0;
}

uint64_t FileSize(const std::string& p)
{
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;
}

bool MakeDirs(const std::string& path)
{
	for (size_t i = 1; i <= path.size(); i++)
		if (i == path.size() || path[i] == '/')
		{
			const std::string sub = path.substr(0, i);
			if (mkdir(sub.c_str(), 0777) != 0 && errno != EEXIST)
				return false;
		}
	return IsDir(path);
}

// Removes a file or a folder and everything in it (links are removed, never followed).
bool RemoveTree(const std::string& path)
{
	struct stat st;
	if (lstat(path.c_str(), &st) != 0)
		return errno == ENOENT;
	if (!S_ISDIR(st.st_mode))
		return unlink(path.c_str()) == 0;
	bool ok = true;
	if (DIR* d = opendir(path.c_str()))
	{
		std::vector<std::string> names;
		while (dirent* e = readdir(d))
			if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0)
				names.emplace_back(e->d_name);
		closedir(d);
		for (const std::string& n : names)
			ok = RemoveTree(path + "/" + n) && ok;
	}
	return rmdir(path.c_str()) == 0 && ok;
}

// One whole file. `no_space` when the disk is full.
bool WriteWhole(const std::string& path, const std::vector<uint8_t>& data, bool& no_space)
{
	const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
	{
		no_space = errno == ENOSPC;
		return false;
	}
	size_t at = 0;
	while (at < data.size())
	{
		const ssize_t w = write(fd, data.data() + at, data.size() - at);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
		{
			no_space = w < 0 && errno == ENOSPC;
			close(fd);
			return false;
		}
		at += static_cast<size_t>(w);
	}
	if (close(fd) != 0)
	{
		no_space = errno == ENOSPC;
		return false;
	}
	return true;
}

// 2026-10-05 (AI-assisted): the unpacked files' writers. On the console each new file costs tens of milliseconds of
// file-system work: Ratchet & Clank's 13,200 files took more than 15 minutes written one at a time (pr9h). A few threads
// write them while the unpacking thread decompresses the next, with at most kMaxBytes waiting.
class FileWriters
{
public:
	FileWriters(int threads, const std::function<void(const char*)>& thread_start)
	{
		for (int i = 0; i < threads; i++)
			m_threads.emplace_back([this, thread_start] {
				if (thread_start)
					thread_start("writer");
				Run();
			});
	}
	~FileWriters() { Finish(); }
	FileWriters(const FileWriters&) = delete;
	FileWriters& operator=(const FileWriters&) = delete;

	// Queues one file, waiting while the queue is full. False once a write has failed.
	bool Put(std::string path, std::vector<uint8_t> data)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_room.wait(lock, [&] { return m_failed || m_items.empty() || (m_bytes + data.size() <= kMaxBytes && m_items.size() < kMaxItems); });
		if (m_failed)
			return false;
		m_bytes += data.size();
		m_items.push_back(Item{std::move(path), std::move(data)});
		m_work.notify_one();
		return true;
	}

	// Waits until every queued file is written (or dropped after a failure). True when all were written.
	bool Finish()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_done = true;
		}
		m_work.notify_all();
		for (std::thread& t : m_threads)
			if (t.joinable())
				t.join();
		std::lock_guard<std::mutex> lock(m_mutex);
		return !m_failed;
	}

	uint32_t Written()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_written;
	}
	bool NoSpace()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_no_space;
	}
	std::string FailedPath()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_failed_path;
	}
	// Seconds the writers have spent writing, all of them together.
	double WriteSeconds() const { return m_write_us.load() / 1e6; }

private:
	static constexpr size_t kMaxBytes = 64u << 20;
	static constexpr size_t kMaxItems = 2048;
	struct Item
	{
		std::string path;
		std::vector<uint8_t> data;
	};

	void Run()
	{
		for (;;)
		{
			Item item;
			bool skip = false;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_work.wait(lock, [&] { return m_done || !m_items.empty(); });
				if (m_items.empty())
					return;
				item = std::move(m_items.front());
				m_items.pop_front();
				skip = m_failed; // after a failure the rest is only dropped
			}
			bool no_space = false;
			const auto t0 = std::chrono::steady_clock::now();
			const bool ok = skip || WriteWhole(item.path, item.data, no_space);
			m_write_us += static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_bytes -= item.data.size();
				if (ok && !skip)
					m_written++;
				else if (!ok && !m_failed)
				{
					m_failed = true;
					m_no_space = no_space;
					m_failed_path = item.path;
				}
			}
			m_room.notify_all();
		}
	}

	std::vector<std::thread> m_threads;
	std::mutex m_mutex;
	std::condition_variable m_work, m_room;
	std::deque<Item> m_items;
	size_t m_bytes = 0;
	bool m_done = false, m_failed = false, m_no_space = false;
	uint32_t m_written = 0;
	std::string m_failed_path;
	std::atomic<uint64_t> m_write_us{0};
};

// 2026-10-05 (AI-assisted): a pack's replacements written into one file, <serial>/replacements.pak (pcsx2/OrbisTexturePak.h).
// The writers above still took Ratchet & Clank 2 most of an hour (pr9k: 3,385 files in 11 s, then 5 to 10 files a second
// with all four busy), while a test payload wrote 256 MB into one file in a second. Entries are appended through an 8 MB
// buffer; the index and then the header go at the end, so a pack cut short is never read.
class PakWriter
{
public:
	PakWriter() = default;
	~PakWriter()
	{
		if (m_fd >= 0)
			close(m_fd);
	}
	PakWriter(const PakWriter&) = delete;
	PakWriter& operator=(const PakWriter&) = delete;

	bool Open(const std::string& path)
	{
		m_path = path;
		m_fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (m_fd < 0)
			return Fail();
		m_buffer.assign(OrbisTexturePak::kHeaderSize, 0); // the header's place, written last
		m_offset = OrbisTexturePak::kHeaderSize;
		return true;
	}

	// One entry, `name` its path under replacements/.
	bool Add(const std::string& name, const std::vector<uint8_t>& data)
	{
		if (m_fd < 0)
			return false;
		OrbisTexturePak::Entry e;
		e.offset = m_offset;
		e.size = data.size();
		e.name = name;
		m_index.push_back(std::move(e));
		m_offset += data.size();
		if (m_buffer.size() + data.size() <= kFlushBytes)
		{
			m_buffer.insert(m_buffer.end(), data.begin(), data.end());
			return m_buffer.size() < kFlushBytes || Flush();
		}
		// A big entry goes straight after what is buffered.
		return Flush() && WriteAll(data.data(), data.size());
	}

	// The rest of the data, the index, then the header. False (Error, NoSpace) when a write failed.
	bool Finish()
	{
		if (m_fd < 0)
			return false;
		if (!Flush())
			return false;
		std::vector<uint8_t> index;
		for (const OrbisTexturePak::Entry& e : m_index)
			OrbisTexturePak::AppendIndexEntry(index, e);
		if (!WriteAll(index.data(), index.size()))
			return false;
		const std::vector<uint8_t> header = OrbisTexturePak::MakeHeader(static_cast<uint32_t>(m_index.size()), m_offset, index.size());
		size_t at = 0;
		while (at < header.size())
		{
			const ssize_t w = pwrite(m_fd, header.data() + at, header.size() - at, static_cast<off_t>(at));
			if (w < 0 && errno == EINTR)
				continue;
			if (w <= 0)
				return Fail();
			at += static_cast<size_t>(w);
		}
		const int fd = m_fd;
		m_fd = -1;
		if (close(fd) != 0)
			return Fail();
		return true;
	}

	uint32_t Count() const { return static_cast<uint32_t>(m_index.size()); }
	uint64_t Bytes() const { return m_offset; }
	bool NoSpace() const { return m_errno == ENOSPC; }
	const std::string& Path() const { return m_path; }
	std::string Error() const { return std::strerror(m_errno ? m_errno : EIO); }
	double WriteSeconds() const { return m_write_us / 1e6; }

private:
	static constexpr size_t kFlushBytes = 8u << 20;

	bool Fail()
	{
		if (!m_errno)
			m_errno = errno ? errno : EIO;
		return false;
	}
	bool WriteAll(const uint8_t* p, size_t n)
	{
		const auto t0 = std::chrono::steady_clock::now();
		while (n > 0)
		{
			const ssize_t w = write(m_fd, p, n);
			if (w < 0 && errno == EINTR)
				continue;
			if (w <= 0)
				return Fail();
			p += w;
			n -= static_cast<size_t>(w);
		}
		m_write_us += static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
		return true;
	}
	bool Flush()
	{
		if (m_buffer.empty())
			return true;
		const bool ok = WriteAll(m_buffer.data(), m_buffer.size());
		m_buffer.clear();
		return ok;
	}

	std::string m_path;
	int m_fd = -1;
	int m_errno = 0;
	uint64_t m_offset = 0;
	uint64_t m_write_us = 0;
	std::vector<uint8_t> m_buffer;
	std::vector<OrbisTexturePak::Entry> m_index;
};

// "<serial>/replacements/<name>" (any case of "replacements"): the serial and the name under replacements/, for the pack.
bool SplitReplacement(const std::string& rel, std::string& serial, std::string& name)
{
	const size_t slash = rel.find('/');
	if (slash == std::string::npos)
		return false;
	const size_t second = rel.find('/', slash + 1);
	if (second == std::string::npos || second + 1 >= rel.size())
		return false;
	std::string folder = rel.substr(slash + 1, second - slash - 1);
	for (char& c : folder)
		c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	if (folder != "replacements")
		return false;
	serial = rel.substr(0, slash);
	name = rel.substr(second + 1);
	return OrbisTexturePak::NameIsSane(name);
}

// "<serial>/replacements.pak" (any case): the pack file's own place, which nothing in the archive may take.
bool IsPakPlace(const std::string& rel)
{
	const size_t slash = rel.find('/');
	if (slash == std::string::npos)
		return false;
	const size_t second = rel.find('/', slash + 1);
	std::string part = rel.substr(slash + 1, second == std::string::npos ? std::string::npos : second - slash - 1);
	for (char& c : part)
		c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	return part == OrbisTexturePak::kFileName;
}

bool HasEntries(const std::string& dir)
{
	DIR* d = opendir(dir.c_str());
	if (!d)
		return false;
	bool any = false;
	while (dirent* e = readdir(d))
		if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0)
		{
			any = true;
			break;
		}
	closedir(d);
	return any;
}

std::string Lower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	return s;
}

std::string Trim(const std::string& s)
{
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t'))
		b++;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
		e--;
	return s.substr(b, e - b);
}

// "key=value" lines.
std::map<std::string, std::string> ReadKeyValues(const std::string& path)
{
	std::map<std::string, std::string> out;
	std::string text;
	if (!settings::ReadFile(path, text))
		return out;
	size_t at = 0;
	while (at < text.size())
	{
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = text.substr(at, end - at);
		const size_t eq = line.find('=');
		if (eq != std::string::npos)
			out[line.substr(0, eq)] = line.substr(eq + 1);
		at = end + 1;
	}
	return out;
}

std::string OneLine(std::string s)
{
	for (char& c : s)
		if (c == '\n' || c == '\r')
			c = ' ';
	return s;
}

const char* StateName(State s)
{
	switch (s)
	{
		case State::Loading: return "loading";
		case State::Unavailable: return "unavailable";
		case State::None: return "none";
		case State::Available: return "available";
		case State::Queued: return "queued";
		case State::Checking: return "checking";
		case State::Downloading: return "downloading";
		case State::Verifying: return "verifying";
		case State::Unpacking: return "unpacking";
		case State::Installing: return "installing";
		case State::Installed: return "installed";
		case State::Removing: return "removing";
		case State::Failed: return "failed";
		case State::NeedSpace: return "needs space";
	}
	return "?";
}
} // namespace

// ---- names and the list

std::string NormalSerial(const std::string& text)
{
	if (text.size() != 10 || text[4] != '-')
		return {};
	std::string out = text;
	for (int i = 0; i < 4; i++)
	{
		char c = out[static_cast<size_t>(i)];
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
		if (c < 'A' || c > 'Z')
			return {};
		out[static_cast<size_t>(i)] = c;
	}
	for (int i = 5; i < 10; i++)
		if (out[static_cast<size_t>(i)] < '0' || out[static_cast<size_t>(i)] > '9')
			return {};
	return out;
}

std::string PercentEncode(const std::string& text)
{
	static const char* const hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text)
	{
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			out += static_cast<char>(c);
		else
		{
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

std::string PercentDecode(const std::string& text)
{
	auto value = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
	std::string out;
	for (size_t i = 0; i < text.size(); i++)
	{
		if (text[i] == '%' && i + 2 < text.size() && value(text[i + 1]) >= 0 && value(text[i + 2]) >= 0)
		{
			out += static_cast<char>(value(text[i + 1]) * 16 + value(text[i + 2]));
			i += 2;
		}
		else
			out += text[i];
	}
	return out;
}

std::string TexturePackSha256(const void* data, size_t size)
{
	Sha256 h;
	h.Update(data, size);
	return h.Hex();
}

std::vector<TexturePackPart> TexturePackPieces(const TexturePack& pack)
{
	if (!pack.parts.empty())
		return pack.parts;
	return {TexturePackPart{pack.url, pack.bytes, pack.sha256}};
}

std::string UrlHost(const std::string& url)
{
	const size_t scheme = url.find("://");
	const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
	size_t end = url.find_first_of("/?#", start);
	if (end == std::string::npos)
		end = url.size();
	std::string host = Lower(url.substr(start, end - start));
	const size_t at = host.rfind('@');
	if (at != std::string::npos)
		host = host.substr(at + 1);
	const size_t colon = host.find(':');
	if (colon != std::string::npos)
		host = host.substr(0, colon);
	if (host.compare(0, 4, "www.") == 0)
		host = host.substr(4);
	return host;
}

std::string FormatBytes(uint64_t bytes)
{
	char buf[32];
	if (bytes >= 1000ull * 1000 * 1000)
		std::snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / 1e9);
	else if (bytes >= 1000ull * 1000)
		std::snprintf(buf, sizeof(buf), "%.0f MB", static_cast<double>(bytes) / 1e6);
	else
		std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1e3);
	return buf;
}

namespace
{
// Catalog text, kept to one line of printable characters and a sane length (it is shown on the TV as it is).
std::string CatalogText(const rapidjson::Value& o, const char* key, size_t limit)
{
	const auto m = o.FindMember(key);
	if (m == o.MemberEnd() || !m->value.IsString())
		return {};
	std::string out;
	const char* p = m->value.GetString();
	const size_t n = m->value.GetStringLength();
	for (size_t i = 0; i < n && out.size() < limit; i++)
	{
		const unsigned char c = static_cast<unsigned char>(p[i]);
		if (c == '\n' || c == '\r' || c == '\t')
			out += ' ';
		else if (c >= 0x20 && c != 0x7f)
			out += static_cast<char>(c);
	}
	return Trim(out);
}

uint64_t CatalogNumber(const rapidjson::Value& o, const char* key)
{
	const auto m = o.FindMember(key);
	if (m == o.MemberEnd())
		return 0;
	if (m->value.IsUint64())
		return m->value.GetUint64();
	if (m->value.IsString())
		return std::strtoull(m->value.GetString(), nullptr, 10);
	return 0;
}

bool IsSha256(const std::string& s)
{
	if (s.size() != 64)
		return false;
	for (char c : s)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return false;
	return true;
}

bool IsHttps(const std::string& url)
{
	if (url.size() < 9 || url.size() > 2000 || Lower(url.substr(0, 8)) != "https://" || UrlHost(url).empty())
		return false;
	for (unsigned char c : url)
		if (c <= 0x20 || c == 0x7f)
			return false;
	return true;
}
} // namespace

bool ParseTexturePackCatalog(const std::string& json, std::vector<TexturePack>& out, std::string& error)
{
	out.clear();
	rapidjson::Document doc;
	doc.Parse(json.c_str(), json.size());
	if (doc.HasParseError() || !doc.IsObject())
	{
		error = "The texture catalog isn't readable";
		return false;
	}
	const auto version = doc.FindMember("schemaVersion");
	if (version != doc.MemberEnd() && (!version->value.IsInt() || version->value.GetInt() != 1))
	{
		error = "The texture catalog is a newer version: update Caliza";
		return false;
	}
	const auto entries = doc.FindMember("entries");
	if (entries == doc.MemberEnd() || !entries->value.IsArray())
	{
		error = "The texture catalog has no packs";
		return false;
	}
	for (const rapidjson::Value& e : entries->value.GetArray())
	{
		if (!e.IsObject())
			continue;
		TexturePack p;
		p.id = CatalogText(e, "id", 200);
		p.name = CatalogText(e, "name", 200);
		p.game = CatalogText(e, "gameTitle", 200);
		p.version = CatalogText(e, "version", 120);
		p.credits = CatalogText(e, "credits", 1200);
		p.description = CatalogText(e, "description", 1200);
		p.source_url = CatalogText(e, "sourceUrl", 2000);
		p.license = CatalogText(e, "license", 200);
		p.url = CatalogText(e, "downloadUrl", 2000);
		p.bytes = CatalogNumber(e, "sizeBytes");
		p.sha256 = Lower(CatalogText(e, "sha256", 64));
		p.files = static_cast<uint32_t>(std::min<uint64_t>(CatalogNumber(e, "fileCount"), UINT32_MAX));
		if (!IsHttps(p.source_url))
			p.source_url.clear();
		const auto authors = e.FindMember("authors");
		if (authors != e.MemberEnd() && authors->value.IsArray())
			for (const rapidjson::Value& a : authors->value.GetArray())
				if (a.IsString() && p.authors.size() < 12)
				{
					std::string name;
					for (size_t i = 0; i < a.GetStringLength() && name.size() < 80; i++)
					{
						const unsigned char c = static_cast<unsigned char>(a.GetString()[i]);
						if (c >= 0x20 && c != 0x7f)
							name += static_cast<char>(c);
					}
					name = Trim(name);
					if (!name.empty())
						p.authors.push_back(name);
				}
		const auto serials = e.FindMember("serials");
		if (serials != e.MemberEnd() && serials->value.IsArray())
			for (const rapidjson::Value& v : serials->value.GetArray())
			{
				const std::string serial = v.IsString() ? NormalSerial(std::string(v.GetString(), v.GetStringLength())) : std::string();
				if (!serial.empty() && std::find(p.serials.begin(), p.serials.end(), serial) == p.serials.end())
					p.serials.push_back(serial);
			}
		bool parts_ok = true;
		const auto parts = e.FindMember("parts");
		if (parts != e.MemberEnd() && parts->value.IsArray() && !parts->value.Empty())
		{
			uint64_t sum = 0;
			for (const rapidjson::Value& v : parts->value.GetArray())
			{
				TexturePackPart part;
				if (v.IsObject())
				{
					part.url = CatalogText(v, "downloadUrl", 2000);
					part.bytes = CatalogNumber(v, "sizeBytes");
					part.sha256 = Lower(CatalogText(v, "sha256", 64));
				}
				if (!IsHttps(part.url) || part.bytes == 0 || !IsSha256(part.sha256))
					parts_ok = false;
				if (part.bytes > p.bytes - sum)
				{
					parts_ok = false;
					break;
				}
				sum += part.bytes;
				p.parts.push_back(std::move(part));
			}
			parts_ok = parts_ok && p.parts.size() >= 2 && p.parts.size() <= 64 && sum == p.bytes;
		}
		if (p.id.empty() || p.name.empty() || p.serials.empty() || !IsHttps(p.url) || p.bytes == 0 || p.bytes > UINT64_MAX / 4 || !IsSha256(p.sha256) || !parts_ok)
			continue;
		if (std::any_of(out.begin(), out.end(), [&](const TexturePack& o) { return o.id == p.id; }))
			continue;
		out.push_back(std::move(p));
	}
	if (out.empty())
	{
		error = "The texture catalog has no packs";
		return false;
	}
	return true;
}

std::string TexturePackTarget(const std::string& entry, const std::string& serial)
{
	if (entry.empty() || entry[0] == '/' || entry.size() > 900)
		return {};
	std::vector<std::string> parts;
	size_t at = 0;
	while (at <= entry.size())
	{
		size_t end = entry.find('/', at);
		if (end == std::string::npos)
			end = entry.size();
		const std::string part = entry.substr(at, end - at);
		if (part == "..")
			return {};
		for (unsigned char c : part)
			if (c < 0x20 || c == 0x7f || c == '\\')
				return {};
		if (!part.empty() && part != ".")
			parts.push_back(part);
		at = end + 1;
	}
	auto join = [&](size_t from) {
		std::string out;
		for (size_t i = from; i < parts.size(); i++)
			out += (out.empty() ? "" : "/") + parts[i];
		return out;
	};
	// The pack's own layout: <serial>/replacements/... (also a second disc's serial); its dumps/ are the maker's, not needed.
	for (size_t i = 0; i < parts.size(); i++)
	{
		const std::string s = NormalSerial(parts[i]);
		if (s.empty())
			continue;
		if (i + 1 >= parts.size() || Lower(parts[i + 1]) == "dumps")
			return {};
		return s + "/" + join(i + 1);
	}
	// Else a replacements/ folder: under this game's serial.
	const std::string own = NormalSerial(serial);
	for (size_t i = 0; i < parts.size(); i++)
		if (Lower(parts[i]) == "replacements" && i + 1 < parts.size() && !own.empty())
			return own + "/replacements/" + join(i + 1);
	return {};
}

// ---- the manager

TexturePackManager::TexturePackManager(TexturePackPlatform platform, std::string textures_dir, std::string work_dir, std::string catalog_cache)
	: m_platform(std::move(platform))
	, m_textures(std::move(textures_dir))
	, m_work(std::move(work_dir))
	, m_cache(std::move(catalog_cache))
	, m_finished(std::make_shared<bool>(false))
{
}

TexturePackManager::~TexturePackManager()
{
	if (m_thread.joinable())
		Stop(60000);
}

double TexturePackManager::Now() const
{
	if (m_platform.now)
		return m_platform.now();
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void TexturePackManager::Log(const std::string& line) const
{
	if (m_platform.log)
		m_platform.log("[texpacks] " + line);
}

void TexturePackManager::Start()
{
	if (m_started)
		return;
	m_started = true;
	auto finished = m_finished;
	m_thread = std::thread([this, finished] {
		if (m_platform.thread_start)
			m_platform.thread_start("worker");
		Run();
		std::lock_guard<std::mutex> lock(m_mutex);
		*finished = true;
	});
}

bool TexturePackManager::Stop(int wait_ms)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_platform.abort)
		m_platform.abort();
	if (!m_thread.joinable())
		return true;
	const double until = Now() + wait_ms / 1000.0;
	for (;;)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (*m_finished)
				break;
		}
		if (Now() >= until)
		{
			Log("still busy at the shelf's end: left to finish on its own");
			m_thread.detach();
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	m_thread.join();
	return true;
}

// pr9h (2026-10-05) took the folder above the textures folder as the textures folder when the textures folder wasn't there
// (main-boot's OrbisDir fallback): its downloads, its unpacking and its installed packs went into /data/caliza itself.
// This moves what it left where it belongs: the downloads folder (so a download carries on), the unpacking folder (the
// next unpack clears it) and serial folders that carry this manager's marker. Renames only, nothing else is touched.
void TexturePackManager::MoveFromParent()
{
	const size_t slash = m_textures.rfind('/');
	if (slash == std::string::npos || slash == 0)
		return;
	const std::string parent = m_textures.substr(0, slash);
	const auto move = [&](const std::string& from, const std::string& to) {
		if (!Exists(from) || Exists(to))
			return;
		MakeDirs(to.substr(0, to.rfind('/')));
		const bool ok = rename(from.c_str(), to.c_str()) == 0;
		Log(std::string(ok ? "moved " : "couldn't move ") + from + " to " + to);
	};
	if (m_work.compare(0, m_textures.size() + 1, m_textures + "/") == 0)
		move(parent + m_work.substr(m_textures.size()), m_work);
	move(parent + "/.caliza-unpack", m_textures + "/.caliza-unpack");
	if (DIR* d = opendir(parent.c_str()))
	{
		std::vector<std::string> names;
		while (dirent* e = readdir(d))
			if (NormalSerial(e->d_name) == e->d_name)
				names.emplace_back(e->d_name);
		closedir(d);
		for (const std::string& n : names)
			if (Exists(parent + "/" + n + "/" + kMarker))
			{
				move(parent + "/" + n, m_textures + "/" + n);
				std::lock_guard<std::mutex> lock(m_mutex);
				m_installed.erase(n);
			}
	}
}

// `path` renamed to <textures>/.caliza-old-<time>-<n>, for EmptySetAside. False when it isn't there or can't be renamed
// (then the caller deletes it itself).
bool TexturePackManager::SetAside(const std::string& path)
{
	if (!Exists(path))
		return false;
	static std::atomic<unsigned> counter{0};
	const std::string aside = m_textures + "/.caliza-old-" + std::to_string(static_cast<long long>(std::time(nullptr))) + "-" +
		std::to_string(counter++);
	const bool ok = rename(path.c_str(), aside.c_str()) == 0;
	Log(ok ? "set aside " + path + " as " + aside + ", to delete later" : "couldn't set aside " + path);
	return ok;
}

// Deletes what SetAside renamed, a file at a time, and gives way (within 64 files) as soon as the worker has anything else
// to do or is asked to stop; the next idle moment carries on.
void TexturePackManager::EmptySetAside()
{
	std::vector<std::string> old;
	if (DIR* d = opendir(m_textures.c_str()))
	{
		while (dirent* e = readdir(d))
			if (std::strncmp(e->d_name, ".caliza-old-", 12) == 0)
				old.emplace_back(m_textures + "/" + e->d_name);
		closedir(d);
	}
	if (old.empty())
		return;
	const auto busy = [this] {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_stop || m_retry || !m_queue.empty() || !m_removals.empty();
	};
	const double t0 = Now();
	uint32_t removed = 0;
	bool interrupted = false;
	std::function<bool(const std::string&)> remove = [&](const std::string& p) -> bool {
		struct stat st;
		if (lstat(p.c_str(), &st) != 0)
			return true;
		if (!S_ISDIR(st.st_mode))
		{
			if ((removed & 63) == 63 && busy())
			{
				interrupted = true;
				return false;
			}
			if (unlink(p.c_str()) == 0)
				removed++;
			return true;
		}
		std::vector<std::string> names;
		if (DIR* d = opendir(p.c_str()))
		{
			while (dirent* e = readdir(d))
				if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0)
					names.emplace_back(e->d_name);
			closedir(d);
		}
		for (const std::string& n : names)
			if (!remove(p + "/" + n))
				return false;
		rmdir(p.c_str());
		return true;
	};
	for (const std::string& p : old)
		if (!remove(p))
			break;
	Log("deleted " + std::to_string(removed) + " files set aside, in " + std::to_string(static_cast<int>(Now() - t0)) + " s" +
		(interrupted ? " (the rest later)" : ""));
}

void TexturePackManager::Run()
{
	MoveFromParent();
	LoadCatalog();
	ResumeJobs();
	for (;;)
	{
		EmptySetAside(); // at once when nothing was set aside or there's other work
		Job job;
		std::string removal;
		bool retry = false;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [&] { return m_stop || m_retry || !m_queue.empty() || !m_removals.empty(); });
			if (m_stop)
				return;
			if (m_retry)
			{
				m_retry = false;
				retry = true;
			}
			else if (!m_removals.empty())
			{
				removal = m_removals.front();
				m_removals.pop_front();
			}
			else
			{
				job = m_queue.front();
				m_queue.pop_front();
				m_current = job.serial;
				m_cancel_current = false;
			}
		}
		if (retry)
		{
			LoadCatalog();
			continue;
		}
		if (!removal.empty())
		{
			// pr9l: renamed out of the way, so the sheet shows it gone at once; EmptySetAside deletes its files.
			const std::string dir = m_textures + "/" + removal;
			const bool ok = ReadKeyValues(dir + "/" + kMarker).count("name") != 0 && SetAside(dir);
			if (ok)
			{
				Log("removed " + dir);
				std::lock_guard<std::mutex> lock(m_mutex);
				m_progress.erase(removal);
				m_installed.erase(removal);
			}
			else
				SetState(removal, State::Failed, 0, 0, "Couldn't remove the installed pack");
			continue;
		}
		Process(job);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_current.clear();
		m_installed.erase(job.serial);
	}
}

void TexturePackManager::LoadCatalog()
{
	std::string json, error;
	std::vector<TexturePack> packs;
	struct stat st;
	const bool cached = stat(m_cache.c_str(), &st) == 0 && settings::ReadFile(m_cache, json);
	const double age = cached ? std::difftime(std::time(nullptr), st.st_mtime) : 1e9;
	bool ok = false;
	if (cached && age >= 0 && age < 24 * 3600 && ParseTexturePackCatalog(json, packs, error))
	{
		ok = true;
		Log("catalog from the cache (" + std::to_string(static_cast<int>(age / 60)) + " min old): " + std::to_string(packs.size()) + " packs");
	}
	else
	{
		std::string body;
		const double t0 = Now();
		const int status = m_platform.get_text ? m_platform.get_text(kTexturePackCatalogUrl, body) : -1;
		Log("catalog: status " + std::to_string(status) + ", " + std::to_string(body.size()) + " bytes in " +
			std::to_string(static_cast<int>((Now() - t0) * 1000)) + " ms");
		error.clear();
		if (status == 200 && ParseTexturePackCatalog(body, packs, error))
		{
			ok = true;
			std::string dir = m_cache.substr(0, m_cache.rfind('/'));
			MakeDirs(dir);
			if (!settings::WriteFileAtomic(m_cache, body))
				Log("couldn't keep the catalog in " + m_cache);
			Log(std::to_string(packs.size()) + " packs in the catalog");
		}
		else if (cached && ParseTexturePackCatalog(json, packs, error))
		{
			ok = true;
			Log("the catalog didn't come; using the one from " + std::to_string(static_cast<int>(age / 3600)) + " h ago");
		}
		else if (error.empty() || status != 200)
			error = status < 0 ? "Can't reach the texture catalog: check the connection" :
								 "The texture catalog answered " + std::to_string(status);
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	if (ok)
	{
		m_catalog = std::move(packs);
		m_catalog_state = Catalog::Ready;
		m_catalog_error.clear();
	}
	else
	{
		m_catalog_state = Catalog::Failed;
		m_catalog_error = error;
		Log("no catalog: " + error);
	}
}

void TexturePackManager::ResumeJobs()
{
	DIR* d = opendir(m_work.c_str());
	if (!d)
		return;
	std::vector<std::string> names;
	while (dirent* e = readdir(d))
	{
		const std::string n = e->d_name;
		if (n.size() > 4 && n.compare(n.size() - 4, 4, ".job") == 0)
			names.push_back(n);
	}
	closedir(d);
	std::sort(names.begin(), names.end());
	for (const std::string& n : names)
	{
		auto kv = ReadKeyValues(m_work + "/" + n);
		Job j;
		j.serial = NormalSerial(kv["serial"]);
		j.id = kv["id"];
		j.name = kv["name"];
		j.sha256 = Lower(kv["sha256"]);
		j.bytes = std::strtoull(kv["bytes"].c_str(), nullptr, 10);
		j.title = kv["title"];
		j.settings = kv["settings"];
		j.header = kv["header"];
		j.source = kv["source"];
		j.source_url = kv["page"];
		const int count = std::atoi(kv["parts"].c_str());
		uint64_t sum = 0;
		for (int i = 0; i < count && i < 64; i++)
		{
			// "part.<i>=<bytes> <sha256> <url>"
			const std::string line = kv["part." + std::to_string(i)];
			const size_t a = line.find(' '), b = a == std::string::npos ? a : line.find(' ', a + 1);
			if (b == std::string::npos)
				break;
			TexturePackPart part;
			part.bytes = std::strtoull(line.substr(0, a).c_str(), nullptr, 10);
			part.sha256 = Lower(line.substr(a + 1, b - a - 1));
			part.url = line.substr(b + 1);
			if (!IsHttps(part.url) || !IsSha256(part.sha256) || !part.bytes || part.bytes > j.bytes - sum)
				break;
			sum += part.bytes;
			j.parts.push_back(std::move(part));
		}
		const bool download_ok = j.source.empty() && IsSha256(j.sha256) && j.bytes > 0 && j.bytes <= UINT64_MAX / 4 && !j.parts.empty() &&
			static_cast<int>(j.parts.size()) == count && sum == j.bytes;
		const bool import_ok = !j.source.empty() && j.parts.empty() && j.sha256.empty() && j.id == "file:" + j.name;
		if (j.serial.empty() || n != j.serial + ".job" || j.id.empty() || j.name.empty() || !(download_ok || import_ok))
		{
			Log("left out a job file that doesn't read: " + n);
			continue;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_progress.count(j.serial))
			continue;
		m_queue.push_back(j);
		Progress& p = m_progress[j.serial];
		p = Progress();
		p.state = State::Queued;
		p.id = j.id;
		p.name = j.name;
		p.title = j.title;
		Log("carrying on with " + j.name + " for " + j.serial);
	}
}

bool TexturePackManager::WriteJob(const Job& j)
{
	MakeDirs(m_work);
	std::string text = "serial=" + j.serial + "\nid=" + OneLine(j.id) + "\nname=" + OneLine(j.name) + "\nsha256=" + j.sha256 +
		"\nbytes=" + std::to_string(j.bytes) + "\ntitle=" + OneLine(j.title) + "\nsettings=" + OneLine(j.settings) +
		"\nheader=" + OneLine(j.header) + "\nsource=" + OneLine(j.source) + "\npage=" + OneLine(j.source_url) +
		"\nparts=" + std::to_string(j.parts.size()) + "\n";
	for (size_t i = 0; i < j.parts.size(); i++)
		text += "part." + std::to_string(i) + "=" + std::to_string(j.parts[i].bytes) + " " + j.parts[i].sha256 + " " + OneLine(j.parts[i].url) + "\n";
	return settings::WriteFileAtomic(m_work + "/" + j.serial + ".job", text);
}

void TexturePackManager::DeleteJobFiles(const Job& j)
{
	// An imported archive is the user's: only this app's own files go.
	if (j.source.empty() && IsSha256(j.sha256))
	{
		unlink((m_work + "/" + j.serial + "-" + j.sha256 + ".part").c_str());
		unlink((m_work + "/" + j.serial + "-" + j.sha256 + ".zip").c_str());
	}
	unlink((m_work + "/" + j.serial + ".job").c_str());
}

// A queued, failed or replaced job's files, by what its job file says.
void TexturePackManager::DeleteDownload(const std::string& serial, const std::string& id)
{
	auto kv = ReadKeyValues(m_work + "/" + serial + ".job");
	Job j;
	j.serial = serial;
	j.id = id;
	j.source = kv["source"];
	j.sha256 = Lower(kv["sha256"]);
	if (kv["id"].empty() || kv["id"] == id)
		DeleteJobFiles(j);
}

void TexturePackManager::SetState(const std::string& serial, State state, uint64_t done, uint64_t total, const std::string& message)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Progress& p = m_progress[serial];
	if (p.state != state || !message.empty())
		Log(serial + ": " + StateName(state) + (message.empty() ? std::string() : " (" + message + ")"));
	p.state = state;
	p.done = done;
	p.total = total;
	p.message = message;
	if (state != State::Downloading)
		p.rate = 0;
}

void TexturePackManager::SetProgress(const std::string& serial, uint64_t done)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Progress& p = m_progress[serial];
	p.done = done;
	if (p.state == State::Downloading)
	{
		const double now = Now();
		if (now - m_rate_time >= 1.0)
		{
			const double rate = m_rate_time > 0 && done >= m_rate_bytes ? (done - m_rate_bytes) / (now - m_rate_time) : 0;
			p.rate = p.rate > 0 ? p.rate * 0.6 + rate * 0.4 : rate;
			m_rate_bytes = done;
			m_rate_time = now;
		}
	}
}

bool TexturePackManager::Stopping(const std::string& serial) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_stop || (m_current == serial && m_cancel_current);
}

void TexturePackManager::Process(const Job& job)
{
	const bool imported = !job.source.empty();
	const std::string part = m_work + "/" + job.serial + "-" + job.sha256 + ".part";
	const std::string full = imported ? job.source : m_work + "/" + job.serial + "-" + job.sha256 + ".zip";
	auto cancelled = [&] {
		bool c;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			c = m_current == job.serial && m_cancel_current && !m_stop;
		}
		if (c)
		{
			DeleteJobFiles(job);
			RemoveTree(m_textures + "/.caliza-unpack");
			std::lock_guard<std::mutex> lock(m_mutex);
			m_progress.erase(job.serial);
			Log("cancelled " + job.name);
		}
		return c;
	};
	uint64_t bytes = job.bytes;
	if (imported && !LocalTextureArchive(full, bytes))
	{
		DeleteJobFiles(job);
		SetState(job.serial, State::Failed, 0, 0, "The archive is no longer available");
		return;
	}
	Log("starting " + job.name + " for " + job.serial + " (" + FormatBytes(bytes) + (imported ? ", from a file" : "") + ")");
	const std::string target = m_textures + "/" + job.serial;
	struct stat existing{};
	if (lstat(target.c_str(), &existing) == 0)
	{
		// A shutdown can happen after publishing the pack, before updating its settings or clearing the journal.
		const auto marker = S_ISDIR(existing.st_mode) ? ReadKeyValues(target + "/" + kMarker) : std::map<std::string, std::string>{};
		const auto id = marker.find("id"), checksum = marker.find("sha256");
		if (id != marker.end() && id->second == job.id && checksum != marker.end() && checksum->second == job.sha256)
		{
			Job installed = job;
			installed.bytes = bytes;
			Finish(installed);
		}
		else
		{
			DeleteJobFiles(job);
			SetState(job.serial, State::Failed, 0, 0, "A texture folder already exists for this game");
		}
		return;
	}
	// Recheck even a complete file when resuming an interrupted install.
	if (!imported && FileSize(full) == job.bytes && rename(full.c_str(), part.c_str()) != 0)
	{
		SetState(job.serial, State::Failed, 0, 0, "Couldn't reopen the saved download");
		return;
	}
	if (!imported && !Download(job, part, full))
	{
		cancelled();
		return;
	}
	if (cancelled() || Stopping(job.serial))
		return;
	// Room to unpack: the textures come to about twice their archive (DDS in RAR came to 2.7 times; the catalog's ZIPs
	// hold PNG and DDS that deflate less).
	const uint64_t need = bytes * 2 + (512ull << 20);
	const uint64_t room = m_platform.free_bytes ? m_platform.free_bytes(m_textures) : UINT64_MAX;
	if (room < need)
	{
		SetState(job.serial, State::NeedSpace, 0, 0, "Needs " + FormatBytes(need) + " free to unpack (" + FormatBytes(room) + " now)");
		Log("not enough room to unpack: " + FormatBytes(room) + " free, " + FormatBytes(need) + " wanted");
		return;
	}
	const std::string staging = m_textures + "/.caliza-unpack";
	// pr9l: an unpack cut short by an older build left thousands of files here: renamed now, deleted when the worker is idle.
	if (!SetAside(staging))
		RemoveTree(staging);
	if (!MakeDirs(staging))
	{
		SetState(job.serial, State::Failed, 0, 0, "Couldn't make " + staging);
		return;
	}
	uint32_t files = 0;
	bool no_space = false;
	std::string error;
	const double t0 = Now();
	Job sized = job;
	sized.bytes = bytes;
	const bool ok = Unpack(sized, full, staging, files, no_space, error);
	if (cancelled())
		return;
	if (Stopping(job.serial))
	{
		RemoveTree(staging); // the next start unpacks it again
		return;
	}
	if (!ok || files == 0)
	{
		RemoveTree(staging);
		if (no_space)
			SetState(job.serial, State::NeedSpace, 0, 0, "The disk filled up while unpacking: free some space");
		else
			SetState(job.serial, State::Failed, 0, 0, ok ? "Nothing in it is a texture for " + job.serial : error);
		Log("unpacking failed: " + (ok ? std::string("no textures for ") + job.serial : error));
		if (imported)
			unlink((m_work + "/" + job.serial + ".job").c_str());
		return;
	}
	Log("unpacked " + std::to_string(files) + " files in " + std::to_string(static_cast<int>(Now() - t0)) + " s");
	bool install = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		install = !m_stop && !m_cancel_current;
		if (install)
			m_progress[job.serial].state = State::Installing;
	}
	if (!install)
	{
		if (!cancelled())
			RemoveTree(staging);
		return;
	}
	const std::string staged_pack = staging + "/" + job.serial;
	if (!WriteMarker(sized, files, staged_pack) || rename(staged_pack.c_str(), target.c_str()) != 0)
	{
		RemoveTree(staging);
		SetState(job.serial, State::Failed, 0, 0, "Couldn't install the pack into " + target);
		return;
	}
	RemoveTree(staging);
	Finish(sized);
}

bool TexturePackManager::Download(const Job& job, const std::string& part, const std::string& full)
{
	MakeDirs(m_work);
	const std::vector<TexturePackPart>& pieces = job.parts;
	uint64_t have = FileSize(part);
	if (have > job.bytes)
	{
		unlink(part.c_str());
		have = 0;
	}
	// Room for the rest of the download and for unpacking it.
	const uint64_t need = (job.bytes - have) + job.bytes * 2 + (512ull << 20);
	const uint64_t room = m_platform.free_bytes ? m_platform.free_bytes(m_textures) : UINT64_MAX;
	if (room < need)
	{
		SetState(job.serial, State::NeedSpace, 0, 0, "Needs " + FormatBytes(need) + " free (" + FormatBytes(room) + " now)");
		Log("not enough room: " + FormatBytes(room) + " free, " + FormatBytes(need) + " wanted");
		return false;
	}
	// The whole file's hash, and the piece's: each piece is checked as it ends (the parts of a big pack have their own).
	Sha256 whole, piece;
	size_t index = 0;
	uint64_t fed = 0, piece_start = 0;
	int bad_piece = -1;
	const auto reset = [&] {
		whole = Sha256();
		piece = Sha256();
		index = 0;
		fed = piece_start = 0;
		bad_piece = -1;
	};
	const auto feed = [&](const void* data, size_t n) {
		const uint8_t* p = static_cast<const uint8_t*>(data);
		while (n > 0 && index < pieces.size())
		{
			const uint64_t piece_end = piece_start + pieces[index].bytes;
			const size_t take = static_cast<size_t>(std::min<uint64_t>(n, piece_end - fed));
			whole.Update(p, take);
			piece.Update(p, take);
			fed += take;
			p += take;
			n -= take;
			if (fed == piece_end)
			{
				if (piece.Hex() != pieces[index].sha256)
				{
					bad_piece = static_cast<int>(index);
					return false;
				}
				piece = Sha256();
				piece_start = piece_end;
				index++;
			}
		}
		return n == 0;
	};
	const auto piece_failed = [&](const std::string& got) {
		unlink(part.c_str());
		const std::string what = pieces.size() > 1 ? "Part " + std::to_string(bad_piece + 1) + " of " + std::to_string(pieces.size()) +
				" didn't match the catalog's checksum: try again" :
													 std::string("The download didn't match the catalog's checksum: try again");
		SetState(job.serial, State::Failed, 0, 0, what);
		Log("SHA-256 of piece " + std::to_string(bad_piece + 1) + " is " + got + ", the catalog says " +
			pieces[static_cast<size_t>(bad_piece)].sha256 + ": the download is gone");
	};
	// What came before goes through the checksums first.
	if (have > 0)
	{
		SetState(job.serial, State::Checking, 0, have);
		FILE* f = std::fopen(part.c_str(), "rb");
		std::vector<uint8_t> buf(4 << 20);
		uint64_t read = 0;
		bool fine = true;
		while (f && read < have)
		{
			const size_t n = std::fread(buf.data(), 1, static_cast<size_t>(std::min<uint64_t>(buf.size(), have - read)), f);
			if (n == 0)
				break;
			if (!feed(buf.data(), n))
			{
				fine = false;
				break;
			}
			read += n;
			SetProgress(job.serial, read);
			if (Stopping(job.serial))
			{
				std::fclose(f);
				return false;
			}
		}
		if (f)
			std::fclose(f);
		if (!fine || read != have)
		{
			Log(fine ? "the part file was short: starting over" : "a finished piece of the part file doesn't match: starting over");
			unlink(part.c_str());
			have = 0;
			reset();
		}
		Log("picking up " + job.name + " at " + FormatBytes(have));
	}
	FILE* f = std::fopen(part.c_str(), have > 0 ? "ab" : "wb");
	if (!f)
	{
		SetState(job.serial, State::Failed, 0, 0, "Couldn't write to " + m_work);
		return false;
	}
	SetState(job.serial, State::Downloading, have, job.bytes);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_rate_bytes = have;
		m_rate_time = Now();
	}
	const double t0 = Now();
	const uint64_t at_start = have;
	int failures = 0;
	bool write_failed = false;
	while (have < job.bytes && index < pieces.size())
	{
		if (Stopping(job.serial))
			break;
		const uint64_t in_piece = have - piece_start;
		const uint64_t want = std::min<uint64_t>(kChunk, pieces[index].bytes - in_piece);
		const std::string url = pieces[index].url;
		uint64_t got = 0;
		const auto sink = [&](const void* data, size_t n) {
			if (got + n > want)
				return false;
			if (std::fwrite(data, 1, n, f) != n)
			{
				write_failed = true;
				return false;
			}
			got += n;
			if (!feed(data, n))
				return false;
			SetProgress(job.serial, have + got);
			return !Stopping(job.serial);
		};
		const int status = m_platform.get_range ? m_platform.get_range(url, in_piece, want, sink) : -1;
		if (std::fflush(f) != 0)
			write_failed = true;
		have += got;
		if (write_failed)
		{
			std::fclose(f);
			SetState(job.serial, State::NeedSpace, 0, 0, "Couldn't write the download (is the disk full?)");
			Log("writing the download failed at " + FormatBytes(have));
			return false;
		}
		if (bad_piece >= 0)
		{
			std::fclose(f);
			piece_failed("different");
			return false;
		}
		if (got == want && (status == 206 || status == 200))
		{
			failures = 0;
			continue;
		}
		if (Stopping(job.serial))
			break;
		// A short answer: the part file is cut back to what the hashes have seen (they already include `got`).
		failures++;
		Log("range at " + std::to_string(in_piece) + " of piece " + std::to_string(index + 1) + ": status " + std::to_string(status) +
			", " + std::to_string(got) + " of " + std::to_string(want) + " bytes (try " + std::to_string(failures) + ")");
		if (failures >= 6)
		{
			std::fclose(f);
			SetState(job.serial, State::Failed, 0, 0,
				status > 0 ? "The download server answered " + std::to_string(status) : "The download keeps failing: check the connection");
			return false;
		}
		// 2, 4, 8, 16 and 30 s between tries, cut short by a stop.
		const double until = Now() + std::min(30.0, 2.0 * (1 << (failures - 1)));
		while (Now() < until && !Stopping(job.serial))
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	if (std::fclose(f) != 0)
	{
		SetState(job.serial, State::NeedSpace, 0, 0, "Couldn't save the download (is the disk full?)");
		return false;
	}
	const double secs = Now() - t0;
	Log("downloaded " + FormatBytes(have - at_start) + " in " + std::to_string(static_cast<int>(secs)) + " s" +
		(secs > 0 ? " (" + FormatBytes(static_cast<uint64_t>((have - at_start) / secs)) + "/s)" : std::string()));
	if (have < job.bytes)
		return false; // stopped: the part stays for next time
	SetState(job.serial, State::Verifying, job.bytes, job.bytes);
	const std::string sum = whole.Hex();
	if (sum != job.sha256)
	{
		unlink(part.c_str());
		SetState(job.serial, State::Failed, 0, 0, "The download didn't match the catalog's checksum: try again");
		Log("SHA-256 " + sum + ", the catalog says " + job.sha256 + ": the download is gone");
		return false;
	}
	if (rename(part.c_str(), full.c_str()) != 0)
	{
		SetState(job.serial, State::Failed, 0, 0, "Couldn't keep the download");
		return false;
	}
	Log("SHA-256 matches the catalog's");
	return true;
}

bool TexturePackManager::Unpack(const Job& job, const std::string& full, const std::string& staging, uint32_t& files, bool& no_space,
	std::string& error)
{
	SetState(job.serial, State::Unpacking, 0, job.bytes);
	archive* a = archive_read_new();
	archive_read_support_format_rar(a);
	archive_read_support_format_rar5(a);
	archive_read_support_format_zip(a);
	if (archive_read_open_filename(a, full.c_str(), 1 << 20) != ARCHIVE_OK)
	{
		error = std::string("Can't open the pack: ") + (archive_error_string(a) ? archive_error_string(a) : "?");
		archive_read_free(a);
		return false;
	}
	uint32_t skipped = 0;
	bool ok = true;
	archive_entry* e = nullptr;
	int r;
	std::vector<std::string> made; // folders made, so each is made once
	// pr9l: <serial>/replacements/... goes into <serial>/replacements.pak (PakWriter); anything else in the pack is written
	// as it is, by FileWriters (made when the first such file comes).
	std::map<std::string, std::unique_ptr<PakWriter>> paks;
	std::unique_ptr<FileWriters> loose;
	// Where the time goes, logged every 10 s (the console's first unpack took over 30 minutes for 13,200 files, pr9h).
	using Clock = std::chrono::steady_clock;
	const auto Seconds = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); };
	double t_read = 0, t_pack = 0, t_queue = 0, t_dirs = 0;
	uint64_t out_bytes = 0;
	uint32_t packed = 0, queued = 0;
	auto last_report = Clock::now();
	const auto unpack_start = last_report;
	const auto Report = [&](const char* what) {
		char line[320];
		std::snprintf(line, sizeof(line),
			"%s: %u files (%.0f MB) in %.0f s; reading %.0f s, writing the pack %.1f s, folders %.1f s; %u other files (waiting %.1f s, "
			"their writers %.1f s)",
			what, packed + queued, out_bytes / 1048576.0, Seconds(unpack_start, Clock::now()), t_read, t_pack, t_dirs, queued, t_queue,
			loose ? loose->WriteSeconds() : 0.0);
		Log(line);
	};
	while (true)
	{
		const auto th = Clock::now();
		r = archive_read_next_header(a, &e);
		t_read += Seconds(th, Clock::now());
		if (r != ARCHIVE_OK)
			break;
		if (Seconds(last_report, Clock::now()) >= 10.0)
		{
			last_report = Clock::now();
			Report("unpacking");
		}
		if (Stopping(job.serial))
		{
			ok = false;
			break;
		}
		SetProgress(job.serial, static_cast<uint64_t>(std::max<la_int64_t>(0, archive_filter_bytes(a, -1))));
		if (archive_entry_filetype(e) != AE_IFREG || archive_entry_hardlink(e))
			continue;
		const char* utf8 = archive_entry_pathname_utf8(e);
		const char* raw = archive_entry_pathname(e);
		const std::string rel = TexturePackTarget(utf8 ? utf8 : raw ? raw : "", job.serial);
		if (rel.compare(0, job.serial.size() + 1, job.serial + "/") != 0)
		{
			skipped++;
			archive_read_data_skip(a);
			continue;
		}
		std::string pak_serial, pak_name;
		const bool to_pak = SplitReplacement(rel, pak_serial, pak_name);
		if (!to_pak && IsPakPlace(rel))
		{
			skipped++;
			archive_read_data_skip(a);
			continue;
		}
		const std::string out = staging + "/" + rel;
		const std::string dir = to_pak ? staging + "/" + pak_serial : out.substr(0, out.rfind('/'));
		if (std::find(made.begin(), made.end(), dir) == made.end())
		{
			const auto td = Clock::now();
			const bool dir_ok = MakeDirs(dir);
			t_dirs += Seconds(td, Clock::now());
			if (!dir_ok)
			{
				error = "Couldn't make " + dir;
				no_space = errno == ENOSPC;
				ok = false;
				break;
			}
			made.push_back(dir);
		}
		// The entry, whole (a gap in a sparse entry is zeros).
		std::vector<uint8_t> data;
		if (archive_entry_size_is_set(e) && archive_entry_size(e) > 0 && archive_entry_size(e) < (1ll << 30))
			data.reserve(static_cast<size_t>(archive_entry_size(e)));
		const void* buf = nullptr;
		size_t size = 0;
		la_int64_t offset = 0;
		int d;
		const auto tr = Clock::now();
		while ((d = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK)
		{
			if (offset < 0 || static_cast<uint64_t>(offset) < data.size() || static_cast<uint64_t>(offset) + size > (1ull << 30))
			{
				d = ARCHIVE_FATAL;
				break;
			}
			data.resize(static_cast<size_t>(offset)); // zeros for a gap
			const uint8_t* p = static_cast<const uint8_t*>(buf);
			data.insert(data.end(), p, p + size);
		}
		t_read += Seconds(tr, Clock::now());
		if (d != ARCHIVE_EOF)
		{
			error = std::string("The pack is damaged: ") + (archive_error_string(a) ? archive_error_string(a) : "an entry is too big");
			ok = false;
			break;
		}
		out_bytes += data.size();
		if (to_pak)
		{
			const auto tp = Clock::now();
			std::unique_ptr<PakWriter>& pak = paks[pak_serial];
			bool added;
			if (!pak)
			{
				pak = std::make_unique<PakWriter>();
				added = pak->Open(dir + "/" + OrbisTexturePak::kFileName) && pak->Add(pak_name, data);
			}
			else
				added = pak->Add(pak_name, data);
			t_pack += Seconds(tp, Clock::now());
			if (!added)
			{
				no_space = pak->NoSpace();
				error = no_space ? std::string("The disk is full") : "Couldn't write " + pak_serial + "/" + OrbisTexturePak::kFileName + " (" + pak->Error() + ")";
				ok = false;
				break;
			}
			packed++;
			continue;
		}
		if (!loose)
			loose = std::make_unique<FileWriters>(2, m_platform.thread_start);
		const auto tq = Clock::now();
		const bool put = loose->Put(out, std::move(data));
		t_queue += Seconds(tq, Clock::now());
		if (!put)
			break; // a write failed: Finish says which
		queued++;
	}
	// The packs' indexes and headers.
	for (auto& [serial, pak] : paks)
	{
		if (!ok)
			break; // the staging folder goes
		const auto tp = Clock::now();
		const bool done = pak->Finish();
		t_pack += Seconds(tp, Clock::now());
		if (!done)
		{
			no_space = pak->NoSpace();
			error = no_space ? std::string("The disk is full") : "Couldn't write " + serial + "/" + OrbisTexturePak::kFileName + " (" + pak->Error() + ")";
			ok = false;
		}
		else
			Log(serial + "/" + OrbisTexturePak::kFileName + ": " + std::to_string(pak->Count()) + " files, " + FormatBytes(pak->Bytes()));
	}
	bool all_written = true;
	if (loose)
	{
		const auto tf = Clock::now();
		all_written = loose->Finish();
		t_queue += Seconds(tf, Clock::now());
	}
	Report("unpacked");
	if (!all_written)
	{
		no_space = loose->NoSpace();
		const std::string failed = loose->FailedPath();
		error = no_space ? std::string("The disk is full") :
						   "Couldn't write " + (failed.compare(0, staging.size() + 1, staging + "/") == 0 ? failed.substr(staging.size() + 1) : failed);
		ok = false;
	}
	files = packed > 0 ? packed + (loose ? loose->Written() : 0) : 0;
	if (ok && r != ARCHIVE_EOF)
	{
		error = std::string("The pack is damaged: ") + (archive_error_string(a) ? archive_error_string(a) : "?");
		ok = false;
	}
	Log(std::string("read ") + (archive_format_name(a) ? archive_format_name(a) : "archive") + ": " + std::to_string(files) + " files kept (" + std::to_string(packed) +
		" in the pack), " + std::to_string(skipped) + " left out (dumps or other folders)");
	archive_read_free(a);
	return ok;
}

bool TexturePackManager::WriteMarker(const Job& job, uint32_t files, const std::string& folder)
{
	const std::string marker = "# Installed by Caliza\nname=" + OneLine(job.name) + "\nid=" + OneLine(job.id) +
		"\nsource=" + OneLine(job.source_url) + "\nsha256=" + job.sha256 + "\nfiles=" + std::to_string(files) + "\n";
	return settings::WriteFileAtomic(folder + "/" + kMarker, marker);
}

void TexturePackManager::Finish(const Job& job)
{
	// Texture replacements on for this game.
	std::string what, error;
	if (!job.settings.empty())
	{
		const std::vector<settings::Change> changes = {{settings::Change::Set, "LoadTextureReplacements", "true"}};
		if (settings::EditSettingsFile(job.settings, job.header, changes, what, error))
			Log("settings: " + job.settings + (what.empty() ? " already had Texture replacements on" : ": " + what));
		else
			Log("couldn't switch Texture replacements on: " + error);
	}
	DeleteJobFiles(job);
	Log("installed " + job.name + " in " + m_textures + "/" + job.serial);
	if (m_platform.notify)
		m_platform.notify(job.title, true, error.empty() ? FormatBytes(job.bytes) : "Enable Texture replacements in Game settings");
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_progress.erase(job.serial);
		m_installed.erase(job.serial);
	}
}

// What the textures folder has for a serial: file-system work, so Status calls it without the lock (and not while a pack
// is being unpacked, installed or removed).
TexturePackManager::Installed TexturePackManager::LookAtFolder(const std::string& serial) const
{
	Installed in;
	in.checked = Now();
	const std::string dir = m_textures + "/" + serial;
	struct stat info{};
	const auto kv = lstat(dir.c_str(), &info) == 0 && S_ISDIR(info.st_mode) ?
		ReadKeyValues(dir + "/" + kMarker) : std::map<std::string, std::string>{};
	const auto name = kv.find("name");
	if (name != kv.end())
	{
		in.present = in.ours = true;
		in.name = name->second;
		const auto id = kv.find("id");
		if (id != kv.end())
			in.id = id->second;
		in.where = dir;
		return in;
	}
	if (HasEntries(dir + "/replacements") || Exists(dir + "/" + OrbisTexturePak::kFileName))
	{
		in.present = true;
		in.where = dir;
		return in;
	}
	if (m_platform.existing_pack)
	{
		const std::string other = m_platform.existing_pack(serial);
		if (!other.empty() && other.compare(0, m_textures.size(), m_textures) != 0)
		{
			in.present = true;
			in.where = other;
		}
	}
	return in;
}

std::vector<TexturePack> TexturePackManager::PacksFor(const std::string& serial) const
{
	std::vector<TexturePack> out;
	for (const TexturePack& p : m_catalog)
		if (std::find(p.serials.begin(), p.serials.end(), serial) != p.serials.end())
			out.push_back(p);
	return out;
}

TexturePackStatus TexturePackManager::Status(const std::string& raw_serial) const
{
	TexturePackStatus s;
	const std::string serial = NormalSerial(raw_serial);
	std::unique_lock<std::mutex> lock(m_mutex);
	if (serial.empty())
	{
		s.state = State::None;
		return s;
	}
	s.catalog_loading = m_catalog_state == Catalog::Loading;
	s.catalog_ready = m_catalog_state == Catalog::Ready;
	s.catalog_message = m_catalog_error;
	s.packs = PacksFor(serial);
	const auto it = m_progress.find(serial);
	if (it != m_progress.end())
	{
		const Progress& p = it->second;
		s.state = p.state;
		s.done = p.done;
		s.total = p.total;
		s.rate = p.rate;
		s.message = p.message;
		s.job_name = p.name;
		return s;
	}
	// The folder, from the cache; looked at again when that is a few seconds old, without the lock (the worker's progress
	// waits on it) and not while a pack is being unpacked, installed or removed: the disk is busy then, and this is the
	// menu's thread (a look there waited up to 0.9 s on pr9h's unpack).
	Installed in;
	const auto cached = m_installed.find(serial);
	const bool fresh = cached != m_installed.end() && cached->second.checked >= 0 && Now() - cached->second.checked < 3.0;
	bool busy = false;
	for (const auto& kv : m_progress)
		busy = busy || kv.second.state == State::Unpacking || kv.second.state == State::Installing || kv.second.state == State::Removing;
	if (fresh || busy)
		in = cached != m_installed.end() ? cached->second : Installed();
	else
	{
		lock.unlock();
		in = LookAtFolder(serial);
		lock.lock();
		m_installed[serial] = in;
	}
	if (in.present)
	{
		s.state = State::Installed;
		s.installed = in.name;
		s.installed_id = in.id;
		s.where = in.where;
		s.ours = in.ours;
		return s;
	}
	if (m_catalog_state == Catalog::Loading)
		s.state = State::Loading;
	else if (m_catalog_state == Catalog::Failed)
	{
		s.state = State::Unavailable;
		s.message = m_catalog_error;
	}
	else
		s.state = s.packs.empty() ? State::None : State::Available;
	return s;
}

bool TexturePackManager::Begin(const std::string& raw_serial, int index, const std::string& settings_path, const std::string& settings_header,
	const std::string& title, const std::string& id)
{
	const std::string serial = NormalSerial(raw_serial);
	std::lock_guard<std::mutex> lock(m_mutex);
	if (serial.empty() || m_catalog_state != Catalog::Ready)
		return false;
	const auto packs = PacksFor(serial);
	const auto it = id.empty() ?
		(index >= 0 && static_cast<size_t>(index) < packs.size() ? packs.begin() + index : packs.end()) :
		std::find_if(packs.begin(), packs.end(), [&](const TexturePack& p) { return p.id == id; });
	if (it == packs.end())
		return false;
	Job job;
	job.serial = serial;
	job.id = it->id;
	job.name = it->name;
	job.sha256 = it->sha256;
	job.title = title;
	job.settings = settings_path;
	job.header = settings_header;
	job.source_url = it->source_url;
	job.bytes = it->bytes;
	job.parts = TexturePackPieces(*it);
	return Queue(std::move(job));
}

bool TexturePackManager::Import(const std::string& raw_serial, const std::string& archive_path, const std::string& settings_path,
	const std::string& settings_header, const std::string& title)
{
	const std::string serial = NormalSerial(raw_serial);
	uint64_t bytes = 0;
	if (serial.empty() || OneLine(archive_path) != archive_path || !LocalTextureArchive(archive_path, bytes))
		return false;
	Job job;
	job.serial = serial;
	job.source = archive_path;
	job.bytes = bytes;
	job.name = archive_path.substr(archive_path.rfind('/') + 1);
	job.id = "file:" + job.name;
	job.title = title;
	job.settings = settings_path;
	job.header = settings_header;
	std::lock_guard<std::mutex> lock(m_mutex);
	return Queue(std::move(job));
}

// Called with m_mutex held.
bool TexturePackManager::Queue(Job job)
{
	if (m_stop || m_current == job.serial)
		return false;
	const auto pending = m_progress.find(job.serial);
	if (pending != m_progress.end() && pending->second.state != State::Failed && pending->second.state != State::NeedSpace)
		return false;
	struct stat existing{};
	if (lstat((m_textures + "/" + job.serial).c_str(), &existing) == 0)
		return false;
	if (pending != m_progress.end())
	{
		const auto saved = ReadKeyValues(m_work + "/" + job.serial + ".job");
		const auto digest = saved.find("sha256");
		if (pending->second.id != job.id || (digest != saved.end() && digest->second != job.sha256))
			DeleteDownload(job.serial, pending->second.id);
	}
	if (!WriteJob(job))
		return false;
	Progress& progress = m_progress[job.serial];
	progress = Progress();
	progress.id = job.id;
	progress.name = job.name;
	progress.title = job.title;
	m_queue.push_back(std::move(job));
	m_cv.notify_all();
	return true;
}

bool TexturePackManager::Cancel(const std::string& raw_serial)
{
	const std::string serial = NormalSerial(raw_serial);
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_progress.find(serial);
	if (it == m_progress.end())
		return false;
	if (it->second.state == State::Installing || it->second.state == State::Removing)
		return false;
	if (m_current == serial && it->second.state != State::Failed && it->second.state != State::NeedSpace)
	{
		m_cancel_current = true;
		if (m_platform.abort)
			m_platform.abort();
		return true;
	}
	// Queued, failed or short of space: its files go now.
	for (auto q = m_queue.begin(); q != m_queue.end();)
		q = q->serial == serial ? m_queue.erase(q) : q + 1;
	DeleteDownload(serial, it->second.id);
	m_progress.erase(it);
	Log("forgot the download for " + serial);
	return true;
}

bool TexturePackManager::Remove(const std::string& raw_serial)
{
	const std::string serial = NormalSerial(raw_serial);
	std::lock_guard<std::mutex> lock(m_mutex);
	const std::string folder = m_textures + "/" + serial;
	struct stat info{};
	if (serial.empty() || m_progress.count(serial) || lstat(folder.c_str(), &info) != 0 ||
		!S_ISDIR(info.st_mode) || ReadKeyValues(folder + "/" + kMarker).count("name") == 0)
		return false; // only a pack this app installed, and not while it's busy with it
	Progress& p = m_progress[serial];
	p = Progress();
	p.state = State::Removing;
	m_removals.push_back(serial);
	m_cv.notify_all();
	return true;
}

void TexturePackManager::Retry()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_catalog_state != Catalog::Failed)
		return;
	m_catalog_state = Catalog::Loading;
	m_retry = true;
	m_cv.notify_all();
}

TexturePackActivity TexturePackManager::Activity() const
{
	TexturePackActivity a;
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_current.empty())
		return a;
	const auto it = m_progress.find(m_current);
	if (it == m_progress.end())
		return a;
	const Progress& p = it->second;
	if (p.state == State::Failed || p.state == State::NeedSpace)
		return a;
	a.active = true;
	a.title = p.title;
	a.state = p.state;
	a.fraction = p.total > 0 ? std::min(1.0, static_cast<double>(p.done) / static_cast<double>(p.total)) : 0;
	a.waiting = static_cast<int>(m_queue.size());
	return a;
}

TexturePackService TexturePackManager::Service()
{
	TexturePackService s;
	s.status = [this](const std::string& serial) { return Status(serial); };
	s.begin = [this](const std::string& serial, int index, const std::string& path, const std::string& header, const std::string& title,
		const std::string& id) { return Begin(serial, index, path, header, title, id); };
	s.import = [this](const std::string& serial, const std::string& path, const std::string& settings, const std::string& header,
		const std::string& title) { return Import(serial, path, settings, header, title); };
	s.cancel = [this](const std::string& serial) { return Cancel(serial); };
	s.remove = [this](const std::string& serial) { return Remove(serial); };
	s.retry = [this] { Retry(); };
	s.activity = [this] { return Activity(); };
	return s;
}
} // namespace fe
