// Caliza Estratos: view-model to 2D composition.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/ui_model.h"
#include "draw_list.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fe
{
class Fonts;
}

namespace caliza
{
// Interface motion carried from one frame to the next: which screen is showing and since when, retargetable
// tweens for gliding focus, and the open/close moments of transient layers. Build() owns all updates.
struct MotionMemory
{
	struct Tween
	{
		float from = 0.0f, to = 0.0f;
		double start = 0.0;
		float duration = 0.0f;
		double epoch = -1.0; // screen_since when created; a new screen snaps instead of gliding across
		bool ready = false;
	};

	bool started = false;
	Screen screen = Screen::Boot;
	double screen_since = 0.0;
	int direction = 1; // +1 deeper, -1 back
	std::map<std::string, Tween> tweens;
	std::string focus_id;
	double focus_since = 0.0;
	std::string value_id;
	int value_index = -1;
	int value_direction = 0;
	double value_since = -10.0;
	std::string toast;
	double toast_since = -10.0;
	double toast_gone = -10.0;
	bool toast_leaving = false;
	bool dialog_open = false;
	double dialog_since = -10.0;
	bool keyboard_open = false;
	double keyboard_since = -10.0;
};

class EstratosScene
{
public:
	EstratosScene();
	~EstratosScene();

	// Font data is copied. The text font is Roboto and the icon font is PromptFont in the host preview;
	// console integration can provide the same open-licence faces through the existing font service.
	bool InitFonts(const uint8_t* text_font, size_t text_size, const uint8_t* icon_font, size_t icon_size);
	bool Ready() const { return m_ready; }

	const fe::Fonts& Fonts() const;
	const Texture2D* AtlasTexture() const { return &m_atlas; }
	const Texture2D* CoverFor(const std::string& key, const std::string& title);
	// Procedural atmosphere textures, generated once: a soft radial glow sprite and a nebula fog field.
	const Texture2D* GlowTexture() const { return &m_glow; }
	const Texture2D* FogTexture() const { return &m_fog; }

	DrawList Build(const UiView& view, uint32_t width, uint32_t height, double seconds);
	void BuildInto(DrawList& out, const UiView& view, uint32_t width, uint32_t height, double seconds);
	MotionMemory& Motion() { return m_motion; }

	struct BakedAtmosphere
	{
		Texture2D backdrop;   // opaque void + 3 glows + 2 back fog layers
		Texture2D front_haze; // front fog layer + readability edge shades
	};
	const BakedAtmosphere& AtmosphereFor(int look_group, bool high_contrast, uint32_t width, uint32_t height,
		float cx_frac, float cy_frac, float left_shade);

private:
	std::vector<uint8_t> m_text_font;
	std::vector<uint8_t> m_icon_font;
	fe::Fonts* m_fonts = nullptr;
	Texture2D m_atlas;
	Texture2D m_glow;
	Texture2D m_fog;
	std::map<std::string, Texture2D> m_covers;
	std::map<uint64_t, BakedAtmosphere> m_atmosphere;
	MotionMemory m_motion;
	bool m_ready = false;
};
} // namespace caliza
