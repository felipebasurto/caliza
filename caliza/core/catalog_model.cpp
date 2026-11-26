// Caliza PS4: portable catalog identity/model implementation.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalog_model.h"

#include "legacy_compat.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace caliza::catalog
{
namespace
{
std::string Lower(std::string value)
{
	for (char& c : value)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return value;
}

std::string Trim(const std::string& text)
{
	size_t first = 0, last = text.size();
	while (first < last && std::isspace(static_cast<unsigned char>(text[first])))
		++first;
	while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
		--last;
	return text.substr(first, last - first);
}

bool HasExtension(const std::string& name, const char* extension)
{
	if (name.size() < std::strlen(extension))
		return false;
	const std::string tail = Lower(name.substr(name.size() - std::strlen(extension)));
	return tail == extension;
}

const char* const kRegions[] = {"USA", "Europe", "Japan", "Korea", "Asia", "World", "Australia", "France", "Germany",
	"Italy", "Spain", "UK", "Canada", "Brazil", "Russia", "China", "Taiwan", "Netherlands", "Sweden"};

std::string EscapeJson(const std::string& text)
{
	static const char hex[] = "0123456789abcdef";
	std::string out;
	for (const char raw : text)
	{
		const unsigned char c = static_cast<unsigned char>(raw);
		switch (c)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20)
				{
					out += "\\u00";
					out += hex[c >> 4];
					out += hex[c & 15];
				}
				else
					out += static_cast<char>(c);
		}
	}
	return out;
}

bool ParseJsonStringAt(const std::string& json, size_t& pos, std::string& out)
{
	out.clear();
	while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
		++pos;
	if (pos >= json.size() || json[pos] != '"')
		return false;
	++pos;
	while (pos < json.size())
	{
		const unsigned char c = static_cast<unsigned char>(json[pos++]);
		if (c == '"')
			return true;
		if (c != '\\')
		{
			out += static_cast<char>(c);
			continue;
		}
		if (pos >= json.size())
			return false;
		const char escape = json[pos++];
		switch (escape)
		{
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u':
			{
				if (pos + 4 > json.size())
					return false;
				unsigned value = 0;
				for (int i = 0; i < 4; ++i)
				{
					const char h = json[pos++];
					value <<= 4;
					if (h >= '0' && h <= '9') value += static_cast<unsigned>(h - '0');
					else if (h >= 'a' && h <= 'f') value += static_cast<unsigned>(h - 'a' + 10);
					else if (h >= 'A' && h <= 'F') value += static_cast<unsigned>(h - 'A' + 10);
					else return false;
				}
				auto append_utf8 = [&out](uint32_t cp) {
					if (cp < 0x80)
						out += static_cast<char>(cp);
					else if (cp < 0x800)
					{
						out += static_cast<char>(0xc0 | (cp >> 6));
						out += static_cast<char>(0x80 | (cp & 0x3f));
					}
					else if (cp < 0x10000)
					{
						out += static_cast<char>(0xe0 | (cp >> 12));
						out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
						out += static_cast<char>(0x80 | (cp & 0x3f));
					}
					else
					{
						out += static_cast<char>(0xf0 | (cp >> 18));
						out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
						out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
						out += static_cast<char>(0x80 | (cp & 0x3f));
					}
				};
				if (value >= 0xd800 && value <= 0xdbff)
				{
					if (pos + 6 > json.size() || json[pos] != '\\' || json[pos + 1] != 'u')
						return false;
					pos += 2;
					uint32_t low = 0;
					for (int i = 0; i < 4; ++i)
					{
						const char h = json[pos++];
						low <<= 4;
						if (h >= '0' && h <= '9') low += static_cast<uint32_t>(h - '0');
						else if (h >= 'a' && h <= 'f') low += static_cast<uint32_t>(h - 'a' + 10);
						else if (h >= 'A' && h <= 'F') low += static_cast<uint32_t>(h - 'A' + 10);
						else return false;
					}
					if (low < 0xdc00 || low > 0xdfff)
						return false;
					append_utf8(0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00));
				}
				else if (value >= 0xdc00 && value <= 0xdfff)
					return false;
				else
					append_utf8(value);
				break;
			}
			default: return false;
		}
	}
	return false;
}

void SkipJsonWhitespace(const std::string& json, size_t& pos)
{
	while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
		++pos;
}

