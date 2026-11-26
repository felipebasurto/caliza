// Caliza core: canonical data-root layout and resumable legacy-to-Caliza migration.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "data_root.h"

#include "storage.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iomanip>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

namespace caliza::dataroot
{
namespace
{
using storage::fs::IsDirectory;
using storage::fs::IsPathWithin;
using storage::fs::IsRegularFile;
using storage::fs::IsSymlink;
using storage::fs::JoinPath;
using storage::fs::NormalizePath;

const std::set<std::string>& PreservedDirectories()
{
	static const std::set<std::string> dirs = {"bios", "memcards", "savestates", "settings", "patches", "cheats", "covers",
		"lang", "flags", "inputprofiles"};
	return dirs;
}

const std::set<std::string>& LargeDataDirectories()
{
	static const std::set<std::string> dirs = {"games", "textures"};
	return dirs;
}

const std::set<std::string>& RegenerableDirectories()
{
	static const std::set<std::string> dirs = {"cache", "logs", "resources", ".caliza-migration"};
	return dirs;
}

std::string LowerAscii(std::string text)
{
	for (char& c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
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

std::string ErrnoString(const std::string& what, int error)
{
	return what + ": " + std::strerror(error);
}

std::string PercentEncode(const std::string& text)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string out;
	for (const unsigned char c : text)
	{
		const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
			c == '.' || c == '/' || c == ':';
		if (plain)
			out.push_back(static_cast<char>(c));
		else
		{
			out.push_back('%');
			out.push_back(kHex[c >> 4]);
			out.push_back(kHex[c & 15]);
		}
	}
	return out;
}

const char* StatusName(MigrationItemStatus status)
{
	switch (status)
	{
	case MigrationItemStatus::Planned: return "planned";
	case MigrationItemStatus::Copied: return "copied";
	case MigrationItemStatus::AlreadyIdentical: return "already-identical";
	case MigrationItemStatus::Hardlinked: return "hardlinked";
	case MigrationItemStatus::Collision: return "collision";
	case MigrationItemStatus::RejectedSymlink: return "rejected-symlink";
	case MigrationItemStatus::RejectedSpecial: return "rejected-special";
	case MigrationItemStatus::ReadError: return "read-error";
	case MigrationItemStatus::WriteError: return "write-error";
	case MigrationItemStatus::SkippedLargeData: return "skipped-large-data";
	}
	return "unknown";
}

const char* ActionName(MigrationItemAction action)
{
	switch (action)
	{
	case MigrationItemAction::None: return "none";
	case MigrationItemAction::Copy: return "copy";
	case MigrationItemAction::Hardlink: return "hardlink";
	case MigrationItemAction::Reference: return "reference";
	}
	return "unknown";
}

std::vector<std::string> ListDirectorySorted(const std::string& path, bool* opened)
{
	std::vector<std::string> names;
	DIR* dir = opendir(path.c_str());
	if (!dir)
	{
		if (opened)
			*opened = false;
		return names;
	}
	while (const dirent* entry = readdir(dir))
	{
		// Migration is a data-preservation operation, not a browser: dotfiles are user data and must be
		// inventoried. Only the directory navigation entries themselves are excluded. This also lets retry
		// cleanup see the dot-prefixed temporary files created by an interrupted process.
		if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, ".."))
			continue;
		names.emplace_back(entry->d_name);
	}
	closedir(dir);
	std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
		const std::string la = LowerAscii(a);
		const std::string lb = LowerAscii(b);
		return la == lb ? a < b : la < lb;
	});
	if (opened)
		*opened = true;
	return names;
}

struct TreeStats
{
	uint64_t bytes = 0;
	uint32_t files = 0;
	bool complete = true;
};

TreeStats MeasureTree(const std::string& root, uint32_t max_depth)
{
	TreeStats stats;
	std::vector<std::pair<std::string, uint32_t>> stack;
	stack.emplace_back(root, 0);
	while (!stack.empty())
	{
		const auto item = stack.back();
		stack.pop_back();
		if (item.second > max_depth)
		{
			stats.complete = false;
			continue;
		}
		bool opened = false;
		for (const std::string& name : ListDirectorySorted(item.first, &opened))
		{
			const std::string child = JoinPath(item.first, name);
			struct stat st = {};
			if (lstat(child.c_str(), &st) != 0)
			{
				stats.complete = false;
				continue;
			}
			if (S_ISREG(st.st_mode))
			{
				stats.files++;
				stats.bytes += static_cast<uint64_t>(st.st_size);
			}
			else if (S_ISDIR(st.st_mode))
				stack.emplace_back(child, item.second + 1);
			else if (!S_ISLNK(st.st_mode))
				stats.complete = false;
		}
		if (!opened)
			stats.complete = false;
	}
	return stats;
}

bool PreservedTopLevelFile(const std::string& name)
{
	const std::string lower = LowerAscii(name);
	if (EndsWithCaseInsensitive(lower, ".ini") && lower.size() > 4)
		return true;
	return lower == "playtime.dat" || lower == "lastgame.txt";
}

void SyncDirectory(const std::string& path)
{
	const int fd = open(path.c_str(), O_RDONLY);
	if (fd >= 0)
	{
		(void)fsync(fd);
		(void)close(fd);
	}
}

class MigrationLock
{
public:
	~MigrationLock()
	{
		if (m_fd >= 0)
			close(m_fd);
	}

	bool Acquire(const std::string& path, std::string* error)
	{
		std::string mkdir_error;
		if (!storage::fs::MakeDirs(storage::fs::DirName(path), &mkdir_error))
		{
			*error = mkdir_error;
			return false;
		}
		m_fd = open(path.c_str(), O_RDWR | O_CREAT, 0666);
		if (m_fd < 0)
		{
			*error = ErrnoString("can't open migration lock " + path, errno);
			return false;
		}
		struct flock lock = {};
		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_SET;
		lock.l_start = 0;
		lock.l_len = 0;
		if (fcntl(m_fd, F_SETLK, &lock) != 0)
		{
			const int saved = errno;
			close(m_fd);
			m_fd = -1;
			if (saved == EACCES || saved == EAGAIN)
				*error = "another migration process holds the destination lock";
			else
				*error = ErrnoString("can't lock migration state", saved);
			return false;
		}
		return true;
	}

private:
	int m_fd = -1;
};

