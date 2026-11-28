// Caliza core: asynchronous storage-backed implementation of the interface contract.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace caliza
{
namespace
{
std::string LowerAscii(std::string text)
{
	for (char& c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
}

std::string Trim(const std::string& text)
{
	size_t first = 0;
	size_t last = text.size();
	while (first < last && std::isspace(static_cast<unsigned char>(text[first])))
		first++;
	while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
		last--;
	return text.substr(first, last - first);
}

bool EndsWithCaseInsensitive(const std::string& text, const std::string& suffix)
{
	if (text.size() < suffix.size())
		return false;
	const size_t at = text.size() - suffix.size();
	for (size_t i = 0; i < suffix.size(); i++)
	{
		if (std::tolower(static_cast<unsigned char>(text[at + i])) !=
			std::tolower(static_cast<unsigned char>(suffix[i])))
			return false;
	}
	return true;
}

std::string ExtensionLower(const std::string& name)
{
	const size_t dot = name.rfind('.');
	if (dot == std::string::npos)
		return std::string();
	return LowerAscii(name.substr(dot));
}

std::string StemOf(const std::string& file)
{
	const size_t dot = file.rfind('.');
	return dot == std::string::npos ? file : file.substr(0, dot);
}

int ParseDiscNumber(const std::string& name, std::string* rest)
{
	const std::string stem = StemOf(name);
	const std::string low = LowerAscii(stem);
	int number = 0;
	size_t at = std::string::npos;
	size_t end = 0;
	for (size_t p = low.find("(disc"); p != std::string::npos; p = low.find("(disc", p + 1))
	{
		size_t q = p + 5;
		while (q < low.size() && low[q] == ' ')
			++q;
		size_t digit = q;
		while (digit < low.size() && std::isdigit(static_cast<unsigned char>(low[digit])))
			++digit;
		if (digit == q || digit - q > 2)
			continue;
		size_t e = digit;
		if (low.compare(e, 4, " of ") == 0)
		{
			e += 4;
			const size_t total_digits = e;
			while (e < low.size() && std::isdigit(static_cast<unsigned char>(low[e])))
				++e;
			if (e == total_digits)
				continue;
		}
		if (e >= low.size() || low[e] != ')')
			continue;
		number = std::atoi(low.c_str() + q);
		at = p;
		end = e + 1;
		break;
	}
	if (rest)
	{
		const std::string raw = at == std::string::npos ? low : low.substr(0, at) + low.substr(end);
		std::string compact;
		for (const char c : raw)
			if (!(c == ' ' && !compact.empty() && compact.back() == ' '))
				compact += c;
		*rest = Trim(compact);
	}
	return number;
}

std::string StripDiscTag(const std::string& title)
{
	const std::string low = LowerAscii(title);
	for (size_t p = low.find("(disc"); p != std::string::npos; p = low.find("(disc", p + 1))
	{
		size_t q = p + 5;
		while (q < low.size() && low[q] == ' ')
			++q;
		size_t digit = q;
		while (digit < low.size() && std::isdigit(static_cast<unsigned char>(low[digit])))
			++digit;
		if (digit == q || digit - q > 2)
			continue;
		size_t e = digit;
		if (low.compare(e, 4, " of ") == 0)
		{
			e += 4;
			const size_t total_digits = e;
			while (e < low.size() && std::isdigit(static_cast<unsigned char>(low[e])))
				++e;
			if (e == total_digits)
				continue;
		}
		if (e >= low.size() || low[e] != ')')
			continue;
		std::string raw = title.substr(0, p) + title.substr(e + 1);
		std::string compact;
		for (const char c : raw)
			if (!(c == ' ' && !compact.empty() && compact.back() == ' '))
				compact += c;
		const std::string trimmed = Trim(compact);
		return trimmed.empty() ? title : trimmed;
	}
	return title;
}

std::vector<std::string> ReadM3uPlaylist(const std::string& m3u_path,
	const std::string& dir_path,
	const std::string& root_path)
{
	std::vector<std::string> out;
	std::FILE* f = std::fopen(m3u_path.c_str(), "rb");
	if (!f)
		return out;
	std::string text;
	char buf[4096];
	while (text.size() < 64 * 1024)
	{
		const size_t n = std::fread(buf, 1, sizeof(buf), f);
		if (n == 0)
			break;
		text.append(buf, n);
	}
	std::fclose(f);

	const std::string norm_root = storage::fs::NormalizePath(root_path);
	std::set<std::string> seen;
	size_t pos = 0;
	while (pos < text.size() && out.size() < 32)
	{
		size_t eol = text.find('\n', pos);
		if (eol == std::string::npos)
			eol = text.size();
		std::string line = Trim(text.substr(pos, eol - pos));
		pos = eol + 1;
		if (line.empty() || line[0] == '#')
			continue;
		const std::string raw = line[0] == '/' ? line : storage::fs::JoinPath(dir_path, line);
		const std::string resolved = storage::fs::NormalizePath(raw);
		if (resolved.empty() || !storage::fs::IsPathWithin(resolved, norm_root))
			continue;
		if (!seen.insert(resolved).second)
			continue;
		const std::string base = storage::fs::BaseName(resolved);
		if (!storage::LooksLikeGameImageName(base) || ExtensionLower(base) == ".elf")
			continue;
		struct stat st = {};
		if (lstat(resolved.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		out.push_back(resolved);
	}
	return out;
}

bool ReadAt(int fd, uint64_t offset, void* buffer, size_t size)
{
	uint8_t* out = static_cast<uint8_t*>(buffer);
	while (size)
	{
		const ssize_t n = pread(fd, out, size, static_cast<off_t>(offset));
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
			return false;
		out += n;
		offset += static_cast<uint64_t>(n);
		size -= static_cast<size_t>(n);
	}
	return true;
}

uint32_t Le32(const uint8_t* p)
{
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
		(static_cast<uint32_t>(p[3]) << 24);
}

struct IsoLayout
{
	uint32_t block = 2048;
	uint32_t offset = 0;
	bool detected = false;
};

// Plain ISOs and the raw CD sector layouts also recognized by the existing frontend's fe_games serial reader.
// The compressed-format decompressors deliberately stay behind ImageInspector because libchdr/lz4/zlib are not
// portable host-test dependencies of this core slice.
bool DetectIsoLayout(int fd, IsoLayout* layout)
{
	static constexpr uint32_t kLayouts[][2] = {{2048, 0}, {2352, 24}, {2352, 16}, {2336, 8}, {2448, 24}, {2448, 16}};
	for (const auto& candidate : kLayouts)
	{
		uint8_t pvd[6] = {};
		const uint64_t offset = static_cast<uint64_t>(16) * candidate[0] + candidate[1];
		if (ReadAt(fd, offset, pvd, sizeof(pvd)) && pvd[0] == 1 && std::memcmp(pvd + 1, "CD001", 5) == 0)
		{
			layout->block = candidate[0];
			layout->offset = candidate[1];
			layout->detected = true;
			return true;
		}
	}
	*layout = IsoLayout{};
	return false;
}

bool ReadIsoData(int fd, const IsoLayout& layout, uint32_t lba, void* buffer, size_t size)
{
	uint8_t* out = static_cast<uint8_t*>(buffer);
	for (; size; lba++)
	{
		const size_t chunk = std::min<size_t>(size, 2048);
		if (!ReadAt(fd, static_cast<uint64_t>(lba) * layout.block + layout.offset, out, chunk))
			return false;
		out += chunk;
		size -= chunk;
	}
	return true;
}

// Reads the BOOT2 executable from SYSTEM.CNF in the ISO9660 root and normalizes SLUS_213.51 to SLUS-21351.
// This is the uncompressed-disc subset of fe_games.cpp's serial logic, kept dependency-free for host tests.
std::string SerialFromIso9660(int fd, const IsoLayout& layout)
{
	uint8_t pvd[2048] = {};
	if (!ReadIsoData(fd, layout, 16, pvd, sizeof(pvd)) || pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0)
		return std::string();
	const uint8_t* root = pvd + 156;
	const uint32_t root_lba = Le32(root + 2);
	const uint32_t root_size = std::min<uint32_t>(Le32(root + 10), 64u * 2048u);
	if (!root_size)
		return std::string();
	std::vector<uint8_t> directory(root_size);
	if (!ReadIsoData(fd, layout, root_lba, directory.data(), directory.size()))
		return std::string();
	for (size_t offset = 0; offset < directory.size();)
	{
		const uint8_t record_length = directory[offset];
		if (!record_length)
		{
			offset = (offset / 2048 + 1) * 2048;
			continue;
		}
		if (record_length < 34 || offset + record_length > directory.size())
			break;
		const uint8_t name_length = directory[offset + 32];
		const size_t available = record_length > 33 ? record_length - 33 : 0;
		std::string name(reinterpret_cast<const char*>(directory.data() + offset + 33), std::min<size_t>(name_length, available));
		if (LowerAscii(name).rfind("system.cnf", 0) == 0)
		{
			const uint32_t lba = Le32(directory.data() + offset + 2);
			const uint32_t size = std::min<uint32_t>(Le32(directory.data() + offset + 10), 4096u);
			std::string cnf(size, '\0');
			if (!size || !ReadIsoData(fd, layout, lba, cnf.data(), cnf.size()))
				return std::string();
			const size_t boot = cnf.find("BOOT2");
			const size_t slash = cnf.find('\\', boot == std::string::npos ? 0 : boot);
			if (boot == std::string::npos || slash == std::string::npos)
				return std::string();
			std::string serial;
			for (size_t i = slash + 1; i < cnf.size() && cnf[i] != ';' && cnf[i] != '\r' && cnf[i] != '\n'; i++)
			{
				const char c = cnf[i];
				if (c == '_')
					serial.push_back('-');
				else if (c != '.')
					serial.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
			}
			return serial.size() >= 9 && serial.size() <= 12 ? serial : std::string();
		}
		offset += record_length;
	}
	return std::string();
}

bool HasCardExtension(const std::string& name)
{
	static constexpr const char* kExtensions[] = {".ps2", ".mcr", ".mcd", ".bin", ".mc2"};
	for (const char* extension : kExtensions)
	{
		if (EndsWithCaseInsensitive(name, extension))
			return true;
	}
	return false;
}

uint64_t NowUnix()
{
	return static_cast<uint64_t>(std::time(nullptr));
}
} // namespace

void PosixImageInspector::ParseDisplayName(const std::string& stem, std::string* title, std::string* region)
{
	static const std::set<std::string> kRegions = {"usa", "europe", "japan", "korea", "asia", "world", "australia", "france",
		"germany", "italy", "spain", "uk", "canada", "brazil", "russia", "china", "taiwan", "netherlands", "sweden"};
	*title = Trim(stem);
	region->clear();
	const size_t open = stem.find('(');
	if (open == std::string::npos)
		return;
	const size_t close = stem.find(')', open + 1);
	if (close == std::string::npos)
		return;
	std::string inside = Trim(stem.substr(open + 1, close - open - 1));
	const std::string lower = LowerAscii(inside);
	bool region_found = false;
	for (const std::string& known : kRegions)
	{
		if (lower == known || lower.compare(0, known.size(), known) == 0)
		{
			region_found = true;
			break;
		}
	}
	if (!region_found)
		return;
	*region = inside;
	std::string before = Trim(stem.substr(0, open));
	if (!before.empty())
		*title = before;
}

bool PosixImageInspector::Inspect(GameEntry& entry)
{
	const std::string extension = ExtensionLower(entry.file);
	int fd = open(entry.path.c_str(), O_RDONLY);
	if (fd < 0)
	{
		entry.damaged = true;
		entry.can_launch = false;
		entry.problem = "image can't be opened by this process";
		return true;
	}
	bool valid = true;
	std::string problem;
	if (entry.bytes == 0)
	{
		valid = false;
		problem = "image is empty";
	}
	else if (extension == ".elf")
	{
		uint8_t magic[4] = {};
		if (!ReadAt(fd, 0, magic, sizeof(magic)) || magic[0] != 0x7f || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F')
		{
			valid = false;
			problem = "file has an .elf extension but no ELF header";
		}
	}
	else if (extension == ".chd")
	{
		char magic[8] = {};
		if (!ReadAt(fd, 0, magic, sizeof(magic)) || std::memcmp(magic, "MComprHD", 8) != 0)
		{
			valid = false;
			problem = "This file has a .chd extension, but no CHD header.";
		}
	}
	else if (extension == ".cso" || extension == ".zso")
	{
		const bool zso = extension == ".zso";
		const char* fmt_name = zso ? "ZSO" : "CSO";
		uint8_t hdr[24] = {};
		if (entry.bytes < sizeof(hdr) || !ReadAt(fd, 0, hdr, sizeof(hdr)))
		{
			valid = false;
			problem = std::string("This ") + fmt_name + " file is cut short or damaged.";
		}
		else if (std::memcmp(hdr, zso ? "ZISO" : "CISO", 4) != 0)
		{
			valid = false;
			problem = std::string("This file has a ") + extension + " extension, but its " + fmt_name + " header is invalid.";
		}
		else
		{
			const uint64_t total_bytes = static_cast<uint64_t>(Le32(hdr + 8)) | (static_cast<uint64_t>(Le32(hdr + 12)) << 32);
			const uint32_t frame_size = Le32(hdr + 16);
			const uint8_t version = hdr[20];
			const uint8_t align = hdr[21];
			if (version > 1)
			{
				valid = false;
				problem = std::string("This ") + fmt_name + " uses an unsupported version (only v1 is supported).";
			}
			else if (frame_size < 2048 || frame_size > (1u << 20) || (frame_size & (frame_size - 1)) != 0)
			{
				valid = false;
				problem = std::string("This ") + fmt_name + " header has an invalid frame size.";
			}
			else if (align > 8)
			{
				valid = false;
				problem = std::string("This ") + fmt_name + " header has an invalid index alignment.";
			}
			else if (total_bytes == 0)
			{
				valid = false;
				problem = std::string("This ") + fmt_name + " header reports an empty disc.";
			}
			else
			{
				const uint64_t frames = (total_bytes + frame_size - 1) / frame_size;
				if (frames > 0x1fffffffULL || 24ULL + (frames + 1ULL) * 4ULL > entry.bytes)
				{
					valid = false;
					problem = std::string("This ") + fmt_name + " file is cut short or damaged.";
				}
			}
		}
	}
	else if (extension == ".iso" || extension == ".bin" || extension == ".img")
	{
		IsoLayout iso_layout;
		if (!DetectIsoLayout(fd, &iso_layout))
		{
			valid = false;
			problem = "no ISO 9660 volume descriptor at sector 16";
		}
		else
			entry.serial = SerialFromIso9660(fd, iso_layout);
	}
	close(fd);

	ParseDisplayName(StemOf(entry.file), &entry.title, &entry.region);
	if (entry.title.empty())
		entry.title = entry.file;
	entry.damaged = !valid;
	entry.can_launch = valid;
	entry.problem = valid ? std::string() : problem;
	return true;
}

namespace
{
struct ScanControl
{
	std::atomic<bool> cancel{false};
};

struct ScanJob
{
	uint64_t batch = 0;
	std::string source_id;
	std::string root_path;
	uint32_t max_depth = 32;
	std::shared_ptr<ScanControl> control;
};

struct ScanOutcome
{
	ScanState state = ScanState::Complete;
	std::string error;
	uint32_t files_seen = 0;
	uint32_t games_found = 0;
	std::vector<std::string> seen_ids;
};

class ScanEngine
{
public:
	using ProgressCallback = std::function<void(const ScanJob&, const ScanProgress&, const GameEntry*)>;
	using DoneCallback = std::function<void(const ScanJob&, const ScanOutcome&)>;

	ScanEngine(std::shared_ptr<ImageInspector> inspector, uint32_t progress_interval, uint32_t step_delay_microseconds)
		: m_inspector(std::move(inspector))
		, m_progress_interval(std::max<uint32_t>(1, progress_interval))
		, m_step_delay_microseconds(step_delay_microseconds)
	{
	}

	~ScanEngine()
	{
		Shutdown();
	}

	void Start(ProgressCallback progress, DoneCallback done)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_thread.joinable())
			return;
		m_progress = std::move(progress);
		m_done = std::move(done);
		m_stop = false;
		m_thread = std::thread(&ScanEngine::WorkerMain, this);
	}

	void Shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stop = true;
			if (m_active_control)
				m_active_control->cancel.store(true, std::memory_order_release);
			for (ScanJob& job : m_queue)
				job.control->cancel.store(true, std::memory_order_release);
		}
		m_cv.notify_all();
		if (m_thread.joinable())
			m_thread.join();
	}

	bool Enqueue(ScanJob job)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_active_source == job.source_id)
				return false;
			for (const ScanJob& queued : m_queue)
			{
				if (queued.source_id == job.source_id)
					return false;
			}
			m_queue.push_back(std::move(job));
		}
		m_cv.notify_one();
		return true;
	}

	uint32_t Cancel(const std::string& source_id)
	{
		uint32_t count = 0;
		std::lock_guard<std::mutex> lock(m_mutex);
		const bool all = source_id.empty();
		if (!m_active_source.empty() && (all || m_active_source == source_id))
		{
			m_active_control->cancel.store(true, std::memory_order_release);
			count++;
		}
		for (ScanJob& job : m_queue)
		{
			if (all || job.source_id == source_id)
			{
				job.control->cancel.store(true, std::memory_order_release);
				count++;
			}
		}
		return count;
	}

