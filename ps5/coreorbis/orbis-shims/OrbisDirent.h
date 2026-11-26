// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace OrbisDirent
{
enum class Result
{
	Entry,
	Exhausted,
	Invalid,
};

// Read FreeBSD's directory records without assuming every record names a live file.
inline Result Next(const char* buffer, size_t length, size_t& offset, const char*& record)
{
	while (offset < length)
	{
		const auto* bytes = reinterpret_cast<const uint8_t*>(buffer + offset);
		const size_t remaining = length - offset;
		if (remaining < 8)
			return Result::Invalid;
		const size_t record_size = bytes[4] | (static_cast<size_t>(bytes[5]) << 8);
		if (record_size < 12 || record_size % 4 != 0 || record_size > remaining)
			return Result::Invalid;

		offset += record_size;
		const size_t name_size = bytes[7];
		if (!(bytes[0] || bytes[1] || bytes[2] || bytes[3]) || name_size == 0)
			continue;
		if (8 + name_size >= record_size || bytes[8 + name_size] != '\0' ||
			std::memchr(bytes + 8, '\0', name_size))
			return Result::Invalid;
		record = reinterpret_cast<const char*>(bytes);
		return Result::Entry;
	}
	return Result::Exhausted;
}
}