void RemoveOwnedTempFiles(const std::string& temp_dir)
{
	bool opened = false;
	for (const std::string& name : ListDirectorySorted(temp_dir, &opened))
	{
		if (name.compare(0, 13, ".migrate-tmp-") == 0)
			unlink(JoinPath(temp_dir, name).c_str());
	}
}

bool AppendJournal(const std::string& journal_path, const MigrationItem& item, std::string* error)
{
	std::string mkdir_error;
	if (!storage::fs::MakeDirs(storage::fs::DirName(journal_path), &mkdir_error))
	{
		*error = mkdir_error;
		return false;
	}
	const int fd = open(journal_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd < 0)
	{
		*error = ErrnoString("can't open migration journal", errno);
		return false;
	}
	std::ostringstream line;
	line << "1\t" << StatusName(item.status) << '\t' << ActionName(item.action) << '\t' << PercentEncode(item.relative_path) << '\t'
		 << item.bytes << '\t' << (item.directory ? 'd' : 'f') << '\n';
	const std::string text = line.str();
	size_t written = 0;
	while (written < text.size())
	{
		const ssize_t n = write(fd, text.data() + written, text.size() - written);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			const int saved = errno;
			close(fd);
			*error = ErrnoString("can't write migration journal", saved);
			return false;
		}
		written += static_cast<size_t>(n);
	}
	if (fsync(fd) != 0)
	{
		const int saved = errno;
		close(fd);
		*error = ErrnoString("can't flush migration journal", saved);
		return false;
	}
	if (close(fd) != 0)
	{
		*error = ErrnoString("can't close migration journal", errno);
		return false;
	}
	SyncDirectory(storage::fs::DirName(journal_path));
	return true;
}

bool WriteManifest(const std::string& path, const MigrationOptions& options, const std::vector<MigrationItem>& items,
	const std::vector<MigrationException>& large_exceptions, const std::vector<MigrationException>& skipped, std::string* error)
{
	std::ostringstream out;
	out << "caliza-migration-v1\n";
	out << "source=" << PercentEncode(options.source_root) << "\n";
	out << "destination=" << PercentEncode(options.destination_root) << "\n";
	out << "large_data_strategy=" <<
		(options.large_data_strategy == MigrationLargeDataStrategy::ReferenceOnly ? "reference-only" : "hardlink-verified") << "\n";
	out << "item_count=" << items.size() << "\n";
	for (size_t i = 0; i < items.size(); i++)
	{
		const MigrationItem& item = items[i];
		const std::string prefix = "item." + std::to_string(i) + ".";
		out << prefix << "path=" << PercentEncode(item.relative_path) << "\n";
		out << prefix << "category=" << PercentEncode(item.category) << "\n";
		out << prefix << "bytes=" << item.bytes << "\n";
		out << prefix << "directory=" << (item.directory ? 1 : 0) << "\n";
		out << prefix << "action=" << ActionName(item.action) << "\n";
		out << prefix << "status=" << StatusName(item.status) << "\n";
		out << prefix << "message=" << PercentEncode(item.message) << "\n";
	}
	out << "large_exception_count=" << large_exceptions.size() << "\n";
	for (size_t i = 0; i < large_exceptions.size(); i++)
	{
		const std::string prefix = "large_exception." + std::to_string(i) + ".";
		out << prefix << "path=" << PercentEncode(large_exceptions[i].path) << "\n";
		out << prefix << "kind=" << PercentEncode(large_exceptions[i].kind) << "\n";
		out << prefix << "bytes=" << large_exceptions[i].bytes << "\n";
		out << prefix << "reason=" << PercentEncode(large_exceptions[i].reason) << "\n";
	}
	out << "skipped_count=" << skipped.size() << "\n";
	for (size_t i = 0; i < skipped.size(); i++)
	{
		const std::string prefix = "skipped." + std::to_string(i) + ".";
		out << prefix << "path=" << PercentEncode(skipped[i].path) << "\n";
		out << prefix << "kind=" << PercentEncode(skipped[i].kind) << "\n";
		out << prefix << "bytes=" << skipped[i].bytes << "\n";
		out << prefix << "reason=" << PercentEncode(skipped[i].reason) << "\n";
	}
	return storage::fs::WriteFileAtomic(path, out.str(), error);
}

MigrationItem MakeItem(const std::string& source_root, const std::string& destination_root, const std::string& relative,
	const std::string& category, const struct stat& st, bool directory, MigrationItemAction action)
{
	MigrationItem item;
	item.relative_path = relative;
	item.source_path = JoinPath(source_root, relative);
	item.destination_path = JoinPath(destination_root, relative);
	item.category = category;
	item.bytes = directory ? 0 : static_cast<uint64_t>(st.st_size);
	item.directory = directory;
	item.action = action;
	return item;
}

MigrationItem MakeSpecialItem(const std::string& source_root, const std::string& destination_root, const std::string& relative,
	const std::string& category, MigrationItemStatus status, const std::string& message)
{
	MigrationItem item;
	item.relative_path = relative;
	item.source_path = JoinPath(source_root, relative);
	item.destination_path = JoinPath(destination_root, relative);
	item.category = category;
	item.status = status;
	item.action = MigrationItemAction::None;
	item.message = message;
	return item;
}

