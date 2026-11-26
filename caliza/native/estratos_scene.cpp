// Caliza Estratos: view-model to 2D composition.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Visual language: a deep indigo void with a still nebula haze and a field of glassy cubes floating in perspective.
// The background stays calm: the cubes only bob slowly in place, and nothing else in it moves. Menus are plain light
// text over that space; focus is a band of cold light with a bright leading edge. All geometry and textures are
// generated here at runtime.

#include "estratos_scene.h"

#include "../../ps5/frontend/fe_text.h"
#include "estratos_theme.h"
#include "procedural_covers.h"
#include "../../ps5/frontend/third_party/qrcodegen/qrcodegen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace caliza
{
namespace
{
float Clamp01(float v)
{
	return std::max(0.0f, std::min(1.0f, v));
}

float SmoothStep(float a, float b, float x)
{
	const float t = Clamp01((x - a) / (b - a));
	return t * t * (3.0f - 2.0f * t);
}

uint32_t HashGrid(int x, int y, uint32_t seed)
{
	uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

float ValueNoise(float x, float y, uint32_t seed, int period)
{
	const int x0 = static_cast<int>(std::floor(x));
	const int y0 = static_cast<int>(std::floor(y));
	const float fx = x - x0, fy = y - y0;
	auto at = [&](int ix, int iy) {
		ix = ((ix % period) + period) % period;
		iy = ((iy % period) + period) % period;
		return static_cast<float>(HashGrid(ix, iy, seed) & 0xffff) / 65535.0f;
	};
	const float sx = fx * fx * (3.0f - 2.0f * fx);
	const float sy = fy * fy * (3.0f - 2.0f * fy);
	const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
	const float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
	return a + (b - a) * sy;
}

Texture2D MakeGlow()
{
	constexpr int kSize = 128;
	Texture2D tex;
	tex.width = kSize;
	tex.height = kSize;
	tex.format = Texture2D::Format::RGBA8;
	tex.pixels.resize(static_cast<size_t>(kSize) * kSize * 4);
	for (int y = 0; y < kSize; y++)
		for (int x = 0; x < kSize; x++)
		{
			const float dx = (x + 0.5f) / kSize * 2.0f - 1.0f;
			const float dy = (y + 0.5f) / kSize * 2.0f - 1.0f;
			const float d = std::sqrt(dx * dx + dy * dy);
			const float a = std::exp(-d * d * 5.0f) * (1.0f - SmoothStep(0.82f, 1.0f, d));
			uint8_t* p = tex.pixels.data() + (static_cast<size_t>(y) * kSize + x) * 4;
			p[0] = p[1] = p[2] = 255;
			p[3] = static_cast<uint8_t>(Clamp01(a) * 255.0f + 0.5f);
		}
	return tex;
}

Texture2D MakeFog()
{
	// Domain-warped fractal noise, transparent towards the edges so the clamped texture never shows a seam.
	constexpr int kW = 448, kH = 252;
	Texture2D tex;
	tex.width = kW;
	tex.height = kH;
	tex.format = Texture2D::Format::RGBA8;
	tex.pixels.resize(static_cast<size_t>(kW) * kH * 4);
	for (int y = 0; y < kH; y++)
		for (int x = 0; x < kW; x++)
		{
			const float u = static_cast<float>(x) / kH * 3.0f;
			const float v = static_cast<float>(y) / kH * 3.0f;
			const float wx = ValueNoise(u * 0.7f, v * 0.7f, 11, 64) * 2.2f;
			const float wy = ValueNoise(u * 0.7f + 5.2f, v * 0.7f + 1.3f, 12, 64) * 2.2f;
			float n = 0.0f, amp = 0.55f, freq = 1.0f;
			for (int o = 0; o < 5; o++)
			{
				n += ValueNoise((u + wx) * freq, (v + wy) * freq, 21 + o, 64) * amp;
				amp *= 0.5f;
				freq *= 2.03f;
			}
			n = SmoothStep(0.34f, 0.92f, n);
			const float ex = std::min(x, kW - 1 - x) / (kW * 0.18f);
			const float ey = std::min(y, kH - 1 - y) / (kH * 0.18f);
			const float edge = Clamp01(std::min(ex, ey));
			uint8_t* p = tex.pixels.data() + (static_cast<size_t>(y) * kW + x) * 4;
			p[0] = p[1] = p[2] = 255;
			p[3] = static_cast<uint8_t>(Clamp01(n * edge * edge) * 255.0f + 0.5f);
		}
	return tex;
}

std::string Capitalized(std::string text)
{
	if (!text.empty() && text[0] >= 'a' && text[0] <= 'z')
		text[0] = static_cast<char>(text[0] - 'a' + 'A');
	return text;
}
} // namespace

EstratosScene::EstratosScene() : m_fonts(new fe::Fonts()), m_glow(MakeGlow()), m_fog(MakeFog()) {}

EstratosScene::~EstratosScene()
{
	delete m_fonts;
}

bool EstratosScene::InitFonts(const uint8_t* text_font, size_t text_size, const uint8_t* icon_font, size_t icon_size)
{
	m_text_font.assign(text_font, text_font + text_size);
	m_icon_font.assign(icon_font, icon_font + icon_size);
	m_ready = m_fonts->Init(m_text_font.data(), m_text_font.size(), m_icon_font.data(), m_icon_font.size());
	if (m_ready)
	{
		m_atlas.width = m_fonts->AtlasWidth();
		m_atlas.height = m_fonts->AtlasHeight();
		m_atlas.format = Texture2D::Format::R8;
		m_atlas.pixels = m_fonts->AtlasPixels();
	}
	return m_ready;
}

const fe::Fonts& EstratosScene::Fonts() const
{
	return *m_fonts;
}

const Texture2D* EstratosScene::CoverFor(const std::string& key, const std::string& title)
{
	const std::string cache_key = key + "|" + title;
	auto it = m_covers.find(cache_key);
	if (it != m_covers.end())
		return &it->second;
	const auto [inserted, _] = m_covers.emplace(cache_key, MakeProceduralCover(key, title));
	(void)_;
	return &inserted->second;
}

namespace
{
using namespace estratos;

constexpr Color kTextSoft = HexColor(0xC9D0F2);

struct Layout
{
	uint32_t width = 0, height = 0;
	float scale = 1.0f;
	float text = 1.0f;
	float safe_x = 0, safe_y = 0, right = 0, bottom = 0;
	float body = 28, small = 23, title = 52;
};

// How the background field sits behind each screen.
struct Look
{
	bool field = true;
	float intensity = 1.0f; // decorative strength of the cubes
	float cx = 0.5f, cy = 0.5f; // field centre as fractions of the screen
	float zoom = 1.0f;
	float left_shade = 0.0f; // darkening behind left-aligned content
};

struct V3
{
	float x = 0, y = 0, z = 0;
};

V3 RotateXYZ(V3 p, float ax, float ay, float az)
{
	float c = std::cos(ax), s = std::sin(ax);
	p = {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
	c = std::cos(ay), s = std::sin(ay);
	p = {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
	c = std::cos(az), s = std::sin(az);
	return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}

float Dot3(V3 a, V3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Color Mix(Color a, Color b, float t)
{
	return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// CSS cubic-bezier(x1, y1, x2, y2) at progress t: solve x(u) = t by bisection, then return y(u).
float CubicBezier(float x1, float y1, float x2, float y2, float t)
{
	if (t <= 0.0f)
		return 0.0f;
	if (t >= 1.0f)
		return 1.0f;
	const auto curve = [](float a, float b, float u) {
		const float v = 1.0f - u;
		return 3.0f * v * v * u * a + 3.0f * v * u * u * b + u * u * u;
	};
	float lo = 0.0f, hi = 1.0f, u = t;
	for (int i = 0; i < 24; i++)
	{
		const float x = curve(x1, x2, u);
		if (std::fabs(x - t) < 1e-5f)
			break;
		(x < t ? lo : hi) = u;
		u = (lo + hi) * 0.5f;
	}
	return curve(y1, y2, u);
}

// The interface's curve: a strong ease-out for things arriving or responding.
float EaseOut(float t)
{
	return CubicBezier(0.23f, 1.0f, 0.32f, 1.0f, t);
}

// How deep a screen sits, so entering moves content in from the right and going back from the left.
int Depth(Screen screen)
{
	switch (screen)
	{
		case Screen::Boot: return 0;
		case Screen::Home:
		case Screen::FirstRun: return 1;
		case Screen::GameDetail:
		case Screen::TexturePacks:
		case Screen::FolderPicker: return 3;
		case Screen::PauseOverlay: return 4;
		default: return 2;
	}
}

struct CubeSpec
{
	V3 pos;
	float size;
	V3 tilt; // fixed orientation in radians
	float bob_phase; // radians; offsets each cube's float so they never move in step
	float bob_speed; // radians per second
	bool dark;
};

const std::vector<CubeSpec>& Cubes()
{
	static const std::vector<CubeSpec> cubes = [] {
		std::vector<CubeSpec> out;
		uint64_t state = 0x9E3779B97F4A7C15ull;
		auto rnd = [&]() {
			state = state * 6364136223846793005ull + 1442695040888963407ull;
			return static_cast<float>((state >> 40) & 0xffffff) / static_cast<float>(0xffffff);
		};
		for (int i = 0; i < 26; i++)
		{
			CubeSpec c;
			c.pos = {-2.3f + rnd() * 4.6f, -1.45f + rnd() * 2.9f, -2.2f + rnd() * 4.4f};
			c.size = 0.09f + rnd() * 0.17f;
			c.tilt = {rnd() * 6.28f, rnd() * 6.28f, rnd() * 6.28f};
			c.bob_phase = rnd() * 6.28f;
			c.bob_speed = 0.45f + rnd() * 0.30f; // one rise and fall every 8 to 14 seconds
			c.dark = (i % 6) == 3;
			out.push_back(c);
		}
		return out;
	}();
	return cubes;
}

enum DetailMode
{
	kDetailNever,
	kDetailAlways,
	kDetailFocused,
};

class Painter
{
public:
	Painter(EstratosScene& scene, DrawList& out, const UiView& view, uint32_t width, uint32_t height, double seconds)
		: m_scene(scene), m_fonts(scene.Fonts()), m_out(out), m_view(view), m_mo(scene.Motion()), m_now(seconds)
	{
		if (!m_mo.started || view.screen != m_mo.screen)
		{
			m_mo.direction = !m_mo.started || Depth(view.screen) >= Depth(m_mo.screen) ? 1 : -1;
			m_mo.screen = view.screen;
			m_mo.screen_since = seconds;
			m_mo.started = true;
		}
		if (view.focus_id != m_mo.focus_id)
		{
			m_mo.focus_id = view.focus_id;
			m_mo.focus_since = seconds;
		}
		if (view.dialog.open != m_mo.dialog_open)
		{
			m_mo.dialog_open = view.dialog.open;
			m_mo.dialog_since = seconds;
		}
		if (view.keyboard.open != m_mo.keyboard_open)
		{
			m_mo.keyboard_open = view.keyboard.open;
			m_mo.keyboard_since = seconds;
		}
		// Reduced motion freezes the ambient field on a composed frame instead of removing it.
		m_t = view.reduce_motion ? 41.0f : static_cast<float>(seconds) + 41.0f;
		m_ui_t = static_cast<float>(seconds);
		m_l.width = width;
		m_l.height = height;
		m_l.scale = static_cast<float>(height) / 1080.0f;
		const float requested = std::max(100u, view.accessibility.text_scale_percent) / 100.0f;
		m_l.text = m_l.scale * requested;
		m_l.safe_x = width * kSafeFraction;
		m_l.safe_y = height * kSafeFraction;
		m_l.right = width - m_l.safe_x;
		m_l.bottom = height - m_l.safe_y;
		m_l.body = kBaseBodyPx * m_l.text;
		m_l.small = kBaseSmallPx * m_l.text;
		m_l.title = kBaseScreenTitlePx * m_l.scale * std::min(1.5f, requested);
	}

	void Paint()
	{
		Background();
		if (m_view.screen == Screen::Boot)
			PaintBoot();
		else if (m_view.screen == Screen::PauseOverlay)
			PaintPause();
		else
		{
			// Each screen arrives as one layer: a short slide from the side it is entered from, and a fade.
			const size_t layer = m_out.VertexCount();
			if (m_view.screen != Screen::Home)
				Chrome();
			switch (m_view.screen)
			{
				case Screen::FirstRun: PaintFirstRun(); break;
				case Screen::Home: PaintHome(); break;
				case Screen::Library: PaintLibrary(); break;
				case Screen::GameDetail: PaintGameDetail(); break;
				case Screen::TexturePacks: PaintTexturePacks(); break;
				case Screen::MemoryCards: PaintMemoryCards(); break;
				case Screen::Settings: PaintSettings(); break;
				case Screen::Controls: PaintControls(); break;
				case Screen::Accessibility: PaintAccessibility(); break;
				case Screen::Diagnostics: PaintDiagnostics(); break;
				case Screen::FolderPicker: PaintFolderPicker(); break;
				default: break;
			}
			const float slide = Travel(Progress(m_mo.screen_since, 0.0f, 0.30f));
			const float fade = Progress(m_mo.screen_since, 0.0f, 0.20f);
			if (slide < 1.0f || fade < 1.0f)
				m_out.Transform(layer, (1.0f - slide) * S(36.0f) * static_cast<float>(m_mo.direction), 0.0f, 1.0f, 0.0f, 0.0f, fade);
		}
		if (m_view.screen != Screen::Boot)
			Toast();
		if (m_view.screen != Screen::PauseOverlay && !m_view.keyboard.open && !m_view.dialog.open)
			Footer();
		if (m_view.keyboard.open)
			Keyboard();
		if (m_view.dialog.open)
			Dialog();
	}

private:
	EstratosScene& m_scene;
	const fe::Fonts& m_fonts;
	DrawList& m_out;
	const UiView& m_view;
	MotionMemory& m_mo;
	double m_now = 0.0;
	Layout m_l;
	float m_t = 0.0f;
	float m_ui_t = 0.0f;
	bool m_status_shown = false;
	bool m_list_layer = false; // List() draws row backdrops and the gliding focus band itself

	// Eased 0..1 progress of an animation that started at `since`, after `delay`, lasting `duration` seconds.
	float Progress(double since, float delay, float duration) const
	{
		return EaseOut(Clamp01(static_cast<float>((m_now - since - delay) / duration)));
	}

	// Reduce motion keeps fades but removes travel: entering things are already in place, leaving ones stay put.
	float Travel(float p) const { return m_view.reduce_motion ? 1.0f : p; }
	float Away(float p) const { return m_view.reduce_motion ? 0.0f : p; }

	// A value that glides to `target` whenever the target changes, retargeting from wherever it is mid-flight.
	// A new screen snaps every tween to its target instead of gliding across from the old screen.
	float Tween(const std::string& key, float target, float duration)
	{
		MotionMemory::Tween& tw = m_mo.tweens[key];
		const auto current = [&]() {
			const float t = tw.duration <= 0.0f ? 1.0f : Clamp01(static_cast<float>((m_now - tw.start) / tw.duration));
			return tw.from + (tw.to - tw.from) * EaseOut(t);
		};
		if (!tw.ready || m_view.reduce_motion || tw.epoch != m_mo.screen_since)
		{
			tw = {target, target, m_now, duration, m_mo.screen_since, true};
			return target;
		}
		if (std::fabs(target - tw.to) > 0.001f)
		{
			tw.from = current();
			tw.to = target;
			tw.start = m_now;
			tw.duration = duration;
		}
		return current();
	}

	// Rows and tiles cascade in when a screen opens: each starts a little after the one before.
	void Cascade(size_t layer, int index)
	{
		const float p = Progress(m_mo.screen_since, 0.035f * static_cast<float>(std::min(index, 7)), 0.32f);
		if (p < 1.0f)
			m_out.Transform(layer, 0.0f, (1.0f - Travel(p)) * S(16.0f), 1.0f, 0.0f, 0.0f, p);
	}

	float S(float value) const { return value * m_l.scale; }
	// Scales layout that holds text: grows with the accessibility text size, within reason.
	float T(float value) const { return value * std::min(m_l.text, m_l.scale * 1.6f); }

	// ---------------------------------------------------------------------------------------------------------------
	// Primitives

	float Width(const std::string& text, float px) const { return m_fonts.Measure(text.c_str(), px); }

	std::string Ellipsize(const std::string& text, float px, float max_width) const
	{
		if (max_width <= 0 || Width(text, px) <= max_width)
			return text;
		static const std::string ellipsis = "…";
		size_t len = text.size();
		while (len > 0)
		{
			while (len > 0 && (static_cast<unsigned char>(text[len]) & 0xc0) == 0x80)
				len--;
			const std::string candidate = text.substr(0, len) + ellipsis;
			if (Width(candidate, px) <= max_width)
				return candidate;
			len--;
		}
		return ellipsis;
	}

	float Text(const std::string& text, float x, float baseline, float px, Color color, float weight = 0.0f,
		int align = 0, float max_width = -1.0f)
	{
		const std::string fitted = Ellipsize(text, px, max_width);
		m_out.AddText(m_fonts, fitted, x, baseline, px, color, weight, align);
		return Width(fitted, px);
	}

	std::vector<std::string> Wrap(const std::string& text, float px, float max_width, size_t max_lines = 3) const
	{
		std::vector<std::string> lines;
		std::istringstream words(text);
		std::string word, line;
		while (words >> word && lines.size() < max_lines)
		{
			const std::string candidate = line.empty() ? word : line + " " + word;
			if (!line.empty() && Width(candidate, px) > max_width)
			{
				lines.push_back(line);
				line = word;
			}
			else
				line = candidate;
		}
		if (!line.empty() && lines.size() < max_lines)
			lines.push_back(line);
		if (!lines.empty() && words >> word)
			lines.back() = Ellipsize(lines.back() + " …", px, max_width);
		return lines;
	}

	void Grad(float x0, float y0, float x1, float y1, Color tl, Color tr, Color br, Color bl)
	{
		const DrawVertex a{x0, y0, 0, 0, tl.r, tl.g, tl.b, tl.a, 0, 0, 0, 3};
		const DrawVertex b{x1, y0, 0, 0, tr.r, tr.g, tr.b, tr.a, 0, 0, 0, 3};
		const DrawVertex c{x1, y1, 0, 0, br.r, br.g, br.b, br.a, 0, 0, 0, 3};
		const DrawVertex d{x0, y1, 0, 0, bl.r, bl.g, bl.b, bl.a, 0, 0, 0, 3};
		m_out.AddTriangle(nullptr, a, b, c);
		m_out.AddTriangle(nullptr, a, c, d);
	}

	void HGrad(float x0, float y0, float x1, float y1, Color left, Color right) { Grad(x0, y0, x1, y1, left, right, right, left); }
	void VGrad(float x0, float y0, float x1, float y1, Color top, Color bottom) { Grad(x0, y0, x1, y1, top, top, bottom, bottom); }

	void Glow(float cx, float cy, float radius, Color color)
	{
		m_out.AddImage(m_scene.GlowTexture(), cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, color);
	}

	void Dot(float cx, float cy, float r, Color color) { m_out.AddRoundedRect(cx - r, cy - r, r * 2.0f, r * 2.0f, r, color); }

	// A luminous node: the small terminal dot that ends hairlines throughout the interface.
	void Node(float cx, float cy, Color color = kIce)
	{
		Glow(cx, cy, S(16.0f), WithAlpha(color, 0.35f));
		Dot(cx, cy, std::max(1.5f, S(3.2f)), color);
	}

	void Hairline(float x0, float y, float x1, Color left, Color right, float thickness = -1.0f)
	{
		const float t = thickness > 0 ? thickness : std::max(1.0f, S(1.4f));
		HGrad(x0, y - t * 0.5f, x1, y + t * 0.5f, left, right);
	}

	void VHairline(float x, float y0, float y1, Color color)
	{
		const float t = std::max(1.0f, S(1.4f));
		const float mid = (y0 + y1) * 0.5f;
		VGrad(x - t * 0.5f, y0, x + t * 0.5f, mid, WithAlpha(color, 0.0f), color);
		VGrad(x - t * 0.5f, mid, x + t * 0.5f, y1, color, WithAlpha(color, 0.0f));
	}

	// Line-art panel: a dark translucent plate with a rounded hairline edge.
	void Panel(float x, float y, float w, float h, float alpha = 0.86f)
	{
		const float r = S(14.0f);
		const float edge = std::max(1.0f, S(1.4f));
		m_out.AddRoundedRect(x - edge, y - edge, w + edge * 2, h + edge * 2, r + edge, WithAlpha(HexColor(0x8C9BFF), 0.42f));
		m_out.AddRoundedRect(x, y, w, h, r, HexColor(0x060823, m_view.high_contrast ? 0.98f : alpha));
	}

	void FocusBand(float x, float y, float w, float h)
	{
		const bool large = m_view.accessibility.large_focus;
		const bool hc = m_view.high_contrast;
		HGrad(x, y, x + w, y + h, WithAlpha(kFocus, hc ? 0.50f : 0.30f), WithAlpha(kFocus, hc ? 0.18f : 0.03f));
		const float line = (large ? 2.6f : 1.5f) * m_l.scale + (hc ? 1.0f : 0.0f);
		HGrad(x, y, x + w * 0.92f, y + line, WithAlpha(kIce, 0.95f), WithAlpha(kIce, 0.0f));
		HGrad(x, y + h - line, x + w * 0.92f, y + h, WithAlpha(kIce, 0.55f), WithAlpha(kIce, 0.0f));
		const float bar = (large ? 7.0f : 4.0f) * m_l.scale;
		m_out.AddSolidQuad(x, y, x + bar, y + h, kIce);
		if (large && hc)
		{
			const float o = S(4.0f);
			const Color c = WithAlpha(kIce, 0.95f);
			m_out.AddLine(x - o, y - o, x + w + o, y - o, line, c);
			m_out.AddLine(x + w + o, y - o, x + w + o, y + h + o, line, c);
			m_out.AddLine(x + w + o, y + h + o, x - o, y + h + o, line, c);
			m_out.AddLine(x - o, y + h + o, x - o, y - o, line, c);
		}
	}

	// Focus for artwork and tiles: a crisp ice frame.
	void TileFocus(float x, float y, float w, float h)
	{
		const float line = (m_view.accessibility.large_focus ? 4.0f : 2.4f) * m_l.scale;
		const float o = S(5.0f);
		m_out.AddLine(x - o, y - o, x + w + o, y - o, line, kIce);
		m_out.AddLine(x + w + o, y - o, x + w + o, y + h + o, line, kIce);
		m_out.AddLine(x + w + o, y + h + o, x - o, y + h + o, line, kIce);
		m_out.AddLine(x - o, y + h + o, x - o, y - o, line, kIce);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Background: void, nebula and the glass cube field.

	Look LookFor(Screen screen) const
	{
		switch (screen)
		{
			case Screen::Boot: return {true, 1.0f, 0.50f, 0.47f, 1.25f, 0.0f};
			case Screen::Home: return {true, 1.0f, 0.71f, 0.50f, 1.0f, 0.55f};
			case Screen::FirstRun: return {true, 0.85f, 0.76f, 0.50f, 0.95f, 0.60f};
			case Screen::PauseOverlay: return {false, 0.0f, 0.5f, 0.5f, 1.0f, 0.0f};
			case Screen::GameDetail:
			case Screen::Accessibility: return {true, 0.30f, 0.90f, 0.62f, 0.62f, 0.45f};
			default: return {true, 0.24f, 0.90f, 0.66f, 0.58f, 0.50f};
		}
	}

	void Background()
	{
		const float w = static_cast<float>(m_l.width);
		const float h = static_cast<float>(m_l.height);
		const Look look = LookFor(m_view.screen);
		const float hc_dim = m_view.high_contrast ? 0.45f : 1.0f;
		Grad(0, 0, w, h, kVoidTop, HexColor(0x0A0D36), kVoid, kVoid);
		if (!look.field)
			return;
		// Each screen frames the field its own way. The framing cuts with the screen instead of moving a camera:
		// a background that sweeps and zooms on every press is tiring to watch.
		const float cx = w * look.cx;
		const float cy = h * look.cy;
		// Nebula: broad light pools and three still haze layers.
		Glow(cx, cy, h * 0.95f, WithAlpha(kNebula, 0.50f * hc_dim));
		Glow(cx - w * 0.16f, cy + h * 0.22f, h * 0.60f, WithAlpha(kViolet, 0.26f * hc_dim));
		Glow(w * 0.12f, h * 0.05f, h * 0.55f, WithAlpha(kDeep, 0.65f * hc_dim));
		const Texture2D* fog = m_scene.FogTexture();
		const float fw = w * 1.35f, fh = h * 1.35f;
		m_out.AddImageUV(fog, -w * 0.17f, -h * 0.17f, fw, fh, 0.0f, 0.0f, 1.0f, 1.0f,
			WithAlpha(HexColor(0x4656E6), 0.30f * hc_dim));
		m_out.AddImageUV(fog, -w * 0.20f, -h * 0.10f, fw, fh, 1.0f, 1.0f, 0.0f,
			0.0f, WithAlpha(HexColor(0x6A48D6), 0.16f * hc_dim));
		CubeField(cx, cy, h * 0.95f * look.zoom, look.intensity * hc_dim);
		m_out.AddImageUV(fog, -w * 0.12f, -h * 0.22f, fw, fh, 0.0f, 1.0f, 1.0f,
			0.0f, WithAlpha(HexColor(0x3A48D0), 0.14f * hc_dim));
		// Readability: shade behind left-aligned content, the footer band and the corners.
		if (look.left_shade > 0.0f)
			HGrad(0, 0, w * 0.62f, h, WithAlpha(kVoid, look.left_shade), WithAlpha(kVoid, 0.0f));
		VGrad(0, h * 0.80f, w, h, WithAlpha(kVoid, 0.0f), WithAlpha(kVoid, 0.70f));
		VGrad(0, 0, w, h * 0.16f, WithAlpha(kVoid, 0.45f), WithAlpha(kVoid, 0.0f));
	}

	struct ProjectedCube
	{
		const CubeSpec* spec;
		V3 corners[8];
		V3 center;
	};

	bool Project(V3 p, float cx, float cy, float f, float& sx, float& sy) const
	{
		if (p.z < 0.6f)
			return false;
		sx = cx + p.x / p.z * f;
		sy = cy - p.y / p.z * f;
		return true;
	}

	void CubeField(float cx, float cy, float f, float intensity)
	{
		if (intensity <= 0.01f)
			return;
		// The field holds one fixed view. Each cube floats up and down a little around its own place, at its own pace.
		constexpr float kYaw = 0.45f + 41.0f * 0.035f;
		constexpr float kPitch = -0.18f;
		constexpr float kDepth = 5.2f;
		constexpr float kBob = 0.035f; // world units, about a sixth of an average cube
		std::vector<ProjectedCube> cubes;
		cubes.reserve(Cubes().size());
		for (const CubeSpec& spec : Cubes())
		{
			ProjectedCube pc;
			pc.spec = &spec;
			const V3 world = RotateXYZ(spec.pos, kPitch, kYaw, 0.0f);
			pc.center = {world.x, world.y + kBob * std::sin(spec.bob_phase + m_t * spec.bob_speed), world.z + kDepth};
			for (int i = 0; i < 8; i++)
			{
				V3 local = {(i & 1) ? spec.size : -spec.size, (i & 2) ? spec.size : -spec.size, (i & 4) ? spec.size : -spec.size};
				local = RotateXYZ(local, spec.tilt.x, spec.tilt.y, spec.tilt.z);
				local = RotateXYZ(local, kPitch, kYaw, 0.0f);
				pc.corners[i] = {pc.center.x + local.x, pc.center.y + local.y, pc.center.z + local.z};
			}
			cubes.push_back(pc);
		}
		std::sort(cubes.begin(), cubes.end(), [](const ProjectedCube& a, const ProjectedCube& b) { return a.center.z > b.center.z; });

		static const int kFaces[6][4] = {{1, 3, 7, 5}, {0, 4, 6, 2}, {2, 6, 7, 3}, {0, 1, 5, 4}, {4, 5, 7, 6}, {0, 2, 3, 1}};
		const V3 light = {-0.38f, 0.78f, -0.50f};
		for (const ProjectedCube& cube : cubes)
		{
			const float fade = Clamp01(1.25f - (cube.center.z - 3.0f) / 5.0f) * intensity;
			float sx[8], sy[8];
			bool ok = true;
			for (int i = 0; i < 8; i++)
				ok = ok && Project(cube.corners[i], cx, cy, f, sx[i], sy[i]);
			if (!ok)
				continue;
			for (int fi = 0; fi < 6; fi++)
			{
				const V3& a = cube.corners[kFaces[fi][0]];
				const V3& b = cube.corners[kFaces[fi][1]];
				const V3& c = cube.corners[kFaces[fi][2]];
				const V3 e1 = {b.x - a.x, b.y - a.y, b.z - a.z};
				const V3 e2 = {c.x - a.x, c.y - a.y, c.z - a.z};
				V3 n = {e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
				const float len = std::sqrt(Dot3(n, n));
				if (len <= 1e-6f)
					continue;
				n = {n.x / len, n.y / len, n.z / len};
				// Outward normal: the face centre lies away from the cube centre along it.
				V3 fc = {(a.x + c.x) * 0.5f - cube.center.x, (a.y + c.y) * 0.5f - cube.center.y, (a.z + c.z) * 0.5f - cube.center.z};
				if (Dot3(n, fc) < 0)
					n = {-n.x, -n.y, -n.z};
				const V3 to_face = {(a.x + c.x) * 0.5f, (a.y + c.y) * 0.5f, (a.z + c.z) * 0.5f};
				if (Dot3(n, to_face) >= 0)
					continue; // back face
				const float lit = Clamp01((Dot3(n, light) + 0.35f) / 1.35f);
				std::vector<std::pair<float, float>> poly;
				for (int k = 0; k < 4; k++)
					poly.push_back({sx[kFaces[fi][k]], sy[kFaces[fi][k]]});
				if (cube.spec->dark)
				{
					m_out.AddPoly(poly, WithAlpha(Mix(HexColor(0x03040F), HexColor(0x1C2163), lit * 0.75f), 0.94f * fade));
				}
				else
				{
					m_out.AddPoly(poly, WithAlpha(Mix(HexColor(0x2A32A6), kGlass, 0.20f + lit * 0.80f), (0.08f + lit * 0.24f) * fade));
				}
				const float edge_w = std::max(1.0f, m_l.scale * 1.5f * (4.5f / cube.center.z));
				const Color edge = cube.spec->dark ? WithAlpha(HexColor(0x6A74D8), 0.22f * fade) :
					WithAlpha(kGlassEdge, (0.16f + lit * 0.26f) * fade);
				for (int k = 0; k < 4; k++)
				{
					const int i0 = kFaces[fi][k], i1 = kFaces[fi][(k + 1) % 4];
					m_out.AddLine(sx[i0], sy[i0], sx[i1], sy[i1], edge_w, edge);
				}
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Shared pieces

	// The memory card glyph on a slot: a card with a clipped corner and a label window.
	void MemoryCardGlyph(float cx, float cy, float s, Color color)
	{
		m_out.AddPoly({{cx - s * 0.28f, cy - s * 0.36f}, {cx + s * 0.16f, cy - s * 0.36f}, {cx + s * 0.28f, cy - s * 0.24f},
			{cx + s * 0.28f, cy + s * 0.36f}, {cx - s * 0.28f, cy + s * 0.36f}}, color);
		m_out.AddSolidQuad(cx - s * 0.16f, cy - s * 0.22f, cx + s * 0.16f, cy - s * 0.04f, kVoid);
	}

	void Quad(const std::pair<float, float> (&p)[4], const Color (&c)[4])
	{
		DrawVertex v[4];
		for (int i = 0; i < 4; i++)
			v[i] = DrawVertex{p[i].first, p[i].second, 0, 0, c[i].r, c[i].g, c[i].b, c[i].a, 0, 0, 0, 3};
		m_out.AddTriangle(nullptr, v[0], v[1], v[2]);
		m_out.AddTriangle(nullptr, v[0], v[2], v[3]);
	}

	// Caliza mark: a limestone block cut into three stepped strata. The geometry and colours mirror
	// ps4/tools/make_brand_assets.py, which also produces the console icon and the companion-panel SVGs.
	// `assemble_since` >= 0 plays the boot assembly: the strata settle onto each other from the bottom up, then the
	// wordmark letters follow. Reduce motion keeps it as a fade.
	void Logo(float x, float y, float h, bool wordmark = true, double assemble_since = -1.0)
	{
		const bool assemble = assemble_since >= 0.0;
		struct Stratum
		{
			float t0, t1, shift;
		};
		static constexpr Stratum kStrata[3] = {{0.00f, 0.27f, -0.10f}, {0.33f, 0.64f, 0.0f}, {0.70f, 1.00f, 0.08f}};
		constexpr float c30 = 0.8660254f;
		const float k = h / 2.09f;
		const float ox = x + 0.95f * k;
		const float oy = y + 1.05f * k;
		enum Face
		{
			kTop,
			kLeft,
			kRight
		};
		// Linear gradient per face kind, from a light point to a dark point in unit space.
		const auto shade = [&](Face face, float ux, float uy) {
			float t = 0.0f;
			if (face == kTop)
				return Mix(HexColor(0xF4FBFF), HexColor(0x9EDFFF), Clamp01(uy + 1.0f));
			if (face == kLeft)
			{
				t = ((ux + c30) * c30 + (uy + 0.5f) * 1.5f) / (c30 * c30 + 2.25f);
				return Mix(HexColor(0xA3CBFA), HexColor(0x5470D8), Clamp01(t));
			}
			t = ux * c30 + uy * 0.5f;
			return Mix(HexColor(0x5163CF), HexColor(0x27318D), Clamp01(t));
		};
		const auto face = [&](Face kind, const std::pair<float, float> (&u)[4]) {
			std::pair<float, float> p[4];
			Color c[4];
			for (int i = 0; i < 4; i++)
			{
				p[i] = {ox + u[i].first * k, oy + u[i].second * k};
				c[i] = shade(kind, u[i].first, u[i].second);
			}
			Quad(p, c);
		};
		const size_t glow = m_out.VertexCount();
		Glow(ox, oy, h * 0.95f, WithAlpha(kNebula, 0.28f));
		if (assemble)
			m_out.Transform(glow, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, Progress(assemble_since, 0.0f, 0.6f));
		for (int i = 2; i >= 0; i--)
		{
			const size_t layer = m_out.VertexCount();
			const Stratum& s = kStrata[i];
			const float dx = s.shift * c30, dy = s.shift * 0.5f;
			face(kTop, {{dx, -1.0f + s.t0 + dy}, {c30 + dx, -0.5f + s.t0 + dy}, {dx, s.t0 + dy}, {-c30 + dx, -0.5f + s.t0 + dy}});
			face(kLeft, {{-c30 + dx, -0.5f + s.t0 + dy}, {dx, s.t0 + dy}, {dx, s.t1 + dy}, {-c30 + dx, -0.5f + s.t1 + dy}});
			face(kRight, {{dx, s.t0 + dy}, {c30 + dx, -0.5f + s.t0 + dy}, {c30 + dx, -0.5f + s.t1 + dy}, {dx, s.t1 + dy}});
			const float lw = std::max(1.0f, h * 0.022f);
			m_out.AddSolidQuad(ox + dx * k - lw * 0.5f, oy + (s.t0 + dy) * k, ox + dx * k + lw * 0.5f, oy + (s.t1 + dy) * k,
				WithAlpha(HexColor(0xE4F7FF), 0.7f));
			if (assemble)
			{
				const float p = Progress(assemble_since, 0.06f + 0.11f * static_cast<float>(2 - i), 0.46f);
				m_out.Transform(layer, 0.0f, -(1.0f - Travel(p)) * h * 0.55f, 1.0f, 0.0f, 0.0f, p);
			}
		}
		if (!wordmark)
			return;
		float pen = x + h * 1.10f;
		const float px = h * 0.46f;
		const float baseline = y + h * 0.66f;
		int letter = 0;
		for (char c : std::string("CALIZA"))
		{
			const std::string one(1, c);
			const size_t layer = m_out.VertexCount();
			m_out.AddText(m_fonts, one, pen, baseline, px, kText, 0.30f);
			if (assemble)
			{
				const float p = Progress(assemble_since, 0.34f + 0.045f * static_cast<float>(letter), 0.34f);
				m_out.Transform(layer, 0.0f, (1.0f - Travel(p)) * px * 0.35f, 1.0f, 0.0f, 0.0f, p);
			}
			pen += m_fonts.Measure(one.c_str(), px) + px * 0.30f;
			letter++;
		}
	}

	void ImageCover(const Texture2D* texture, float x, float y, float w, float h, Color tint = {}, bool reflect = false)
	{
		if (!texture || texture->width <= 0 || texture->height <= 0)
			return;
		const float source_aspect = static_cast<float>(texture->width) / static_cast<float>(texture->height);
		const float target_aspect = w / std::max(1.0f, h);
		float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
		if (source_aspect > target_aspect)
		{
			const float visible = target_aspect / source_aspect;
			u0 = (1.0f - visible) * 0.5f;
			u1 = 1.0f - u0;
		}
		else
		{
			const float visible = source_aspect / target_aspect;
			v0 = (1.0f - visible) * 0.5f;
			v1 = 1.0f - v0;
		}
		m_out.AddImageUV(texture, x, y, w, h, u0, v0, u1, v1, tint);
		if (reflect && !m_view.high_contrast)
		{
			// A short mirrored fade below the artwork, as if it stood on dark glass.
			const float rh = h * 0.28f;
			const float vr = v1 - (v1 - v0) * 0.28f;
			const float gap = S(6.0f);
			const DrawVertex a{x, y + h + gap, u0, v1, tint.r, tint.g, tint.b, 0.20f, 0, 0, 0, 2};
			const DrawVertex b{x + w, y + h + gap, u1, v1, tint.r, tint.g, tint.b, 0.20f, 0, 0, 0, 2};
			const DrawVertex c{x + w, y + h + gap + rh, u1, vr, tint.r, tint.g, tint.b, 0.0f, 0, 0, 0, 2};
			const DrawVertex d{x, y + h + gap + rh, u0, vr, tint.r, tint.g, tint.b, 0.0f, 0, 0, 0, 2};
			m_out.AddTriangle(texture, a, b, c);
			m_out.AddTriangle(texture, a, c, d);
		}
	}

	// A list row: plain text over the scene. Focus is a band of light; selection (the active rail entry) is a
	// short ice bar. Details show always, only when focused, or never (shown in a help area instead).
	void Row(const UiEntry& entry, float x, float y, float w, float h,
		DetailMode detail_mode = kDetailAlways, float label_px = 0.0f)
	{
		const float px = label_px > 0.0f ? label_px : m_l.body;
		if (m_view.high_contrast && !m_list_layer)
			m_out.AddRoundedRect(x, y, w, h, S(6.0f), HexColor(0x01020A, 0.88f));
		if (entry.focused)
		{
			if (!m_list_layer)
				FocusBand(x, y, w, h);
		}
		else if (entry.selected)
		{
			m_out.AddSolidQuad(x, y + h * 0.28f, x + S(3.0f), y + h * 0.72f, WithAlpha(kIce, 0.85f));
			HGrad(x, y, x + w * 0.7f, y + h, WithAlpha(kFocus, 0.10f), WithAlpha(kFocus, 0.0f));
		}
		const float tx = x + S(28.0f);
		Color label = entry.disabled ? kTextFaint : entry.focused ? kText : entry.selected ? kText : kTextSoft;
		if (entry.destructive && entry.focused)
			label = kAmber;
		const float value_px = std::min(px, m_l.body);
		const std::string value = DisplayValue(entry.value);
		// The value's own width is kept apart from the gutter: (width + gutter) - gutter can round below the
		// measured width and ellipsize a value that fits.
		const float value_natural = value.empty() ? 0.0f : Width(value, value_px);
		const float value_text = std::min(w * 0.45f, value_natural);
		const float value_width = value.empty() ? 0.0f : value_text + S(28.0f);
		const float label_width = w - (tx - x) - value_width - S(24.0f);
		const bool show_detail = !entry.detail.empty() &&
			(detail_mode == kDetailAlways || (detail_mode == kDetailFocused && entry.focused));
		const float label_base = show_detail ? y + h * 0.5f - S(2.0f) : y + h * 0.5f + px * 0.36f;
		if (entry.focused && !entry.disabled)
			Text(entry.label, tx, label_base, px, label, 0.30f, 0, label_width);
		else
			Text(entry.label, tx, label_base, px, label, entry.selected ? 0.2f : 0.0f, 0, label_width);
		if (show_detail)
			Text(entry.detail, tx, label_base + m_l.small * 1.22f, m_l.small,
				entry.disabled ? kTextFaint : entry.focused ? kTextDim : WithAlpha(kTextDim, 0.80f), 0, 0, label_width);
		if (!value.empty())
		{
			float vx = x + w - S(26.0f);
			const float vbase = y + h * 0.5f + value_px * 0.36f;
			Color value_color = entry.focused ? kIce : entry.disabled ? kTextFaint : kTextDim;
			if (entry.focused)
			{
				// A changed value slides in from the side it was moved towards and settles from white to ice.
				const float p = ValueChange(entry);
				vx += (1.0f - Travel(p)) * S(14.0f) * static_cast<float>(m_mo.value_direction);
				value_color = Mix(kText, kIce, p);
			}
			Text(value, vx, vbase, value_px, value_color, entry.focused ? 0.2f : 0.0f, 2,
				value_text < value_natural ? value_text : -1.0f);
			if (entry.modified)
				Dot(vx - value_text - S(16.0f), vbase - value_px * 0.34f, S(4.0f), kIce);
		}
		if (entry.progress >= 0.0f)
		{
			const float bw = std::min(w * 0.24f, S(300.0f));
			const float bx = x + w - bw - S(26.0f);
			const float by = y + h - S(12.0f);
			Hairline(bx, by, bx + bw, WithAlpha(kGlass, 0.25f), WithAlpha(kGlass, 0.25f), S(3.0f));
			Hairline(bx, by, bx + bw * Clamp01(entry.progress), kIce, kIce, S(3.0f));
		}
	}

	// Progress of the focused setting's last value change (1 when settled). Tracks the option index so the
	// value can slide in from the direction the player pressed, including wrapping round the ends.
	float ValueChange(const UiEntry& entry)
	{
		if (entry.option_index < 0)
			return 1.0f;
		if (m_mo.value_id != entry.id)
		{
			m_mo.value_id = entry.id;
			m_mo.value_index = entry.option_index;
			m_mo.value_since = -10.0;
		}
		else if (m_mo.value_index != entry.option_index)
		{
			const int count = static_cast<int>(entry.options.size());
			int step = entry.option_index > m_mo.value_index ? 1 : -1;
			if (count > 2 && std::abs(entry.option_index - m_mo.value_index) > count / 2)
				step = -step;
			m_mo.value_direction = step;
			m_mo.value_index = entry.option_index;
			m_mo.value_since = m_now;
		}
		return Progress(m_mo.value_since, 0.0f, 0.20f);
	}

	int FirstVisible(int count, int focus, int visible) const
	{
		return std::max(0, std::min(focus - visible + 1 + (focus + 1 < count ? 1 : 0), std::max(0, count - visible)));
	}

	// A vertical list that scrolls to keep focus (or selection) visible and never crosses `bottom`.
	void List(const std::vector<UiEntry>& entries, float x, float y, float w, float row_h, float gap, float bottom,
		DetailMode detail_mode, float label_px = 0.0f, bool draw_band = true)
	{
		if (entries.empty())
			return;
		const int count = static_cast<int>(entries.size());
		const int visible = std::max(1, static_cast<int>((bottom - y + gap) / (row_h + gap)));
		int focus = -1;
		for (int i = 0; i < count; i++)
			if (entries[static_cast<size_t>(i)].focused)
				focus = i;
		if (focus < 0)
			for (int i = 0; i < count; i++)
				if (entries[static_cast<size_t>(i)].selected)
					focus = i;
		const int first = visible >= count ? 0 : FirstVisible(count, std::max(0, focus), visible);
		const int shown = std::min(visible, count - first);
		if (m_view.high_contrast)
			for (int i = 0; i < shown; i++)
				m_out.AddRoundedRect(x, y + i * (row_h + gap), w, row_h, S(6.0f), HexColor(0x01020A, 0.88f));
		// One focus band per list that glides between rows, so the eye follows the move.
		if (draw_band && focus >= 0 && entries[static_cast<size_t>(focus)].focused)
		{
			const std::string key = "list:" + std::to_string(static_cast<int>(x)) + ":" + std::to_string(static_cast<int>(y));
			FocusBand(x, Tween(key, y + (focus - first) * (row_h + gap), 0.16f), w, row_h);
		}
		m_list_layer = true;
		for (int i = 0; i < shown; i++)
		{
			const size_t layer = m_out.VertexCount();
			Row(entries[static_cast<size_t>(first + i)], x, y + i * (row_h + gap), w, row_h, detail_mode, label_px);
			Cascade(layer, i);
		}
		m_list_layer = false;
		// Scroll cues: a fading node above or below when more rows exist.
		if (first > 0)
			Node(x + S(14.0f), y - gap * 0.5f - S(8.0f), WithAlpha(kGlassEdge, 0.8f));
		if (first + visible < count)
			Node(x + S(14.0f), y + visible * (row_h + gap) - gap * 0.5f + S(4.0f), WithAlpha(kGlassEdge, 0.8f));
	}

	const UiEntry* Focused(const std::vector<UiEntry>& entries) const
	{
		for (const UiEntry& entry : entries)
			if (entry.focused)
				return &entry;
		return nullptr;
	}

	// The description of the focused row, set apart under a list instead of truncated inside it.
	void HelpText(const std::string& text, float x, float y, float w, size_t lines = 3)
	{
		if (text.empty())
			return;
		Hairline(x, y, x + w * 0.6f, WithAlpha(kGlassEdge, 0.35f), WithAlpha(kGlassEdge, 0.0f));
		float base = y + m_l.small * 1.55f;
		for (const std::string& line : Wrap(text, m_l.small, w, lines))
		{
			Text(line, x, base, m_l.small, kTextDim);
			base += m_l.small * 1.38f;
		}
	}

	static std::string DisplayValue(const std::string& v)
	{
		if (v == "true")
			return "On";
		if (v == "false")
			return "Off";
		return v;
	}

	// Where a list actually ends, so help text sits under the last row instead of at the screen bottom.
	static float ListEnd(const std::vector<UiEntry>& items, float top, float row_h, float gap, float bottom)
	{
		return std::min(bottom, top + static_cast<float>(items.size()) * (row_h + gap));
	}

	float HelpHeight(size_t lines = 3) const { return m_l.small * (0.7f + 1.38f * lines) + S(18.0f); }

	void Chrome()
	{
		Logo(m_l.safe_x, m_l.safe_y - S(10.0f), S(34.0f));
		const float title_base = m_l.safe_y + S(92.0f);
		const float tw = Text(m_view.title, m_l.safe_x, title_base, m_l.title, kText, -0.25f, 0, m_l.right - m_l.safe_x);
		if (!m_view.subtitle.empty() && m_view.screen != Screen::Diagnostics)
		{
			const float sx = m_l.safe_x + tw + S(28.0f);
			const float room = m_l.right - sx - S(420.0f);
			if (room > S(220.0f))
				Text(m_view.subtitle, sx, title_base, m_l.small, kTextDim, 0.0f, 0, room);
			else
				Text(m_view.subtitle, m_l.safe_x, title_base + m_l.small * 1.5f, m_l.small, kTextDim, 0.0f, 0,
					m_l.right - m_l.safe_x);
		}
		const float line_y = HeaderLine();
		Hairline(m_l.safe_x, line_y, m_l.right, WithAlpha(kGlassEdge, 0.55f), WithAlpha(kGlassEdge, 0.0f));
	}

	float HeaderLine() const
	{
		const bool wrapped_subtitle = !m_view.subtitle.empty() && m_view.screen != Screen::Diagnostics &&
			(m_l.right - (m_l.safe_x + Width(m_view.title, m_l.title) + S(28.0f)) - S(420.0f)) <= S(220.0f);
		return m_l.safe_y + S(118.0f) + (wrapped_subtitle ? m_l.small * 1.4f : 0.0f);
	}

	float ContentTop() const { return HeaderLine() + S(46.0f); }
	float ContentBottom() const { return m_l.bottom - S(92.0f); }

	const char* Glyph(PromptGlyph glyph, Color& color) const
	{
		switch (glyph)
		{
			case PromptGlyph::Circle: color = kCircleRed; return fe::icon::Circle;
			case PromptGlyph::Square: color = kSquarePink; return fe::icon::Square;
			case PromptGlyph::Triangle: color = kTriangleGreen; return fe::icon::Triangle;
			case PromptGlyph::Options: color = kTextDim; return fe::icon::Options;
			case PromptGlyph::Dpad: color = kTextDim; return fe::icon::DpadUpDown;
			case PromptGlyph::DpadLeftRight: color = kTextDim; return fe::icon::DpadLeftRight;
			case PromptGlyph::Cross:
			default: color = kCrossBlue; return fe::icon::Cross;
		}
	}

	// Prompts on the right and status on the left, under one hairline.
	void Footer()
	{
		const float line_y = m_l.bottom - S(64.0f);
		Hairline(m_l.safe_x, line_y, m_l.right, WithAlpha(kGlassEdge, 0.0f), WithAlpha(kGlassEdge, 0.45f));
		const float baseline = m_l.bottom - S(14.0f);
		const float px = m_l.small * 1.08f;
		float total = 0;
		for (const UiPrompt& prompt : m_view.prompts)
		{
			Color c;
			total += Width(Glyph(prompt.glyph, c), px * 1.1f) + S(10.0f) + Width(prompt.label, px) + S(40.0f);
		}
		float x = m_l.right - total + S(40.0f);
		for (const UiPrompt& prompt : m_view.prompts)
		{
			Color c;
			const char* glyph = Glyph(prompt.glyph, c);
			x += Text(glyph, x, baseline + px * 0.05f, px * 1.1f, c, 0.2f) + S(10.0f);
			x += Text(prompt.label, x, baseline, px, kText, 0.05f) + S(40.0f);
		}
		if (!m_view.status.empty() && !m_status_shown)
		{
			const float room = m_l.right - total - m_l.safe_x - S(40.0f);
			const bool problem = m_view.screen == Screen::FirstRun || m_view.scan.state == ScanState::Failed;
			if (m_view.scan.state == ScanState::Scanning)
				Dot(m_l.safe_x + S(6.0f), baseline - m_l.small * 0.35f, S(4.0f) * (m_view.reduce_motion ? 1.0f : 0.7f + 0.3f * std::sin(m_ui_t * 5.0f)), kIce);
			Text(m_view.status, m_l.safe_x + (m_view.scan.state == ScanState::Scanning ? S(22.0f) : 0.0f), baseline,
				m_l.small, problem ? kAmber : kTextDim, 0.0f, 0, room);
		}
	}

	// Transient confirmation: a small line-art capsule in the top-right corner, clear of content and prompts.
	void Toast()
	{
		// Arrives from above and leaves the same way. A new message replaces the old one in place.
		if (!m_view.toast.empty())
		{
			if (m_view.toast != m_mo.toast || m_mo.toast_leaving)
			{
				if (m_view.toast != m_mo.toast || m_mo.toast.empty())
					m_mo.toast_since = m_now;
				m_mo.toast = m_view.toast;
				m_mo.toast_leaving = false;
			}
		}
		else if (!m_mo.toast.empty() && !m_mo.toast_leaving)
		{
			m_mo.toast_leaving = true;
			m_mo.toast_gone = m_now;
		}
		if (m_mo.toast.empty())
			return;
		float alpha = 1.0f, dy = 0.0f;
		if (m_mo.toast_leaving)
		{
			const float q = Progress(m_mo.toast_gone, 0.0f, 0.18f);
			if (q >= 1.0f)
			{
				m_mo.toast.clear();
				m_mo.toast_leaving = false;
				return;
			}
			alpha = 1.0f - q;
			dy = -Away(q) * S(14.0f);
		}
		else
		{
			const float p = Progress(m_mo.toast_since, 0.0f, 0.24f);
			alpha = p;
			dy = -(1.0f - Travel(p)) * S(18.0f);
		}
		const size_t layer = m_out.VertexCount();
		const std::string text = Capitalized(m_mo.toast);
		const float px = m_l.small;
		const float w = std::min(Width(text, px) + S(48.0f), (m_l.right - m_l.safe_x) * 0.5f);
		const float h = px * 2.0f;
		const float x = m_l.right - w;
		const float y = m_l.safe_y - S(18.0f);
		m_out.AddRoundedRect(x - S(1.4f), y - S(1.4f), w + S(2.8f), h + S(2.8f), h * 0.5f + S(1.4f), WithAlpha(kIce, 0.55f));
		m_out.AddRoundedRect(x, y, w, h, h * 0.5f, HexColor(0x0A0E3A, 0.96f));
		Text(text, x + S(24.0f), y + h * 0.5f + px * 0.36f, px, kText, 0.1f, 0, w - S(48.0f));
		m_out.Transform(layer, 0.0f, dy, 1.0f, 0.0f, 0.0f, alpha);
	}

	// A centred modal: the blackout fades while the panel grows from 96% and fades in. Modals scale from their centre.
	void ModalEnter(size_t blackout, size_t panel, double since, float cx, float cy)
	{
		const float p = Progress(since, 0.0f, 0.22f);
		if (p >= 1.0f)
			return;
		m_out.Transform(blackout, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, p);
		m_out.Transform(panel, 0.0f, 0.0f, 0.96f + 0.04f * Travel(p), cx, cy, 1.0f);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Screens

	void PaintBoot()
	{
		const float cx = m_l.width * 0.5f;
		const float cy = m_l.height * 0.47f;
		const float lh = S(96.0f);
		// Measure the wordmark to centre the whole lockup.
		float wordmark = 0.0f;
		for (char c : std::string("CALIZA"))
		{
			const std::string one(1, c);
			wordmark += m_fonts.Measure(one.c_str(), lh * 0.46f) + lh * 0.46f * 0.30f;
		}
		const float total = lh * 1.10f + wordmark - lh * 0.46f * 0.30f;
		Glow(cx, cy + S(30.0f), S(520.0f), WithAlpha(HexColor(0x02030C), 0.80f));
		Logo(cx - total * 0.5f, cy - lh * 0.5f, lh, true, m_mo.screen_since);
		const std::string status = m_view.status.empty() ? "Starting" : Capitalized(m_view.status);
		Text(status, cx, cy + S(120.0f), m_l.small, kTextDim, 0.0f, 1);
		const float bw = S(220.0f);
		Hairline(cx - bw * 0.5f, cy + S(146.0f), cx + bw * 0.5f, WithAlpha(kGlassEdge, 0.18f), WithAlpha(kGlassEdge, 0.18f));
		if (!m_view.reduce_motion)
		{
			const float p = std::fmod(m_ui_t * 0.6f, 1.0f);
			const float head = cx - bw * 0.5f + bw * p;
			Hairline(std::max(cx - bw * 0.5f, head - S(70.0f)), cy + S(146.0f), head, WithAlpha(kIce, 0.0f), kIce, S(2.0f));
			Node(head, cy + S(146.0f));
		}
	}

	void PaintHome()
	{
		Logo(m_l.safe_x, m_l.safe_y + S(4.0f), S(48.0f));
		const float x = m_l.safe_x - S(10.0f);
		const float w = std::min(S(700.0f), (m_l.right - m_l.safe_x) * 0.46f);
		const float px = m_l.body * 1.28f;
		const float row_h = T(78.0f);
		const float gap = S(4.0f);
		const float card_h = T(132.0f);
		const bool has_continue = !m_view.items.empty() && m_view.items[0].id == "continue" && !m_view.hero_cover_key.empty();
		std::vector<UiEntry> rows(m_view.items.begin() + (has_continue ? 1 : 0), m_view.items.end());
		const float top = m_l.safe_y + S(150.0f);
		const float bottom = ContentBottom();
		float list_h = rows.size() * (row_h + gap) + (has_continue ? card_h + S(28.0f) : 0.0f);
		float y = std::max(top, top + (bottom - top - list_h) * 0.45f);
		// One focus band for the card and the menu, gliding in position and height between them.
		{
			float band_y = -1.0f, band_h = row_h;
			if (has_continue && m_view.items[0].focused)
			{
				band_y = y;
				band_h = card_h;
			}
			const float rows_top = y + (has_continue ? card_h + S(28.0f) : 0.0f);
			for (size_t i = 0; i < rows.size(); i++)
				if (rows[i].focused)
					band_y = rows_top + static_cast<float>(i) * (row_h + gap);
			if (band_y >= 0.0f)
				FocusBand(x, Tween("home:y", band_y, 0.18f), w, Tween("home:h", band_h, 0.18f));
		}
		if (has_continue)
		{
			const UiEntry& entry = m_view.items[0];
			const Texture2D* cover = m_scene.CoverFor(m_view.hero_cover_key, m_view.hero_title);
			const float ch = card_h - S(24.0f);
			const float cw = ch * 0.75f;
			const float cx = x + S(28.0f);
			ImageCover(cover, cx, y + S(12.0f), cw, ch);
			const float tx = cx + cw + S(30.0f);
			const float room = x + w - tx - S(20.0f);
			Text("Continue", tx, y + card_h * 0.36f, m_l.small, kTextDim, 0.0f, 0, room);
			const std::string title = m_view.hero_title.empty() ? entry.label : m_view.hero_title;
			if (entry.focused)
				Text(title, tx, y + card_h * 0.36f + px * 1.15f, px, kText, 0.3f, 0, room);
			else
				Text(title, tx, y + card_h * 0.36f + px * 1.15f, px, kTextSoft, 0.0f, 0, room);
			y += card_h + S(28.0f);
		}
		List(rows, x, y, w, row_h, gap, bottom, kDetailFocused, px, false);
	}

	void PaintFirstRun()
	{
		const float w = std::min(S(760.0f), (m_l.right - m_l.safe_x) * 0.52f);
		float y = ContentTop();
		// Steps: a short checklist with a ring that fills once the step is done.
		for (const UiEntry& step : m_view.rail)
		{
			const float h = T(82.0f);
			const float cy = y + h * 0.5f;
			const float r = S(15.0f);
			const float cx = m_l.safe_x + r;
			if (step.selected)
			{
				Dot(cx, cy, r, kIce);
				m_out.AddLine(cx - r * 0.45f, cy, cx - r * 0.1f, cy + r * 0.38f, S(3.0f), kVoid);
				m_out.AddLine(cx - r * 0.1f, cy + r * 0.38f, cx + r * 0.5f, cy - r * 0.35f, S(3.0f), kVoid);
			}
			else
			{
				Dot(cx, cy, r, step.icon == EntryIcon::Warning ? kAmber : kTextDim);
				Dot(cx, cy, r - S(2.5f), kVoid);
			}
			std::string label = step.label;
			while (!label.empty() && (std::isdigit(static_cast<unsigned char>(label[0])) || label[0] == ' '))
				label.erase(0, 1);
			Text(label, cx + r + S(24.0f), cy - S(3.0f), m_l.body, kText, 0.2f, 0, w);
			Text(step.detail, cx + r + S(24.0f), cy - S(3.0f) + m_l.small * 1.25f, m_l.small,
				step.selected ? kTextDim : step.icon == EntryIcon::Warning ? kAmber : kTextDim, 0, 0, w - r * 2 - S(24.0f));
			y += h + S(10.0f);
		}
		y += S(30.0f);
		Hairline(m_l.safe_x, y - S(16.0f), m_l.safe_x + w, WithAlpha(kGlassEdge, 0.35f), WithAlpha(kGlassEdge, 0.0f));
		List(m_view.items, m_l.safe_x - S(10.0f), y, w, T(84.0f), S(6.0f), ContentBottom(), kDetailAlways);
	}

	void PaintLibrary()
	{
		const float rail_w = S(300.0f);
		List(m_view.rail, m_l.safe_x - S(10.0f), ContentTop(), rail_w, T(60.0f), S(4.0f), ContentBottom(), kDetailNever);
		const float gx = m_l.safe_x + rail_w + S(64.0f);
		VHairline(gx - S(32.0f), ContentTop(), ContentBottom(), WithAlpha(kGlassEdge, 0.35f));
		const float gw = m_l.right - gx;
		if (!m_view.tiles.empty())
		{
			constexpr int cols = UiModel::kLibraryColumns;
			const float gap = S(44.0f);
			const float tile_w = std::min((gw - gap * (cols - 1)) / cols, S(208.0f));
			const float art_h = tile_w * 4.0f / 3.0f;
			const float label_h = m_l.small * 2.2f;
			const float tile_h = art_h + label_h;
			const float avail = ContentBottom() - ContentTop();
			const int rows_visible = std::max(1, static_cast<int>((avail + S(18.0f)) / (tile_h + S(18.0f))));
			int focused = 0;
			for (size_t i = 0; i < m_view.tiles.size(); i++)
				if (m_view.tiles[i].focused)
					focused = static_cast<int>(i);
			const int total_rows = static_cast<int>((m_view.tiles.size() + cols - 1) / cols);
			const int first_row = std::max(0, std::min(focused / cols - rows_visible + 1, std::max(0, total_rows - rows_visible)));
			const float grid_w = cols * tile_w + (cols - 1) * gap;
			const float x0 = gx + std::max(0.0f, (gw - grid_w) * 0.5f);
			float fx = 0, fy = 0, fw = 0, fh = 0;
			for (int pass = 0; pass < 2; pass++)
				for (int row = 0; row < rows_visible; row++)
					for (int col = 0; col < cols; col++)
					{
						const int index = (first_row + row) * cols + col;
						if (index >= static_cast<int>(m_view.tiles.size()))
							continue;
						const UiTile& tile = m_view.tiles[static_cast<size_t>(index)];
						if ((pass == 1) != tile.focused)
							continue; // the focused tile is drawn last, above its neighbours
						// The focused cover rises and grows a little; the one left behind settles back.
						const float lift = Tween("lift:" + tile.id, tile.focused ? S(12.0f) : 0.0f, 0.20f);
						const float x = x0 + col * (tile_w + gap) - lift * 0.5f;
						const float y = ContentTop() + row * (tile_h + S(18.0f)) - lift;
						const float tw = tile_w + lift, th = art_h + lift * 4.0f / 3.0f;
						const Texture2D* cover = m_scene.CoverFor(tile.cover_key, tile.title);
						const size_t layer = m_out.VertexCount();
						if (tile.focused)
						{
							// The frame glides from the previous cover to this one.
							fx = Tween("tile:x", x, 0.18f);
							fy = Tween("tile:y", y, 0.18f);
							fw = tw;
							fh = th;
						}
						const Color tint = (tile.disabled || tile.hidden) ? Color{0.40f, 0.44f, 0.58f, 1.0f} :
							tile.focused ? Color{} : Color{0.72f, 0.76f, 0.90f, 1.0f};
						ImageCover(cover, x, y, tw, th, tint);
						if (tile.damaged)
						{
							VGrad(x, y, x + tw, y + m_l.small * 2.0f, HexColor(0x01020A, 0.85f), HexColor(0x01020A, 0.0f));
							Text("Needs attention", x + S(14.0f), y + m_l.small * 1.25f, m_l.small * 0.92f, kAmber, 0.25f, 0, tw - S(28.0f));
						}
						if (tile.focused)
							TileFocus(fx, fy, fw, fh);
						const float tb = y + th + m_l.small * 1.45f;
						if (tile.focused)
							Text(tile.title, x + tw * 0.5f, tb, m_l.small * 1.05f, kText, 0.25f, 1, tw + gap * 0.8f);
						else
							Text(tile.title, x + tw * 0.5f, tb, m_l.small, tile.disabled ? kTextFaint : kTextDim, 0.0f, 1, tw);
						Cascade(layer, row * cols + col);
					}
		}
		else
		{
			float y = ContentTop() + S(40.0f);
			const float w = std::min(S(860.0f), gw);
			const std::string message = m_view.status.empty() ? "No games match this view" : m_view.status;
			m_status_shown = true;
			for (const std::string& line : Wrap(message, m_l.body * 1.2f, w, 2))
			{
				Text(line, gx, y + m_l.body * 1.2f, m_l.body * 1.2f, kText, -0.1f, 0, w);
				y += m_l.body * 1.6f;
			}
			y += S(36.0f);
			List(m_view.items, gx - S(10.0f), y, std::min(S(640.0f), gw), T(84.0f), S(6.0f), ContentBottom(), kDetailAlways);
		}
	}

	void PaintGameDetail()
	{
		const float cover_h = std::min(S(560.0f), ContentBottom() - ContentTop() - S(150.0f));
		const float cover_w = cover_h * 0.75f;
		const float cx = m_l.safe_x + S(20.0f);
		const float cy = ContentTop() + S(10.0f);
		const std::string key = m_view.hero_cover_key.empty() ? "proc:" + m_view.title : m_view.hero_cover_key;
		const Texture2D* cover = m_scene.CoverFor(key, m_view.hero_title.empty() ? m_view.title : m_view.hero_title);
		ImageCover(cover, cx, cy, cover_w, cover_h, Color{}, true);
		const float x = cx + cover_w + S(90.0f);
		const float w = std::min(S(820.0f), m_l.right - x);
		List(m_view.items, x, ContentTop() + S(10.0f), w, T(88.0f), S(8.0f), ContentBottom(), kDetailAlways);
	}

	void PaintTexturePacks()
	{
		const float w = std::min(S(1150.0f), m_l.right - m_l.safe_x);
		const float bottom = ContentBottom() - (m_view.status.empty() ? 0.0f : m_l.small * 2.0f);
		List(m_view.items, m_l.safe_x - S(10.0f), ContentTop(), w, T(76.0f), S(4.0f), bottom, kDetailAlways);
		if (!m_view.status.empty())
			Text(m_view.status, m_l.safe_x, ContentBottom() - S(6.0f), m_l.small, kTextDim, 0, 0, w);
	}

	void PaintPause()
	{
		// Host preview stand-in for the live frame: the current fictional cover, then a readable left panel.
		const float w = static_cast<float>(m_l.width);
		const float h = static_cast<float>(m_l.height);
		const Texture2D* cover = m_scene.CoverFor(m_view.hero_cover_key.empty() ? "proc:" + m_view.subtitle : m_view.hero_cover_key,
			m_view.hero_title.empty() ? m_view.subtitle : m_view.hero_title);
		ImageCover(cover, 0, 0, w, h, Color{0.62f, 0.68f, 0.84f, 1.0f});
		const size_t layer = m_out.VertexCount();
		const float a = m_view.high_contrast ? 0.98f : 0.92f;
		m_out.AddSolidQuad(0, 0, w * 0.30f, h, HexColor(0x03040E, a));
		HGrad(w * 0.30f, 0, w * 0.52f, h, HexColor(0x03040E, a), HexColor(0x03040E, 0.0f));
		Glow(w * 0.10f, h * 0.30f, h * 0.7f, WithAlpha(kNebula, 0.30f));
		Logo(m_l.safe_x, m_l.safe_y - S(10.0f), S(34.0f));
		Text(m_view.title, m_l.safe_x, m_l.safe_y + S(92.0f), m_l.title, kText, -0.25f);
		if (!m_view.subtitle.empty())
			Text(m_view.subtitle, m_l.safe_x, m_l.safe_y + S(92.0f) + m_l.small * 1.6f, m_l.small, kTextDim, 0.0f, 0, w * 0.32f);
		List(m_view.items, m_l.safe_x - S(10.0f), m_l.safe_y + S(190.0f), w * 0.34f, T(76.0f), S(6.0f), ContentBottom(),
			kDetailFocused);
		// The menu slides over the paused frame from the left edge it is anchored to.
		const float p = Progress(m_mo.screen_since, 0.0f, 0.26f);
		if (p < 1.0f)
			m_out.Transform(layer, -(1.0f - Travel(p)) * S(60.0f), 0.0f, 1.0f, 0.0f, 0.0f, p);
		Footer();
	}

	void PaintMemoryCards()
	{
		float x = m_l.safe_x;
		const float slot_w = T(330.0f), slot_h = T(190.0f);
		const float sy = ContentTop();
		for (const UiEntry& slot : m_view.rail)
		{
			const bool focus = slot.focused;
			// The slot itself reads as a glass memory card standing in the void.
			m_out.AddRoundedRect(x, sy, slot_w, slot_h, S(12.0f), WithAlpha(Mix(HexColor(0x2A32A6), kGlass, focus ? 0.45f : 0.22f), focus ? 0.42f : 0.26f));
			HGrad(x, sy, x + slot_w, sy + S(2.0f), WithAlpha(kGlassEdge, focus ? 0.9f : 0.45f), WithAlpha(kGlassEdge, 0.05f));
			MemoryCardGlyph(x + S(46.0f), sy + S(52.0f), S(46.0f), focus ? kIce : kTextDim);
			const float tw = slot_w - S(110.0f);
			if (focus)
				Text(slot.label, x + S(94.0f), sy + S(54.0f), m_l.body * 1.1f, kText, 0.3f, 0, tw);
			else
				Text(slot.label, x + S(94.0f), sy + S(54.0f), m_l.body * 1.1f, slot.selected ? kText : kTextSoft, 0.1f, 0, tw);
			Text(slot.detail, x + S(30.0f), sy + slot_h - S(58.0f), m_l.small, kTextDim, 0, 0, slot_w - S(60.0f));
			if (!slot.value.empty())
				Text(slot.value, x + S(30.0f), sy + slot_h - S(26.0f), m_l.small * 0.9f, kIce, 0, 0, slot_w - S(60.0f));
			if (focus)
				TileFocus(x, sy, slot_w, slot_h);
			else if (slot.selected)
				m_out.AddSolidQuad(x, sy + slot_h - S(3.0f), x + slot_w, sy + slot_h, WithAlpha(kIce, 0.7f));
			x += slot_w + S(40.0f);
		}
		const float y = sy + slot_h + S(54.0f);
		const float w = std::min(S(760.0f), m_l.right - m_l.safe_x);
		List(m_view.items, m_l.safe_x - S(10.0f), y, w, T(84.0f), S(6.0f), ContentBottom(), kDetailAlways);
	}

	void PaintSettings()
	{
		const float rail_w = S(290.0f);
		List(m_view.rail, m_l.safe_x - S(10.0f), ContentTop(), rail_w, T(58.0f), S(4.0f), ContentBottom(), kDetailNever);
		const float x = m_l.safe_x + rail_w + S(60.0f);
		VHairline(x - S(32.0f), ContentTop(), ContentBottom(), WithAlpha(kGlassEdge, 0.35f));
		const float list_w = (m_l.right - x) * 0.50f;
		List(m_view.items, x - S(10.0f), ContentTop(), list_w + S(10.0f), T(66.0f), S(2.0f), ContentBottom(), kDetailNever);
		const float px = x + list_w + S(44.0f);
		SettingExplainer(px, ContentTop(), m_l.right - px, ContentBottom() - ContentTop());
	}

	// The focused setting explained in full: what it is, every option with the current one lit, what it does,
	// and when it takes effect. Nothing is drawn while the category rail has focus: the list already shows its rows.
	void SettingExplainer(float x, float y, float w, float max_h)
	{
		const UiEntry* entry = Focused(m_view.items);
		if (!entry)
			return;
		const float pad = S(40.0f);
		const float iw = w - pad * 2.0f;
		const float body = m_l.small * 1.04f;
		const float line = body * 1.45f;
		const float title_px = m_l.body * 1.4f;

		// Lay out first so the panel hugs its content.
		std::vector<std::vector<int>> option_lines;
		const float chip_h = m_l.small * 1.9f;
		const float chip_gap = S(10.0f);
		bool chips = false;
		if (entry->options.size() >= 2 && entry->options.size() <= 8)
		{
			chips = true;
			std::vector<int> current;
			float used = 0.0f;
			for (size_t i = 0; i < entry->options.size(); i++)
			{
				const float cw = Width(entry->options[i], m_l.small) + S(36.0f);
				if (!current.empty() && used + cw > iw)
				{
					option_lines.push_back(current);
					current.clear();
					used = 0.0f;
				}
				current.push_back(static_cast<int>(i));
				used += cw + chip_gap;
			}
			option_lines.push_back(current);
		}
		const std::string& text = entry->detail;
		const bool restart_tag = entry->needs_restart && entry->detail.find("Applies") == std::string::npos;
		const bool tags = entry->modified || restart_tag || entry->game_scoped;
		const float head_h = pad + title_px * 0.75f;
		float value_h = 0.0f;
		if (chips)
			value_h = static_cast<float>(option_lines.size()) * (chip_h + chip_gap) + S(18.0f);
		else if (!entry->value.empty())
			value_h = m_l.body * 1.7f + S(10.0f);
		const float tags_h = tags ? chip_h + S(26.0f) : 0.0f;
		const float room = max_h - head_h - value_h - tags_h - S(44.0f) - S(24.0f);
		const size_t max_lines = static_cast<size_t>(std::max(2.0f, room / line));
		const std::vector<std::string> lines = Wrap(text, body, iw, max_lines);
		const float h = std::min(max_h, head_h + value_h + S(24.0f) + static_cast<float>(lines.size()) * line + tags_h + S(30.0f));
		Panel(x, y, w, h);

		// Content crossfades when focus moves, rising a few pixels so the change reads as new information.
		const size_t layer = m_out.VertexCount();
		float yy = y + head_h;
		Text(entry->label, x + pad, yy, title_px, kText, 0.15f, 0, iw);
		yy += S(18.0f);
		if (chips)
		{
			const float p = ValueChange(*entry);
			for (const std::vector<int>& row : option_lines)
			{
				float cx = x + pad;
				for (int i : row)
				{
					const std::string& label = entry->options[static_cast<size_t>(i)];
					const float cw = Width(label, m_l.small) + S(36.0f);
					const bool on = i == entry->option_index;
					if (on)
					{
						const float slide = (1.0f - Travel(p)) * S(10.0f) * static_cast<float>(m_mo.value_direction);
						m_out.AddRoundedRect(cx + slide, yy, cw, chip_h, chip_h * 0.5f, WithAlpha(kFocus, 0.45f));
						m_out.AddRoundedRect(cx + slide + S(1.5f), yy + S(1.5f), cw - S(3.0f), chip_h - S(3.0f), chip_h * 0.5f,
							HexColor(0x22307A, 0.95f));
						Text(label, cx + cw * 0.5f + slide, yy + chip_h * 0.5f + m_l.small * 0.36f, m_l.small, Mix(kText, kIce, p), 0.2f, 1);
					}
					else
					{
						m_out.AddRoundedRect(cx, yy, cw, chip_h, chip_h * 0.5f, WithAlpha(kGlass, 0.16f));
						m_out.AddRoundedRect(cx + S(1.2f), yy + S(1.2f), cw - S(2.4f), chip_h - S(2.4f), chip_h * 0.5f, HexColor(0x060823, 0.92f));
						Text(label, cx + cw * 0.5f, yy + chip_h * 0.5f + m_l.small * 0.36f, m_l.small, kTextDim, 0.0f, 1);
					}
					cx += cw + chip_gap;
				}
				yy += chip_h + chip_gap;
			}
			yy += S(18.0f);
		}
		else if (!entry->value.empty())
		{
			const float p = ValueChange(*entry);
			const float slide = (1.0f - Travel(p)) * S(12.0f) * static_cast<float>(m_mo.value_direction);
			float vx = x + pad + slide;
			if (entry->option_index >= 0)
			{
				vx += Text(fe::icon::DpadLeft, vx, yy + m_l.body * 1.05f, m_l.body, kTextFaint) + S(12.0f);
			}
			vx += Text(DisplayValue(entry->value), vx, yy + m_l.body * 1.05f, m_l.body * 1.15f, Mix(kText, kIce, p), 0.2f, 0, iw * 0.7f);
			if (entry->option_index >= 0)
			{
				Text(fe::icon::DpadRight, vx + S(12.0f), yy + m_l.body * 1.05f, m_l.body, kTextFaint);
				Text(std::to_string(entry->option_index + 1) + " of " + std::to_string(entry->options.size()), x + w - pad,
					yy + m_l.body * 1.05f, m_l.small, kTextDim, 0.0f, 2);
			}
			yy += m_l.body * 1.7f + S(10.0f);
		}
		Hairline(x + pad, yy, x + pad + iw * 0.7f, WithAlpha(kGlassEdge, 0.35f), WithAlpha(kGlassEdge, 0.0f));
		yy += S(24.0f) + body;
		for (const std::string& l : lines)
		{
			Text(l, x + pad, yy, body, kTextSoft);
			yy += line;
		}
		if (tags)
		{
			yy += S(10.0f) - body;
			float tx = x + pad;
			const auto tag = [&](const std::string& label, Color color) {
				const float tw = Width(label, m_l.small * 0.92f) + S(32.0f);
				m_out.AddRoundedRect(tx, yy, tw, chip_h, chip_h * 0.5f, WithAlpha(color, 0.16f));
				Text(label, tx + S(16.0f), yy + chip_h * 0.5f + m_l.small * 0.33f, m_l.small * 0.92f, color, 0.1f);
				tx += tw + S(10.0f);
			};
			if (entry->modified)
				tag("Changed", kIce);
			if (restart_tag)
				tag("Applies after a restart", kAmber);
			if (entry->game_scoped)
				tag("This game only", kGlassEdge);
		}
		const float p = Progress(m_mo.focus_since, 0.0f, 0.18f);
		if (p < 1.0f)
			m_out.Transform(layer, 0.0f, (1.0f - Travel(p)) * S(6.0f), 1.0f, 0.0f, 0.0f, p);
	}

	void PaintControls()
	{
		const float list_w = (m_l.right - m_l.safe_x) * 0.50f;
		const float help_h = HelpHeight(2);
		const float list_bottom = ContentBottom() - help_h;
		List(m_view.items, m_l.safe_x - S(10.0f), ContentTop(), list_w, T(64.0f), S(2.0f), list_bottom, kDetailNever);
		if (const UiEntry* focused = Focused(m_view.items))
			HelpText(focused->detail, m_l.safe_x + S(18.0f),
				ListEnd(m_view.items, ContentTop(), T(64.0f), S(2.0f), list_bottom) + S(14.0f), list_w - S(36.0f), 2);
		// Original simplified controller: glass geometry only, no proprietary silhouette asset.
		const float cx = m_l.safe_x + list_w + (m_l.right - m_l.safe_x - list_w) * 0.52f;
		const float cy = ContentTop() + S(250.0f);
		const float s = S(300.0f);
		const Color glass = WithAlpha(Mix(HexColor(0x2A32A6), kGlass, 0.38f), 0.34f);
		const Color grip = WithAlpha(Mix(HexColor(0x2A32A6), kGlass, 0.25f), 0.34f);
		m_out.AddRoundedRect(cx - s * 0.62f, cy - s * 0.06f, s * 0.36f, s * 0.58f, s * 0.17f, grip);
		m_out.AddRoundedRect(cx + s * 0.26f, cy - s * 0.06f, s * 0.36f, s * 0.58f, s * 0.17f, grip);
		m_out.AddRoundedRect(cx - s * 0.56f, cy - s * 0.27f, s * 1.12f, s * 0.54f, s * 0.20f, glass);
		HGrad(cx - s * 0.42f, cy - s * 0.27f, cx + s * 0.42f, cy - s * 0.27f + S(2.0f), WithAlpha(kGlassEdge, 0.0f), WithAlpha(kGlassEdge, 0.8f));
		m_out.AddSolidQuad(cx - s * 0.40f, cy - s * 0.035f, cx - s * 0.20f, cy + s * 0.035f, kGlassEdge);
		m_out.AddSolidQuad(cx - s * 0.335f, cy - s * 0.10f, cx - s * 0.265f, cy + s * 0.10f, kGlassEdge);
		for (float sxo : {-0.13f, 0.13f})
		{
			Dot(cx + s * sxo, cy + s * 0.15f, s * 0.075f, WithAlpha(kGlassEdge, 0.50f));
			Dot(cx + s * sxo, cy + s * 0.15f, s * 0.055f, HexColor(0x1A2070, 0.95f));
		}
		const float gp = m_l.body * 1.1f;
		Text(fe::icon::Triangle, cx + s * 0.30f, cy - s * 0.07f, gp, kTriangleGreen, 0.2f, 1);
		Text(fe::icon::Square, cx + s * 0.19f, cy + s * 0.04f, gp, kSquarePink, 0.2f, 1);
		Text(fe::icon::Circle, cx + s * 0.41f, cy + s * 0.04f, gp, kCircleRed, 0.2f, 1);
		Text(fe::icon::Cross, cx + s * 0.30f, cy + s * 0.15f, gp, kCrossBlue, 0.2f, 1);
		if (!m_view.subtitle.empty() && m_view.subtitle.find("Press") != std::string::npos)
			Text(m_view.subtitle, cx, cy + s * 0.75f, m_l.body, kIce, 0.25f, 1, m_l.right - m_l.safe_x - list_w);
	}

	void PaintAccessibility()
	{
		const float list_w = (m_l.right - m_l.safe_x) * 0.50f;
		List(m_view.items, m_l.safe_x - S(10.0f), ContentTop(), list_w, T(88.0f), S(6.0f), ContentBottom(), kDetailAlways);
		const float px = m_l.safe_x + list_w + S(60.0f);
		const float pw = m_l.right - px;
		const float ph = std::min(ContentBottom() - ContentTop(), S(52.0f) + m_l.body * 2.6f + T(70.0f) + S(34.0f));
		Panel(px, ContentTop(), pw, ph);
		Text("Preview", px + S(40.0f), ContentTop() + S(52.0f), m_l.small, kTextDim, 0.0f, 0, pw - S(80.0f));
		Text("Ready to play", px + S(40.0f), ContentTop() + S(52.0f) + m_l.body * 1.7f, m_l.body * 1.25f, kText, -0.1f, 0, pw - S(80.0f));
		UiEntry sample;
		sample.label = "Focus stays visible at every size";
		sample.focused = true;
		const float sy = ContentTop() + S(52.0f) + m_l.body * 2.6f;
		if (sy + T(70.0f) <= ContentTop() + ph)
			Row(sample, px + S(30.0f), sy, pw - S(60.0f), T(70.0f), kDetailNever, m_l.small * 1.1f);
	}

	// QR modules for a URL, cached because the address rarely changes between frames. Empty when encoding fails.
	static const std::vector<uint8_t>& QrModules(const std::string& text, int* size)
	{
		static std::string cached_text;
		static std::vector<uint8_t> modules;
		static int cached_size = 0;
		if (text != cached_text)
		{
			cached_text = text;
			modules.clear();
			cached_size = 0;
			uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
			uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
			if (qrcodegen_encodeText(text.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
			{
				cached_size = qrcodegen_getSize(qr);
				modules.resize(static_cast<size_t>(cached_size) * cached_size);
				for (int y = 0; y < cached_size; y++)
					for (int x = 0; x < cached_size; x++)
						modules[static_cast<size_t>(y) * cached_size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
			}
		}
		*size = cached_size;
		return modules;
	}

	// Dark modules on a light tile with a four-module quiet zone, snapped to whole pixels so phones read it reliably.
	// Returns the tile's side, or 0 when nothing was drawn.
	float QrCode(const std::string& text, float x, float y, float max_side)
	{
		int n = 0;
		const std::vector<uint8_t>& modules = QrModules(text, &n);
		if (n == 0)
			return 0.0f;
		const int quiet = 4;
		const float module = std::floor(max_side / static_cast<float>(n + quiet * 2));
		if (module < 2.0f)
			return 0.0f;
		const float side = module * static_cast<float>(n + quiet * 2);
		x = std::round(x);
		y = std::round(y);
		m_out.AddRoundedRect(x, y, side, side, module * 1.5f, HexColor(0xEEF2FF));
		const Color ink = HexColor(0x0A0D2E);
		const float ox = x + module * quiet, oy = y + module * quiet;
		for (int row = 0; row < n; row++)
		{
			// Merge horizontal runs of dark modules into single quads.
			for (int col = 0; col < n;)
			{
				if (!modules[static_cast<size_t>(row) * n + col])
				{
					col++;
					continue;
				}
				int end = col;
				while (end < n && modules[static_cast<size_t>(row) * n + end])
					end++;
				m_out.AddSolidQuad(ox + module * col, oy + module * row, ox + module * end, oy + module * (row + 1), ink);
				col = end;
			}
		}
		return side;
	}

	void PaintDiagnostics()
	{
		const float list_w = (m_l.right - m_l.safe_x) * 0.54f;
		const float help_h = HelpHeight(2);
		const float list_bottom = ContentBottom() - help_h;
		List(m_view.items, m_l.safe_x - S(10.0f), ContentTop(), list_w, T(72.0f), S(4.0f), list_bottom, kDetailNever);
		if (const UiEntry* focused = Focused(m_view.items))
			HelpText(focused->detail, m_l.safe_x + S(18.0f),
				ListEnd(m_view.items, ContentTop(), T(72.0f), S(4.0f), list_bottom) + S(14.0f), list_w - S(36.0f), 2);

		const float x = m_l.safe_x + list_w + S(60.0f);
		const float w = m_l.right - x;
		const float pad = S(40.0f);
		const bool has_url = m_view.subtitle.rfind("http", 0) == 0;
		const std::string url = m_view.subtitle.empty() ? "Unavailable" : m_view.subtitle;
		const std::vector<std::string> hint = Wrap(has_url ?
				"Scan the code with a phone, or open this address in a browser on the same network." :
				"The panel starts when the console is connected to a network.",
			m_l.small, w - pad * 2.0f, 3);

		const float top = pad + m_l.body;
		float y = ContentTop() + top;
		const float text_h = m_l.body * 1.4f + static_cast<float>(hint.size()) * m_l.small * 1.38f;
		// The code takes the room left under the text, capped so it stays a comfortable size on a TV.
		const float qr_room = ContentBottom() - (y + text_h) - S(40.0f) - pad;
		const float qr_max = has_url ? std::min({S(300.0f), w - pad * 2.0f, qr_room}) : 0.0f;
		int modules = 0;
		if (has_url)
			QrModules(url, &modules);
		const float qr_side = modules > 0 ? std::floor(qr_max / static_cast<float>(modules + 8)) * static_cast<float>(modules + 8) : 0.0f;
		const bool show_qr = qr_side >= static_cast<float>(modules + 8) * 2.0f;
		const float h = top + text_h + (show_qr ? S(28.0f) + qr_side : 0.0f) + pad - m_l.small * 0.4f;
		Panel(x, ContentTop(), w, h);
		Text(url, x + pad, y, m_l.body * 1.35f, kText, 0.2f, 0, w - pad * 2.0f);
		y += m_l.body * 1.4f;
		for (const std::string& line : hint)
		{
			Text(line, x + pad, y, m_l.small, kTextDim);
			y += m_l.small * 1.38f;
		}
		if (show_qr)
			QrCode(url, x + pad, y - m_l.small * 0.4f + S(28.0f), qr_max);
	}

	void PaintFolderPicker()
	{
		const float w = std::min(S(1150.0f), m_l.right - m_l.safe_x);
		std::vector<UiEntry> rows;
		for (const UiPickerRow& row : m_view.picker.rows)
		{
			UiEntry entry;
			entry.id = row.id;
			entry.label = row.label;
			entry.detail = row.detail;
			entry.disabled = row.disabled;
			entry.focused = row.focused;
			entry.icon = row.use_current ? EntryIcon::Action : row.image ? EntryIcon::Game : EntryIcon::Folder;
			rows.push_back(entry);
		}
		float bottom = ContentBottom();
		if (!m_view.picker.status.empty())
		{
			bottom -= m_l.small * 2.0f;
			Text(m_view.picker.status, m_l.safe_x, ContentBottom() - S(6.0f), m_l.small, kAmber, 0, 0, w);
		}
		List(rows, m_l.safe_x - S(10.0f), ContentTop(), w, T(76.0f), S(4.0f), bottom, kDetailAlways);
	}

	void Dialog()
	{
		const size_t blackout = m_out.VertexCount();
		m_out.AddSolidQuad(0, 0, static_cast<float>(m_l.width), static_cast<float>(m_l.height), kBlackout);
		const size_t panel = m_out.VertexCount();
		const float w = S(820.0f);
		const std::vector<std::string> body = Wrap(m_view.dialog.body, m_l.small, w - S(96.0f), 4);
		const float h = S(150.0f) + m_l.body * 1.3f + body.size() * m_l.small * 1.4f + T(70.0f);
		const float x = (m_l.width - w) * 0.5f;
		const float y = (m_l.height - h) * 0.46f;
		Panel(x, y, w, h, 0.96f);
		float base = y + S(64.0f);
		Text(m_view.dialog.title, x + S(48.0f), base, m_l.body * 1.25f, m_view.dialog.destructive ? kAmber : kText, 0.0f, 0,
			w - S(96.0f));
		base += m_l.body * 1.3f;
		for (const std::string& line : body)
		{
			Text(line, x + S(48.0f), base, m_l.small, kTextDim);
			base += m_l.small * 1.4f;
		}
		const float bw = (w - S(120.0f)) * 0.5f;
		const float by = y + h - T(70.0f) - S(32.0f);
		UiEntry cancel, confirm;
		cancel.label = m_view.dialog.cancel_label;
		cancel.focused = m_view.dialog.focused_option == 0;
		confirm.label = m_view.dialog.confirm_label;
		confirm.focused = m_view.dialog.focused_option == 1;
		confirm.destructive = m_view.dialog.destructive;
		Row(cancel, x + S(48.0f), by, bw, T(70.0f));
		Row(confirm, x + S(72.0f) + bw, by, bw, T(70.0f));
		ModalEnter(blackout, panel, m_mo.dialog_since, x + w * 0.5f, y + h * 0.5f);
		Footer();
	}

	void Keyboard()
	{
		const size_t blackout = m_out.VertexCount();
		m_out.AddSolidQuad(0, 0, static_cast<float>(m_l.width), static_cast<float>(m_l.height), HexColor(0x01020A, 0.62f));
		const size_t panel = m_out.VertexCount();
		const float w = S(1040.0f);
		const float x = (m_l.width - w) * 0.5f;
		const float key_h = T(62.0f);
		const float h = S(210.0f) + m_view.keyboard.rows.size() * (key_h + S(10.0f));
		const float y = std::max(m_l.safe_y, (m_l.height - h) * 0.42f);
		Panel(x, y, w, h, 0.96f);
		Text(m_view.keyboard.title, x + S(48.0f), y + S(62.0f), m_l.body * 1.1f, kText, 0.0f);
		const float field_y = y + S(126.0f);
		const float tw = Text(m_view.keyboard.text, x + S(48.0f), field_y, m_l.body * 1.15f, kIce, 0.1f, 0, w - S(110.0f));
		if (m_view.reduce_motion || std::fmod(m_ui_t, 1.0f) < 0.6f)
			m_out.AddSolidQuad(x + S(52.0f) + tw, field_y - m_l.body * 0.95f, x + S(55.0f) + tw, field_y + S(6.0f), kIce);
		Hairline(x + S(48.0f), field_y + S(18.0f), x + w - S(48.0f), WithAlpha(kGlassEdge, 0.6f), WithAlpha(kGlassEdge, 0.1f));
		float ky = field_y + S(52.0f);
		for (const auto& keys : m_view.keyboard.rows)
		{
			const float gap = S(10.0f);
			const float key_w = (w - S(96.0f) - gap * (keys.size() - 1)) / std::max<size_t>(1, keys.size());
			for (size_t c = 0; c < keys.size(); c++)
			{
				const float kx = x + S(48.0f) + c * (key_w + gap);
				const UiKeyboardKey& key = keys[c];
				if (key.focused)
				{
					m_out.AddRoundedRect(kx, ky, key_w, key_h, S(8.0f), WithAlpha(kFocus, 0.55f));
					HGrad(kx, ky, kx + key_w, ky + S(2.0f), kIce, WithAlpha(kIce, 0.2f));
				}
				else
					m_out.AddRoundedRect(kx, ky, key_w, key_h, S(8.0f), WithAlpha(kGlass, 0.08f));
				Text(key.label, kx + key_w * 0.5f, ky + key_h * 0.5f + m_l.body * 0.36f, m_l.body,
					key.focused ? kText : kTextSoft, key.focused ? 0.3f : 0.0f, 1, key_w - S(12.0f));
			}
			ky += key_h + S(10.0f);
		}
		ModalEnter(blackout, panel, m_mo.keyboard_since, x + w * 0.5f, y + h * 0.5f);
		Footer();
	}
};

} // namespace

DrawList EstratosScene::Build(const UiView& view, uint32_t width, uint32_t height, double seconds)
{
	DrawList out;
	out.Reserve(65536);
	if (!m_ready)
		return out;
	Painter painter(*this, out, view, width, height, seconds);
	painter.Paint();
	return out;
}
} // namespace caliza