private:
	void WorkerMain()
	{
		for (;;)
		{
			ScanJob job;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
				if (m_stop && m_queue.empty())
					return;
				job = std::move(m_queue.front());
				m_queue.pop_front();
				m_active_source = job.source_id;
				m_active_control = job.control;
			}
			ScanOutcome outcome = Scan(job);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_active_source.clear();
				m_active_control.reset();
			}
			if (m_done)
				m_done(job, outcome);
		}
	}

	bool Cancelled(const ScanJob& job) const
	{
		return job.control->cancel.load(std::memory_order_acquire);
	}

	void Publish(const ScanJob& job, ScanProgress& progress, const GameEntry* game, bool force)
	{
		if (!force && progress.files_seen % m_progress_interval != 0)
			return;
		progress.cancel_requested = Cancelled(job);
		if (m_progress)
			m_progress(job, progress, game);
	}

	ScanOutcome Scan(const ScanJob& job)
	{
		ScanOutcome outcome;
		ScanProgress progress;
		progress.state = ScanState::Scanning;
		progress.source_id = job.source_id;
		progress.current_path = job.root_path;
		Publish(job, progress, nullptr, true);

		if (Cancelled(job))
		{
			outcome.state = ScanState::Cancelled;
			outcome.error = "scan cancelled before it started";
			return outcome;
		}
		if (!storage::fs::IsDirectory(job.root_path) || !storage::fs::IsReadableDirectory(job.root_path))
		{
			outcome.state = ScanState::Failed;
			outcome.error = "scan root is missing or unreadable";
			return outcome;
		}

		std::vector<std::pair<std::string, uint32_t>> stack;
		stack.emplace_back(job.root_path, 0);
		std::set<std::string> suppressed_disc_paths;
		std::string first_error;
		while (!stack.empty())
		{
			if (Cancelled(job))
			{
				outcome.state = ScanState::Cancelled;
				outcome.error = "scan cancelled";
				break;
			}
			struct stat root_st = {};
			if (lstat(job.root_path.c_str(), &root_st) != 0 || !S_ISDIR(root_st.st_mode))
			{
				outcome.state = ScanState::Failed;
				outcome.error = "scan root disappeared during the scan";
				break;
			}

			const auto current = stack.back();
			stack.pop_back();
			progress.current_path = current.first;
			DIR* dir = opendir(current.first.c_str());
			if (!dir)
			{
				if (first_error.empty())
					first_error = "can't read " + current.first;
				continue;
			}
			std::vector<std::string> names;
			int read_error = 0;
			for (;;)
			{
				errno = 0;
				const dirent* entry = readdir(dir);
				if (!entry)
				{
					read_error = errno;
					break;
				}
				if (entry->d_name[0] != '.')
					names.emplace_back(entry->d_name);
			}
			closedir(dir);
			if (read_error != 0 && first_error.empty())
				first_error = "Can't list " + current.first + ": " + std::strerror(read_error);
			std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
				const std::string la = LowerAscii(a);
				const std::string lb = LowerAscii(b);
				return la == lb ? a < b : la < lb;
			});

			struct DiscSetMeta
			{
				int disc_number = 0;
				std::string disc_set_key;
				std::vector<std::string> disc_paths;
				std::string playlist_title;
			};
			std::map<std::string, DiscSetMeta> primary_sets;

			// 1) Read .m3u playlists in this folder first so playlist order wins over filename heuristics.
			for (const std::string& name : names)
			{
				if (ExtensionLower(name) != ".m3u")
					continue;
				const std::string m3u_path = storage::fs::JoinPath(current.first, name);
				struct stat m3u_st = {};
				if (lstat(m3u_path.c_str(), &m3u_st) != 0 || !S_ISREG(m3u_st.st_mode))
					continue;
				std::vector<std::string> discs = ReadM3uPlaylist(m3u_path, current.first, job.root_path);
				if (discs.empty())
					continue;
				const std::string primary = discs.front();
				if (suppressed_disc_paths.count(primary) || primary_sets.count(primary))
					continue;
				DiscSetMeta meta;
				meta.disc_number = 1;
				meta.disc_set_key = "m3u:" + LowerAscii(StemOf(name));
				meta.disc_paths = discs;
				meta.playlist_title = Trim(StemOf(name));
				primary_sets[primary] = std::move(meta);
				for (size_t i = 1; i < discs.size(); ++i)
					suppressed_disc_paths.insert(discs[i]);
			}

			// 2) Group remaining "(Disc N)" / "(Disc N of M)" images in this folder by disc_set_key.
			struct DiscCandidate
			{
				int disc_number = 0;
				std::string path;
				std::string name;
			};
			std::map<std::string, std::vector<DiscCandidate>> disc_groups;
			for (const std::string& name : names)
			{
				if (!storage::LooksLikeGameImageName(name) || ExtensionLower(name) == ".elf")
					continue;
				const std::string child = storage::fs::NormalizePath(storage::fs::JoinPath(current.first, name));
				if (suppressed_disc_paths.count(child) || primary_sets.count(child))
					continue;
				struct stat st = {};
				if (lstat(child.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
					continue;
				std::string key;
				const int num = ParseDiscNumber(name, &key);
				if (num > 0 && !key.empty())
					disc_groups[key].push_back({num, child, name});
			}
			for (auto& kv : disc_groups)
			{
				auto& group = kv.second;
				std::sort(group.begin(), group.end(), [](const DiscCandidate& a, const DiscCandidate& b) {
					if (a.disc_number != b.disc_number)
						return a.disc_number < b.disc_number;
					return LowerAscii(a.name) < LowerAscii(b.name);
				});
				DiscSetMeta meta;
				meta.disc_number = group.front().disc_number;
				meta.disc_set_key = kv.first;
				for (const DiscCandidate& c : group)
					meta.disc_paths.push_back(c.path);
				primary_sets[group.front().path] = std::move(meta);
				for (size_t i = 1; i < group.size(); ++i)
					suppressed_disc_paths.insert(group[i].path);
			}

			auto inspect_and_emit = [&](const std::string& file_path, const std::string& file_name, uint64_t bytes, const DiscSetMeta* meta) {
				GameEntry game;
				game.id = file_path;
				game.path = file_path;
				game.file = file_name;
				game.source_id = job.source_id;
				game.bytes = bytes;
				if (meta)
				{
					game.disc_number = meta->disc_number;
					game.disc_set_key = meta->disc_set_key;
					game.disc_paths = meta->disc_paths;
				}
				if (!m_inspector || m_inspector->Inspect(game))
				{
					if (game.disc_paths.size() > 1)
					{
						if (meta && !meta->playlist_title.empty() && game.title == StemOf(file_name))
							game.title = meta->playlist_title;
						else
							game.title = StripDiscTag(game.title);
					}
					progress.games_found++;
					outcome.seen_ids.push_back(game.id);
					Publish(job, progress, &game, true);
				}
			};

			// Emit any .m3u primary disc that resides in a different subfolder right now and suppress it there.
			const std::string norm_current = storage::fs::NormalizePath(current.first);
			for (const auto& kv : primary_sets)
			{
				if (Cancelled(job))
					break;
				const std::string& primary_path = kv.first;
				const size_t slash = primary_path.rfind('/');
				const std::string parent_dir = slash == std::string::npos ? std::string() : primary_path.substr(0, slash);
				if (parent_dir == norm_current)
					continue;
				struct stat st = {};
				if (lstat(primary_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
					continue;
				suppressed_disc_paths.insert(primary_path);
				progress.files_seen++;
				progress.current_path = primary_path;
				inspect_and_emit(primary_path, storage::fs::BaseName(primary_path), static_cast<uint64_t>(st.st_size), &kv.second);
			}

			for (const std::string& name : names)
			{
				if (Cancelled(job))
					break;
				// Check the root before trusting child lstat failures: after a USB pull every child lookup fails,
				// which must be reported as a lost scan root rather than a clean scan with missing entries.
				struct stat live_root_before_entry = {};
				if (lstat(job.root_path.c_str(), &live_root_before_entry) != 0 || !S_ISDIR(live_root_before_entry.st_mode))
				{
					outcome.state = ScanState::Failed;
					outcome.error = "scan root disappeared during the scan";
					break;
				}
				const std::string child = storage::fs::JoinPath(current.first, name);
				struct stat st = {};
				if (lstat(child.c_str(), &st) != 0)
				{
					if (first_error.empty())
						first_error = "an entry disappeared during the scan";
					continue;
				}
				if (S_ISDIR(st.st_mode))
				{
					if (current.second < job.max_depth)
						stack.emplace_back(child, current.second + 1);
					else if (first_error.empty())
						first_error = "maximum scan depth reached";
					continue;
				}
				if (!S_ISREG(st.st_mode))
					continue;

				progress.files_seen++;
				progress.current_path = child;
				if (storage::LooksLikeGameImageName(name))
				{
					const std::string norm_child = storage::fs::NormalizePath(child);
					if (!suppressed_disc_paths.count(norm_child))
					{
						const auto meta_it = primary_sets.find(norm_child);
						inspect_and_emit(child, name, static_cast<uint64_t>(st.st_size),
							meta_it == primary_sets.end() ? nullptr : &meta_it->second);
					}
				}
				Publish(job, progress, nullptr, false);
				if (m_step_delay_microseconds)
					std::this_thread::sleep_for(std::chrono::microseconds(m_step_delay_microseconds));
			}
		}

		outcome.files_seen = progress.files_seen;
		outcome.games_found = progress.games_found;
		// The inner entry loop can break on cancellation just as the directory stack becomes empty. Re-check
		// the cooperative cancellation edge here so the terminal event cannot be misreported as a successful scan.
		if (outcome.state != ScanState::Failed && Cancelled(job))
		{
			outcome.state = ScanState::Cancelled;
			outcome.error = "scan cancelled";
		}
		else if (outcome.state != ScanState::Cancelled && outcome.state != ScanState::Failed)
		{
			outcome.state = first_error.empty() ? ScanState::Complete : ScanState::Failed;
			outcome.error = first_error;
		}
		return outcome;
	}

	std::shared_ptr<ImageInspector> m_inspector;
	uint32_t m_progress_interval;
	uint32_t m_step_delay_microseconds;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::deque<ScanJob> m_queue;
	std::thread m_thread;
	bool m_stop = false;
	std::string m_active_source;
	std::shared_ptr<ScanControl> m_active_control;
	ProgressCallback m_progress;
	DoneCallback m_done;
};

struct CommandExecution
{
	CommandStatus status = CommandStatus::Failed;
	std::string message;
	bool asynchronous = false;
};

struct ScanBatch
{
	CommandId command = 0;
	CommandType type = CommandType::None;
	uint32_t remaining = 0;
	std::string error;
	bool cancelled = false;
};


BackendOptions NormalizeBackendOptions(BackendOptions options)
{
	// A host-test data root normally implies the matching internal discovery root. If the caller explicitly
	// customized discovery.internal_root to something other than the console default, keep that choice.
	if (options.discovery.internal_root == dataroot::kCanonicalDataRoot && options.data_root != dataroot::kCanonicalDataRoot)
		options.discovery.internal_root = options.data_root;
	return options;
}

uint64_t CardBytesForMb(uint64_t mb)
{
	return 1024ull * 528ull * 2ull * mb;
}

bool ValidCardName(const std::string& name)
{
	if (name.empty() || name.size() > 80 || name[0] == '.')
		return false;
	return name.find('/') == std::string::npos && name.find('\\') == std::string::npos && name.find("..") == std::string::npos;
}
} // namespace