void ScanPreservedTree(const std::string& source_root, const std::string& destination_root, const std::string& root_name,
	const std::string& current_relative, uint32_t depth, const MigrationOptions& options, std::vector<MigrationItem>* items)
{
	if (depth > options.maximum_depth)
	{
		items->push_back(MakeSpecialItem(source_root, destination_root, current_relative, root_name,
			MigrationItemStatus::RejectedSpecial, "maximum migration depth reached"));
		return;
	}
	const std::string current_source = JoinPath(source_root, current_relative);
	bool opened = false;
	const std::vector<std::string> names = ListDirectorySorted(current_source, &opened);
	if (!opened)
	{
		items->push_back(MakeSpecialItem(source_root, destination_root, current_relative, root_name, MigrationItemStatus::ReadError,
			"directory is not readable"));
		return;
	}
	for (const std::string& name : names)
	{
		const std::string relative = current_relative + "/" + name;
		const std::string source = JoinPath(source_root, relative);
		struct stat st = {};
		if (lstat(source.c_str(), &st) != 0)
		{
			items->push_back(MakeSpecialItem(source_root, destination_root, relative, root_name, MigrationItemStatus::ReadError,
				"can't inspect entry"));
			continue;
		}
		if (S_ISLNK(st.st_mode))
		{
			items->push_back(MakeSpecialItem(source_root, destination_root, relative, root_name,
				MigrationItemStatus::RejectedSymlink, "symlinks are never followed or migrated"));
			continue;
		}
		if (S_ISDIR(st.st_mode))
		{
			items->push_back(MakeItem(source_root, destination_root, relative, root_name, st, true, MigrationItemAction::Copy));
			ScanPreservedTree(source_root, destination_root, root_name, relative, depth + 1, options, items);
			continue;
		}
		if (S_ISREG(st.st_mode))
		{
			items->push_back(MakeItem(source_root, destination_root, relative, root_name, st, false, MigrationItemAction::Copy));
			continue;
		}
		items->push_back(MakeSpecialItem(source_root, destination_root, relative, root_name, MigrationItemStatus::RejectedSpecial,
			"only regular files and directories are migrated"));
	}
}

void ScanLargeTreeForHardlinks(const std::string& source_root, const std::string& destination_root, const std::string& root_name,
	const std::string& current_relative, uint32_t depth, const MigrationOptions& options, std::vector<MigrationItem>* items)
{
	if (depth > options.maximum_depth)
	{
		items->push_back(MakeSpecialItem(source_root, destination_root, current_relative, root_name,
			MigrationItemStatus::RejectedSpecial, "maximum migration depth reached"));
		return;
	}
	bool opened = false;
	const std::vector<std::string> names = ListDirectorySorted(JoinPath(source_root, current_relative), &opened);
	if (!opened)
	{
		items->push_back(MakeSpecialItem(source_root, destination_root, current_relative, root_name, MigrationItemStatus::ReadError,
			"large-data directory is not readable"));
		return;
	}
	for (const std::string& name : names)
	{
		const std::string relative = current_relative + "/" + name;
		const std::string source = JoinPath(source_root, relative);
		struct stat st = {};
		if (lstat(source.c_str(), &st) != 0)
		{
			items->push_back(MakeSpecialItem(source_root, destination_root, relative, root_name, MigrationItemStatus::ReadError,
				"can't inspect large-data entry"));
			continue;
		}
		if (S_ISLNK(st.st_mode))
		{
			items->push_back(MakeSpecialItem(source_root, destination_root, relative, root_name,
				MigrationItemStatus::RejectedSymlink, "large-data symlinks are never followed or linked"));
			continue;
		}
		if (S_ISDIR(st.st_mode))
		{
			items->push_back(MakeItem(source_root, destination_root, relative, root_name, st, true, MigrationItemAction::Copy));
			ScanLargeTreeForHardlinks(source_root, destination_root, root_name, relative, depth + 1, options, items);
		}
		else if (S_ISREG(st.st_mode))
			items->push_back(MakeItem(source_root, destination_root, relative, root_name, st, false, MigrationItemAction::Hardlink));
		else
		{
			items->push_back(MakeSpecialItem(source_root, destination_root, relative, root_name,
				MigrationItemStatus::RejectedSpecial, "only regular large-data files can be hardlinked"));
		}
	}
}

bool CompareRegularFiles(const std::string& a, const std::string& b, bool* read_error)
{
	*read_error = false;
	struct stat sa = {};
	struct stat sb = {};
	if (stat(a.c_str(), &sa) != 0 || stat(b.c_str(), &sb) != 0 || !S_ISREG(sa.st_mode) || !S_ISREG(sb.st_mode))
		return false;
	if (sa.st_size != sb.st_size)
		return false;
	const int fa = open(a.c_str(), O_RDONLY);
	if (fa < 0)
	{
		*read_error = true;
		return false;
	}
	const int fb = open(b.c_str(), O_RDONLY);
	if (fb < 0)
	{
		close(fa);
		*read_error = true;
		return false;
	}
	char ba[32768];
	char bb[32768];
	bool same = true;
	const auto read_up_to = [](int fd, char* buffer, size_t size, ssize_t* out) {
		size_t total = 0;
		while (total < size)
		{
			const ssize_t n = read(fd, buffer + total, size - total);
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				*out = -1;
				return;
			}
			if (n == 0)
				break;
			total += static_cast<size_t>(n);
		}
		*out = static_cast<ssize_t>(total);
	};
	for (;;)
	{
		ssize_t na = 0;
		ssize_t nb = 0;
		read_up_to(fa, ba, sizeof(ba), &na);
		read_up_to(fb, bb, sizeof(bb), &nb);
		if (na < 0 || nb < 0)
		{
			*read_error = true;
			same = false;
			break;
		}
		if (na != nb)
		{
			same = false;
			break;
		}
		if (na == 0)
			break;
		if (std::memcmp(ba, bb, static_cast<size_t>(na)) != 0)
		{
			same = false;
			break;
		}
	}
	close(fa);
	close(fb);
	return same;
}

bool CompareMemoryToFile(const std::string& memory, const std::string& path, bool* read_error)
{
	*read_error = false;
	struct stat st = {};
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || static_cast<uint64_t>(st.st_size) != memory.size())
		return false;
	const int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
	{
		*read_error = true;
		return false;
	}
	bool same = true;
	size_t offset = 0;
	char buffer[32768];
	while (offset < memory.size())
	{
		const size_t wanted = std::min(sizeof(buffer), memory.size() - offset);
		size_t got = 0;
		while (got < wanted)
		{
			const ssize_t n = read(fd, buffer + got, wanted - got);
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				*read_error = true;
				same = false;
				break;
			}
			if (n == 0)
			{
				same = false;
				break;
			}
			got += static_cast<size_t>(n);
		}
		if (!same)
			break;
		if (std::memcmp(buffer, memory.data() + offset, got) != 0)
		{
			same = false;
			break;
		}
		offset += got;
	}
	close(fd);
	return same;
}

