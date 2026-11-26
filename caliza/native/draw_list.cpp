// Caliza Estratos: renderer-independent 2D draw list.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "draw_list.h"

#include "../../ps5/frontend/fe_text.h"

#include <algorithm>
#include <cmath>

namespace caliza
{
void DrawList::Clear()
{
	m_vertices.clear();
	m_batches.clear();
}

void DrawList::Reserve(size_t vertices)
{
	m_vertices.reserve(vertices);
}

void DrawList::Append(const Texture2D* texture, const DrawVertex* vertices, size_t count)
{
	if (!count)
		return;
	if (m_batches.empty() || m_batches.back().texture != texture)
		m_batches.push_back({texture, m_vertices.size(), 0});
	m_vertices.insert(m_vertices.end(), vertices, vertices + count);
	m_batches.back().count += count;
}

void DrawList::AddTriangle(const Texture2D* texture, const DrawVertex& a, const DrawVertex& b, const DrawVertex& c)
{
	const DrawVertex vertices[] = {a, b, c};
	Append(texture, vertices, 3);
}

void DrawList::AddSolidQuad(float x0, float y0, float x1, float y1, Color color)
{
	DrawVertex a{x0, y0, 0, 0, color.r, color.g, color.b, color.a, 0, 0, 0, 3};
	DrawVertex b{x1, y0, 0, 0, color.r, color.g, color.b, color.a, 0, 0, 0, 3};
	DrawVertex c{x1, y1, 0, 0, color.r, color.g, color.b, color.a, 0, 0, 0, 3};
	DrawVertex d{x0, y1, 0, 0, color.r, color.g, color.b, color.a, 0, 0, 0, 3};
	const DrawVertex vertices[] = {a, b, c, a, c, d};
	Append(nullptr, vertices, 6);
}

void DrawList::AddPoly(const std::vector<std::pair<float, float>>& points, Color color)
{
	if (points.size() < 3)
		return;
	for (size_t i = 1; i + 1 < points.size(); i++)
	{
		DrawVertex a{points[0].first, points[0].second, 0, 0, color.r, color.g, color.b, color.a, 0, 0, 0, 3};
		DrawVertex b{points[i].first, points[i].second, 0, 0, color.r, color.g, color.b, color.a, 0, 0, 0, 3};
		DrawVertex c{points[i + 1].first, points[i + 1].second, 0, 0, color.r, color.g, color.b, color.a, 0, 0, 0, 3};
		AddTriangle(nullptr, a, b, c);
	}
}

void DrawList::Transform(size_t first, float dx, float dy, float scale, float ox, float oy, float alpha)
{
	for (size_t i = first; i < m_vertices.size(); i++)
	{
		DrawVertex& v = m_vertices[i];
		v.x = ox + (v.x - ox) * scale + dx;
		v.y = oy + (v.y - oy) * scale + dy;
		v.a *= alpha;
	}
}

void DrawList::AddRoundedRect(float x, float y, float w, float h, float radius, Color color)
{
	const float hw = w * 0.5f;
	const float hh = h * 0.5f;
	const float r = std::min(radius, std::min(hw, hh));
	DrawVertex a{x, y, -hw, -hh, color.r, color.g, color.b, color.a, hw, hh, r, 1};
	DrawVertex b{x + w, y, hw, -hh, color.r, color.g, color.b, color.a, hw, hh, r, 1};
	DrawVertex c{x + w, y + h, hw, hh, color.r, color.g, color.b, color.a, hw, hh, r, 1};
	DrawVertex d{x, y + h, -hw, hh, color.r, color.g, color.b, color.a, hw, hh, r, 1};
	const DrawVertex vertices[] = {a, b, c, a, c, d};
	Append(nullptr, vertices, 6);
}

void DrawList::AddLine(float x0, float y0, float x1, float y1, float width, Color color)
{
	const float dx = x1 - x0;
	const float dy = y1 - y0;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len <= 0.0001f)
		return;
	const float nx = -dy / len * width * 0.5f;
	const float ny = dx / len * width * 0.5f;
	AddPoly({{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}}, color);
}

void DrawList::AddImage(const Texture2D* texture, float x, float y, float w, float h, Color tint)
{
	DrawVertex a{x, y, 0, 0, tint.r, tint.g, tint.b, tint.a, 0, 0, 0, 2};
	DrawVertex b{x + w, y, 1, 0, tint.r, tint.g, tint.b, tint.a, 0, 0, 0, 2};
	DrawVertex c{x + w, y + h, 1, 1, tint.r, tint.g, tint.b, tint.a, 0, 0, 0, 2};
	DrawVertex d{x, y + h, 0, 1, tint.r, tint.g, tint.b, tint.a, 0, 0, 0, 2};
	const DrawVertex vertices[] = {a, b, c, a, c, d};
	Append(texture, vertices, 6);
}

void DrawList::AddImageUV(const Texture2D* texture, float x, float y, float w, float h, float u0, float v0, float u1,
	float v1, Color tint)
{
	DrawVertex a{x, y, u0, v0, tint.r, tint.g, tint.b, tint.a, 0, 0, 0, 2};
	DrawVertex b{x + w, y, u1, v0, tint.r, tint.g, tint.b, tint.a, 0, 0, 0, 2};
	DrawVertex c{x + w, y + h, u1, v1, tint.r, tint.g, tint.b, tint.a, 0, 0, 0, 2};
	DrawVertex d{x, y + h, u0, v1, tint.r, tint.g, tint.b, tint.a, 0, 0, 0, 2};
	const DrawVertex vertices[] = {a, b, c, a, c, d};
	Append(texture, vertices, 6);
}

void DrawList::AddText(const fe::Fonts& fonts, const std::string& utf8, float x, float baseline, float px, Color color,
	float weight, int align)
{
	if (utf8.empty())
		return;
	std::vector<fe::UiVertex> source;
	const uint32_t packed = (static_cast<uint32_t>(color.a * 255.0f + 0.5f) << 24) |
		(static_cast<uint32_t>(color.b * 255.0f + 0.5f) << 16) |
		(static_cast<uint32_t>(color.g * 255.0f + 0.5f) << 8) | static_cast<uint32_t>(color.r * 255.0f + 0.5f);
	fonts.AddText(source, utf8.c_str(), x, baseline, px, packed, weight,
		align == 1 ? fe::Fonts::Center : align == 2 ? fe::Fonts::Right : fe::Fonts::Left);
	std::vector<DrawVertex> converted;
	converted.reserve(source.size());
	for (const fe::UiVertex& in : source)
	{
		DrawVertex out;
		out.x = in.x;
		out.y = in.y;
		out.u = in.u;
		out.v = in.v;
		out.r = static_cast<float>(in.color & 0xff) / 255.0f;
		out.g = static_cast<float>((in.color >> 8) & 0xff) / 255.0f;
		out.b = static_cast<float>((in.color >> 16) & 0xff) / 255.0f;
		out.a = static_cast<float>((in.color >> 24) & 0xff) / 255.0f;
		out.p0 = in.p0;
		out.p1 = in.p1;
		out.p2 = in.p2;
		out.p3 = in.p3;
		converted.push_back(out);
	}
	Append(nullptr, converted.data(), converted.size());
}
} // namespace caliza
