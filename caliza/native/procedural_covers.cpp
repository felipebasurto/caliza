// Caliza Estratos: deterministic fictional cover art for host previews.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "procedural_covers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace caliza
{
namespace
{
struct Rgb
{
	float r, g, b;
};

uint64_t Hash64(const std::string& text)
{
	uint64_t h = 1469598103934665603ull;
	for (unsigned char c : text)
	{
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

uint32_t Rand32(uint64_t& state)
{
	state = state * 6364136223846793005ull + 1442695040888963407ull;
	return static_cast<uint32_t>(state >> 32);
}

float Unit(uint64_t seed, int shift)
{
	return static_cast<float>((seed >> shift) & 1023) / 1023.0f;
}

Rgb Mix(Rgb a, Rgb b, float t)
{
	return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

struct Canvas
{
	int w = 0, h = 0;
	std::vector<uint8_t> px;

	Canvas(int width, int height) : w(width), h(height), px(static_cast<size_t>(width) * height * 4, 0) {}

	void Put(int x, int y, Rgb color, float alpha)
	{
		if (x < 0 || y < 0 || x >= w || y >= h || alpha <= 0.0f)
			return;
		uint8_t* p = px.data() + (static_cast<size_t>(y) * w + x) * 4;
		const float a = std::min(1.0f, alpha);
		p[0] = static_cast<uint8_t>(std::min(255.0f, p[0] * (1.0f - a) + color.r * 255.0f * a + 0.5f));
		p[1] = static_cast<uint8_t>(std::min(255.0f, p[1] * (1.0f - a) + color.g * 255.0f * a + 0.5f));
		p[2] = static_cast<uint8_t>(std::min(255.0f, p[2] * (1.0f - a) + color.b * 255.0f * a + 0.5f));
		p[3] = 255;
	}

	void VerticalGradient(Rgb top, Rgb bottom, int y0, int y1)
	{
		y0 = std::max(0, y0);
		y1 = std::min(h, y1);
		for (int y = y0; y < y1; y++)
		{
			const float t = y1 == y0 ? 0.0f : static_cast<float>(y - y0) / static_cast<float>(y1 - y0);
			const Rgb c = Mix(top, bottom, t);
			for (int x = 0; x < w; x++)
				Put(x, y, c, 1.0f);
		}
	}

	bool Inside(const std::vector<std::pair<float, float>>& poly, float x, float y) const
	{
		bool inside = false;
		for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
		{
			const float xi = poly[i].first, yi = poly[i].second;
			const float xj = poly[j].first, yj = poly[j].second;
			if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi + 0.000001f) + xi)
				inside = !inside;
		}
		return inside;
	}

	void FillPoly(const std::vector<std::pair<float, float>>& poly, Rgb color, float alpha = 1.0f)
	{
		if (poly.size() < 3)
			return;
		float minx = w, maxx = 0, miny = h, maxy = 0;
		for (const auto& p : poly)
		{
			minx = std::min(minx, p.first);
			maxx = std::max(maxx, p.first);
			miny = std::min(miny, p.second);
			maxy = std::max(maxy, p.second);
		}
		const int x0 = std::max(0, static_cast<int>(std::floor(minx)) - 1);
		const int x1 = std::min(w - 1, static_cast<int>(std::ceil(maxx)) + 1);
		const int y0 = std::max(0, static_cast<int>(std::floor(miny)) - 1);
		const int y1 = std::min(h - 1, static_cast<int>(std::ceil(maxy)) + 1);
		for (int y = y0; y <= y1; y++)
			for (int x = x0; x <= x1; x++)
			{
				// Four tap coverage keeps diagonal mineral edges readable without needing MSAA.
				int covered = 0;
				for (int sy = 0; sy < 2; sy++)
					for (int sx = 0; sx < 2; sx++)
						if (Inside(poly, x + 0.25f + 0.5f * sx, y + 0.25f + 0.5f * sy))
							covered++;
				if (covered)
					Put(x, y, color, alpha * covered * 0.25f);
			}
	}

	void FillCircle(float cx, float cy, float radius, Rgb color, float alpha = 1.0f)
	{
		const int x0 = std::max(0, static_cast<int>(cx - radius - 1));
		const int x1 = std::min(w - 1, static_cast<int>(cx + radius + 1));
		const int y0 = std::max(0, static_cast<int>(cy - radius - 1));
		const int y1 = std::min(h - 1, static_cast<int>(cy + radius + 1));
		for (int y = y0; y <= y1; y++)
			for (int x = x0; x <= x1; x++)
			{
				const float dx = x + 0.5f - cx;
				const float dy = y + 0.5f - cy;
				const float d = std::sqrt(dx * dx + dy * dy);
				const float a = std::max(0.0f, std::min(1.0f, radius + 0.75f - d));
				if (a > 0)
					Put(x, y, color, alpha * a);
			}
	}

	void FillRect(int x0, int y0, int x1, int y1, Rgb color, float alpha)
	{
		for (int y = std::max(0, y0); y < std::min(h, y1); y++)
			for (int x = std::max(0, x0); x < std::min(w, x1); x++)
				Put(x, y, color, alpha);
	}

	void Ridge(float base_y, float amp, uint64_t seed, Rgb color, float alpha)
	{
		std::vector<std::pair<float, float>> poly;
		uint64_t state = seed;
		poly.push_back({-10, static_cast<float>(h + 10)});
		poly.push_back({-10, base_y});
		for (int x = 0; x <= w + 32; x += 32)
		{
			const float r = static_cast<float>(Rand32(state) & 0xffff) / 65535.0f;
			const float wave = std::sin(static_cast<float>(x) * 0.031f + static_cast<float>(seed & 31)) * 0.3f;
			poly.push_back({static_cast<float>(x), base_y - amp * (0.25f + r * 0.75f + wave)});
		}
		poly.push_back({static_cast<float>(w + 10), static_cast<float>(h + 10)});
		FillPoly(poly, color, alpha);
	}

	void DiagonalStrata(float y, float thickness, Rgb color, float alpha, float slope)
	{
		FillPoly({{-30, y + slope * -30}, {static_cast<float>(w + 30), y + slope * (w + 30)},
			{static_cast<float>(w + 30), y + thickness + slope * (w + 30)}, {-30, y + thickness + slope * -30}}, color, alpha);
	}

	void Grain(uint64_t seed)
	{
		uint64_t state = seed;
		for (int y = 0; y < h; y += 2)
			for (int x = 0; x < w; x += 2)
			{
				const float n = static_cast<float>(Rand32(state) & 15) / 15.0f;
				const float a = 0.018f * n;
				Put(x, y, {1, 1, 1}, a);
			}
	}

	void Vignette()
	{
		const float cx = w * 0.5f, cy = h * 0.5f;
		const float maxd = std::sqrt(cx * cx + cy * cy);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				const float dx = x - cx, dy = y - cy;
				const float d = std::sqrt(dx * dx + dy * dy) / maxd;
				if (d > 0.55f)
					Put(x, y, {0.005f, 0.008f, 0.018f}, (d - 0.55f) * 0.55f);
			}
	}
};

void RallyPoster(Canvas& c, uint64_t seed)
{
	c.VerticalGradient({0.12f, 0.19f, 0.39f}, {0.88f, 0.43f, 0.20f}, 0, c.h);
	c.FillCircle(c.w * (0.58f + 0.28f * Unit(seed, 8)), c.h * (0.14f + 0.08f * Unit(seed, 18)), c.w * 0.12f, {1.0f, 0.78f, 0.43f}, 0.8f);
	c.Ridge(c.h * 0.42f, c.h * 0.12f, seed + 1, {0.34f, 0.18f, 0.17f}, 1);
	c.Ridge(c.h * 0.55f, c.h * 0.10f, seed + 2, {0.55f, 0.25f, 0.17f}, 1);
	for (int i = 0; i < 5; i++)
		c.DiagonalStrata(c.h * (0.54f + i * 0.035f), 3.0f + i, {0.95f, 0.55f + i * 0.04f, 0.25f}, 0.22f, -0.07f);
	const float cx = c.w * (0.42f + 0.18f * Unit(seed, 28));
	c.FillPoly({{cx - 24, c.h}, {cx + 55, c.h}, {cx + 12, c.h * 0.58f}, {cx - 6, c.h * 0.58f}}, {0.12f, 0.10f, 0.13f}, 0.95f);
	c.FillPoly({{cx + 2, c.h}, {cx + 13, c.h}, {cx + 6, c.h * 0.60f}, {cx + 1, c.h * 0.60f}}, {0.91f, 0.78f, 0.52f}, 0.75f);
}

void CoastPoster(Canvas& c, uint64_t seed)
{
	c.VerticalGradient({0.08f, 0.15f, 0.34f}, {0.40f, 0.65f, 0.78f}, 0, c.h * 0.62f);
	c.VerticalGradient({0.03f, 0.14f, 0.25f}, {0.04f, 0.31f, 0.43f}, c.h * 0.62f, c.h);
	c.FillCircle(c.w * (0.58f + 0.28f * Unit(seed, 8)), c.h * (0.14f + 0.08f * Unit(seed, 18)), c.w * 0.10f, {0.95f, 0.91f, 0.70f}, 0.9f);
	c.Ridge(c.h * 0.53f, c.h * 0.13f, seed + 3, {0.09f, 0.15f, 0.20f}, 1);
	c.FillPoly({{0, c.h * 0.68f}, {c.w * 0.36f, c.h * 0.45f}, {c.w * 0.48f, c.h}, {0, c.h}}, {0.07f, 0.09f, 0.12f}, 1);
	const float lx = c.w * (0.20f + 0.20f * Unit(seed, 28));
	c.FillPoly({{lx, c.h * 0.46f}, {lx + c.w * 0.06f, c.h * 0.46f}, {lx + c.w * 0.05f, c.h * 0.31f}, {lx + c.w * 0.01f, c.h * 0.31f}},
		{0.86f, 0.88f, 0.82f}, 1);
	c.FillPoly({{lx - c.w * 0.015f, c.h * 0.31f}, {lx + c.w * 0.075f, c.h * 0.31f}, {lx + c.w * 0.06f, c.h * 0.285f}, {lx, c.h * 0.285f}},
		{0.10f, 0.12f, 0.16f}, 1);
	c.FillPoly({{lx + c.w * 0.06f, c.h * 0.32f}, {lx + c.w * 0.70f, c.h * 0.23f}, {lx + c.w * 0.70f, c.h * 0.28f}}, {0.75f, 0.93f, 1.0f}, 0.17f);
	for (int i = 0; i < 6; i++)
		c.DiagonalStrata(c.h * (0.68f + i * 0.035f), 2.5f, {0.62f, 0.91f, 1.0f}, 0.17f, 0.04f);
}

void QuarryPoster(Canvas& c, uint64_t seed)
{
	c.VerticalGradient({0.09f, 0.12f, 0.22f}, {0.53f, 0.62f, 0.71f}, 0, c.h);
	uint64_t state = seed + 17;
	for (int i = 0; i < 8; i++)
	{
		const float x = -40.0f + static_cast<float>(Rand32(state) % c.w);
		const float y = c.h * (0.25f + 0.06f * i);
		const float ww = 90.0f + static_cast<float>(Rand32(state) % 150);
		const float hh = 36.0f + static_cast<float>(Rand32(state) % 70);
		const float shade = 0.48f + 0.06f * (i % 3);
		c.FillPoly({{x, y}, {x + ww, y - hh * 0.35f}, {x + ww + 35, y + hh}, {x + 25, y + hh * 1.3f}},
			{shade, shade + 0.04f, shade + 0.08f}, 0.92f);
	}
	for (int i = 0; i < 5; i++)
		c.DiagonalStrata(c.h * (0.49f + i * 0.07f), 4.0f + i, {0.64f, 0.87f, 1.0f}, 0.18f, -0.15f);
	c.Ridge(c.h * 0.82f, c.h * 0.08f, seed + 4, {0.08f, 0.10f, 0.14f}, 1);
}

void StationPoster(Canvas& c, uint64_t seed)
{
	c.VerticalGradient({0.015f, 0.018f, 0.055f}, {0.16f, 0.08f, 0.30f}, 0, c.h);
	uint64_t state = seed + 23;
	for (int i = 0; i < 90; i++)
	{
		const int x = static_cast<int>(Rand32(state) % c.w);
		const int y = static_cast<int>(Rand32(state) % c.h);
		const float a = 0.25f + static_cast<float>(Rand32(state) % 70) / 100.0f;
		c.FillCircle(x, y, (Rand32(state) % 2) + 0.7f, {0.75f, 0.86f, 1.0f}, a);
	}
	const float planet_x = 0.62f + 0.24f * Unit(seed, 8);
	c.FillCircle(c.w * planet_x, c.h * (0.20f + 0.12f * Unit(seed, 18)), c.w * 0.34f, {0.31f, 0.18f, 0.53f}, 0.95f);
	c.FillCircle(c.w * (planet_x - 0.07f), c.h * (0.17f + 0.10f * Unit(seed, 18)), c.w * 0.32f, {0.48f, 0.28f, 0.68f}, 0.36f);
	c.FillPoly({{c.w * 0.08f, c.h * 0.57f}, {c.w * 0.84f, c.h * 0.43f}, {c.w * 0.86f, c.h * 0.47f}, {c.w * 0.10f, c.h * 0.61f}},
		{0.54f, 0.74f, 0.86f}, 0.8f);
	c.FillCircle(c.w * 0.48f, c.h * 0.52f, c.w * 0.12f, {0.66f, 0.78f, 0.86f}, 1);
	for (int i = 0; i < 4; i++)
		c.DiagonalStrata(c.h * (0.69f + i * 0.045f), 3.0f + i, {0.55f, 0.87f, 1.0f}, 0.19f, -0.11f);
}

} // namespace

Texture2D MakeProceduralCover(const std::string& key, const std::string& title, int width, int height)
{
	const uint64_t seed = Hash64(key + "|" + title);
	Canvas canvas(width, height);
	switch ((seed ^ (seed >> 13) ^ (seed >> 7)) % 4)
	{
		case 0: RallyPoster(canvas, seed); break;
		case 1: CoastPoster(canvas, seed); break;
		case 2: QuarryPoster(canvas, seed); break;
		default: StationPoster(canvas, seed); break;
	}
	canvas.Grain(seed + 100);
	canvas.Vignette();

	Texture2D out;
	out.width = width;
	out.height = height;
	out.format = Texture2D::Format::RGBA8;
	out.pixels = std::move(canvas.px);
	return out;
}
} // namespace caliza