bool DestinationHasSymlinkComponent(const std::string& destination_root, const std::string& destination_path)
{
	const std::string root = NormalizePath(destination_root);
	const std::string path = NormalizePath(destination_path);
	if (!IsPathWithin(path, root) || path == root)
		return true;
	const std::string relative = path.substr(root.size() + 1);
	std::string current = root;
	size_t begin = 0;
	while (begin <= relative.size())
	{
		const size_t slash = relative.find('/', begin);
		const std::string component = relative.substr(begin, slash == std::string::npos ? std::string::npos : slash - begin);
		if (!component.empty())
		{
			current = JoinPath(current, component);
			struct stat st = {};
			if (lstat(current.c_str(), &st) != 0)
				return false; // missing components are created by the migration itself
			if (S_ISLNK(st.st_mode))
				return true;
		}
		if (slash == std::string::npos)
			break;
		begin = slash + 1;
	}
	return false;
}

enum class PublishResult
{
	Copied,
	AlreadyIdentical,
	Collision,
	ReadError,
	WriteError,
	Interrupted,
};

struct PublishOutcome
{
	PublishResult result = PublishResult::WriteError;
	std::string message;
	uint64_t bytes = 0;
};

std::string TempFileName(const std::string& temp_dir, const std::string& relative)
{
	uint64_t hash = 14695981039346656037ull;
	for (const unsigned char c : relative)
	{
		hash ^= c;
		hash *= 1099511628211ull;
	}
	std::ostringstream name;
	static std::atomic<uint64_t> counter{0};
	name << ".migrate-tmp-" << static_cast<unsigned long long>(getpid()) << '-' << std::hex << std::setfill('0') << std::setw(16)
		 << hash << '-' << static_cast<unsigned long long>(counter.fetch_add(1, std::memory_order_relaxed));
	return JoinPath(temp_dir, name.str());
}