bool ParseJsonU64At(const std::string& json, size_t& pos, uint64_t& out)
{
	SkipJsonWhitespace(json, pos);
	if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos])))
		return false;
	char* end = nullptr;
	out = std::strtoull(json.c_str() + pos, &end, 10);
	if (end == json.c_str() + pos)
		return false;
	pos = static_cast<size_t>(end - json.c_str());
	return true;
}

bool ParseJsonBoolAt(const std::string& json, size_t& pos, bool& out)
{
	SkipJsonWhitespace(json, pos);
	if (json.compare(pos, 4, "true") == 0)
	{
		out = true;
		pos += 4;
		return true;
	}
	if (json.compare(pos, 5, "false") == 0)
	{
		out = false;
		pos += 5;
		return true;
	}
	return false;
}

bool SkipJsonValue(const std::string& json, size_t& pos, unsigned depth = 0)
{
	if (depth > 32)
		return false;
	SkipJsonWhitespace(json, pos);
	if (pos >= json.size())
		return false;
	if (json[pos] == '"')
	{
		std::string ignored;
		return ParseJsonStringAt(json, pos, ignored);
	}
	if (json[pos] == '{' || json[pos] == '[')
	{
		const char open = json[pos++];
		const char close = open == '{' ? '}' : ']';
		SkipJsonWhitespace(json, pos);
		if (pos < json.size() && json[pos] == close)
		{
			++pos;
			return true;
		}
		for (;;)
		{
			if (open == '{')
			{
				std::string key;
				if (!ParseJsonStringAt(json, pos, key))
					return false;
				SkipJsonWhitespace(json, pos);
				if (pos >= json.size() || json[pos] != ':')
					return false;
				++pos;
			}
			if (!SkipJsonValue(json, pos, depth + 1))
				return false;
			SkipJsonWhitespace(json, pos);
			if (pos < json.size() && json[pos] == ',')
			{
				++pos;
				continue;
			}
			if (pos < json.size() && json[pos] == close)
			{
				++pos;
				return true;
			}
			return false;
		}
	}
	if (json.compare(pos, 4, "null") == 0)
	{
		pos += 4;
		return true;
	}
	bool boolean = false;
	if (ParseJsonBoolAt(json, pos, boolean))
		return true;
	const size_t start = pos;
	if (pos < json.size() && json[pos] == '-')
		++pos;
	while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
		++pos;
	if (pos < json.size() && json[pos] == '.')
	{
		++pos;
		while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
			++pos;
	}
	if (pos < json.size() && (json[pos] == 'e' || json[pos] == 'E'))
	{
		++pos;
		if (pos < json.size() && (json[pos] == '+' || json[pos] == '-'))
			++pos;
		while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
			++pos;
	}
	return pos > start;
}

bool ParseJsonStringArrayAt(const std::string& json, size_t& pos, std::vector<std::string>& out)
{
	out.clear();
	SkipJsonWhitespace(json, pos);
	if (pos >= json.size() || json[pos] != '[')
		return false;
	++pos;
	SkipJsonWhitespace(json, pos);
	if (pos < json.size() && json[pos] == ']')
	{
		++pos;
		return true;
	}
	for (;;)
	{
		std::string item;
		if (!ParseJsonStringAt(json, pos, item))
			return false;
		out.push_back(item);
		SkipJsonWhitespace(json, pos);
		if (pos < json.size() && json[pos] == ',')
		{
			++pos;
			continue;
		}
		if (pos < json.size() && json[pos] == ']')
		{
			++pos;
			return true;
		}
		return false;
	}
}
} // namespace

bool IsDiscImageName(const std::string& name)
{
	return HasExtension(name, ".iso") || HasExtension(name, ".chd") || HasExtension(name, ".cso") || HasExtension(name, ".zso") ||
	       HasExtension(name, ".bin") || HasExtension(name, ".img") || IsElfName(name);
}

bool IsElfName(const std::string& name)
{
	return HasExtension(name, ".elf");
}

bool IsCatalogMediaName(const std::string& name)
{
	return !name.empty() && name[0] != '.' && IsDiscImageName(name);
}

bool IsRawImageCandidateName(const std::string& name)
{
	return HasExtension(name, ".bin") || HasExtension(name, ".img");
}

