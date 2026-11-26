#include "ps4_wsi_decision.h"

#include <cstdio>

static int failures = 0;

static void check(bool condition, const char* what)
{
	if (!condition)
	{
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

int main()
{
	check(ps4_wsi_image_available(0, false, 0, -1, 8, 0), "never-presented image is free without flip status");
	check(!ps4_wsi_image_available(7, false, 9, 9, 8, 1), "presented image is busy without flip status");
	check(!ps4_wsi_image_available(7, true, 6, 9, 8, 1), "stale flip status cannot free an image");
	check(!ps4_wsi_image_available(7, true, 7, 9, 8, 1), "image still on screen is busy");
	check(ps4_wsi_image_available(7, true, 7, 8, 8, 1), "image is free after scanout moves away");

	const int64_t all_free[3] = {0, 0, 0};
	uint32_t image = 99, next = 99;
	check(ps4_wsi_select_image(all_free, 1, false, 0, -1, 8, &image, &next) && image == 1 && next == 2,
		"selection prefers round-robin image");
	const int64_t first_busy[3] = {4, 0, 0};
	check(ps4_wsi_select_image(first_busy, 0, true, 4, 8, 8, &image, &next) && image == 1 && next == 2,
		"selection skips the image still on screen");
	const int64_t all_busy[3] = {4, 4, 4};
	check(!ps4_wsi_select_image(all_busy, 0, false, 0, -1, 8, &image, &next), "no free image means acquire waits");

	check(ps4_wsi_flip_mode(true, 11, 22) == 11, "FIFO present mode uses the vsync flip mode");
	check(ps4_wsi_flip_mode(false, 11, 22) == 22, "non-FIFO present mode uses the immediate flip mode");
	check(!ps4_wsi_gpu_flip_needs_cpu_fallback(0), "successful GPU flip does not fall back");
	check(ps4_wsi_gpu_flip_needs_cpu_fallback(1), "positive GPU-flip refusal falls back");
	check(ps4_wsi_gpu_flip_needs_cpu_fallback(-1), "negative GPU-flip failure falls back");
	check(ps4_wsi_flip_succeeded(0), "zero final flip result is success");
	check(ps4_wsi_flip_succeeded(1), "positive final flip result is success");
	check(!ps4_wsi_flip_succeeded(-1), "negative final flip result is failure");

	if (failures)
		return 1;
	std::printf("wsi decision tests: PASS\n");
	return 0;
}