PublishOutcome CopyFileAtomicNoOverwrite(const MigrationItem& item, const struct stat& source_stat, const std::string& destination_root,
	const std::string& temp_dir, size_t buffer_bytes, const MigrationStopCallback& should_stop)
{
	PublishOutcome outcome;
	if (should_stop && should_stop())
	{
		outcome.result = PublishResult::Interrupted;
		outcome.message = "interrupted before copying";
		return outcome;
	}

	int open_flags = O_RDONLY;
#ifdef O_NOFOLLOW
	open_flags |= O_NOFOLLOW;
#endif
	const int source_fd = open(item.source_path.c_str(), open_flags);
	if (source_fd < 0)
	{
		outcome.result = PublishResult::ReadError;
		outcome.message = ErrnoString("can't open source file", errno);
		return outcome;
	}
	struct stat opened_stat = {};
	if (fstat(source_fd, &opened_stat) != 0 || !S_ISREG(opened_stat.st_mode))
	{
		close(source_fd);
		outcome.result = PublishResult::ReadError;
		outcome.message = "source is not a regular file";
		return outcome;
	}

	// INI text is user configuration, not opaque bytes: the pre-transition key namespace and internal-root
	// links must become canonical Caliza state in the destination while the source file remains untouched.
	// The legacy migration format uses same-length replacement spellings, but reading through the already
	// O_NOFOLLOW-opened descriptor still protects against a source-path symlink swap before publication.
	const bool translate_ini = EndsWithCaseInsensitive(item.relative_path, ".ini");
	static constexpr uint64_t kIniTranslationLimit = 64ull * 1024ull * 1024ull;
	std::string translated_ini;
	const std::string* memory_payload = nullptr;
	if (translate_ini)
	{
		if (static_cast<uint64_t>(opened_stat.st_size) > kIniTranslationLimit)
		{
			close(source_fd);
			outcome.result = PublishResult::ReadError;
			outcome.message = "INI file exceeds the 64 MiB migration-translation limit";
			return outcome;
		}
		translated_ini.reserve(static_cast<size_t>(opened_stat.st_size));
		char read_buffer[32768];
		for (;;)
		{
			const ssize_t n = read(source_fd, read_buffer, sizeof(read_buffer));
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				close(source_fd);
				outcome.result = PublishResult::ReadError;
				outcome.message = ErrnoString("can't read source INI file", errno);
				return outcome;
			}
			if (n == 0)
				break;
			translated_ini.append(read_buffer, static_cast<size_t>(n));
		}
		translated_ini = legacy::MigrateIniText(std::move(translated_ini));
		memory_payload = &translated_ini;
	}

	if (DestinationHasSymlinkComponent(destination_root, item.destination_path))
	{
		close(source_fd);
		outcome.result = PublishResult::Collision;
		outcome.message = "destination path contains a symlink component; it was not followed";
		return outcome;
	}

	std::string error;
	if (!storage::fs::MakeDirs(storage::fs::DirName(item.destination_path), &error) || !storage::fs::MakeDirs(temp_dir, &error))
	{
		close(source_fd);
		outcome.result = PublishResult::WriteError;
		outcome.message = error;
		return outcome;
	}
	const std::string temp = TempFileName(temp_dir, item.relative_path);
	const int temp_fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (temp_fd < 0)
	{
		close(source_fd);
		outcome.result = PublishResult::WriteError;
		outcome.message = ErrnoString("can't create migration temporary file", errno);
		return outcome;
	}

	std::vector<char> buffer(buffer_bytes);
	bool failed = false;
	bool interrupted = false;
	if (memory_payload)
	{
		size_t written = 0;
		while (!failed && !interrupted && written < memory_payload->size())
		{
			if (should_stop && should_stop())
			{
				interrupted = true;
				break;
			}
			const size_t chunk = std::min(buffer.size(), memory_payload->size() - written);
			std::memcpy(buffer.data(), memory_payload->data() + written, chunk);
			size_t part = 0;
			while (part < chunk)
			{
				const ssize_t w = write(temp_fd, buffer.data() + part, chunk - part);
				if (w < 0)
				{
					if (errno == EINTR)
						continue;
					outcome.result = PublishResult::WriteError;
					outcome.message = ErrnoString("can't write migration temporary file", errno);
					failed = true;
					break;
				}
				part += static_cast<size_t>(w);
			}
			if (!failed)
				written += chunk;
		}
	}
	else
	{
		for (;;)
		{
			if (should_stop && should_stop())
			{
				interrupted = true;
				break;
			}
			const ssize_t n = read(source_fd, buffer.data(), buffer.size());
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				outcome.result = PublishResult::ReadError;
				outcome.message = ErrnoString("can't read source file", errno);
				failed = true;
				break;
			}
			if (n == 0)
				break;
			size_t written = 0;
			while (written < static_cast<size_t>(n))
			{
				const ssize_t w = write(temp_fd, buffer.data() + written, static_cast<size_t>(n) - written);
				if (w < 0)
				{
					if (errno == EINTR)
						continue;
					outcome.result = PublishResult::WriteError;
					outcome.message = ErrnoString("can't write migration temporary file", errno);
					failed = true;
					break;
				}
				written += static_cast<size_t>(w);
			}
			if (failed)
				break;
		}
	}
	if (!failed && !interrupted)
	{
		(void)fchmod(temp_fd, source_stat.st_mode & 07777);
		if (fsync(temp_fd) != 0)
		{
			outcome.result = PublishResult::WriteError;
			outcome.message = ErrnoString("can't flush migration temporary file", errno);
			failed = true;
		}
	}
	close(source_fd);
	if (close(temp_fd) != 0 && !failed && !interrupted)
	{
		outcome.result = PublishResult::WriteError;
		outcome.message = ErrnoString("can't close migration temporary file", errno);
		failed = true;
	}

	if (interrupted)
	{
		unlink(temp.c_str());
		outcome.result = PublishResult::Interrupted;
		outcome.message = "interrupted before atomic publication";
		return outcome;
	}
	if (failed)
	{
		unlink(temp.c_str());
		return outcome;
	}

	// Publish with link(2), which atomically fails when the destination name exists instead of overwriting it.
	// Filesystems that cannot hardlink cannot provide a portable no-overwrite rename; fail closed there rather
	// than racing an unrelated writer and violating the collision guarantee. needs proper testing on OpenOrbis.
	if (link(temp.c_str(), item.destination_path.c_str()) == 0)
	{
		unlink(temp.c_str());
		struct utimbuf times = {};
		times.actime = source_stat.st_atime;
		times.modtime = source_stat.st_mtime;
		(void)utime(item.destination_path.c_str(), &times);
		SyncDirectory(storage::fs::DirName(item.destination_path));
		outcome.result = PublishResult::Copied;
		outcome.bytes = memory_payload ? memory_payload->size() : item.bytes;
		return outcome;
	}
	if (errno != EEXIST)
	{
		const int saved = errno;
		unlink(temp.c_str());
		outcome.result = PublishResult::WriteError;
		outcome.message = ErrnoString("can't publish migration file without overwriting", saved) +
			"; no overwrite-capable rename fallback was attempted";
		return outcome;
	}

	bool read_error = false;
	const bool identical = memory_payload ? CompareMemoryToFile(*memory_payload, item.destination_path, &read_error) :
		CompareRegularFiles(item.source_path, item.destination_path, &read_error);
	unlink(temp.c_str());
	if (identical)
	{
		outcome.result = PublishResult::AlreadyIdentical;
		return outcome;
	}
	outcome.result = read_error ? PublishResult::ReadError : PublishResult::Collision;
	outcome.message = read_error ? "can't compare existing destination data" :
		"destination exists with different data; it was not overwritten";
	return outcome;
}

PublishOutcome HardlinkFileVerified(const MigrationItem& item, const std::string& destination_root)
{
	PublishOutcome outcome;
	struct stat source_stat = {};
	if (lstat(item.source_path.c_str(), &source_stat) != 0 || !S_ISREG(source_stat.st_mode))
	{
		outcome.result = PublishResult::ReadError;
		outcome.message = "large-data source is missing or not a regular file";
		return outcome;
	}
	struct stat destination_before = {};
	if (lstat(item.destination_path.c_str(), &destination_before) == 0)
	{
		if (S_ISREG(destination_before.st_mode) && destination_before.st_dev == source_stat.st_dev &&
			destination_before.st_ino == source_stat.st_ino)
		{
			outcome.result = PublishResult::AlreadyIdentical;
			return outcome;
		}
		bool read_error = false;
		if (S_ISREG(destination_before.st_mode) && CompareRegularFiles(item.source_path, item.destination_path, &read_error))
		{
			// Identical data is safe to keep, but it is not a no-duplication hardlink. Report it honestly rather
			// than replacing user data merely to satisfy a linking optimization.
			outcome.result = PublishResult::AlreadyIdentical;
			outcome.message = "identical destination data already exists as a separate file";
			return outcome;
		}
		outcome.result = read_error ? PublishResult::ReadError : PublishResult::Collision;
		outcome.message = read_error ? "can't compare existing destination data" :
			"destination exists with different data; it was not overwritten";
		return outcome;
	}

	if (DestinationHasSymlinkComponent(destination_root, item.destination_path))
	{
		outcome.result = PublishResult::Collision;
		outcome.message = "destination path contains a symlink component; it was not followed";
		return outcome;
	}

	std::string error;
	if (!storage::fs::MakeDirs(storage::fs::DirName(item.destination_path), &error))
	{
		outcome.result = PublishResult::WriteError;
		outcome.message = error;
		return outcome;
	}
	if (link(item.source_path.c_str(), item.destination_path.c_str()) != 0)
	{
		outcome.result = PublishResult::WriteError;
		outcome.message = ErrnoString("can't create verified large-data hardlink", errno) +
			"; no byte-copy fallback was attempted";
		return outcome;
	}
	struct stat linked = {};
	if (lstat(item.destination_path.c_str(), &linked) != 0 || linked.st_dev != source_stat.st_dev || linked.st_ino != source_stat.st_ino ||
		linked.st_nlink < 2)
	{
		// The destination entry was created by this operation, so removing the failed link does not touch user data.
		unlink(item.destination_path.c_str());
		outcome.result = PublishResult::WriteError;
		outcome.message = "hardlink could not be verified by device and inode";
		return outcome;
	}
	SyncDirectory(storage::fs::DirName(item.destination_path));
	outcome.result = PublishResult::Copied;
	outcome.bytes = item.bytes;
	return outcome;
}