bool ShouldSkipScanSubfolder(const std::string& name)
{
	if (name.empty() || name[0] == '.' || name[0] == '$')
		return true;
	static const char* const kSkip[] = {"bios", "cache", "cheats", "cheats_ws", "covers", "flags", "inputprofiles", "lang",
		"logs", "memcards", "patches", "resources", "settings", "snaps", "sstates", "savestates", "textures", "videos",
		"System Volume Information", "LOST.DIR"};
	const std::string lower = Lower(name);
	for (const char* skip : kSkip)
		if (lower == Lower(skip))
			return true;
	return false;
}

std::vector<ScanRoot> DefaultScanRoots()
{
	std::vector<ScanRoot> roots;
	roots.push_back({"data-games", "Internal games", "/data/caliza/games", RootKind::DataGames, false, false, false, 0, false});
	roots.push_back({"data-root", "Internal storage", "/data/caliza", RootKind::DataRoot, false, false, false, 1, false});
	for (int i = 0; i < 8; ++i)
		roots.push_back({"usb" + std::to_string(i), "USB drive " + std::to_string(i + 1), "/mnt/usb" + std::to_string(i),
			RootKind::UsbDrive, true, false, false, 10 + i, false});
	for (int i = 0; i < 2; ++i)
		roots.push_back({"ext" + std::to_string(i), i == 0 ? "Extended storage" : "Extended storage 2", "/mnt/ext" + std::to_string(i),
			RootKind::ExtendedStorage, true, false, false, 20 + i, false});
	return roots;
}

std::vector<ScanRoot> LegacyCompatibilityScanRoots()
{
	const std::string legacy_root = legacy::RetiredDataRoot();
	return {
		{"legacy-data-games", "Legacy internal games", legacy_root + "/games", RootKind::DataGames, false, false, false, -2, true},
		{"legacy-data-root", "Legacy internal storage", legacy_root, RootKind::DataRoot, false, false, false, -1, true},
	};
}

std::vector<ScanRoot> ExpandDriveLayouts(const ScanRoot& root)
{
	if (root.kind != RootKind::UsbDrive && root.kind != RootKind::ExtendedStorage)
		return {root};
	std::vector<ScanRoot> out{root};
	const std::string retired = legacy::RetiredLowerName();
	const std::vector<std::pair<std::string, bool>> plain = {
		{"games", false}, {"DVD", false}, {"CD", false}, {"caliza", false}, {retired, true},
	};
	const std::vector<std::pair<std::string, bool>> nested_parents = {
		{"PCSX2", false}, {"caliza", false}, {retired, true},
	};
	const char* const kNested[] = {"games", "DVD", "CD"};
	int order = root.order * 100;
	for (const auto& sub : plain)
		out.push_back({root.id + "-" + Lower(sub.first), root.label + "/" + sub.first, JoinPath(root.path, sub.first), root.kind,
			root.removable, root.user_configured, root.network, order++, sub.second});
	for (const auto& parent : nested_parents)
		for (const char* sub : kNested)
			out.push_back({root.id + "-" + Lower(parent.first) + "-" + Lower(sub), root.label + "/" + parent.first + "/" + sub,
				JoinPath(JoinPath(root.path, parent.first), sub), root.kind, root.removable, root.user_configured, root.network,
				order++, parent.second});
	return out;
}

std::string NormalizePath(std::string path)
{
	std::replace(path.begin(), path.end(), '\\', '/');
	std::vector<std::string> parts;
	size_t pos = 0;
	while (pos <= path.size())
	{
		const size_t slash = path.find('/', pos);
		const std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
		if (!part.empty() && part != ".")
		{
			if (part == ".." && !parts.empty() && parts.back() != "..")
				parts.pop_back();
			else if (part != ".." || path.empty() || path[0] != '/')
				parts.push_back(part);
		}
		if (slash == std::string::npos)
			break;
		pos = slash + 1;
	}
	std::string out = (!path.empty() && path[0] == '/') ? "/" : "";
	for (const std::string& part : parts)
	{
		if (!out.empty() && out.back() != '/')
			out += '/';
		out += part;
	}
	if (out.empty() && (!path.empty() && path[0] == '/'))
		return "/";
	return out;
}

