// PS4 port (AI-assisted): the shared layout of the Vulkan WSI health counters.
// Keep this header as the single definition on both sides of ps4_wsi_present_stats.
#pragma once

#include <cstdint>

struct Ps4WsiPresentStats
{
	uint64_t presents;
	uint64_t present_failures;
	uint64_t flip_fallbacks;
	uint64_t acquire_timeouts;
	uint64_t acquire_not_ready;
	uint64_t videoout_register_failures;
	uint64_t videoout_register_retries;
	uint64_t videoout_unregister_retries;
	uint64_t videoout_unregister_retry_failures;
	uint64_t videoout_unregister_retry_successes;
	uint64_t videoout_slot_exhausted;
};

static_assert(sizeof(Ps4WsiPresentStats) == 11 * sizeof(uint64_t), "PS4 WSI stats layout changed");