PublishOutcome PublishDirectory(const MigrationItem& item, const std::string& destination_root)
{
	PublishOutcome outcome;
	if (DestinationHasSymlinkComponent(destination_root, item.destination_path))
	{
		outcome.result = PublishResult::Collision;
		outcome.message = "destination path contains a symlink component; it was not followed";
		return outcome;
	}
	struct stat st = {};
	if (lstat(item.destination_path.c_str(), &st) == 0)
	{
		if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
		{
			outcome.result = PublishResult::AlreadyIdentical;
			return outcome;
		}
		outcome.result = PublishResult::Collision;
		outcome.message = "destination exists and is not a directory; it was not replaced";
		return outcome;
	}
	std::string error;
	if (!storage::fs::MakeDirs(storage::fs::DirName(item.destination_path), &error))
	{
		outcome.result = PublishResult::WriteError;
		outcome.message = error;
		return outcome;
	}
	if (mkdir(item.destination_path.c_str(), 0777) != 0)
	{
		outcome.result = errno == EEXIST ? PublishResult::AlreadyIdentical : PublishResult::WriteError;
		if (outcome.result == PublishResult::WriteError)
			outcome.message = ErrnoString("can't create destination directory", errno);
		return outcome;
	}
	SyncDirectory(storage::fs::DirName(item.destination_path));
	outcome.result = PublishResult::Copied;
	return outcome;
}

bool TerminalInJournal(const MigrationItem& item)
{
	return item.status != MigrationItemStatus::Planned;
}

bool IsPreservedCategoryName(const std::string& lower)
{
	return PreservedDirectories().count(lower) != 0;
}

bool IsLargeDataName(const std::string& lower)
{
	return LargeDataDirectories().count(lower) != 0;
}

void InventorySkipped(const std::string& path, const std::string& kind, const std::string& reason, uint32_t max_depth,
	std::vector<MigrationException>* skipped)
{
	MigrationException exception;
	exception.path = path;
	exception.kind = kind;
	exception.reason = reason;
	const TreeStats stats = MeasureTree(path, max_depth);
	exception.bytes = stats.bytes;
	if (!stats.complete)
		exception.reason += "; size inventory is incomplete";
	skipped->push_back(std::move(exception));
}
} // namespace

DataRootLayout DataRootLayout::ForRoot(const std::string& root)
{
	DataRootLayout layout;
	layout.root = storage::fs::NormalizePath(root);
	layout.games = storage::fs::JoinPath(layout.root, "games");
	layout.bios = storage::fs::JoinPath(layout.root, "bios");
	layout.memcards = storage::fs::JoinPath(layout.root, "memcards");
	layout.savestates = storage::fs::JoinPath(layout.root, "savestates");
	layout.settings = storage::fs::JoinPath(layout.root, "settings");
	layout.patches = storage::fs::JoinPath(layout.root, "patches");
	layout.cheats = storage::fs::JoinPath(layout.root, "cheats");
	layout.covers = storage::fs::JoinPath(layout.root, "covers");
	layout.lang = storage::fs::JoinPath(layout.root, "lang");
	layout.flags = storage::fs::JoinPath(layout.root, "flags");
	layout.inputprofiles = storage::fs::JoinPath(layout.root, "inputprofiles");
	layout.cache = storage::fs::JoinPath(layout.root, "cache");
	layout.logs = storage::fs::JoinPath(layout.root, "logs");
	layout.library = storage::fs::JoinPath(layout.root, "library");
	layout.migration_state = storage::fs::JoinPath(layout.root, ".caliza-migration");
	return layout;
}

std::vector<std::string> DataRootLayout::UserDataDirectories() const
{
	return {bios, memcards, savestates, settings, patches, cheats, covers, lang, flags, inputprofiles};
}

std::vector<std::string> DataRootLayout::AllDirectories() const
{
	std::vector<std::string> dirs = UserDataDirectories();
	dirs.insert(dirs.end(), {games, cache, logs, library, migration_state});
	return dirs;
}

bool EnsureDataRoot(const DataRootLayout& layout, std::string* error)
{
	for (const std::string& dir : layout.AllDirectories())
	{
		if (!storage::fs::MakeDirs(dir, error))
			return false;
	}
	return true;
}