std::string FileName(const std::string& path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string FileStem(const std::string& file_or_path)
{
	std::string stem = FileName(file_or_path);
	const size_t dot = stem.rfind('.');
	if (dot != std::string::npos && dot > 0)
		stem.erase(dot);
	return stem;
}

std::string JoinPath(const std::string& dir, const std::string& file)
{
	if (dir.empty() || dir == "/")
		return dir + file;
	return dir + "/" + file;
}

bool PathWithin(const std::string& path, const std::string& root)
{
	const std::string p = NormalizePath(path), r = NormalizePath(root);
	if (r.empty() || r == "/")
		return !p.empty() && p[0] == '/';
	return p == r || (p.size() > r.size() && p.compare(0, r.size(), r) == 0 && p[r.size()] == '/');
}

std::string MakeStableId(const std::string& path, const std::string& volume_id, const std::string& root_path)
{
	const std::string normalized_path = NormalizePath(path);
	const std::string normalized_root = NormalizePath(root_path);
	if (!volume_id.empty() && !normalized_root.empty() && PathWithin(normalized_path, normalized_root))
	{
		std::string relative = normalized_path == normalized_root ? std::string() : normalized_path.substr(normalized_root.size() + 1);
		return "volume:" + volume_id + ":" + relative;
	}
	return "path:" + normalized_path;
}

std::string MakeContentKey(const std::string& serial, uint64_t bytes, MediaKind media)
{
	return MediaKindName(media) + ":" + (serial.empty() ? "no-serial" : serial) + ":" + std::to_string(bytes);
}

GameIdentity MakeIdentity(const std::string& path, const std::string& volume_id, const std::string& root_path,
	const std::string& serial, uint64_t bytes, MediaKind media)
{
	GameIdentity identity;
	identity.stable_id = MakeStableId(path, volume_id, root_path);
	identity.legacy_web_id = FileName(path);
	identity.settings_stem = FileStem(path);
	identity.content_key = MakeContentKey(serial, bytes, media);
	return identity;
}

void ParseDisplayTitle(const std::string& stem, std::string& title, std::string& region, std::string& extra)
{
	std::string base = stem;
	std::vector<std::string> groups;
	for (;;)
	{
		const std::string trimmed = Trim(base);
		if (trimmed.empty() || (trimmed.back() != ')' && trimmed.back() != ']'))
		{
			base = trimmed;
			break;
		}
		const char open = trimmed.back() == ')' ? '(' : '[';
		const size_t at = trimmed.rfind(open);
		if (at == std::string::npos || at == 0)
		{
			base = trimmed;
			break;
		}
		groups.insert(groups.begin(), trimmed.substr(at + 1, trimmed.size() - at - 2));
		base = trimmed.substr(0, at);
	}
	region.clear();
	extra.clear();
	for (const std::string& group : groups)
	{
		bool is_region = false;
		for (const char* candidate : kRegions)
			if (group.find(candidate) == 0)
				is_region = true;
		if (is_region && region.empty())
			region = group;
		else
			extra += (extra.empty() ? "" : ", ") + group;
	}
	const size_t dash = base.find(" - ");
	std::string first = dash == std::string::npos ? base : base.substr(0, dash);
	const std::string rest = dash == std::string::npos ? std::string() : base.substr(dash + 3);
	for (const char* article : {", The", ", A", ", An"})
	{
		const size_t length = std::strlen(article);
		if (first.size() > length && first.compare(first.size() - length, length, article) == 0)
		{
			first = std::string(article + 2) + " " + first.substr(0, first.size() - length);
			break;
		}
	}
	title = rest.empty() ? first : first + ": " + rest;
}

int DiscNumber(const std::string& name, std::string* rest)
{
	std::string stem = FileStem(name);
	const std::string low = Lower(stem);
	int number = 0;
	size_t at = std::string::npos, end = 0;
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

std::string DiscSetKey(const std::string& name)
{
	std::string key;
	return DiscNumber(name, &key) == 0 ? std::string() : key;
}

bool CatalogLess(const GameEntry& a, const GameEntry& b)
{
	const std::string left = Lower(a.title), right = Lower(b.title);
	if (left != right)
		return left < right;
	return a.file < b.file;
}

std::string MediaKindName(MediaKind media)
{
	switch (media)
	{
		case MediaKind::DiscImage: return "disc";
		case MediaKind::ElfExecutable: return "elf";
		case MediaKind::SystemMenu: return "system-menu";
	}
	return "disc";
}

bool MediaKindFromName(const std::string& name, MediaKind& media)
{
	if (name == "disc") media = MediaKind::DiscImage;
	else if (name == "elf") media = MediaKind::ElfExecutable;
	else if (name == "system-menu") media = MediaKind::SystemMenu;
	else return false;
	return true;
}

std::string GameEntryToJson(const GameEntry& game)
{
	std::string out = "{\"schema_version\":" + std::to_string(game.schema_version);
	auto string_field = [&](const char* key, const std::string& value) {
		out += ",\"" + std::string(key) + "\":\"" + EscapeJson(value) + "\"";
	};
	string_field("stable_id", game.identity.stable_id);
	string_field("legacy_web_id", game.identity.legacy_web_id);
	string_field("settings_stem", game.identity.settings_stem);
	string_field("content_key", game.identity.content_key);
	string_field("path", game.path);
	string_field("file", game.file);
	string_field("title", game.title);
	string_field("region", game.region);
	string_field("extra", game.extra);
	string_field("serial", game.serial);
	string_field("source_root_id", game.source_root_id);
	out += ",\"bytes\":" + std::to_string(game.bytes);
	out += ",\"media\":\"" + std::string(MediaKindName(game.media)) + "\"";
	out += std::string(",\"hidden\":") + (game.hidden ? "true" : "false");
	out += std::string(",\"damaged\":") + (game.damaged ? "true" : "false");
	string_field("damage_reason", game.damage_reason);
	out += ",\"badges\":[";
	for (size_t i = 0; i < game.badges.size(); ++i)
		out += std::string(i ? "," : "") + "\"" + EscapeJson(game.badges[i]) + "\"";
	out += "]";
	string_field("cover_path", game.cover_path);
	out += ",\"disc_number\":" + std::to_string(game.disc_number);
	string_field("disc_set_key", game.disc_set_key);
	out += ",\"scan_revision\":" + std::to_string(game.scan_revision);
	return out + "}";
}

bool GameEntryFromJson(const std::string& json, GameEntry& game, std::string* error)
{
	game = GameEntry();
	size_t pos = 0;
	SkipJsonWhitespace(json, pos);
	if (pos >= json.size() || json[pos] != '{')
	{
		if (error) *error = "catalog entry is not a JSON object";
		return false;
	}
	++pos;
	enum RequiredBit : uint32_t
	{
		kVersion = 1u << 0, kStableId = 1u << 1, kLegacyId = 1u << 2, kSettingsStem = 1u << 3,
		kContentKey = 1u << 4, kPath = 1u << 5, kFile = 1u << 6, kTitle = 1u << 7, kRegion = 1u << 8,
		kExtra = 1u << 9, kSerial = 1u << 10, kSourceRoot = 1u << 11, kBytes = 1u << 12, kMedia = 1u << 13,
		kHidden = 1u << 14, kDamaged = 1u << 15, kDamageReason = 1u << 16, kBadges = 1u << 17,
		kDiscNumber = 1u << 18, kDiscSetKey = 1u << 19, kScanRevision = 1u << 20, kCoverPath = 1u << 21,
	};
	uint32_t seen = 0;
	uint64_t version = 0, disc_number = 0, revision = 0;
	std::string media;
	SkipJsonWhitespace(json, pos);
	if (pos < json.size() && json[pos] != '}')
	for (;;)
	{
		std::string key;
		if (!ParseJsonStringAt(json, pos, key))
		{
			if (error) *error = "malformed catalog object key";
			return false;
		}
		SkipJsonWhitespace(json, pos);
		if (pos >= json.size() || json[pos] != ':')
		{
			if (error) *error = "missing catalog object colon";
			return false;
		}
		++pos;
		bool ok = true;
		uint32_t bit = 0;
		std::string* string_target = nullptr;
		if (key == "schema_version") { bit = kVersion; ok = ParseJsonU64At(json, pos, version); }
		else if (key == "stable_id") { bit = kStableId; string_target = &game.identity.stable_id; }
		else if (key == "legacy_web_id") { bit = kLegacyId; string_target = &game.identity.legacy_web_id; }
		else if (key == "settings_stem") { bit = kSettingsStem; string_target = &game.identity.settings_stem; }
		else if (key == "content_key") { bit = kContentKey; string_target = &game.identity.content_key; }
		else if (key == "path") { bit = kPath; string_target = &game.path; }
		else if (key == "file") { bit = kFile; string_target = &game.file; }
		else if (key == "title") { bit = kTitle; string_target = &game.title; }
		else if (key == "region") { bit = kRegion; string_target = &game.region; }
		else if (key == "extra") { bit = kExtra; string_target = &game.extra; }
		else if (key == "serial") { bit = kSerial; string_target = &game.serial; }
		else if (key == "source_root_id") { bit = kSourceRoot; string_target = &game.source_root_id; }
		else if (key == "bytes") { bit = kBytes; ok = ParseJsonU64At(json, pos, game.bytes); }
		else if (key == "media") { bit = kMedia; string_target = &media; }
		else if (key == "hidden") { bit = kHidden; ok = ParseJsonBoolAt(json, pos, game.hidden); }
		else if (key == "damaged") { bit = kDamaged; ok = ParseJsonBoolAt(json, pos, game.damaged); }
		else if (key == "damage_reason") { bit = kDamageReason; string_target = &game.damage_reason; }
		else if (key == "badges") { bit = kBadges; ok = ParseJsonStringArrayAt(json, pos, game.badges); }
		else if (key == "cover_path") { bit = kCoverPath; string_target = &game.cover_path; }
		else if (key == "disc_number") { bit = kDiscNumber; ok = ParseJsonU64At(json, pos, disc_number); }
		else if (key == "disc_set_key") { bit = kDiscSetKey; string_target = &game.disc_set_key; }
		else if (key == "scan_revision") { bit = kScanRevision; ok = ParseJsonU64At(json, pos, revision); }
		else ok = SkipJsonValue(json, pos);
		if (string_target)
			ok = ParseJsonStringAt(json, pos, *string_target);
		if (!ok)
		{
			if (error) *error = "malformed value for catalog key " + key;
			return false;
		}
		if (bit)
		{
			if (seen & bit)
			{
				if (error) *error = "duplicate catalog key " + key;
				return false;
			}
			seen |= bit;
		}
		SkipJsonWhitespace(json, pos);
		if (pos < json.size() && json[pos] == ',')
		{
			++pos;
			SkipJsonWhitespace(json, pos);
			continue;
		}
		break;
	}
	SkipJsonWhitespace(json, pos);
	if (pos >= json.size() || json[pos] != '}')
	{
		if (error) *error = "unterminated catalog object";
		return false;
	}
	++pos;
	SkipJsonWhitespace(json, pos);
	if (pos != json.size())
	{
		if (error) *error = "trailing data after catalog object";
		return false;
	}
	constexpr uint32_t kRequired = kVersion | kStableId | kLegacyId | kSettingsStem | kContentKey | kPath | kFile | kTitle |
		kRegion | kExtra | kSerial | kSourceRoot | kBytes | kMedia | kHidden | kDamaged | kDamageReason | kBadges |
		kDiscNumber | kDiscSetKey | kScanRevision | kCoverPath;
	if ((seen & kRequired) != kRequired || version != kCatalogSchemaVersion || !MediaKindFromName(media, game.media))
	{
		if (error) *error = "missing fields or unsupported catalog schema_version";
		return false;
	}
	game.schema_version = static_cast<uint32_t>(version);
	game.disc_number = static_cast<int>(disc_number);
	game.scan_revision = revision;
	return true;
}

std::string CatalogToJsonLines(const std::vector<GameEntry>& games)
{
	std::string out;
	for (const GameEntry& game : games)
		out += GameEntryToJson(game) + "\n";
	return out;
}

bool CatalogFromJsonLines(const std::string& lines, std::vector<GameEntry>& games, std::string* error)
{
	games.clear();
	size_t pos = 0;
	unsigned line_number = 0;
	while (pos <= lines.size())
	{
		size_t end = lines.find('\n', pos);
		if (end == std::string::npos)
			end = lines.size();
		const std::string line = Trim(lines.substr(pos, end - pos));
		pos = end + 1;
		if (!line.empty())
		{
			++line_number;
			GameEntry game;
			if (!GameEntryFromJson(line, game, error))
			{
				if (error)
					*error = "line " + std::to_string(line_number) + ": " + *error;
				return false;
			}
			games.push_back(game);
		}
		if (end == lines.size())
			break;
	}
	return true;
}
} // namespace caliza::catalog
