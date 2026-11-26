// Caliza Estratos: deterministic fictional cover art for host previews.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "draw_list.h"

#include <string>

namespace caliza
{
// Builds an original procedural poster. The key and title seed every decision, so previews are
// reproducible and no game or stock-art asset is required.
Texture2D MakeProceduralCover(const std::string& key, const std::string& title, int width = 384, int height = 512);
} // namespace caliza
