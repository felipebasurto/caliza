// Caliza PS4 Estratos: view-model to 2D composition.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "estratos_scene.h"

#include "../../ps5/frontend/fe_text.h"
#include "estratos_theme.h"
#include "procedural_covers.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace caliza
{
EstratosScene::EstratosScene() : m_fonts(new fe::Fonts()) {}

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

struct Layout
{
	uint32_t width = 0, height = 0;
	float scale = 1.0f;
	float text = 1.0f;
	float safe_x = 0, safe_y = 0, right = 0, bottom = 0;
	float body = 27, small = 23, title = 58, row = 78;
};

class Painter
{
public:
	Painter(EstratosScene& scene, DrawList& out, const UiView& view, uint32_t width, uint32_t height, double seconds)
		: m_scene(scene), m_fonts(scene.Fonts()), m_out(out), m_view(view), m_seconds(seconds)
	{
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
		m_l.title = kBaseScreenTitlePx * m_l.text;
		m_l.row = 78.0f * m_l.scale * std::min(1.45f, requested);
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
			Chrome();
			switch (m_view.screen)
			{
				case Screen::FirstRun: PaintFirstRun(); break;
				case Screen::Home: PaintHome(); break;
				case Screen::Library: PaintLibrary(); break;
				case Screen::GameDetail: PaintGameDetail(); break;
				case Screen::MemoryCards: PaintMemoryCards(); break;
				case Screen::Settings: PaintSettings(); break;
				case Screen::Controls: PaintControls(); break;
				case Screen::Accessibility: PaintAccessibility(); break;
				case Screen::Diagnostics: PaintDiagnostics(); break;
				case Screen::FolderPicker: PaintFolderPicker(); break;
				default: break;
			}
		}
		StatusAndToast();
		if (!m_view.keyboard.open && !m_view.dialog.open)
			Prompts();
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
	Layout m_l;
	double m_seconds = 0.0;

	float S(float value) const { return value * m_l.scale; }

	float Width(const std::string& text, float px) const
	{
		return m_fonts.Measure(text.c_str(), px);
	}

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

	void Panel(float x, float y, float w, float h, Color fill, float radius = 10.0f, bool border = true)
	{
		if (m_view.high_contrast)
			fill.a = std::min(1.0f, fill.a + 0.16f);
		if (border)
			m_out.AddRoundedRect(x - 1.2f, y - 1.2f, w + 2.4f, h + 2.4f, radius + 1.0f, kLine);
		m_out.AddRoundedRect(x, y, w, h, radius, fill);
	}

	void FocusFrame(float x, float y, float w, float h)
	{
		const float width = (m_view.accessibility.large_focus ? 3.0f : 2.0f) * m_l.scale +
			(m_view.high_contrast ? 1.0f : 0.0f);
		const float notch = std::min(22.0f * m_l.scale, h * 0.34f);
		auto pass = [&](float o, Color color) {
			m_out.AddLine(x - o, y - o, x + w - notch, y - o, width, color);
			m_out.AddLine(x + w - notch, y - o, x + w + o, y + notch, width, color);
			m_out.AddLine(x + w + o, y + notch, x + w + o, y + h + o, width, color);
			m_out.AddLine(x + w + o, y + h + o, x - o, y + h + o, width, color);
			m_out.AddLine(x - o, y + h + o, x - o, y - o, width, color);
		};
		pass(0.0f, kIce);
		if (m_view.accessibility.large_focus)
			pass(4.0f * m_l.scale, WithAlpha(kIce, m_view.high_contrast ? 0.92f : 0.55f));
	}

	void Icon(EntryIcon icon, float cx, float cy, float size, Color color)
	{
		const float s = size;
		switch (icon)
		{
			case EntryIcon::Continue:
			case EntryIcon::Game:
				m_out.AddPoly({{cx - s * 0.25f, cy - s * 0.36f}, {cx + s * 0.38f, cy}, {cx - s * 0.25f, cy + s * 0.36f}}, color);
				break;
			case EntryIcon::Library:
				for (int i = 0; i < 3; i++)
				{
					const float yy = cy - s * 0.26f + i * s * 0.22f;
					m_out.AddPoly({{cx, yy - s * 0.16f}, {cx + s * 0.34f, yy}, {cx, yy + s * 0.16f}, {cx - s * 0.34f, yy}},
						WithAlpha(color, 0.62f + i * 0.12f));
				}
				break;
			case EntryIcon::Settings:
			{
				m_out.AddPoly({{cx, cy - s * 0.34f}, {cx + s * 0.29f, cy - s * 0.17f}, {cx + s * 0.29f, cy + s * 0.17f},
					{cx, cy + s * 0.34f}, {cx - s * 0.29f, cy + s * 0.17f}, {cx - s * 0.29f, cy - s * 0.17f}}, color);
				m_out.AddPoly({{cx, cy - s * 0.13f}, {cx + s * 0.11f, cy}, {cx, cy + s * 0.13f}, {cx - s * 0.11f, cy}}, kGraphite);
				break;
			}
			case EntryIcon::MemoryCard:
			case EntryIcon::Slot:
				m_out.AddRoundedRect(cx - s * 0.30f, cy - s * 0.36f, s * 0.60f, s * 0.72f, s * 0.07f, color);
				m_out.AddSolidQuad(cx - s * 0.18f, cy - s * 0.20f, cx + s * 0.18f, cy - s * 0.02f, kGraphite);
				break;
			case EntryIcon::Folder:
				m_out.AddPoly({{cx - s * 0.38f, cy - s * 0.18f}, {cx - s * 0.10f, cy - s * 0.18f}, {cx, cy - s * 0.30f},
					{cx + s * 0.36f, cy - s * 0.30f}, {cx + s * 0.36f, cy + s * 0.28f}, {cx - s * 0.38f, cy + s * 0.28f}}, color);
				break;
			case EntryIcon::Controls:
				m_out.AddRoundedRect(cx - s * 0.40f, cy - s * 0.20f, s * 0.80f, s * 0.40f, s * 0.18f, color);
				m_out.AddSolidQuad(cx - s * 0.25f, cy - s * 0.03f, cx - s * 0.09f, cy + s * 0.03f, kGraphite);
				m_out.AddSolidQuad(cx - s * 0.19f, cy - s * 0.09f, cx - s * 0.15f, cy + s * 0.09f, kGraphite);
				m_out.AddPoly({{cx + s * 0.19f, cy - s * 0.05f}, {cx + s * 0.24f, cy}, {cx + s * 0.19f, cy + s * 0.05f},
					{cx + s * 0.14f, cy}}, kGraphite);
				break;
			case EntryIcon::Accessibility:
				m_out.AddLine(cx, cy - s * 0.10f, cx, cy + s * 0.22f, s * 0.08f, color);
				m_out.AddLine(cx - s * 0.23f, cy, cx + s * 0.23f, cy, s * 0.07f, color);
				m_out.AddLine(cx, cy + s * 0.20f, cx - s * 0.18f, cy + s * 0.40f, s * 0.07f, color);
				m_out.AddLine(cx, cy + s * 0.20f, cx + s * 0.18f, cy + s * 0.40f, s * 0.07f, color);
				m_out.AddPoly({{cx, cy - s * 0.39f}, {cx + s * 0.10f, cy - s * 0.29f}, {cx, cy - s * 0.19f},
					{cx - s * 0.10f, cy - s * 0.29f}}, color);
				break;
			case EntryIcon::Diagnostics:
				m_out.AddRoundedRect(cx - s * 0.34f, cy - s * 0.34f, s * 0.68f, s * 0.68f, s * 0.34f, color);
				m_out.AddRoundedRect(cx - s * 0.10f, cy - s * 0.10f, s * 0.20f, s * 0.20f, s * 0.10f, kGraphite);
				break;
			case EntryIcon::Warning:
				m_out.AddPoly({{cx, cy - s * 0.38f}, {cx + s * 0.40f, cy + s * 0.30f}, {cx - s * 0.40f, cy + s * 0.30f}}, kAmber);
				m_out.AddSolidQuad(cx - s * 0.035f, cy - s * 0.12f, cx + s * 0.035f, cy + s * 0.10f, kBasalt);
				m_out.AddSolidQuad(cx - s * 0.04f, cy + s * 0.17f, cx + s * 0.04f, cy + s * 0.23f, kBasalt);
				break;
			default:
				break;
		}
	}

	void Logo(float x, float y, float h, bool wordmark = true, bool monochrome = false)
	{
		// Approved Caliza mark: three staggered mineral sheets, redrawn as vector geometry.
		const Color shades[3] = {
			monochrome ? kText : HexColor(0xD6F5FF),
			monochrome ? kText : HexColor(0xA5E7FF),
			monochrome ? kText : HexColor(0x6FB9E8),
		};
		for (int i = 0; i < 3; i++)
		{
			const float ox = (i == 0 ? 0.46f : i == 1 ? 0.34f : 0.22f) * h;
			const float oy = i * 0.24f * h;
			m_out.AddPoly({{x + ox, y + oy}, {x + ox + 0.42f * h, y + oy}, {x + ox - 0.06f * h, y + oy + 0.28f * h},
				{x + ox - 0.48f * h, y + oy + 0.28f * h}}, shades[i]);
			m_out.AddLine(x + ox - 0.46f * h, y + oy + 0.27f * h, x + ox - 0.07f * h, y + oy + 0.27f * h,
				std::max(1.0f, h * 0.018f), HexColor(0x4E8FC2, monochrome ? 0.3f : 0.65f));
		}
		if (!wordmark)
			return;
		float pen = x + h * 1.02f;
		const float px = h * 0.54f;
		const float baseline = y + h * 0.58f;
		for (char c : std::string("CALIZA"))
		{
			const std::string one(1, c);
			m_out.AddText(m_fonts, one, pen, baseline, px, kText, 0.65f);
			pen += m_fonts.Measure(one.c_str(), px) + px * 0.22f;
		}
	}

	void ImageCover(const Texture2D* texture, float x, float y, float w, float h, Color tint = {})
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
	}

	void Row(const UiEntry& entry, float x, float y, float w, float h, bool show_icon = true)
	{
		Color fill = entry.focused ? HexColor(0x18283E, 0.98f) : WithAlpha(kGraphite, m_view.high_contrast ? 0.98f : 0.86f);
		if (entry.disabled)
			fill = WithAlpha(kGraphite, 0.58f);
		Panel(x, y, w, h, fill, 8.0f * m_l.scale, true);
		float tx = x + 24.0f * m_l.scale;
		if (show_icon && entry.icon != EntryIcon::None)
		{
			Icon(entry.icon, x + 43.0f * m_l.scale, y + h * 0.5f, 34.0f * m_l.scale,
				entry.disabled ? kTextDim : entry.focused ? kIce : kTextDim);
			tx += 48.0f * m_l.scale;
		}
		Color label = entry.disabled ? WithAlpha(kTextDim, 0.62f) : kText;
		if (entry.destructive)
			label = entry.focused ? kAmber : label;
		const float value_width = entry.value.empty() ? 0.0f : Width(entry.value, m_l.body) + 24.0f * m_l.scale;
		const float label_width = w - (tx - x) - value_width - 28.0f * m_l.scale;
		if (entry.detail.empty())
			Text(entry.label, tx, y + h * 0.5f + m_l.body * 0.35f, m_l.body, label, entry.focused ? 0.35f : 0.05f, 0, label_width);
		else
		{
			Text(entry.label, tx, y + h * 0.43f, m_l.body, label, entry.focused ? 0.35f : 0.05f, 0, label_width);
			Text(entry.detail, tx, y + h * 0.43f + m_l.small * 1.20f, m_l.small, kTextDim, 0, 0, label_width);
		}
		if (!entry.value.empty())
			Text(entry.value, x + w - 24.0f * m_l.scale, y + h * 0.5f + m_l.body * 0.34f, m_l.body,
				entry.focused ? kIce : kTextDim, entry.focused ? 0.2f : 0.0f, 2, value_width);
		if (entry.progress >= 0.0f)
		{
			const float bw = std::min(w * 0.22f, 300.0f * m_l.scale);
			const float bx = x + w - bw - 24.0f * m_l.scale;
			const float by = y + h - 17.0f * m_l.scale;
			m_out.AddRoundedRect(bx, by, bw, 7.0f * m_l.scale, 4.0f, HexColor(0x35415B));
			m_out.AddRoundedRect(bx, by, bw * std::max(0.0f, std::min(1.0f, entry.progress)), 7.0f * m_l.scale, 4.0f, kIce);
		}
		if (entry.focused)
			FocusFrame(x, y, w, h);
	}

	void Background()
	{
		m_out.AddSolidQuad(0, 0, static_cast<float>(m_l.width), static_cast<float>(m_l.height), kBasalt);
		const float w = static_cast<float>(m_l.width);
		const float h = static_cast<float>(m_l.height);
		const double ambient = m_view.reduce_motion ? 0.0 : m_seconds;
		const float pulse = m_view.reduce_motion ? 0.0f : static_cast<float>(std::sin(ambient * 0.55) * 0.025);
		// Mineral strata occupy the open diagonal field and stay subordinate to text and cover art.
		for (int i = 0; i < 6; i++)
		{
			const float yy = -h * 0.18f + i * h * 0.105f;
			const float x0 = w * (0.53f + i * 0.035f);
			const float x1 = w + w * 0.08f;
			Color color = i % 2 ? HexColor(0x1B2440, 0.30f) : HexColor(0x25304A, 0.25f);
			if (m_view.high_contrast)
				color.a *= 0.62f;
			m_out.AddPoly({{x0, yy + h * 0.15f}, {x1, yy}, {x1, yy + h * 0.09f}, {x0, yy + h * 0.24f}}, color);
		}
		for (int i = 0; i < 4; i++)
		{
			const float yy = h * (0.52f + i * 0.09f);
			m_out.AddPoly({{w * 0.68f, yy}, {w + 30.0f, yy - h * 0.19f}, {w + 30.0f, yy - h * 0.19f + 8.0f + i * 2.0f},
				{w * 0.68f, yy + 8.0f + i * 2.0f}}, HexColor(0x34415F, 0.18f));
		}
		// A single cold vein: the one luminous cut through the basalt.
		m_out.AddPoly({{w * 0.66f, h * 0.72f}, {w + 20.0f, h * 0.50f}, {w + 20.0f, h * 0.505f + pulse * h},
			{w * 0.66f, h * 0.725f + pulse * h}}, WithAlpha(kIce, m_view.high_contrast ? 0.34f : 0.26f + pulse));
		m_out.AddSolidQuad(0, h * 0.90f, w, h, HexColor(0x05070D, 0.38f));
	}

	void Chrome()
	{
		Logo(m_l.safe_x, m_l.safe_y - 8.0f * m_l.scale, 46.0f * m_l.scale, true, false);
		const float title_y = m_l.safe_y + 105.0f * m_l.scale;
		Text(m_view.title, m_l.safe_x, title_y, m_l.title, kText, 0.45f, 0, m_l.right - m_l.safe_x);
		if (!m_view.subtitle.empty())
			Text(m_view.subtitle, m_l.safe_x, title_y + m_l.small * 1.45f, m_l.small, kTextDim, 0.0f, 0,
				m_l.right - m_l.safe_x);
		m_out.AddLine(m_l.safe_x, m_l.safe_y + 142.0f * m_l.scale, m_l.right, m_l.safe_y + 142.0f * m_l.scale,
			std::max(1.0f, 1.5f * m_l.scale), kLine);
	}

	float ContentTop() const { return m_l.safe_y + 178.0f * m_l.scale; }
	float ContentBottom() const { return m_l.bottom - 105.0f * m_l.scale; }

	void Prompts()
	{
		if (m_view.prompts.empty())
			return;
		const float line_y = m_l.bottom - 82.0f * m_l.scale;
		m_out.AddLine(m_l.safe_x, line_y, m_l.right, line_y, std::max(1.0f, m_l.scale * 1.4f), kLine);
		struct Part
		{
			const char* glyph;
			std::string label;
		};
		std::vector<Part> parts;
		for (const UiPrompt& prompt : m_view.prompts)
		{
			const char* glyph = fe::icon::Cross;
			if (prompt.glyph == PromptGlyph::Circle)
				glyph = fe::icon::Circle;
			else if (prompt.glyph == PromptGlyph::Square)
				glyph = fe::icon::Square;
			else if (prompt.glyph == PromptGlyph::Triangle)
				glyph = fe::icon::Triangle;
			else if (prompt.glyph == PromptGlyph::Options)
				glyph = fe::icon::Options;
			else if (prompt.glyph == PromptGlyph::Dpad)
				glyph = fe::icon::DpadUpDown;
			parts.push_back({glyph, prompt.label});
		}
		const float px = m_l.body * 0.95f;
		float total = 0;
		for (const Part& part : parts)
			total += Width(part.glyph, px) + Width(part.label, px) + 48.0f * m_l.scale;
		float x = std::max(m_l.safe_x, m_l.right - total);
		const float baseline = m_l.bottom - 34.0f * m_l.scale;
		for (const Part& part : parts)
		{
			x += Text(part.glyph, x, baseline, px * 1.05f, part.glyph == fe::icon::Circle ? HexColor(0xFF9AA7) : kIce, 0.15f) + 12.0f * m_l.scale;
			x += Text(part.label, x, baseline, px, kText, 0.05f) + 38.0f * m_l.scale;
		}
	}

	void StatusAndToast()
	{
		const float max_width = (m_l.right - m_l.safe_x) * 0.52f;
		float baseline = m_l.bottom - 103.0f * m_l.scale;
		if (!m_view.status.empty())
		{
			Text(m_view.status, m_l.safe_x, baseline, m_l.small, kTextDim, 0.0f, 0, max_width);
			baseline -= m_l.small * 1.4f;
		}
		if (!m_view.toast.empty())
		{
			const float w = std::min(max_width, Width(m_view.toast, m_l.small) + 44.0f * m_l.scale);
			Panel(m_l.safe_x - 12.0f * m_l.scale, baseline - m_l.small * 1.35f, w, m_l.small * 1.85f,
				HexColor(0x182233, 0.94f), 8.0f * m_l.scale, true);
			Text(m_view.toast, m_l.safe_x + 10.0f * m_l.scale, baseline, m_l.small, kAmber, 0.1f, 0, max_width);
		}
	}

	void PaintBoot()
	{
		const float cx = m_l.width * 0.5f;
		const float cy = m_l.height * 0.43f;
		Logo(cx - 145.0f * m_l.scale, cy - 70.0f * m_l.scale, 118.0f * m_l.scale, true, false);
		Text(m_view.title, cx, cy + 108.0f * m_l.scale, m_l.body * 1.08f, kTextDim, 0.1f, 1);
		Text(m_view.status.empty() ? "Loading" : m_view.status, cx, cy + 154.0f * m_l.scale, m_l.small, kIce, 0.0f, 1);
		if (!m_view.reduce_motion)
		{
			const float sweep = static_cast<float>((std::sin(m_seconds * 1.8) + 1.0) * 0.5);
			m_out.AddLine(cx - 120.0f * m_l.scale, cy + 178.0f * m_l.scale,
				cx - 120.0f * m_l.scale + 240.0f * m_l.scale * sweep, cy + 178.0f * m_l.scale, 3.0f * m_l.scale, kIce);
		}
	}

	void PaintHome()
	{
		const float x = m_l.safe_x;
		float y = ContentTop() + 20.0f * m_l.scale;
		const float w = std::min(780.0f * m_l.scale, (m_l.right - m_l.safe_x) * 0.48f);
		for (size_t i = 0; i < m_view.items.size(); i++)
		{
			const UiEntry& entry = m_view.items[i];
			const bool first = i == 0 && entry.id == "continue";
			const float h = (first ? 148.0f : 64.0f) * m_l.scale * std::min(1.25f, m_l.text / m_l.scale);
			if (first && !m_view.hero_cover_key.empty())
			{
				Panel(x, y, w, h, HexColor(0x101827, 0.96f), 8.0f * m_l.scale, true);
				const Texture2D* cover = m_scene.CoverFor(m_view.hero_cover_key, m_view.hero_title);
				ImageCover(cover, x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f, Color{0.78f, 0.86f, 0.96f, 1.0f});
				// Keep the label legible without hiding the procedural poster entirely.
				m_out.AddSolidQuad(x + 2.0f, y + 2.0f, x + w - 4.0f, y + h - 4.0f, HexColor(0x02050C, 0.18f));
				m_out.AddSolidQuad(x + 2.0f, y + 2.0f, x + w * 0.58f, y + h - 4.0f, HexColor(0x02050C, 0.34f));
				Icon(EntryIcon::Continue, x + 44.0f * m_l.scale, y + h * 0.50f, 42.0f * m_l.scale, kIce);
				Text(entry.label, x + 82.0f * m_l.scale, y + h * 0.43f, m_l.body * 1.24f, kText, 0.55f, 0,
					w - 116.0f * m_l.scale);
				Text(entry.detail, x + 82.0f * m_l.scale, y + h * 0.43f + m_l.small * 1.35f, m_l.small, kText, 0.05f, 0,
					w - 116.0f * m_l.scale);
				if (entry.focused)
					FocusFrame(x, y, w, h);
			}
			else
				Row(entry, x, y, w, h);
			y += h + 10.0f * m_l.scale;
		}
	}

	void PaintFirstRun()
	{
		const float rail_w = 500.0f * m_l.scale;
		float y = ContentTop() + 20.0f * m_l.scale;
		for (const UiEntry& step : m_view.rail)
		{
			Row(step, m_l.safe_x, y, rail_w, 92.0f * m_l.scale, false);
			y += 108.0f * m_l.scale;
		}
		const float ax = m_l.safe_x + rail_w + 100.0f * m_l.scale;
		const float aw = m_l.right - ax;
		// Faceted quartz recovery motif, deliberately vector rather than a bitmap asset.
		const float cx = ax + aw * 0.64f;
		const float cy = ContentTop() + 175.0f * m_l.scale;
		const float s = 150.0f * m_l.scale;
		m_out.AddPoly({{cx, cy - s}, {cx + s * 0.55f, cy - s * 0.45f}, {cx + s * 0.34f, cy + s * 0.55f},
			{cx - s * 0.42f, cy + s * 0.38f}, {cx - s * 0.62f, cy - s * 0.32f}}, HexColor(0x4B6C93, 0.34f));
		m_out.AddPoly({{cx, cy - s}, {cx + s * 0.55f, cy - s * 0.45f}, {cx + s * 0.10f, cy - s * 0.18f}},
			HexColor(0x9CD9F4, 0.34f));
		m_out.AddPoly({{cx - s * 0.62f, cy - s * 0.32f}, {cx + s * 0.10f, cy - s * 0.18f}, {cx - s * 0.42f, cy + s * 0.38f}},
			HexColor(0xD4F4FF, 0.24f));
		float ay = ContentTop() + 340.0f * m_l.scale;
		for (const UiEntry& action : m_view.items)
		{
			Row(action, ax, ay, std::min(590.0f * m_l.scale, aw), 78.0f * m_l.scale, false);
			ay += 92.0f * m_l.scale;
		}
	}

	void PaintLibrary()
	{
		const float rail_w = 315.0f * m_l.scale;
		float ry = ContentTop();
		for (const UiEntry& filter : m_view.rail)
		{
			Row(filter, m_l.safe_x, ry, rail_w, 66.0f * m_l.scale, false);
			ry += 76.0f * m_l.scale;
		}
		const float gx = m_l.safe_x + rail_w + 54.0f * m_l.scale;
		const float gw = m_l.right - gx;
		if (!m_view.tiles.empty())
		{
			constexpr int cols = 4;
			const float gap = 24.0f * m_l.scale;
			const float tile_w = std::min((gw - gap * (cols - 1)) / cols, 220.0f * m_l.scale);
			const float art_h = tile_w * 1.0f;
			const float tile_h = art_h + 48.0f * m_l.scale * std::min(1.7f, m_l.text / m_l.scale);
			const float available = ContentBottom() - ContentTop();
			const int rows_visible = std::max(1, static_cast<int>((available + gap) / (tile_h + gap)));
			int focused = 0;
			for (size_t i = 0; i < m_view.tiles.size(); i++)
				if (m_view.tiles[i].focused)
					focused = static_cast<int>(i);
			const int total_rows = static_cast<int>((m_view.tiles.size() + cols - 1) / cols);
			const int focus_row = focused / cols;
			const int first_row = std::max(0, std::min(focus_row - rows_visible + 1, std::max(0, total_rows - rows_visible)));
			for (int row = 0; row < rows_visible; row++)
			{
				for (int col = 0; col < cols; col++)
				{
					const int index = (first_row + row) * cols + col;
					if (index >= static_cast<int>(m_view.tiles.size()))
						continue;
					const UiTile& tile = m_view.tiles[static_cast<size_t>(index)];
					const float x = gx + col * (tile_w + gap);
					const float y = ContentTop() + row * (tile_h + gap);
					const Texture2D* cover = m_scene.CoverFor(tile.cover_key, tile.title);
					m_out.AddImage(cover, x, y, tile_w, art_h, tile.disabled ? Color{0.45f, 0.52f, 0.62f, 1.0f} : Color{});
					m_out.AddSolidQuad(x, y + art_h - 5.0f * m_l.scale, x + tile_w, y + art_h, tile.damaged ? kAmber : kIce);
					Text(tile.title, x + tile_w * 0.5f, y + art_h + 35.0f * m_l.scale, m_l.body,
						tile.disabled ? kTextDim : kText, tile.focused ? 0.25f : 0.0f, 1, tile_w);
					if (tile.damaged)
						Text("Needs attention", x + tile_w * 0.5f, y + 30.0f * m_l.scale, m_l.small, kAmber, 0.25f, 1, tile_w);
					if (tile.focused)
						FocusFrame(x - 5.0f * m_l.scale, y - 5.0f * m_l.scale, tile_w + 10.0f * m_l.scale,
							art_h + 10.0f * m_l.scale);
				}
			}
		}
		else
		{
			Panel(gx, ContentTop(), gw, ContentBottom() - ContentTop(), WithAlpha(kGraphite, 0.55f), 12.0f * m_l.scale);
			Text(m_view.status.empty() ? "No games match this view" : m_view.status, gx + 44.0f * m_l.scale,
				ContentTop() + 76.0f * m_l.scale, m_l.body * 1.15f, kText, 0.25f, 0, gw - 88.0f * m_l.scale);
			float y = ContentTop() + 132.0f * m_l.scale;
			for (const UiEntry& action : m_view.items)
			{
				Row(action, gx + 44.0f * m_l.scale, y, std::min(520.0f * m_l.scale, gw - 88.0f * m_l.scale),
					76.0f * m_l.scale);
				y += 92.0f * m_l.scale;
			}
		}
	}

	void PaintGameDetail()
	{
		const float cover_w = 390.0f * m_l.scale;
		const float cover_h = cover_w * 4.0f / 3.0f;
		const std::string key = m_view.hero_cover_key.empty() ? "proc:" + m_view.title : m_view.hero_cover_key;
		const Texture2D* cover = m_scene.CoverFor(key, m_view.hero_title.empty() ? m_view.title : m_view.hero_title);
		ImageCover(cover, m_l.safe_x, ContentTop(), cover_w, cover_h);
		m_out.AddLine(m_l.safe_x, ContentTop() + cover_h, m_l.safe_x + cover_w, ContentTop() + cover_h,
			4.0f * m_l.scale, kIce);
		const float x = m_l.safe_x + cover_w + 78.0f * m_l.scale;
		const float w = m_l.right - x;
		float y = ContentTop() + 28.0f * m_l.scale;
		for (const UiEntry& action : m_view.items)
		{
			Row(action, x, y, w, 86.0f * m_l.scale);
			y += 104.0f * m_l.scale;
		}
	}

	void PaintPause()
	{
		// Host preview stand-in for the live frame: the current fictional cover darkened behind a stable panel.
		const Texture2D* cover = m_scene.CoverFor(m_view.hero_cover_key.empty() ? "proc:" + m_view.subtitle : m_view.hero_cover_key, m_view.hero_title.empty() ? m_view.subtitle : m_view.hero_title);
		ImageCover(cover, 0, 0, static_cast<float>(m_l.width), static_cast<float>(m_l.height), Color{0.58f, 0.68f, 0.82f, 1.0f});
		m_out.AddSolidQuad(0, 0, static_cast<float>(m_l.width), static_cast<float>(m_l.height), HexColor(0x02040A, 0.32f));
		const float panel_w = static_cast<float>(m_l.width) * 0.38f;
		m_out.AddSolidQuad(0, 0, panel_w, static_cast<float>(m_l.height), HexColor(0x080B12, m_view.high_contrast ? 0.98f : 0.93f));
		m_out.AddLine(panel_w, 0, panel_w, static_cast<float>(m_l.height), 2.0f * m_l.scale, kLine);
		Logo(m_l.safe_x, m_l.safe_y - 8.0f * m_l.scale, 42.0f * m_l.scale, true, false);
		Text(m_view.title, m_l.safe_x, m_l.safe_y + 118.0f * m_l.scale, m_l.title, kText, 0.45f);
		float y = m_l.safe_y + 185.0f * m_l.scale;
		const float w = panel_w - m_l.safe_x - 44.0f * m_l.scale;
		for (const UiEntry& action : m_view.items)
		{
			Row(action, m_l.safe_x, y, w, 78.0f * m_l.scale, false);
			y += 94.0f * m_l.scale;
		}
		Prompts();
	}

	void PaintMemoryCards()
	{
		float x = m_l.safe_x;
		const float slot_w = 300.0f * m_l.scale;
		for (const UiEntry& slot : m_view.rail)
		{
			const float sy = ContentTop();
			const float sh = 108.0f * m_l.scale;
			Panel(x, sy, slot_w, sh, slot.focused ? HexColor(0x18283E, 0.98f) : WithAlpha(kGraphite, 0.88f), 8.0f * m_l.scale, true);
			Icon(slot.icon, x + 34.0f * m_l.scale, sy + 43.0f * m_l.scale, 30.0f * m_l.scale, slot.focused ? kIce : kTextDim);
			Text(slot.label, x + 66.0f * m_l.scale, sy + 40.0f * m_l.scale, m_l.body, kText, slot.focused ? 0.25f : 0.0f, 0,
				slot_w - 86.0f * m_l.scale);
			Text(slot.detail, x + 66.0f * m_l.scale, sy + 69.0f * m_l.scale, m_l.small, kTextDim, 0, 0,
				slot_w - 86.0f * m_l.scale);
			if (!slot.value.empty())
				Text(slot.value, x + 66.0f * m_l.scale, sy + 94.0f * m_l.scale, m_l.small * 0.86f, kIce, 0, 0,
					slot_w - 86.0f * m_l.scale);
			if (slot.focused)
				FocusFrame(x, sy, slot_w, sh);
			x += slot_w + 28.0f * m_l.scale;
		}
		float y = ContentTop() + 150.0f * m_l.scale;
		const float w = m_l.right - m_l.safe_x;
		for (const UiEntry& card : m_view.items)
		{
			Row(card, m_l.safe_x, y, w, 96.0f * m_l.scale);
			y += 112.0f * m_l.scale;
		}
	}

	void PaintSettings()
	{
		const float rail_w = 370.0f * m_l.scale;
		float ry = ContentTop();
		for (const UiEntry& category : m_view.rail)
		{
			Row(category, m_l.safe_x, ry, rail_w, 72.0f * m_l.scale);
			ry += 84.0f * m_l.scale;
		}
		const float x = m_l.safe_x + rail_w + 58.0f * m_l.scale;
		const float w = m_l.right - x;
		const float row_h = 78.0f * m_l.scale * std::min(1.65f, m_l.text / m_l.scale);
		const float avail = ContentBottom() - ContentTop();
		const int visible = std::max(1, static_cast<int>(avail / (row_h + 10.0f * m_l.scale)));
		int focus = 0;
		for (size_t i = 0; i < m_view.items.size(); i++)
			if (m_view.items[i].focused)
				focus = static_cast<int>(i);
		const int first = std::max(0, std::min(focus - visible + 1, std::max(0, static_cast<int>(m_view.items.size()) - visible)));
		for (int i = 0; i < visible && first + i < static_cast<int>(m_view.items.size()); i++)
			Row(m_view.items[static_cast<size_t>(first + i)], x, ContentTop() + i * (row_h + 10.0f * m_l.scale), w, row_h, false);
	}

	void PaintControls()
	{
		const float list_w = (m_l.right - m_l.safe_x) * 0.48f;
		float y = ContentTop();
		const float row_h = 76.0f * m_l.scale * std::min(1.65f, m_l.text / m_l.scale);
		const int visible = std::max(1, static_cast<int>((ContentBottom() - ContentTop()) / (row_h + 6.0f * m_l.scale)));
		int focus = 0;
		for (size_t i = 0; i < m_view.items.size(); i++)
			if (m_view.items[i].focused)
				focus = static_cast<int>(i);
		const int first = std::max(0, std::min(focus - visible + 1, std::max(0, static_cast<int>(m_view.items.size()) - visible)));
		for (int i = 0; i < visible && first + i < static_cast<int>(m_view.items.size()); i++)
		{
			Row(m_view.items[static_cast<size_t>(first + i)], m_l.safe_x, y, list_w, row_h, false);
			y += row_h + 6.0f * m_l.scale;
		}
		// Original simplified controller diagram: geometry only, no proprietary silhouette asset.
		const float cx = m_l.safe_x + list_w + (m_l.right - m_l.safe_x - list_w) * 0.52f;
		const float cy = ContentTop() + 245.0f * m_l.scale;
		const float s = 250.0f * m_l.scale;
		m_out.AddRoundedRect(cx - s * 0.52f, cy - s * 0.26f, s * 1.04f, s * 0.52f, s * 0.20f, HexColor(0x182134, 0.92f));
		m_out.AddRoundedRect(cx - s * 0.62f, cy - s * 0.08f, s * 0.34f, s * 0.56f, s * 0.16f, HexColor(0x151E30, 0.95f));
		m_out.AddRoundedRect(cx + s * 0.28f, cy - s * 0.08f, s * 0.34f, s * 0.56f, s * 0.16f, HexColor(0x151E30, 0.95f));
		m_out.AddSolidQuad(cx - s * 0.38f, cy - s * 0.04f, cx - s * 0.18f, cy + s * 0.04f, kTextDim);
		m_out.AddSolidQuad(cx - s * 0.30f, cy - s * 0.12f, cx - s * 0.26f, cy + s * 0.12f, kTextDim);
		Text(fe::icon::Triangle, cx + s * 0.28f, cy - s * 0.09f, m_l.body, HexColor(0x8DFFC8), 0.2f, 1);
		Text(fe::icon::Square, cx + s * 0.17f, cy + s * 0.02f, m_l.body, HexColor(0xF5A4C8), 0.2f, 1);
		Text(fe::icon::Circle, cx + s * 0.39f, cy + s * 0.02f, m_l.body, HexColor(0xFF9AA7), 0.2f, 1);
		Text(fe::icon::Cross, cx + s * 0.28f, cy + s * 0.14f, m_l.body, kIce, 0.2f, 1);
		if (!m_view.subtitle.empty())
			Text(m_view.subtitle, cx, cy + s * 0.55f, m_l.small, kTextDim, 0, 1, m_l.right - m_l.safe_x - list_w);
	}

	void PaintAccessibility()
	{
		const float list_w = (m_l.right - m_l.safe_x) * 0.52f;
		float y = ContentTop();
		const float a11y_row = 88.0f * m_l.scale * std::min(1.65f, m_l.text / m_l.scale);
		for (const UiEntry& item : m_view.items)
		{
			Row(item, m_l.safe_x, y, list_w, a11y_row, false);
			y += a11y_row + 16.0f * m_l.scale;
		}
		const float px = m_l.safe_x + list_w + 52.0f * m_l.scale;
		const float pw = m_l.right - px;
		Panel(px, ContentTop(), pw, 360.0f * m_l.scale, WithAlpha(kGraphite, 0.76f), 10.0f * m_l.scale);
		Text("Ready to Play", px + 46.0f * m_l.scale, ContentTop() + 102.0f * m_l.scale, m_l.body * 1.25f, kText, 0.4f, 0,
			pw - 92.0f * m_l.scale);
		FocusFrame(px + 46.0f * m_l.scale, ContentTop() + 165.0f * m_l.scale, pw - 92.0f * m_l.scale, 70.0f * m_l.scale);
		Text("Focus remains visible at every size", px + 70.0f * m_l.scale, ContentTop() + 210.0f * m_l.scale, m_l.small,
			kTextDim, 0, 0, pw - 140.0f * m_l.scale);
	}

	void PaintDiagnostics()
	{
		const float list_w = (m_l.right - m_l.safe_x) * 0.54f;
		float y = ContentTop();
		for (const UiEntry& item : m_view.items)
		{
			Row(item, m_l.safe_x, y, list_w, 82.0f * m_l.scale, false);
			y += 96.0f * m_l.scale;
		}
		const float x = m_l.safe_x + list_w + 60.0f * m_l.scale;
		const float w = m_l.right - x;
		Panel(x, ContentTop(), w, 390.0f * m_l.scale, WithAlpha(kGraphite, 0.82f), 12.0f * m_l.scale);
		Text("Companion Panel", x + w * 0.5f, ContentTop() + 88.0f * m_l.scale, m_l.body * 1.1f, kText, 0.35f, 1);
		const std::string url = m_view.subtitle.empty() ? "Unavailable" : m_view.subtitle;
		for (const std::string& line : Wrap(url, m_l.small, w - 90.0f * m_l.scale, 3))
			Text(line, x + w * 0.5f, ContentTop() + 138.0f * m_l.scale, m_l.small, kTextDim, 0, 1, w - 90.0f * m_l.scale);
		Icon(EntryIcon::Diagnostics, x + w * 0.5f, ContentTop() + 238.0f * m_l.scale, 100.0f * m_l.scale, kIce);
	}

	void PaintFolderPicker()
	{
		float y = ContentTop();
		const float w = std::min(1050.0f * m_l.scale, m_l.right - m_l.safe_x);
		const float row_h = 72.0f * m_l.scale;
		const int visible = std::max(1, static_cast<int>((ContentBottom() - y) / (row_h + 8.0f * m_l.scale)));
		int focus = 0;
		for (size_t i = 0; i < m_view.picker.rows.size(); i++)
			if (m_view.picker.rows[i].focused)
				focus = static_cast<int>(i);
		const int first = std::max(0, std::min(focus - visible + 1, std::max(0, static_cast<int>(m_view.picker.rows.size()) - visible)));
		for (int i = 0; i < visible && first + i < static_cast<int>(m_view.picker.rows.size()); i++)
		{
			const UiPickerRow& row = m_view.picker.rows[static_cast<size_t>(first + i)];
			UiEntry entry;
			entry.id = row.id;
			entry.label = row.label;
			entry.detail = row.detail;
			entry.disabled = row.disabled;
			entry.focused = row.focused;
			entry.icon = row.use_current ? EntryIcon::Action : row.parent ? EntryIcon::Folder : row.image ? EntryIcon::Game : EntryIcon::Folder;
			Row(entry, m_l.safe_x, y, w, row_h);
			y += row_h + 8.0f * m_l.scale;
		}
		if (!m_view.picker.status.empty())
			Text(m_view.picker.status, m_l.safe_x, ContentBottom() - 8.0f * m_l.scale, m_l.small, kAmber, 0, 0, w);
	}

	void Dialog()
	{
		m_out.AddSolidQuad(0, 0, static_cast<float>(m_l.width), static_cast<float>(m_l.height), kBlackout);
		const float w = 760.0f * m_l.scale;
		const float h = 330.0f * m_l.scale;
		const float x = (m_l.width - w) * 0.5f;
		const float y = (m_l.height - h) * 0.47f;
		Panel(x, y, w, h, HexColor(0x111A29, 0.99f), 12.0f * m_l.scale);
		Text(m_view.dialog.title, x + 44.0f * m_l.scale, y + 78.0f * m_l.scale, m_l.body * 1.25f,
			m_view.dialog.destructive ? kAmber : kText, 0.4f, 0, w - 88.0f * m_l.scale);
		float baseline = y + 132.0f * m_l.scale;
		for (const std::string& line : Wrap(m_view.dialog.body, m_l.small, w - 88.0f * m_l.scale, 3))
		{
			Text(line, x + 44.0f * m_l.scale, baseline, m_l.small, kTextDim);
			baseline += m_l.small * 1.35f;
		}
		const float bw = (w - 110.0f * m_l.scale) * 0.5f;
		const float by = y + h - 100.0f * m_l.scale;
		UiEntry cancel, confirm;
		cancel.label = m_view.dialog.cancel_label;
		cancel.focused = m_view.dialog.focused_option == 0;
		confirm.label = m_view.dialog.confirm_label;
		confirm.focused = m_view.dialog.focused_option == 1;
		confirm.destructive = m_view.dialog.destructive;
		Row(cancel, x + 44.0f * m_l.scale, by, bw, 64.0f * m_l.scale, false);
		Row(confirm, x + 66.0f * m_l.scale + bw, by, bw, 64.0f * m_l.scale, false);
		Prompts();
	}

	void Keyboard()
	{
		m_out.AddSolidQuad(0, 0, static_cast<float>(m_l.width), static_cast<float>(m_l.height), HexColor(0x02040A, 0.62f));
		const float w = 980.0f * m_l.scale;
		const float x = (m_l.width - w) * 0.5f;
		const float y = m_l.height * 0.18f;
		Panel(x, y, w, 620.0f * m_l.scale, HexColor(0x101827, 0.99f), 12.0f * m_l.scale);
		Text(m_view.keyboard.title, x + 42.0f * m_l.scale, y + 68.0f * m_l.scale, m_l.body * 1.15f, kText, 0.35f);
		Panel(x + 42.0f * m_l.scale, y + 92.0f * m_l.scale, w - 84.0f * m_l.scale, 64.0f * m_l.scale,
			HexColor(0x080D17, 1.0f), 7.0f * m_l.scale);
		Text(m_view.keyboard.text.empty() ? " " : m_view.keyboard.text, x + 64.0f * m_l.scale, y + 134.0f * m_l.scale,
			m_l.body, kIce, 0.1f, 0, w - 128.0f * m_l.scale);
		float ky = y + 190.0f * m_l.scale;
		for (size_t r = 0; r < m_view.keyboard.rows.size(); r++)
		{
			const auto& keys = m_view.keyboard.rows[r];
			const bool special = keys.size() <= 4;
			const float gap = 10.0f * m_l.scale;
			const float key_w = special ? (w - 84.0f * m_l.scale - gap * (keys.size() - 1)) / keys.size() :
				(w - 84.0f * m_l.scale - gap * (keys.size() - 1)) / keys.size();
			for (size_t c = 0; c < keys.size(); c++)
			{
				UiEntry entry;
				entry.label = keys[c].label;
				entry.focused = keys[c].focused;
				Row(entry, x + 42.0f * m_l.scale + c * (key_w + gap), ky, key_w, 62.0f * m_l.scale, false);
			}
			ky += 72.0f * m_l.scale;
		}
		Prompts();
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