class StorageBackend::Impl
{
public:
	explicit Impl(const BackendOptions& input_options)
		: options(NormalizeBackendOptions(input_options))
		, layout(dataroot::DataRootLayout::ForRoot(input_options.data_root))
		, store(storage::fs::JoinPath(storage::fs::JoinPath(input_options.data_root, "library"), "sources.ini"))
		, scan_engine(input_options.image_inspector ? input_options.image_inspector : std::make_shared<PosixImageInspector>(),
			  input_options.scan_progress_interval_files, input_options.scan_step_delay_microseconds)
	{
		std::string error;
		if (options.create_data_root && !dataroot::EnsureDataRoot(layout, &error))
			initial_errors.push_back(error);
		if (!store.Load(&error) && !error.empty())
			initial_errors.push_back(error);

		snapshot.revision = 0;
		snapshot.product = "Caliza";
		snapshot.build_tag = "caliza-core";
		snapshot.data_root = layout.root;
		snapshot.legacy_data_root = options.legacy_data_root;
		snapshot.diagnostics.build_id = "caliza-core";
		snapshot.diagnostics.console_status = initial_errors.empty() ? "host/POSIX storage backend" : "storage backend started with errors";
		for (const std::string& initial_error : initial_errors)
			snapshot.diagnostics.log_paths.push_back(initial_error);
		snapshot.scan.state = ScanState::Idle;
		snapshot.vm.state = VmState::Unavailable;
		snapshot.vm.message = "console VM adapter is not bound";
		snapshot.bios.path = layout.bios;
		snapshot.time_unix = NowUnix();
		RefreshBiosLocked();
		RefreshMemoryCardsLocked();
		ApplyVolumesLocked(storage::DiscoverVolumes(options.discovery));
		BumpRevisionLocked();

		scan_engine.Start(
			[this](const ScanJob& job, const ScanProgress& progress, const GameEntry* game) { OnScanProgress(job, progress, game); },
			[this](const ScanJob& job, const ScanOutcome& outcome) { OnScanDone(job, outcome); });
		stop_commands.store(false, std::memory_order_release);
		command_thread = std::thread(&Impl::CommandMain, this);
	}

