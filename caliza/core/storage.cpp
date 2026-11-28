// Caliza core: POSIX storage discovery, folder browsing and saved-source persistence.
// SPDX-License-Identifier: GPL-3.0-or-later

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
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace caliza::storage
{
namespace
{
std::string ErrnoString(const std::string& what, int error)
{
	return what + ": " + std::strerror(error);
}

uint64_t Fnv1a64(uint64_t hash, const void* data, size_t size)
{
	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	for (size_t i = 0; i < size; i++)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

uint64_t Fnv1a64(uint64_t hash, const std::string& text)
{
	return Fnv1a64(hash, text.data(), text.size());
}

template <typename T>
uint64_t Fnv1a64Value(uint64_t hash, const T& value)
{
	return Fnv1a64(hash, &value, sizeof(value));
}

std::string Hex64(uint64_t value)
{
	std::ostringstream out;
	out << std::hex << std::setfill('0') << std::setw(16) << value;
	return out.str();
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

std::string PercentEncode(const std::string& text)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(text.size());
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

int HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

bool PercentDecode(const std::string& text, std::string* out)
{
	out->clear();
	for (size_t i = 0; i < text.size(); i++)
	{
		if (text[i] != '%')
		{
			out->push_back(text[i]);
			continue;
		}
		if (i + 2 >= text.size())
			return false;
		const int hi = HexDigit(text[i + 1]);
		const int lo = HexDigit(text[i + 2]);
		if (hi < 0 || lo < 0)
			return false;
		out->push_back(static_cast<char>((hi << 4) | lo));
		i += 2;
	}
	return true;
}

std::vector<std::string> SplitComponents(const std::string& path)
{
	std::vector<std::string> components;
	size_t at = 0;
	while (at <= path.size())
	{
		const size_t slash = path.find('/', at);
		const std::string part = path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
		if (!part.empty())
			components.push_back(part);
		if (slash == std::string::npos)
			break;
		at = slash + 1;
	}
	return components;
}

void SyncDirectory(const std::string& path)
{
	const int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return;
	// Some filesystems reject fsync on a directory. Durability of the renamed file is best-effort there.
	(void)fsync(fd);
	(void)close(fd);
}

struct CountResult
{
	uint32_t count = 0;
	bool complete = true;
};

CountResult CountImagesRecursive(const std::string& root)
{
	CountResult result;
	std::vector<std::pair<std::string, uint32_t>> stack;
	stack.emplace_back(root, 0);
	uint32_t entries_seen = 0;
	while (!stack.empty())
	{
		const auto item = stack.back();
		stack.pop_back();
		if (item.second > 5)
		{
			result.complete = false;
			continue;
		}
		DIR* dir = opendir(item.first.c_str());
		if (!dir)
		{
			result.complete = false;
			continue;
		}
		while (const dirent* entry = readdir(dir))
		{
			const char* name = entry->d_name;
			if (name[0] == '.')
				continue;
			if (++entries_seen > 1024)
			{
				result.complete = false;
				break;
			}
			const std::string child = fs::JoinPath(item.first, name);
			struct stat st = {};
			if (lstat(child.c_str(), &st) != 0)
			{
				result.complete = false;
				continue;
			}
			if (S_ISREG(st.st_mode) && LooksLikeGameImageName(name))
				result.count++;
			else if (S_ISDIR(st.st_mode))
				stack.emplace_back(child, item.second + 1);
		}
		closedir(dir);
		if (!result.complete)
			break;
	}
	return result;
}

bool ProbeStatvfs(const std::string& root, struct statvfs* out)
{
#ifdef ORBIS_PS4
	// OpenOrbis' libc statvfs path has crashed real PS4 processes in the existing frontend; portable storage
	// discovery must never jeopardize library use for capacity metadata. The console adapter can add a libkernel
	// space probe later. needs proper testing against physical Orbis storage hotplug behavior.
	(void)root;
	(void)out;
	return false;
#else
	return statvfs(root.c_str(), out) == 0;
#endif
}

bool BuiltInExtendedManagedEvidence(const std::string& root)
{
	// There is no documented generic Orbis ioctl available to this portable library. These marker checks are
	// intentionally narrow evidence, not a claim that every /mnt/extN mount is PS4-managed. Platform code can
	// supply authoritative evidence through StorageDiscoveryOptions::extended_managed.
	return fs::Exists(fs::JoinPath(root, "system_ex")) || fs::Exists(fs::JoinPath(root, ".extended-storage")) ||
		fs::Exists(fs::JoinPath(root, "PS4_SYSTEM"));
}

VolumeInfo ProbeVolume(const std::string& root, VolumeKind kind, int slot, const StorageDiscoveryOptions& options)
{
	VolumeInfo volume;
	volume.root_path = fs::NormalizePath(root);
	volume.kind = kind;
	volume.slot = slot;
	if (kind == VolumeKind::Internal)
		volume.label = "Internal storage";
	else if (kind == VolumeKind::USB)
		volume.label = slot == 0 ? "USB drive" : "USB drive " + std::to_string(slot + 1);
	else if (kind == VolumeKind::Extended)
		volume.label = slot == 0 ? "Extended storage" : "Extended storage " + std::to_string(slot + 1);
	else
		volume.label = volume.root_path;

	struct stat st = {};
	volume.present = stat(volume.root_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
	if (!volume.present)
	{
		volume.status = "not present";
		return volume;
	}

	// opendir is the decisive readability probe: it uses this process's credentials and does not rely on
	// access(2), whose pre-jailbreak Orbis behavior has historically been misleading. Orbis always leaves
	// empty /mnt/usbN and /mnt/extN mountpoint directories behind when no media is in the slot, so a readable
	// removable root also needs one visible entry before it can be treated as a present volume.
	uint32_t visible_entries = 0;
	DIR* dir = opendir(volume.root_path.c_str());
	volume.readable = dir != nullptr;
	if (dir)
	{
		while (const dirent* entry = readdir(dir))
		{
			if (entry->d_name[0] != '.')
			{
				visible_entries++;
				break;
			}
		}
		closedir(dir);
	}
	if ((kind == VolumeKind::USB || kind == VolumeKind::Extended) && volume.readable && visible_entries == 0)
	{
		volume.present = false;
		volume.status = "empty mount point; no media mounted";
		return volume;
	}

	struct statvfs vfs = {};
	if (ProbeStatvfs(volume.root_path, &vfs))
	{
		volume.total_bytes = static_cast<uint64_t>(vfs.f_blocks) * static_cast<uint64_t>(vfs.f_frsize);
		volume.free_bytes = static_cast<uint64_t>(vfs.f_bavail) * static_cast<uint64_t>(vfs.f_frsize);
	}
	if (options.stable_identity)
		volume.id = options.stable_identity(volume.root_path);
	volume.fingerprint = VolumeFingerprint(volume.root_path);
	if (kind == VolumeKind::Extended)
	{
		volume.extended_managed = options.extended_managed ? options.extended_managed(volume.root_path) :
			BuiltInExtendedManagedEvidence(volume.root_path);
	}
	if (!volume.readable)
		volume.status = "present but not readable by this process";
	else if (volume.extended_managed)
		volume.status = "PS4-managed extended-storage evidence present";
	else if (volume.id.empty())
		volume.status = "readable; no durable platform identity available";
	else
		volume.status = "readable";
	return volume;
}

std::string MakeSourceId(uint64_t order)
{
	std::ostringstream out;
	out << "saved-" << std::setw(6) << std::setfill('0') << order;
	return out.str();
}

bool PathInsideVolume(const std::string& path, const VolumeInfo& volume)
{
	return fs::IsPathWithin(fs::NormalizePath(path), fs::NormalizePath(volume.root_path));
}

std::string RelativeToVolume(const std::string& path, const VolumeInfo& volume)
{
	const std::string normalized = fs::NormalizePath(path);
	const std::string root = fs::NormalizePath(volume.root_path);
	if (normalized == root)
		return std::string();
	if (!fs::IsPathWithin(normalized, root))
		return std::string();
	return normalized.substr(root.size() + 1);
}

bool ValidateSourcePath(const std::string& path, const VolumeInfo& volume, std::string* message)
{
	if (path.empty() || path[0] != '/')
	{
		*message = "saved folders must be absolute POSIX paths";
		return false;
	}
	if (!volume.present)
	{
		*message = "the volume is not present";
		return false;
	}
	if (!volume.readable)
	{
		*message = "the volume is not readable by this process";
		return false;
	}
	if (!PathInsideVolume(path, volume))
	{
		*message = "the folder is not inside the selected volume";
		return false;
	}
	if (volume.kind == VolumeKind::Internal && fs::NormalizePath(path) == fs::NormalizePath(volume.root_path))
	{
		*message = "the Caliza data root cannot be a scan source; choose a folder inside it or on removable storage";
		return false;
	}
	if (fs::IsSymlink(path))
	{
		*message = "symlinked folders are not saved as scan sources";
		return false;
	}
	if (!fs::IsDirectory(path))
	{
		*message = "the folder does not exist";
		return false;
	}
	if (!fs::IsReadableDirectory(path))
	{
		*message = "the folder is not readable by this process";
		return false;
	}
	if (volume.id.empty() && volume.fingerprint.empty())
	{
		*message = "the volume has neither durable identity nor non-authoritative fingerprint evidence";
		return false;
	}
	return true;
}

bool SameSavedPath(const std::string& a, const std::string& b)
{
	const std::string na = fs::NormalizePath(a);
	const std::string nb = fs::NormalizePath(b);
	if (na == nb)
		return true;
	// realpath collapses aliases for existing paths. This is duplicate evidence only; it does not make a
	// symlinked saved folder acceptable to migration or scanning.
	return fs::CanonicalPath(na) == fs::CanonicalPath(nb);
}

bool SameSavedSourceLocation(const SavedSourceRecord& record, const std::string& path, const VolumeInfo& volume)
{
	if (SameSavedPath(record.path, path))
		return true;
	const std::string relative = RelativeToVolume(path, volume);
	if (!record.volume_id.empty() && !volume.id.empty() && record.volume_id == volume.id && record.relative_path == relative)
		return true;
	// Fingerprints are not authoritative enough for reassociation, but matching fingerprint plus relative path is
	// sufficient duplicate-collision evidence: allowing the link would create two indistinguishable saved rows.
	if (record.volume_id.empty() && volume.id.empty() && !record.volume_fingerprint.empty() &&
		record.volume_fingerprint == volume.fingerprint && record.relative_path == relative)
		return true;
	return false;
}

struct VolumeMatch
{
	int index = -1;
	bool ambiguous_id = false;
	bool exact_slot = false;
	bool fingerprint_matches = false;
	bool possible_elsewhere = false;
};

VolumeMatch ResolveRecordVolume(const SavedSourceRecord& record, const std::vector<VolumeInfo>& volumes)
{
	VolumeMatch match;
	if (!record.volume_id.empty())
	{
		int present_count = 0;
		for (size_t i = 0; i < volumes.size(); i++)
		{
			if (!volumes[i].present || volumes[i].id != record.volume_id)
				continue;
			match.index = static_cast<int>(i);
			present_count++;
		}
		match.ambiguous_id = present_count > 1;
		if (present_count == 1)
			match.fingerprint_matches = true;
		return match;
	}

	match.index = FindVolumeIndexForPath(record.path, volumes);
	if (match.index >= 0)
	{
		match.exact_slot = true;
		const VolumeInfo& volume = volumes[static_cast<size_t>(match.index)];
		match.fingerprint_matches = volume.kind == VolumeKind::Internal || volume.fingerprint == record.volume_fingerprint;
	}
	for (size_t i = 0; i < volumes.size(); i++)
	{
		if (!volumes[i].present || static_cast<int>(i) == match.index)
			continue;
		if (!record.volume_fingerprint.empty() && volumes[i].fingerprint == record.volume_fingerprint)
			match.possible_elsewhere = true;
	}
	return match;
}
} // namespace

namespace fs
{
bool Exists(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0;
}

bool IsDirectory(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool IsRegularFile(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool IsSymlink(const std::string& path)
{
	struct stat st = {};
	return lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}

bool IsReadableDirectory(const std::string& path)
{
	DIR* dir = opendir(path.c_str());
	if (!dir)
		return false;
	closedir(dir);
	return true;
}

uint64_t FileSize(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;
}

std::string NormalizePath(const std::string& path)
{
	if (path.empty())
		return ".";
	const bool absolute = path[0] == '/';
	std::vector<std::string> out;
	for (const std::string& component : SplitComponents(path))
	{
		if (component == ".")
			continue;
		if (component == "..")
		{
			if (!out.empty() && out.back() != "..")
				out.pop_back();
			else if (!absolute)
				out.push_back(component);
			continue;
		}
		out.push_back(component);
	}
	std::string result = absolute ? "/" : "";
	for (size_t i = 0; i < out.size(); i++)
	{
		if (i != 0 || !absolute)
		{
			if (!result.empty() && result.back() != '/')
				result.push_back('/');
		}
		result += out[i];
	}
	if (result.empty())
		return absolute ? "/" : ".";
	return result;
}

std::string JoinPath(const std::string& parent, const std::string& child)
{
	if (child.empty())
		return NormalizePath(parent);
	if (!child.empty() && child[0] == '/')
		return NormalizePath(child);
	const std::string normalized = NormalizePath(parent);
	if (normalized == "/")
		return NormalizePath("/" + child);
	return NormalizePath(normalized + "/" + child);
}

std::string BaseName(const std::string& path)
{
	const std::string normalized = NormalizePath(path);
	const size_t slash = normalized.rfind('/');
	if (slash == std::string::npos)
		return normalized;
	if (slash + 1 >= normalized.size())
		return "/";
	return normalized.substr(slash + 1);
}

std::string DirName(const std::string& path)
{
	const std::string normalized = NormalizePath(path);
	const size_t slash = normalized.rfind('/');
	if (slash == std::string::npos)
		return ".";
	if (slash == 0)
		return "/";
	return normalized.substr(0, slash);
}

bool IsPathWithin(const std::string& path, const std::string& root)
{
	const std::string p = NormalizePath(path);
	const std::string r = NormalizePath(root);
	if (p == r)
		return true;
	if (r == "/")
		return !p.empty() && p[0] == '/';
	return p.size() > r.size() && p.compare(0, r.size(), r) == 0 && p[r.size()] == '/';
}

std::string CanonicalPath(const std::string& path)
{
	const std::string normalized = NormalizePath(path);
	char* resolved = realpath(normalized.c_str(), nullptr);
	if (resolved)
	{
		std::string out = NormalizePath(resolved);
		free(resolved);
		return out;
	}

	// realpath(3) fails for a not-yet-created migration destination. Resolve the deepest existing prefix and
	// append the missing components lexically; otherwise platforms such as macOS mix /tmp and /private/tmp
	// forms and overlap checks can miss a destination nested inside its source.
	std::string existing = normalized;
	std::string missing_suffix;
	while (!existing.empty() && existing != ".")
	{
		resolved = realpath(existing.c_str(), nullptr);
		if (resolved)
		{
			std::string out = NormalizePath(resolved);
			free(resolved);
			if (!missing_suffix.empty())
				out = out == "/" ? NormalizePath("/" + missing_suffix) : NormalizePath(out + "/" + missing_suffix);
			return out;
		}
		const std::string base = BaseName(existing);
		missing_suffix = missing_suffix.empty() ? base : base + "/" + missing_suffix;
		const std::string parent = DirName(existing);
		if (parent == existing)
			break;
		existing = parent;
	}
	return normalized;
}

bool MakeDirs(const std::string& path, std::string* error)
{
	const std::string normalized = NormalizePath(path);
	if (normalized.empty() || normalized == ".")
	{
		if (error)
			*error = "empty directory path";
		return false;
	}
	if (IsDirectory(normalized))
		return true;

	std::vector<std::string> components = SplitComponents(normalized);
	std::string current = normalized[0] == '/' ? "/" : "";
	for (const std::string& component : components)
	{
		if (current.empty() || current == "/")
			current += component;
		else
			current += "/" + component;
		struct stat st = {};
		if (stat(current.c_str(), &st) == 0)
		{
			if (!S_ISDIR(st.st_mode))
			{
				if (error)
					*error = current + " exists and is not a directory";
				return false;
			}
			continue;
		}
		if (errno != ENOENT)
		{
			if (error)
				*error = ErrnoString("can't inspect " + current, errno);
			return false;
		}
		if (mkdir(current.c_str(), 0777) != 0)
		{
			if (error)
				*error = ErrnoString("can't create " + current, errno);
			return false;
		}
	}
	return true;
}

bool ReadFile(const std::string& path, std::string* out, std::string* error, uint64_t max_bytes)
{
	out->clear();
	const int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
	{
		if (error)
			*error = ErrnoString("can't open " + path, errno);
		return false;
	}
	char buffer[16384];
	bool too_large = false;
	for (;;)
	{
		const ssize_t n = read(fd, buffer, sizeof(buffer));
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			if (error)
				*error = ErrnoString("can't read " + path, errno);
			close(fd);
			return false;
		}
		if (n == 0)
			break;
		if (out->size() + static_cast<size_t>(n) > max_bytes)
		{
			too_large = true;
			break;
		}
		out->append(buffer, static_cast<size_t>(n));
	}
	close(fd);
	if (too_large)
	{
		if (error)
			*error = path + " is larger than the supported state-file size";
		out->clear();
		return false;
	}
	return true;
}

bool WriteFileAtomic(const std::string& path, const std::string& data, std::string* error)
{
	const std::string dir = DirName(path);
	if (!MakeDirs(dir, error))
		return false;
	static std::atomic<unsigned> counter{0};
	const std::string base = BaseName(path);
	std::string tmp;
	int fd = -1;
	for (unsigned attempt = 0; attempt < 32; attempt++)
	{
		tmp = JoinPath(dir, "." + base + ".tmp-" + std::to_string(static_cast<unsigned long long>(getpid())) + "-" +
			std::to_string(counter.fetch_add(1, std::memory_order_relaxed) + 1));
		fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (fd >= 0)
			break;
		if (errno != EEXIST)
		{
			if (error)
				*error = ErrnoString("can't create temporary file for " + path, errno);
			return false;
		}
	}
	if (fd < 0)
	{
		if (error)
			*error = "can't allocate a temporary file name for " + path;
		return false;
	}

	size_t written = 0;
	while (written < data.size())
	{
		const ssize_t n = write(fd, data.data() + written, data.size() - written);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			const int saved = errno;
			close(fd);
			unlink(tmp.c_str());
			if (error)
				*error = ErrnoString("can't write temporary file for " + path, saved);
			return false;
		}
		written += static_cast<size_t>(n);
	}
	if (fsync(fd) != 0)
	{
		const int saved = errno;
		close(fd);
		unlink(tmp.c_str());
		if (error)
			*error = ErrnoString("can't flush temporary file for " + path, saved);
		return false;
	}
	if (close(fd) != 0)
	{
		const int saved = errno;
		unlink(tmp.c_str());
		if (error)
			*error = ErrnoString("can't close temporary file for " + path, saved);
		return false;
	}
	if (rename(tmp.c_str(), path.c_str()) != 0)
	{
		const int saved = errno;
		unlink(tmp.c_str());
		if (error)
			*error = ErrnoString("can't replace " + path, saved);
		return false;
	}
	SyncDirectory(dir);
	return true;
}
} // namespace fs

StorageDiscoveryOptions::StorageDiscoveryOptions()
{
	for (int i = 0; i < 8; i++)
		usb_roots.push_back("/mnt/usb" + std::to_string(i));
	for (int i = 0; i < 2; i++)
		extended_roots.push_back("/mnt/ext" + std::to_string(i));
}

std::vector<VolumeInfo> DiscoverVolumes(const StorageDiscoveryOptions& options)
{
	std::vector<VolumeInfo> volumes;
	volumes.push_back(ProbeVolume(options.internal_root, VolumeKind::Internal, -1, options));
	for (size_t i = 0; i < options.usb_roots.size(); i++)
		volumes.push_back(ProbeVolume(options.usb_roots[i], VolumeKind::USB, static_cast<int>(i), options));
	for (size_t i = 0; i < options.extended_roots.size(); i++)
		volumes.push_back(ProbeVolume(options.extended_roots[i], VolumeKind::Extended, static_cast<int>(i), options));
	if (!options.include_absent_slots)
	{
		volumes.erase(std::remove_if(volumes.begin(), volumes.end(), [](const VolumeInfo& volume) { return !volume.present; }),
			volumes.end());
	}
	return volumes;
}

std::string VolumeFingerprint(const std::string& root_path)
{
	struct stat st = {};
	if (lstat(root_path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
		return std::string();
	uint64_t hash = 14695981039346656037ull;
	const uint64_t dev = static_cast<uint64_t>(st.st_dev);
	const uint64_t ino = static_cast<uint64_t>(st.st_ino);
	const uint64_t mode = static_cast<uint64_t>(st.st_mode);
	hash = Fnv1a64Value(hash, dev);
	hash = Fnv1a64Value(hash, ino);
	hash = Fnv1a64Value(hash, mode);
	struct statvfs vfs = {};
	if (ProbeStatvfs(root_path, &vfs))
	{
		const uint64_t blocks = static_cast<uint64_t>(vfs.f_blocks);
		const uint64_t frsize = static_cast<uint64_t>(vfs.f_frsize);
		hash = Fnv1a64Value(hash, blocks);
		hash = Fnv1a64Value(hash, frsize);
	}

	std::vector<std::string> names;
	if (DIR* dir = opendir(root_path.c_str()))
	{
		while (const dirent* entry = readdir(dir))
		{
			if (entry->d_name[0] == '.')
				continue;
			names.emplace_back(entry->d_name);
			if (names.size() >= 64)
				break;
		}
		closedir(dir);
	}
	std::sort(names.begin(), names.end());
	for (const std::string& name : names)
	{
		hash = Fnv1a64(hash, name);
		hash = Fnv1a64(hash, "\xff", 1);
	}
	return "fnv1a:" + Hex64(hash);
}

int FindVolumeIndexForPath(const std::string& path, const std::vector<VolumeInfo>& volumes)
{
	const std::string normalized = fs::NormalizePath(path);
	int best = -1;
	size_t best_size = 0;
	for (size_t i = 0; i < volumes.size(); i++)
	{
		if (!volumes[i].present)
			continue;
		const std::string root = fs::NormalizePath(volumes[i].root_path);
		if (root.size() > best_size && fs::IsPathWithin(normalized, root))
		{
			best = static_cast<int>(i);
			best_size = root.size();
		}
	}
	return best;
}

bool LooksLikeGameImageName(const std::string& name)
{
	if (name.empty() || name[0] == '.')
		return false;
	static constexpr const char* kExtensions[] = {".iso", ".chd", ".cso", ".zso", ".bin", ".img", ".elf"};
	for (const char* extension : kExtensions)
	{
		if (EndsWithCaseInsensitive(name, extension))
			return true;
	}
	return false;
}

FolderListing BrowseFolderPosix(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit,
	const std::vector<VolumeInfo>& volumes)
{
	FolderListing listing;
	listing.path = fs::NormalizePath(path.empty() ? "/" : path);
	if (listing.path.empty() || listing.path[0] != '/')
	{
		listing.error = "folder browsing requires an absolute POSIX path";
		return listing;
	}
	const int volume_index = FindVolumeIndexForPath(listing.path, volumes);
	if (volume_index >= 0)
		listing.volume_id = volumes[static_cast<size_t>(volume_index)].id;

	if (!fs::IsDirectory(listing.path))
	{
		listing.error = "folder does not exist";
		return listing;
	}
	DIR* dir = opendir(listing.path.c_str());
	if (!dir)
	{
		listing.error = ErrnoString("folder is not readable by this process", errno);
		return listing;
	}

	std::vector<FolderEntry> all;
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
		const char* name_c = entry->d_name;
		if (name_c[0] == '.')
			continue;
		FolderEntry item;
		item.name = name_c;
		item.path = fs::JoinPath(listing.path, item.name);
		struct stat st = {};
		if (lstat(item.path.c_str(), &st) != 0)
		{
			item.readable = false;
		}
		else
		{
			item.directory = S_ISDIR(st.st_mode);
			item.image = S_ISREG(st.st_mode) && LooksLikeGameImageName(item.name);
			if (item.directory)
				item.readable = fs::IsReadableDirectory(item.path);
			else if (S_ISREG(st.st_mode))
			{
				// open(2) is the reliable process-credential probe on Orbis; access(2) has misleading pre-jailbreak
				// behavior in the existing frontend and is only a mode-bit approximation elsewhere.
				const int fd = open(item.path.c_str(), O_RDONLY);
				item.readable = fd >= 0;
				if (fd >= 0)
					close(fd);
			}
			else
				item.readable = false;
		}
		if (item.directory && item.readable)
		{
			const CountResult count = CountImagesRecursive(item.path);
			if (count.complete)
				item.compatible_count = count.count;
		}
		all.push_back(std::move(item));
	}
	closedir(dir);
	if (read_error != 0)
		listing.error = ErrnoString("can't list " + listing.path, read_error);

	std::sort(all.begin(), all.end(), [](const FolderEntry& a, const FolderEntry& b) {
		if (a.directory != b.directory)
			return a.directory > b.directory;
		const std::string la = LowerAscii(a.name);
		const std::string lb = LowerAscii(b.name);
		if (la != lb)
			return la < lb;
		return a.name < b.name;
	});

	const std::string needle = LowerAscii(filter);
	std::vector<FolderEntry> filtered;
	for (const FolderEntry& item : all)
	{
		if (!needle.empty() && LowerAscii(item.name).find(needle) == std::string::npos)
			continue;
		listing.contains_compatible = listing.contains_compatible || item.image ||
			(item.directory && item.compatible_count != 0);
		filtered.push_back(item);
	}

	const uint32_t total = static_cast<uint32_t>(filtered.size());
	const size_t begin = std::min<size_t>(offset, filtered.size());
	const size_t available = filtered.size() - begin;
	const size_t count = limit == 0 ? available : std::min<size_t>(limit, available);
	listing.entries.insert(listing.entries.end(), filtered.begin() + static_cast<ptrdiff_t>(begin),
		filtered.begin() + static_cast<ptrdiff_t>(begin + count));
	listing.truncated = begin + count < total;
	listing.parent = listing.path == "/" ? std::string() : fs::DirName(listing.path);

	if (volume_index >= 0)
	{
		const VolumeInfo& volume = volumes[static_cast<size_t>(volume_index)];
		listing.breadcrumbs.push_back(volume.label);
		const std::string root = fs::NormalizePath(volume.root_path);
		if (listing.path != root && fs::IsPathWithin(listing.path, root))
		{
			const std::string relative = listing.path.substr(root.size() + 1);
			for (const std::string& component : SplitComponents(relative))
				listing.breadcrumbs.push_back(component);
		}
	}
	else
	{
		listing.breadcrumbs.push_back("/");
		for (const std::string& component : SplitComponents(listing.path))
			listing.breadcrumbs.push_back(component);
	}
	return listing;
}

SavedSourceStore::SavedSourceStore(std::string file_path)
	: m_file_path(fs::NormalizePath(std::move(file_path)))
{
}

bool SavedSourceStore::Load(std::string* error)
{
	m_records.clear();
	m_next_order = 1;
	m_load_failed = false;
	if (!fs::Exists(m_file_path))
		return true;
	std::string text;
	if (!fs::ReadFile(m_file_path, &text, error))
	{
		m_load_failed = true;
		return false;
	}
	std::istringstream input(text);
	std::string line;
	if (!std::getline(input, line) || line != "caliza-sources-v1")
	{
		if (error)
			*error = m_file_path + " is not a Caliza saved-source store";
		m_load_failed = true;
		return false;
	}

	std::map<uint32_t, std::map<std::string, std::string>> grouped;
	uint32_t count = 0;
	bool malformed = false;
	while (std::getline(input, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		const size_t equals = line.find('=');
		if (equals == std::string::npos)
		{
			malformed = true;
			continue;
		}
		const std::string key = line.substr(0, equals);
		const std::string encoded = line.substr(equals + 1);
		std::string value;
		if (!PercentDecode(encoded, &value))
		{
			malformed = true;
			continue;
		}
		if (key == "count")
		{
			count = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
			continue;
		}
		if (key.compare(0, 7, "source.") != 0)
			continue;
		const size_t dot = key.find('.', 7);
		if (dot == std::string::npos)
			continue;
		const uint32_t index = static_cast<uint32_t>(std::strtoul(key.substr(7, dot - 7).c_str(), nullptr, 10));
		grouped[index][key.substr(dot + 1)] = value;
	}

	uint64_t highest_order = 0;
	for (uint32_t i = 0; i < count; i++)
	{
		const auto found = grouped.find(i);
		if (found == grouped.end())
			continue;
		const std::map<std::string, std::string>& values = found->second;
		const auto get = [&values](const char* key) -> std::string {
			const auto it = values.find(key);
			return it == values.end() ? std::string() : it->second;
		};
		SavedSourceRecord record;
		record.id = get("id");
		record.path = fs::NormalizePath(get("path"));
		record.volume_id = get("volume_id");
		record.volume_root = fs::NormalizePath(get("volume_root"));
		record.relative_path = get("relative_path");
		record.volume_fingerprint = get("volume_fingerprint");
		record.label = get("label");
		record.user_added = get("user_added") != "0";
		record.added_order = std::strtoull(get("added_order").c_str(), nullptr, 10);
		record.game_count = static_cast<uint32_t>(std::strtoul(get("game_count").c_str(), nullptr, 10));
		record.last_scan = std::strtoull(get("last_scan").c_str(), nullptr, 10);
		if (record.id.empty() || record.path.empty() || record.path[0] != '/')
		{
			malformed = true;
			continue;
		}
		highest_order = std::max(highest_order, record.added_order);
		m_records.push_back(std::move(record));
	}
	m_next_order = highest_order + 1;
	if (malformed)
	{
		m_records.clear();
		m_next_order = 1;
		m_load_failed = true;
		if (error)
			*error = m_file_path + " contains malformed saved-source entries; it will not be overwritten implicitly";
		return false;
	}
	return true;
}

bool SavedSourceStore::Save(std::string* error) const
{
	std::ostringstream out;
	out << "caliza-sources-v1\n";
	out << "count=" << m_records.size() << "\n";
	for (size_t i = 0; i < m_records.size(); i++)
	{
		const SavedSourceRecord& record = m_records[i];
		const std::string prefix = "source." + std::to_string(i) + ".";
		const auto put = [&out, &prefix](const char* key, const std::string& value) {
			out << prefix << key << '=' << PercentEncode(value) << "\n";
		};
		put("id", record.id);
		put("path", fs::NormalizePath(record.path));
		put("volume_id", record.volume_id);
		put("volume_root", fs::NormalizePath(record.volume_root));
		put("relative_path", record.relative_path);
		put("volume_fingerprint", record.volume_fingerprint);
		put("label", record.label);
		put("user_added", record.user_added ? "1" : "0");
		put("added_order", std::to_string(record.added_order));
		put("game_count", std::to_string(record.game_count));
		put("last_scan", std::to_string(record.last_scan));
	}
	return fs::WriteFileAtomic(m_file_path, out.str(), error);
}

AddSavedSourceResult SavedSourceStore::Add(const std::string& path, const VolumeInfo& volume, const std::string& label)
{
	AddSavedSourceResult result;
	if (m_load_failed)
	{
		result.status = AddSavedSourceStatus::StoreWriteFailed;
		result.message = "the existing saved-source store is unreadable and will not be overwritten implicitly";
		return result;
	}
	const std::string normalized = fs::NormalizePath(path);
	std::string validation;
	if (!ValidateSourcePath(normalized, volume, &validation))
	{
		result.status = validation.find("volume") != std::string::npos ? AddSavedSourceStatus::UnavailableVolume :
			AddSavedSourceStatus::InvalidPath;
		result.message = validation;
		return result;
	}
	if (std::any_of(m_records.begin(), m_records.end(), [&normalized, &volume](const SavedSourceRecord& record) {
			return SameSavedSourceLocation(record, normalized, volume);
		}))
	{
		result.status = AddSavedSourceStatus::DuplicatePath;
		result.message = "this folder is already saved";
		return result;
	}

	SavedSourceRecord record;
	record.added_order = m_next_order++;
	record.id = MakeSourceId(record.added_order);
	record.path = normalized;
	record.volume_id = volume.id;
	record.volume_root = fs::NormalizePath(volume.root_path);
	record.relative_path = RelativeToVolume(normalized, volume);
	record.volume_fingerprint = volume.fingerprint;
	record.label = label.empty() ? (record.relative_path.empty() ? volume.label : fs::BaseName(normalized)) : label;
	record.user_added = true;

	m_records.push_back(record);
	std::string error;
	if (!Save(&error))
	{
		m_records.pop_back();
		m_next_order--;
		result.status = AddSavedSourceStatus::StoreWriteFailed;
		result.message = error;
		return result;
	}
	result.status = AddSavedSourceStatus::Added;
	result.source_id = record.id;
	return result;
}

SavedSourceMutationResult SavedSourceStore::Remove(const std::string& source_id)
{
	SavedSourceMutationResult result;
	if (m_load_failed)
	{
		result.status = SavedSourceMutationStatus::StoreWriteFailed;
		result.message = "the existing saved-source store is unreadable and will not be overwritten implicitly";
		return result;
	}
	const auto it = std::find_if(m_records.begin(), m_records.end(), [&source_id](const SavedSourceRecord& record) {
		return record.id == source_id;
	});
	if (it == m_records.end())
		return result;
	const size_t index = static_cast<size_t>(it - m_records.begin());
	const SavedSourceRecord backup = *it;
	m_records.erase(m_records.begin() + static_cast<ptrdiff_t>(index));
	std::string error;
	if (!Save(&error))
	{
		m_records.insert(m_records.begin() + static_cast<ptrdiff_t>(index), backup);
		result.status = SavedSourceMutationStatus::StoreWriteFailed;
		result.message = error;
		return result;
	}
	result.status = SavedSourceMutationStatus::Changed;
	result.message = "saved folder link removed; no files were deleted";
	return result;
}

SavedSourceMutationResult SavedSourceStore::Relocate(const std::string& source_id, const std::string& new_path,
	const VolumeInfo& volume)
{
	SavedSourceMutationResult result;
	if (m_load_failed)
	{
		result.status = SavedSourceMutationStatus::StoreWriteFailed;
		result.message = "the existing saved-source store is unreadable and will not be overwritten implicitly";
		return result;
	}
	const auto it = std::find_if(m_records.begin(), m_records.end(), [&source_id](const SavedSourceRecord& record) {
		return record.id == source_id;
	});
	if (it == m_records.end())
		return result;
	const size_t index = static_cast<size_t>(it - m_records.begin());
	const std::string normalized = fs::NormalizePath(new_path);
	std::string validation;
	if (!ValidateSourcePath(normalized, volume, &validation))
	{
		result.status = validation.find("volume") != std::string::npos ? SavedSourceMutationStatus::UnavailableVolume :
			SavedSourceMutationStatus::InvalidPath;
		result.message = validation;
		return result;
	}
	for (size_t i = 0; i < m_records.size(); i++)
	{
		if (i != index && SameSavedSourceLocation(m_records[i], normalized, volume))
		{
			result.status = SavedSourceMutationStatus::DuplicatePath;
			result.message = "another saved folder already uses this path";
			return result;
		}
	}

	SavedSourceRecord backup = m_records[index];
	m_records[index].path = normalized;
	m_records[index].volume_id = volume.id;
	m_records[index].volume_root = fs::NormalizePath(volume.root_path);
	m_records[index].relative_path = RelativeToVolume(normalized, volume);
	m_records[index].volume_fingerprint = volume.fingerprint;
	if (m_records[index].label.empty() || m_records[index].label == fs::BaseName(backup.path))
		m_records[index].label = m_records[index].relative_path.empty() ? volume.label : fs::BaseName(normalized);
	std::string error;
	if (!Save(&error))
	{
		m_records[index] = backup;
		result.status = SavedSourceMutationStatus::StoreWriteFailed;
		result.message = error;
		return result;
	}
	result.status = SavedSourceMutationStatus::Changed;
	result.message = "saved folder link relocated; no files were moved";
	return result;
}

SavedSourceMutationResult SavedSourceStore::UpdateScan(const std::string& source_id, uint32_t game_count, uint64_t last_scan)
{
	SavedSourceMutationResult result;
	if (m_load_failed)
	{
		result.status = SavedSourceMutationStatus::StoreWriteFailed;
		result.message = "the existing saved-source store is unreadable and will not be overwritten implicitly";
		return result;
	}
	const auto it = std::find_if(m_records.begin(), m_records.end(), [&source_id](const SavedSourceRecord& record) {
		return record.id == source_id;
	});
	if (it == m_records.end())
		return result;
	const size_t index = static_cast<size_t>(it - m_records.begin());
	const SavedSourceRecord backup = m_records[index];
	m_records[index].game_count = game_count;
	m_records[index].last_scan = last_scan;
	std::string error;
	if (!Save(&error))
	{
		m_records[index] = backup;
		result.status = SavedSourceMutationStatus::StoreWriteFailed;
		result.message = error;
		return result;
	}
	result.status = SavedSourceMutationStatus::Changed;
	return result;
}

const SavedSourceRecord* SavedSourceStore::Find(const std::string& source_id) const
{
	for (const SavedSourceRecord& record : m_records)
	{
		if (record.id == source_id)
			return &record;
	}
	return nullptr;
}

const SavedSourceRecord* SavedSourceStore::FindPath(const std::string& path) const
{
	for (const SavedSourceRecord& record : m_records)
	{
		if (SameSavedPath(record.path, path))
			return &record;
	}
	return nullptr;
}

std::vector<LibrarySource> SavedSourceStore::Resolve(const std::vector<VolumeInfo>& volumes) const
{
	std::vector<LibrarySource> sources;
	for (const SavedSourceRecord& record : m_records)
	{
		LibrarySource source;
		source.id = record.id;
		source.path = record.path;
		source.volume_id = record.volume_id;
		source.label = record.label;
		source.user_added = record.user_added;
		source.game_count = record.game_count;
		source.last_scan = record.last_scan;
		source.state = SourceState::Disconnected;
		source.problem = "volume is not present";

		const VolumeMatch match = ResolveRecordVolume(record, volumes);
		if (match.ambiguous_id)
		{
			source.state = SourceState::Error;
			source.problem = "more than one mounted volume reports the saved durable identity";
		}
		else if (match.index >= 0)
		{
			const VolumeInfo& volume = volumes[static_cast<size_t>(match.index)];
			if (!record.volume_id.empty())
			{
				// Durable platform identity is authoritative enough to follow a disk to a new slot.
				source.path = volume.root_path;
				if (!record.relative_path.empty())
					source.path = fs::JoinPath(volume.root_path, record.relative_path);
				source.problem.clear();
			}
			else if (!match.fingerprint_matches)
			{
				source.state = SourceState::Disconnected;
				source.problem = "the volume at the saved path does not match the recorded non-authoritative fingerprint";
				if (match.possible_elsewhere)
					source.problem += "; matching evidence exists at another slot and was not used automatically";
				sources.push_back(std::move(source));
				continue;
			}

			if (!volume.readable)
			{
				source.state = SourceState::Unreadable;
				source.problem = "volume is present but not readable by this process";
			}
			else if (!fs::IsDirectory(source.path))
			{
				source.state = SourceState::Error;
				source.problem = "saved folder no longer exists on the volume";
			}
			else if (!fs::IsReadableDirectory(source.path))
			{
				source.state = SourceState::Unreadable;
				source.problem = "saved folder is not readable by this process";
			}
			else
			{
				source.state = SourceState::Connected;
				source.problem.clear();
			}
		}
		else if (match.possible_elsewhere)
		{
			source.state = SourceState::Disconnected;
			source.problem = "non-authoritative volume evidence matches another slot; manual relocation is required";
		}
		sources.push_back(std::move(source));
	}
	return sources;
}

std::string ResolveGameSettingsPath(const std::string& settings_dir,
	const std::string& serial,
	const std::string& stem)
{
	std::string safe_serial;
	size_t begin = 0;
	while (begin < serial.size() && std::isspace(static_cast<unsigned char>(serial[begin])))
		++begin;
	size_t end = serial.size();
	while (end > begin && std::isspace(static_cast<unsigned char>(serial[end - 1])))
		--end;
	bool valid = (end > begin);
	for (size_t i = begin; i < end && valid; ++i)
	{
		const unsigned char c = static_cast<unsigned char>(serial[i]);
		if (std::isalnum(c) || c == '-' || c == '_')
			safe_serial.push_back(static_cast<char>(std::toupper(c)));
		else
			valid = false;
	}
	if (!valid)
		safe_serial.clear();

	const std::string stem_path = settings_dir + "/" + stem + ".ini";
	if (safe_serial.empty())
		return stem_path;

	const std::string serial_path = settings_dir + "/" + safe_serial + ".ini";
	if (fs::Exists(serial_path))
		return serial_path;
	if (!stem.empty() && fs::Exists(stem_path))
		return stem_path;
	return serial_path;
}
} // namespace caliza::storage
