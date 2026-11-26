// Caliza PS4 Estratos: view-model to 2D composition.
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

	DrawList Build(const UiView& view, uint32_t width, uint32_t height, double seconds);

private:
	std::vector<uint8_t> m_text_font;
	std::vector<uint8_t> m_icon_font;
	fe::Fonts* m_fonts = nullptr;
	Texture2D m_atlas;
	std::map<std::string, Texture2D> m_covers;
	bool m_ready = false;
};
} // namespace caliza