	~Impl()
	{
		stop_commands.store(true, std::memory_order_release);
		command_cv.notify_all();
		if (command_thread.joinable())
			command_thread.join();
		scan_engine.Shutdown();
	}

	Snapshot GetSnapshot()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return snapshot;
	}

	CommandId Post(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const CommandId id = next_command++;
		commands.emplace_back(id, command);
		PushEventLocked(id, command.type, CommandStatus::Queued, "queued");
		command_cv.notify_one();
		return id;
	}

	std::vector<CommandEvent> PollEvents()
	{
		std::lock_guard<std::mutex> lock(mutex);
		std::vector<CommandEvent> out;
		out.swap(events);
		return out;
	}

	FolderListing BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit)
	{
		std::vector<VolumeInfo> volumes;
		{
			std::lock_guard<std::mutex> lock(mutex);
			volumes = snapshot.volumes;
		}
		return storage::BrowseFolderPosix(path, filter, offset, limit, volumes);
	}

	void RefreshStorage()
	{
		std::vector<VolumeInfo> volumes = storage::DiscoverVolumes(options.discovery);
		std::lock_guard<std::mutex> lock(mutex);
		ApplyVolumesLocked(std::move(volumes));
		BumpRevisionLocked();
	}

private:
	void BumpRevisionLocked()
	{
		snapshot.revision++;
		snapshot.time_unix = NowUnix();
	}

	void PushEventLocked(CommandId id, CommandType type, CommandStatus status, const std::string& message)
	{
		CommandEvent event;
		event.command = id;
		event.type = type;
		event.status = status;
		event.revision = snapshot.revision;
		event.message = message;
		events.push_back(std::move(event));
	}

	LibrarySource MakeAutomaticSource(const std::string& id, const std::string& path, const std::string& label, bool present,
		bool readable) const
	{
		LibrarySource source;
		source.id = id;
		source.path = path;
		source.label = label;
		source.user_added = false;
		if (!present)
		{
			source.state = SourceState::Disconnected;
			source.problem = "volume is not present";
		}
		else if (!readable)
		{
			source.state = SourceState::Unreadable;
			source.problem = "folder is not readable by this process";
		}
		else
			source.state = SourceState::Connected;
		return source;
	}

	void ApplyVolumesLocked(std::vector<VolumeInfo> volumes)
	{
		std::map<std::string, LibrarySource> previous;
		for (const LibrarySource& source : snapshot.sources)
			previous[source.id] = source;
		snapshot.volumes = std::move(volumes);
		std::vector<LibrarySource> sources;

		const bool internal_present = storage::fs::IsDirectory(layout.games);
		sources.push_back(MakeAutomaticSource("internal:games", layout.games, "Internal games", internal_present,
			internal_present && storage::fs::IsReadableDirectory(layout.games)));

		if (options.discover_volume_sources)
		{
			for (const VolumeInfo& volume : snapshot.volumes)
			{
				if ((volume.kind != VolumeKind::USB && volume.kind != VolumeKind::Extended) || !volume.present)
					continue;
				const std::string id = std::string(volume.kind == VolumeKind::USB ? "volume:usb" : "volume:ext") +
					std::to_string(volume.slot);
				LibrarySource source = MakeAutomaticSource(id, volume.root_path, volume.label, volume.present, volume.readable);
				source.volume_id = volume.id;
				if (!source.problem.empty() && volume.extended_managed)
					source.problem += "; PS4-managed extended-storage evidence is present";
				sources.push_back(std::move(source));
			}
		}

		std::vector<LibrarySource> saved = store.Resolve(snapshot.volumes);
		sources.insert(sources.end(), saved.begin(), saved.end());
		for (LibrarySource& source : sources)
		{
			const auto old = previous.find(source.id);
			if (old != previous.end())
			{
				source.game_count = old->second.game_count;
				source.last_scan = old->second.last_scan;
			}
			if (scanning_sources.count(source.id))
				source.state = SourceState::Scanning;
		}
		snapshot.sources = std::move(sources);
	}

	bool DirectoryContainsBiosLikeFile(const std::string& path)
	{
		DIR* dir = opendir(path.c_str());
		if (!dir)
			return false;
		bool found = false;
		while (const dirent* entry = readdir(dir))
		{
			if (entry->d_name[0] == '.')
				continue;
			const std::string file = storage::fs::JoinPath(path, entry->d_name);
			struct stat st = {};
			if (stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
				continue;
			const std::string extension = ExtensionLower(entry->d_name);
			if (extension == ".mec" || extension == ".nvm" || extension == ".zip" || extension == ".rar" || extension == ".7z")
				continue;
			// PCSX2 BIOS images are multi-megabyte ROM dumps. This conservative size floor avoids treating notes or
			// memory-card files as BIOS evidence; exact validation remains the console adapter's responsibility.
			if (static_cast<uint64_t>(st.st_size) >= 1024u * 1024u)
			{
				found = true;
				break;
			}
		}
		closedir(dir);
		return found;
	}

	void RefreshBiosLocked()
	{
		snapshot.bios.checking = false;
		snapshot.bios.present = storage::fs::IsDirectory(snapshot.bios.path) && DirectoryContainsBiosLikeFile(snapshot.bios.path);
		snapshot.bios.problem = snapshot.bios.present ? std::string() : "no BIOS-size ROM file is visible in the configured folder";
	}

	void RefreshMemoryCardsLocked()
	{
		snapshot.memory_cards.clear();
		std::vector<std::string> names;
		if (DIR* dir = opendir(layout.memcards.c_str()))
		{
			while (const dirent* entry = readdir(dir))
			{
				if (entry->d_name[0] != '.' && HasCardExtension(entry->d_name))
					names.emplace_back(entry->d_name);
			}
			closedir(dir);
		}
		std::sort(names.begin(), names.end());
		for (int slot = 0; slot < 2; slot++)
		{
			MemoryCardInfo card;
			card.slot = slot;
			if (static_cast<size_t>(slot) < names.size())
			{
				card.present = true;
				card.enabled = true;
				card.file = storage::fs::JoinPath(layout.memcards, names[static_cast<size_t>(slot)]);
				card.bytes = storage::fs::FileSize(card.file);
			}
			snapshot.memory_cards.push_back(std::move(card));
		}
	}

	void CommandMain()
	{
		for (;;)
		{
			std::pair<CommandId, Command> work;
			{
				std::unique_lock<std::mutex> lock(mutex);
				command_cv.wait(lock, [this] { return stop_commands.load(std::memory_order_acquire) || !commands.empty(); });
				if (stop_commands.load(std::memory_order_acquire) && commands.empty())
					return;
				work = std::move(commands.front());
				commands.pop_front();
				PushEventLocked(work.first, work.second.type, CommandStatus::Running, "running");
			}
			CommandExecution execution = ExecuteCommand(work.first, work.second);
			if (!execution.asynchronous)
			{
				std::lock_guard<std::mutex> lock(mutex);
				PushEventLocked(work.first, work.second.type, execution.status, execution.message);
			}
		}
	}

	CommandExecution ExecuteCommand(CommandId command_id, const Command& command)
	{
		switch (command.type)
		{
		case CommandType::RescanAll: return ExecuteRescanAll(command_id);
		case CommandType::ScanSource: return ExecuteScanSource(command_id, command);
		case CommandType::CancelScan: return ExecuteCancelScan(command);
		case CommandType::AddLibraryFolder: return ExecuteAddLibraryFolder(command);
		case CommandType::RemoveLibraryFolder: return ExecuteRemoveLibraryFolder(command);
		case CommandType::RelocateLibraryFolder: return ExecuteRelocateLibraryFolder(command);
		case CommandType::SetBiosFolder: return ExecuteSetBiosFolder(command);
		case CommandType::CreateMemoryCard: return ExecuteCreateMemoryCard(command);
		case CommandType::SetAccessibility: return ExecuteSetAccessibility(command);
		case CommandType::GenerateReport: return ExecuteGenerateReport(command);
		default:
			return UnsupportedPlatformCommand(command);
		}
	}

	CommandExecution UnsupportedPlatformCommand(const Command& command)
	{
		if (command.type == CommandType::LaunchGame)
		{
			std::lock_guard<std::mutex> lock(mutex);
			const auto game = std::find_if(snapshot.games.begin(), snapshot.games.end(), [&command](const GameEntry& entry) {
				return entry.id == command.id || entry.path == command.id;
			});
			if (game == snapshot.games.end())
				return {CommandStatus::Failed, "game is not in the catalog", false};
			if (game->damaged || !game->can_launch)
				return {CommandStatus::Failed, game->problem.empty() ? "game image is damaged" : game->problem, false};
			if (!snapshot.bios.present)
				return {CommandStatus::Failed, "no BIOS is available", false};
		}
		return {CommandStatus::Failed, "console platform adapter is not bound for this command", false};
	}

	CommandExecution ExecuteRescanAll(CommandId command_id)
	{
		RefreshStorage();
		std::vector<LibrarySource> sources;
		{
			std::lock_guard<std::mutex> lock(mutex);
			for (const LibrarySource& source : snapshot.sources)
			{
				if (source.state == SourceState::Connected)
					sources.push_back(source);
			}
			for (const LibrarySource& source : sources)
			{
				if (scanning_sources.count(source.id))
					return {CommandStatus::Failed, "a scan is already active for " + source.id, false};
			}
		}
		if (sources.empty())
			return {CommandStatus::Succeeded, "no connected sources to scan", false};

		std::vector<std::string> ids;
		for (const LibrarySource& source : sources)
			ids.push_back(source.id);
		const uint64_t batch = RegisterScanBatch(command_id, CommandType::RescanAll, ids);
		for (const LibrarySource& source : sources)
			EnqueueScanJob(batch, source);
		return {CommandStatus::Running, "scanning " + std::to_string(sources.size()) + " sources", true};
	}

	CommandExecution ExecuteScanSource(CommandId command_id, const Command& command)
	{
		RefreshStorage();
		std::lock_guard<std::mutex> lock(mutex);
		const auto it = std::find_if(snapshot.sources.begin(), snapshot.sources.end(), [&command](const LibrarySource& source) {
			return source.id == command.id;
		});
		if (it == snapshot.sources.end())
			return {CommandStatus::Failed, "source is not known", false};
		if (it->state != SourceState::Connected)
			return {CommandStatus::Failed, it->problem.empty() ? "source is not connected" : it->problem, false};
		if (scanning_sources.count(it->id))
			return {CommandStatus::Failed, "a scan is already active for this source", false};
		const uint64_t batch = RegisterScanBatchLocked(command_id, CommandType::ScanSource, {it->id});
		EnqueueScanJob(batch, *it);
		return {CommandStatus::Running, "scanning " + it->label, true};
	}

	CommandExecution ExecuteCancelScan(const Command& command)
	{
		const uint32_t count = scan_engine.Cancel(command.id);
		if (!count)
			return {CommandStatus::Failed, "no matching scan is active or queued", false};
		std::lock_guard<std::mutex> lock(mutex);
		if ((snapshot.scan.state == ScanState::Scanning || snapshot.scan.state == ScanState::Cancelling) &&
			(command.id.empty() || snapshot.scan.source_id == command.id))
		{
			snapshot.scan.cancel_requested = true;
			snapshot.scan.state = ScanState::Cancelling;
			BumpRevisionLocked();
		}
		return {CommandStatus::Succeeded, "cancellation requested for " + std::to_string(count) + " scan job(s)", false};
	}

	CommandExecution ExecuteAddLibraryFolder(const Command& command)
	{
		RefreshStorage();
		std::vector<VolumeInfo> volumes;
		{
			std::lock_guard<std::mutex> lock(mutex);
			volumes = snapshot.volumes;
		}
		const int volume_index = storage::FindVolumeIndexForPath(command.path, volumes);
		if (volume_index < 0)
			return {CommandStatus::Failed, "folder is not on a discovered volume", false};
		std::lock_guard<std::mutex> lock(mutex);
		const storage::AddSavedSourceResult added = store.Add(command.path, volumes[static_cast<size_t>(volume_index)], command.value);
		if (added.status != storage::AddSavedSourceStatus::Added)
			return {CommandStatus::Failed, added.message, false};
		ApplyVolumesLocked(storage::DiscoverVolumes(options.discovery));
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "saved folder link added as " + added.source_id, false};
	}

	CommandExecution ExecuteRemoveLibraryFolder(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const storage::SavedSourceMutationResult removed = store.Remove(command.id);
		if (removed.status != storage::SavedSourceMutationStatus::Changed)
			return {CommandStatus::Failed, removed.message.empty() ? "saved folder link is not known" : removed.message, false};
		ApplyVolumesLocked(storage::DiscoverVolumes(options.discovery));
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, removed.message, false};
	}

	CommandExecution ExecuteRelocateLibraryFolder(const Command& command)
	{
		RefreshStorage();
		std::vector<VolumeInfo> volumes;
		{
			std::lock_guard<std::mutex> lock(mutex);
			volumes = snapshot.volumes;
		}
		const int volume_index = storage::FindVolumeIndexForPath(command.path, volumes);
		if (volume_index < 0)
			return {CommandStatus::Failed, "replacement folder is not on a discovered volume", false};
		std::lock_guard<std::mutex> lock(mutex);
		const storage::SavedSourceMutationResult relocated = store.Relocate(command.id, command.path, volumes[static_cast<size_t>(volume_index)]);
		if (relocated.status != storage::SavedSourceMutationStatus::Changed)
			return {CommandStatus::Failed, relocated.message, false};
		ApplyVolumesLocked(storage::DiscoverVolumes(options.discovery));
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, relocated.message, false};
	}

	CommandExecution ExecuteSetBiosFolder(const Command& command)
	{
		const std::string path = command.path.empty() ? command.id : command.path;
		if (path.empty() || !storage::fs::IsDirectory(path) || !storage::fs::IsReadableDirectory(path))
			return {CommandStatus::Failed, "BIOS folder does not exist or is not readable", false};
		std::lock_guard<std::mutex> lock(mutex);
		snapshot.bios.path = storage::fs::NormalizePath(path);
		RefreshBiosLocked();
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, snapshot.bios.present ? "BIOS folder configured" : "BIOS folder configured; no BIOS found", false};
	}

	CommandExecution ExecuteCreateMemoryCard(const Command& command)
	{
		std::string name = !command.path.empty() ? command.path : (!command.id.empty() ? command.id : command.value);
		if (!ValidCardName(name))
			return {CommandStatus::Failed, "memory-card name is empty, too long, or contains a path", false};
		const uint64_t mb = command.number == 0 ? 8 : static_cast<uint64_t>(command.number);
		if (mb != 8 && mb != 16 && mb != 32 && mb != 64)
			return {CommandStatus::Failed, "memory-card size must be 8, 16, 32 or 64 MB", false};
		if (!HasCardExtension(name))
			name += ".ps2";
		const std::string path = storage::fs::JoinPath(layout.memcards, name);
		if (storage::fs::Exists(path))
			return {CommandStatus::Failed, "a memory card with this name already exists", false};

		const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (fd < 0)
			return {CommandStatus::Failed, "can't create memory-card file", false};
		std::vector<char> fill(32768, static_cast<char>(0xff));
		uint64_t remaining = CardBytesForMb(mb);
		bool write_failed = false;
		while (remaining)
		{
			const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, fill.size()));
			size_t written = 0;
			while (written < chunk)
			{
				const ssize_t n = write(fd, fill.data() + written, chunk - written);
				if (n < 0)
				{
					if (errno == EINTR)
						continue;
					write_failed = true;
					break;
				}
				written += static_cast<size_t>(n);
			}
			if (write_failed)
				break;
			remaining -= chunk;
		}
		const bool flush_failed = !write_failed && fsync(fd) != 0;
		const bool close_failed = close(fd) != 0;
		if (write_failed || flush_failed || close_failed)
		{
			unlink(path.c_str());
			return {CommandStatus::Failed, "can't write the blank memory card", false};
		}
		std::lock_guard<std::mutex> lock(mutex);
		RefreshMemoryCardsLocked();
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "blank memory card created", false};
	}

	CommandExecution ExecuteSetAccessibility(const Command& command)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const std::string id = command.id.empty() ? command.path : command.id;
		const std::string value = command.value;
		const bool truthy = value == "1" || LowerAscii(value) == "true" || LowerAscii(value) == "on" || command.number != 0;
		if (id == "text_scale" || id == "text_scale_percent")
		{
			uint32_t scale = command.number != 0 ? static_cast<uint32_t>(command.number) : static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
			if (scale < 50 || scale > 200)
				return {CommandStatus::Failed, "text scale must be between 50 and 200 percent", false};
			snapshot.accessibility.text_scale_percent = scale;
		}
		else if (id == "high_contrast")
			snapshot.accessibility.high_contrast = truthy;
		else if (id == "reduce_motion")
			snapshot.accessibility.reduce_motion = truthy;
		else if (id == "large_focus")
			snapshot.accessibility.large_focus = truthy;
		else
			return {CommandStatus::Failed, "unknown accessibility setting", false};
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "accessibility updated", false};
	}

	CommandExecution ExecuteGenerateReport(const Command& command)
	{
		Snapshot copy;
		{
			std::lock_guard<std::mutex> lock(mutex);
			copy = snapshot;
		}
		std::string path = command.path;
		if (path.empty())
			path = storage::fs::JoinPath(layout.logs, "caliza-storage-report.txt");
		if (!storage::fs::IsPathWithin(path, layout.root))
			return {CommandStatus::Failed, "report path must stay inside the Caliza data root", false};
		std::ostringstream report;
		report << "Caliza storage report\n";
		report << "revision: " << copy.revision << "\n";
		report << "data root: " << copy.data_root << "\n";
		report << "volumes: " << copy.volumes.size() << "\n";
		for (const VolumeInfo& volume : copy.volumes)
		{
			report << "- " << volume.root_path << ": present=" << volume.present << " readable=" << volume.readable << " kind="
				   << static_cast<int>(volume.kind) << " id=" << (volume.id.empty() ? "none" : volume.id) << " fingerprint="
				   << (volume.fingerprint.empty() ? "none" : volume.fingerprint) << "\n";
		}
		report << "sources: " << copy.sources.size() << "\n";
		for (const LibrarySource& source : copy.sources)
		{
			report << "- " << source.id << " " << source.path << " state=" << static_cast<int>(source.state) << " games="
				   << source.game_count << " " << source.problem << "\n";
		}
		report << "games catalogued: " << copy.games.size() << "\n";
		std::string error;
		if (!storage::fs::WriteFileAtomic(path, report.str(), &error))
			return {CommandStatus::Failed, error, false};
		std::lock_guard<std::mutex> lock(mutex);
		snapshot.diagnostics.report_available = true;
		if (std::find(snapshot.diagnostics.log_paths.begin(), snapshot.diagnostics.log_paths.end(), path) ==
			snapshot.diagnostics.log_paths.end())
			snapshot.diagnostics.log_paths.push_back(path);
		BumpRevisionLocked();
		return {CommandStatus::Succeeded, "storage report written", false};
	}

	uint64_t RegisterScanBatchLocked(CommandId command, CommandType type, const std::vector<std::string>& source_ids)
	{
		const uint64_t batch = next_scan_batch++;
		ScanBatch state;
		state.command = command;
		state.type = type;
		state.remaining = static_cast<uint32_t>(source_ids.size());
		scan_batches[batch] = state;
		for (const std::string& source_id : source_ids)
			scanning_sources.insert(source_id);
		return batch;
	}

	uint64_t RegisterScanBatch(CommandId command, CommandType type, const std::vector<std::string>& source_ids)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return RegisterScanBatchLocked(command, type, source_ids);
	}

	void EnqueueScanJob(uint64_t batch, const LibrarySource& source)
	{
		ScanJob job;
		job.batch = batch;
		job.source_id = source.id;
		job.root_path = source.path;
		job.control = std::make_shared<ScanControl>();
		(void)scan_engine.Enqueue(std::move(job));
	}

	void UpsertGameLocked(const GameEntry& game)
	{
		auto existing = std::find_if(snapshot.games.begin(), snapshot.games.end(), [&game](const GameEntry& entry) {
			return entry.id == game.id;
		});
		if (existing == snapshot.games.end())
		{
			snapshot.games.push_back(game);
			return;
		}
		GameEntry merged = game;
		merged.last_played = existing->last_played;
		*existing = std::move(merged);
	}

	void OnScanProgress(const ScanJob& job, const ScanProgress& progress, const GameEntry* game)
	{
		std::lock_guard<std::mutex> lock(mutex);
		snapshot.scan = progress;
		if (snapshot.scan.state == ScanState::Scanning && progress.cancel_requested)
			snapshot.scan.state = ScanState::Cancelling;
		for (LibrarySource& source : snapshot.sources)
		{
			if (source.id == job.source_id)
				source.state = SourceState::Scanning;
		}
		if (game)
			UpsertGameLocked(*game);
		BumpRevisionLocked();
	}

	void OnScanDone(const ScanJob& job, const ScanOutcome& outcome)
	{
		std::lock_guard<std::mutex> lock(mutex);
		scanning_sources.erase(job.source_id);
		snapshot.scan.state = outcome.state;
		snapshot.scan.source_id = job.source_id;
		snapshot.scan.files_seen = outcome.files_seen;
		snapshot.scan.games_found = outcome.games_found;
		snapshot.scan.cancel_requested = false;
		snapshot.scan.error = outcome.error;
		if (outcome.state == ScanState::Complete)
		{
			std::set<std::string> seen(outcome.seen_ids.begin(), outcome.seen_ids.end());
			snapshot.games.erase(std::remove_if(snapshot.games.begin(), snapshot.games.end(), [&job, &seen](const GameEntry& game) {
				return game.source_id == job.source_id && !seen.count(game.id);
			}),
				snapshot.games.end());
		}
		uint32_t count = 0;
		for (const GameEntry& game : snapshot.games)
			count += game.source_id == job.source_id ? 1 : 0;
		for (LibrarySource& source : snapshot.sources)
		{
			if (source.id != job.source_id)
				continue;
			source.game_count = count;
			source.last_scan = NowUnix();
			if (outcome.state == ScanState::Failed)
			{
				source.state = SourceState::Error;
				source.problem = outcome.error;
			}
			else
			{
				source.state = SourceState::Connected;
				source.problem = outcome.error;
			}
		}
		if (store.Find(job.source_id))
		{
			std::string store_error;
			(void)store.UpdateScan(job.source_id, count, NowUnix());
		}
		BumpRevisionLocked();

		const auto batch_it = scan_batches.find(job.batch);
		if (batch_it != scan_batches.end())
		{
			ScanBatch& batch = batch_it->second;
			if (outcome.state == ScanState::Failed && batch.error.empty())
				batch.error = outcome.error.empty() ? "Scan failed" : outcome.error;
			batch.cancelled = batch.cancelled || outcome.state == ScanState::Cancelled;
			if (batch.remaining)
				batch.remaining--;
			if (batch.remaining == 0)
			{
				CommandStatus status = CommandStatus::Succeeded;
				std::string message = "scan complete";
				if (batch.cancelled)
				{
					status = CommandStatus::Cancelled;
					message = "scan cancelled";
				}
				else if (!batch.error.empty())
				{
					status = CommandStatus::Failed;
					message = batch.error;
					snapshot.scan.state = ScanState::Failed;
					snapshot.scan.error = batch.error;
				}
				PushEventLocked(batch.command, batch.type, status, message);
				scan_batches.erase(batch_it);
			}
		}
	}

	BackendOptions options;
	dataroot::DataRootLayout layout;
	storage::SavedSourceStore store;
	ScanEngine scan_engine;
	mutable std::mutex mutex;
	Snapshot snapshot;
	std::vector<CommandEvent> events;
	std::deque<std::pair<CommandId, Command>> commands;
	std::condition_variable command_cv;
	std::thread command_thread;
	std::atomic<bool> stop_commands{false};
	CommandId next_command = 1;
	uint64_t next_scan_batch = 1;
	std::map<uint64_t, ScanBatch> scan_batches;
	std::set<std::string> scanning_sources;
	std::vector<std::string> initial_errors;
};

StorageBackend::StorageBackend(const BackendOptions& options)
	: m_impl(std::make_unique<Impl>(options))
{
}

StorageBackend::~StorageBackend() = default;

Snapshot StorageBackend::GetSnapshot()
{
	return m_impl->GetSnapshot();
}

CommandId StorageBackend::Post(const Command& command)
{
	return m_impl->Post(command);
}

std::vector<CommandEvent> StorageBackend::PollEvents()
{
	return m_impl->PollEvents();
}

FolderListing StorageBackend::BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit)
{
	return m_impl->BrowseFolder(path, filter, offset, limit);
}

void StorageBackend::RefreshStorage()
{
	m_impl->RefreshStorage();
}

std::unique_ptr<Backend> CreateBackend(const BackendOptions& options)
{
	return std::make_unique<StorageBackend>(options);
}
} // namespace caliza
