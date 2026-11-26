// SPDX-License-Identifier: GPL-3.0-or-later
#include "OrbisDirent.h"

#include <cassert>

int main()
{
	using OrbisDirent::Next;
	using OrbisDirent::Result;
	const char records[] = {
		0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, // unused directory record
		1, 0, 0, 0, 12, 0, 4, 3, 'p', 's', '2', 0,
		2, 0, 0, 0, 16, 0, 8, 5, 'a', '.', 'c', 'h', 'd', 0, 0, 0,
	};
	size_t offset = 0;
	const char* record = nullptr;
	assert(Next(records, sizeof(records), offset, record) == Result::Entry);
	assert(std::strcmp(record + 8, "ps2") == 0);
	assert(Next(records, sizeof(records), offset, record) == Result::Entry);
	assert(std::strcmp(record + 8, "a.chd") == 0);
	assert(Next(records, sizeof(records), offset, record) == Result::Exhausted);

	offset = 0;
	assert(Next(records, 12, offset, record) == Result::Exhausted);
	assert(offset == 12); // a buffer with only unused records requires a refill
	assert(Next(records, sizeof(records) - 1, offset, record) == Result::Entry);
	assert(Next(records, sizeof(records) - 1, offset, record) == Result::Invalid);
}
