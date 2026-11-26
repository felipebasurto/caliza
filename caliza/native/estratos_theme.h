// Caliza PS4 Estratos: approved palette and layout constants.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "draw_list.h"

namespace caliza::estratos
{
constexpr Color kBasalt = HexColor(0x080B12);
constexpr Color kGraphite = HexColor(0x141B28);
constexpr Color kGraphite2 = HexColor(0x1A2333);
constexpr Color kIndigo = HexColor(0x202957);
constexpr Color kIce = HexColor(0xA5E7FF);
constexpr Color kText = HexColor(0xEEF4FF);
constexpr Color kTextDim = HexColor(0xB4C3D8);
constexpr Color kAmber = HexColor(0xFFD092);
constexpr Color kDanger = HexColor(0xFF9AA7);
constexpr Color kLine = HexColor(0x334360, 0.88f);
constexpr Color kBlackout = HexColor(0x02040A, 0.78f);

constexpr float kSafeFraction = 0.05f;
constexpr float kBaseBodyPx = 27.0f;
constexpr float kBaseSmallPx = 23.0f;
constexpr float kBaseTitlePx = 58.0f;
constexpr float kBaseScreenTitlePx = 54.0f;
} // namespace caliza::estratos
