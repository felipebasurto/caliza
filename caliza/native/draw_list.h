// Caliza Estratos: renderer-independent 2D draw list.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fe
{
class Fonts;
}

namespace caliza
{
struct Color
{
	float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

constexpr Color HexColor(uint32_t rgb, float alpha = 1.0f)
{
	return {static_cast<float>((rgb >> 16) & 0xff) / 255.0f, static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
		static_cast<float>(rgb & 0xff) / 255.0f, alpha};
}

constexpr Color WithAlpha(Color color, float alpha)
{
	color.a = alpha;
	return color;
}

struct Texture2D
{
	enum class Format
	{
		R8,
		RGBA8,
	};

	int width = 0;
	int height = 0;
	Format format = Format::RGBA8;
	std::vector<uint8_t> pixels;
};

// p0..p3 are shader parameters. The Estratos shader uses p3 as the primitive mode:
// 0 SDF glyph, 1 analytic rounded rectangle, 2 RGBA texture, 3 plain solid geometry.
struct DrawVertex
{
	float x = 0, y = 0;
	float u = 0, v = 0;
	float r = 1, g = 1, b = 1, a = 1;
	float p0 = 0, p1 = 0, p2 = 0, p3 = 3;
};

struct DrawBatch
{
	const Texture2D* texture = nullptr; // nullptr means the font atlas/neutral texture
	size_t first = 0;
	size_t count = 0;
};

class DrawList
{
public:
	void Clear();
	void Reserve(size_t vertices);

	void AddTriangle(const Texture2D* texture, const DrawVertex& a, const DrawVertex& b, const DrawVertex& c);
	void AddSolidQuad(float x0, float y0, float x1, float y1, Color color);
	void AddPoly(const std::vector<std::pair<float, float>>& points, Color color);
	void AddRoundedRect(float x, float y, float w, float h, float radius, Color color);
	void AddLine(float x0, float y0, float x1, float y1, float width, Color color);
	void AddImage(const Texture2D* texture, float x, float y, float w, float h, Color tint = {});
	void AddImageUV(const Texture2D* texture, float x, float y, float w, float h, float u0, float v0, float u1,
		float v1, Color tint = {});
	void AddText(const fe::Fonts& fonts, const std::string& utf8, float x, float baseline, float px, Color color,
		float weight = 0.0f, int align = 0); // 0 left, 1 centre, 2 right

	// Moves, scales (about ox, oy) and fades every vertex added since `first`. Animation uses it to treat a group of
	// primitives as one layer without each primitive knowing about motion.
	size_t VertexCount() const { return m_vertices.size(); }
	void Transform(size_t first, float dx, float dy, float scale, float ox, float oy, float alpha);

	const std::vector<DrawVertex>& Vertices() const { return m_vertices; }
	const std::vector<DrawBatch>& Batches() const { return m_batches; }

private:
	void Append(const Texture2D* texture, const DrawVertex* vertices, size_t count);

	std::vector<DrawVertex> m_vertices;
	std::vector<DrawBatch> m_batches;
};
} // namespace caliza
