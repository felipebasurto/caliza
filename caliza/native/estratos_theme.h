// Caliza Estratos: palette and layout constants.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "draw_list.h"

namespace caliza::estratos
{
// Deep-space indigo: a near-black void lit by a blue nebula, glassy lavender geometry and cold focus light.
constexpr Color kVoid = HexColor(0x03040E);
constexpr Color kVoidTop = HexColor(0x070826);
constexpr Color kDeep = HexColor(0x0C1040);
constexpr Color kNebula = HexColor(0x3340D0);
constexpr Color kViolet = HexColor(0x5A34B8);
constexpr Color kGlass = HexColor(0xA9B0FF);
constexpr Color kGlassEdge = HexColor(0xD4D9FF);
constexpr Color kIce = HexColor(0xA5E7FF);
constexpr Color kFocus = HexColor(0x6F95FF);
constexpr Color kText = HexColor(0xF2F5FF);
constexpr Color kTextDim = HexColor(0xA3ACDA);
constexpr Color kTextFaint = HexColor(0x6D76AA);
constexpr Color kAmber = HexColor(0xFFD092);
constexpr Color kDanger = HexColor(0xFF8F9E);
constexpr Color kLine = HexColor(0x8C9BFF, 0.30f);
constexpr Color kPlate = HexColor(0x060823, 0.72f);
constexpr Color kBlackout = HexColor(0x01020A, 0.72f);

// Face-button colours used by the button prompts.
constexpr Color kCrossBlue = HexColor(0x8FB4FF);
constexpr Color kCircleRed = HexColor(0xFF7A8C);
constexpr Color kTriangleGreen = HexColor(0x6EE6B0);
constexpr Color kSquarePink = HexColor(0xF59AD6);

constexpr float kSafeFraction = 0.05f;
constexpr float kBaseBodyPx = 28.0f;
constexpr float kBaseSmallPx = 23.0f;
constexpr float kBaseTitlePx = 58.0f;
constexpr float kBaseScreenTitlePx = 52.0f;
} // namespace caliza::estratos
