// PS4 port (AI-assisted): pure WSI acquire/present decisions, separated from Vulkan and
// VideoOut so host tests can pin the exact compatibility behavior.
#pragma once

#include <cstdint>

inline bool ps4_wsi_image_available(int64_t shown_by, bool flip_valid, int64_t flip_arg,
	int current_buffer, int first_buffer, uint32_t image)
{
	if (shown_by == 0)
		return true;
	if (!flip_valid || flip_arg < shown_by)
		return false;
	return current_buffer != first_buffer + static_cast<int>(image);
}

inline bool ps4_wsi_select_image(const int64_t shown_by[3], uint32_t next, bool flip_valid,
	int64_t flip_arg, int current_buffer, int first_buffer, uint32_t* image_out, uint32_t* next_out)
{
	for (uint32_t offset = 0; offset < 3; offset++)
	{
		const uint32_t image = (next + offset) % 3;
		if (!ps4_wsi_image_available(shown_by[image], flip_valid, flip_arg, current_buffer, first_buffer, image))
			continue;
		*next_out = (image + 1) % 3;
		*image_out = image;
		return true;
	}
	return false;
}

inline unsigned ps4_wsi_flip_mode(bool fifo_present_mode, unsigned vsync_mode, unsigned hsync_mode)
{
	return fifo_present_mode ? vsync_mode : hsync_mode;
}

inline bool ps4_wsi_gpu_flip_needs_cpu_fallback(int gpu_flip_result)
{
	return gpu_flip_result != 0;
}

inline bool ps4_wsi_flip_succeeded(int final_flip_result)
{
	return final_flip_result >= 0;
}