MigrationResult MigrateDataRoot(const MigrationOptions& input_options, const MigrationStopCallback& should_stop,
	const MigrationProgressCallback& progress)
{
	MigrationOptions options = input_options;
	options.source_root = NormalizePath(options.source_root);
	options.destination_root = NormalizePath(options.destination_root);
	if (options.copy_buffer_bytes < 4096)
		options.copy_buffer_bytes = 4096;
	if (options.copy_buffer_bytes > 4u * 1024u * 1024u)
		options.copy_buffer_bytes = 4u * 1024u * 1024u;
	if (options.maximum_depth == 0)
		options.maximum_depth = 1;

	MigrationResult result;
	result.source_root = options.source_root;
	result.destination_root = options.destination_root;
	const std::string state_dir = JoinPath(options.destination_root, ".caliza-migration");
	result.manifest_path = JoinPath(state_dir, "manifest.txt");
	result.journal_path = JoinPath(state_dir, "journal.ndjson");
	result.resumable = true;

	if (options.source_root.empty() || options.source_root[0] != '/' || options.destination_root.empty() ||
		options.destination_root[0] != '/')
	{
		result.status = MigrationStatus::InvalidRequest;
		result.message = "migration roots must be absolute POSIX paths";
		return result;
	}
	const std::string canonical_source = storage::fs::CanonicalPath(options.source_root);
	const std::string canonical_destination = storage::fs::CanonicalPath(options.destination_root);
	if (canonical_source == canonical_destination || IsPathWithin(canonical_destination, canonical_source) ||
		IsPathWithin(canonical_source, canonical_destination))
	{
		result.status = MigrationStatus::InvalidRequest;
		result.message = "source and destination roots must not overlap";
		return result;
	}

	struct stat source_stat = {};
	if (lstat(options.source_root.c_str(), &source_stat) != 0)
	{
		result.status = MigrationStatus::NoSource;
		result.message = "legacy source root does not exist";
		return result;
	}
	if (S_ISLNK(source_stat.st_mode) || !S_ISDIR(source_stat.st_mode))
	{
		result.status = MigrationStatus::InvalidRequest;
		result.message = "legacy source root must be a real directory, not a symlink or special file";
		return result;
	}
	struct stat destination_root_stat = {};
	if (lstat(options.destination_root.c_str(), &destination_root_stat) == 0 &&
		(S_ISLNK(destination_root_stat.st_mode) || !S_ISDIR(destination_root_stat.st_mode)))
	{
		result.status = MigrationStatus::InvalidRequest;
		result.message = "destination root must be a real directory when it exists";
		return result;
	}

	std::string error;
	if (!storage::fs::MakeDirs(state_dir, &error))
	{
		result.status = MigrationStatus::Failed;
		result.message = error;
		return result;
	}
	const std::string temp_dir = JoinPath(state_dir, "tmp");
	if (!storage::fs::MakeDirs(temp_dir, &error))
	{
		result.status = MigrationStatus::Failed;
		result.message = error;
		return result;
	}

	MigrationLock lock;
	if (!lock.Acquire(JoinPath(state_dir, "migration.lock"), &error))
	{
		result.status = MigrationStatus::Failed;
		result.message = error;
		return result;
	}
	RemoveOwnedTempFiles(temp_dir);

	std::vector<MigrationItem> items;
	bool source_opened = false;
	for (const std::string& name : ListDirectorySorted(options.source_root, &source_opened))
	{
		const std::string lower = LowerAscii(name);
		const std::string source = JoinPath(options.source_root, name);
		struct stat st = {};
		if (lstat(source.c_str(), &st) != 0)
		{
			items.push_back(MakeSpecialItem(options.source_root, options.destination_root, name, lower,
				MigrationItemStatus::ReadError, "can't inspect top-level entry"));
			continue;
		}
		if (S_ISLNK(st.st_mode))
		{
			items.push_back(MakeSpecialItem(options.source_root, options.destination_root, name, lower,
				MigrationItemStatus::RejectedSymlink, "top-level symlinks are never followed or migrated"));
			continue;
		}
		if (S_ISDIR(st.st_mode))
		{
			if (IsPreservedCategoryName(lower))
			{
				items.push_back(MakeItem(options.source_root, options.destination_root, name, lower, st, true, MigrationItemAction::Copy));
				ScanPreservedTree(options.source_root, options.destination_root, lower, name, 1, options, &items);
			}
			else if (IsLargeDataName(lower))
			{
				MigrationException exception;
				exception.path = source;
				exception.kind = lower;
				const TreeStats stats = MeasureTree(source, options.maximum_depth);
				exception.bytes = stats.bytes;
				exception.reason = options.large_data_strategy == MigrationLargeDataStrategy::ReferenceOnly ?
					"large game/texture data is intentionally left in place; add it as a read-only source instead of duplicating it" :
					"large game/texture data is migrated only through verified same-filesystem hardlinks";
				if (!stats.complete)
					exception.reason += "; size inventory is incomplete";
				result.large_data_exceptions.push_back(exception);
				if (options.large_data_strategy == MigrationLargeDataStrategy::HardlinkVerified)
				{
					items.push_back(MakeItem(options.source_root, options.destination_root, name, lower, st, true, MigrationItemAction::Copy));
					ScanLargeTreeForHardlinks(options.source_root, options.destination_root, lower, name, 1, options, &items);
				}
				else
				{
					MigrationItem skipped = MakeSpecialItem(options.source_root, options.destination_root, name, lower,
						MigrationItemStatus::SkippedLargeData, exception.reason);
					skipped.bytes = stats.bytes;
					skipped.action = MigrationItemAction::Reference;
					items.push_back(std::move(skipped));
				}
			}
			else
			{
				const std::string kind = RegenerableDirectories().count(lower) ? "regenerable" : "unpreserved-directory";
				InventorySkipped(source, kind, RegenerableDirectories().count(lower) ?
					"regenerable or application-owned data is not migrated" : "directory is not part of the documented user-data inventory",
					options.maximum_depth, &result.skipped_exceptions);
			}
			continue;
		}
		if (S_ISREG(st.st_mode))
		{
			if (PreservedTopLevelFile(name))
				items.push_back(MakeItem(options.source_root, options.destination_root, name, "top-level", st, false,
					MigrationItemAction::Copy));
			else
			{
				MigrationException exception;
				exception.path = source;
				exception.kind = "unpreserved-file";
				exception.bytes = static_cast<uint64_t>(st.st_size);
				exception.reason = "top-level file is not part of the documented settings/save-state inventory";
				result.skipped_exceptions.push_back(std::move(exception));
			}
			continue;
		}
		items.push_back(MakeSpecialItem(options.source_root, options.destination_root, name, lower,
			MigrationItemStatus::RejectedSpecial, "only regular files and directories are migrated"));
	}
	if (!source_opened)
	{
		result.status = MigrationStatus::Failed;
		result.message = "can't read the legacy source root";
		return result;
	}

	result.items = items;
	result.files_planned = static_cast<uint32_t>(std::count_if(items.begin(), items.end(), [](const MigrationItem& item) {
		return !item.directory && item.action != MigrationItemAction::Reference;
	}));
	if (!WriteManifest(result.manifest_path, options, result.items, result.large_data_exceptions, result.skipped_exceptions, &error))
	{
		result.status = MigrationStatus::Failed;
		result.message = error;
		return result;
	}

	bool interrupted = false;
	bool journal_failed = false;
	for (MigrationItem& item : result.items)
	{
		if (should_stop && should_stop())
		{
			interrupted = true;
			break;
		}
		if (item.status == MigrationItemStatus::SkippedLargeData)
		{
			if (!AppendJournal(result.journal_path, item, &error))
			{
				journal_failed = true;
				break;
			}
			if (progress)
				progress(item);
			continue;
		}
		if (TerminalInJournal(item))
		{
			result.rejected++;
			if (!AppendJournal(result.journal_path, item, &error))
			{
				journal_failed = true;
				break;
			}
			if (progress)
				progress(item);
			continue;
		}

		struct stat current_source = {};
		if (lstat(item.source_path.c_str(), &current_source) != 0)
		{
			item.status = MigrationItemStatus::ReadError;
			item.message = ErrnoString("source entry disappeared or is unreadable", errno);
		}
		else if (item.directory)
		{
			const PublishOutcome published = PublishDirectory(item, options.destination_root);
			switch (published.result)
			{
			case PublishResult::Copied: item.status = MigrationItemStatus::Copied; break;
			case PublishResult::AlreadyIdentical: item.status = MigrationItemStatus::AlreadyIdentical; break;
			case PublishResult::Collision: item.status = MigrationItemStatus::Collision; break;
			case PublishResult::ReadError: item.status = MigrationItemStatus::ReadError; break;
			case PublishResult::WriteError: item.status = MigrationItemStatus::WriteError; break;
			case PublishResult::Interrupted: interrupted = true; break;
			}
			item.message = published.message;
		}
		else if (!S_ISREG(current_source.st_mode) || S_ISLNK(current_source.st_mode))
		{
			item.status = S_ISLNK(current_source.st_mode) ? MigrationItemStatus::RejectedSymlink : MigrationItemStatus::RejectedSpecial;
			item.message = "source entry changed type before migration; it was not followed";
		}
		else if (item.action == MigrationItemAction::Hardlink)
		{
			const PublishOutcome published = HardlinkFileVerified(item, options.destination_root);
			switch (published.result)
			{
			case PublishResult::Copied:
				item.status = MigrationItemStatus::Hardlinked;
				result.files_hardlinked++;
				break;
			case PublishResult::AlreadyIdentical: item.status = MigrationItemStatus::AlreadyIdentical; break;
			case PublishResult::Collision: item.status = MigrationItemStatus::Collision; break;
			case PublishResult::ReadError: item.status = MigrationItemStatus::ReadError; break;
			case PublishResult::WriteError: item.status = MigrationItemStatus::WriteError; break;
			case PublishResult::Interrupted: interrupted = true; break;
			}
			item.message = published.message;
		}
		else
		{
			const PublishOutcome published = CopyFileAtomicNoOverwrite(item, current_source, options.destination_root, temp_dir, options.copy_buffer_bytes, should_stop);
			switch (published.result)
			{
			case PublishResult::Copied:
				item.status = MigrationItemStatus::Copied;
				result.files_copied++;
				result.bytes_copied += published.bytes;
				break;
			case PublishResult::AlreadyIdentical: item.status = MigrationItemStatus::AlreadyIdentical; break;
			case PublishResult::Collision: item.status = MigrationItemStatus::Collision; break;
			case PublishResult::ReadError: item.status = MigrationItemStatus::ReadError; break;
			case PublishResult::WriteError: item.status = MigrationItemStatus::WriteError; break;
			case PublishResult::Interrupted: interrupted = true; break;
			}
			item.message = published.message;
		}

		if (interrupted)
			break;
		if (item.status == MigrationItemStatus::Collision)
			result.collisions++;
		if (item.status == MigrationItemStatus::RejectedSymlink || item.status == MigrationItemStatus::RejectedSpecial)
			result.rejected++;
		if (item.status == MigrationItemStatus::AlreadyIdentical && !item.directory)
			result.files_already_identical++;
		if (!AppendJournal(result.journal_path, item, &error))
		{
			journal_failed = true;
			break;
		}
		if (progress)
			progress(item);
	}

	RemoveOwnedTempFiles(temp_dir);
	std::string manifest_error;
	(void)WriteManifest(result.manifest_path, options, result.items, result.large_data_exceptions, result.skipped_exceptions,
		&manifest_error);

	if (journal_failed)
	{
		result.status = MigrationStatus::Failed;
		result.message = error;
		return result;
	}
	if (interrupted)
	{
		result.status = MigrationStatus::Interrupted;
		result.message = "migration interrupted cooperatively before atomic publication; retry with the same options";
		return result;
	}

	bool issues = false;
	bool write_errors = false;
	for (const MigrationItem& item : result.items)
	{
		issues = issues || item.status == MigrationItemStatus::Collision || item.status == MigrationItemStatus::RejectedSymlink ||
			item.status == MigrationItemStatus::RejectedSpecial || item.status == MigrationItemStatus::ReadError;
		write_errors = write_errors || item.status == MigrationItemStatus::WriteError;
	}
	if (write_errors)
	{
		result.status = MigrationStatus::Failed;
		result.message = "one or more files could not be written; no differing destination data was overwritten";
	}
	else if (issues)
	{
		result.status = MigrationStatus::CompletedWithIssues;
		result.message = "migration completed with collisions or rejected entries; source and differing destination data were preserved";
	}
	else
	{
		result.status = MigrationStatus::Completed;
		result.message = "migration completed; source data was preserved";
	}
	result.source_preserved = IsDirectory(options.source_root);
	return result;
}

std::vector<std::string> PreservedMigrationCategories()
{
	return {"bios", "memcards", "savestates", "settings", "patches", "cheats", "covers", "lang", "flags", "inputprofiles",
		"top-level *.ini", "playtime.dat", "lastgame.txt"};
}

std::vector<std::string> LargeDataMigrationExceptions()
{
	return {"games", "textures"};
}
} // namespace caliza::dataroot
