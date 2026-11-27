// Host test for ps5/frontend/fe_modern_controls.cpp: the pnach group-name heuristic the native settings page,
// the companion web panel and the boot.log line (caliza/core/console_backend.cpp LogModernControlsForLaunch)
// all share to recognize a "Modern controls" patch or cheat group. See docs/research/modern-controls.md.
//
//   g++ -std=c++17 -I ps5/frontend ps5/coreorbis/tests/modern_controls/test_modern_controls.cpp
//       ps5/frontend/fe_modern_controls.cpp -o /tmp/a.out
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_modern_controls.h"

#include <cstdio>

namespace
{
int g_fails = 0;

void Check(bool ok, const std::string& what)
{
	std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok)
		g_fails++;
}

void TestRealWorldGroupNamesMatch()
{
	// PCSX2/pcsx2_patches pull request #703 (Sprint Car Challenge, SLES-52774).
	Check(fe::IsModernControlsGroup("Modern Control Profiles"), "\"Modern Control Profiles\" matches");
	// A community cheat for King's Field: The Ancient City (Reddit user entrigant, 36E02E91.pnach).
	Check(fe::IsModernControlsGroup("Modern analog controls"), "\"Modern analog controls\" matches");
	Check(fe::IsModernControlsGroup("modern over-the-shoulder camera controls"), "a longer descriptive name matches");
	Check(fe::IsModernControlsGroup("MODERN CONTROLS"), "matching is case-insensitive");
}

void TestUnrelatedGroupNamesDoNotMatch()
{
	Check(!fe::IsModernControlsGroup("60 FPS"), "an unrelated patch does not match");
	Check(!fe::IsModernControlsGroup("Widescreen 16:9"), "the widescreen convention does not match");
	Check(!fe::IsModernControlsGroup("Infinite HP"), "an unrelated cheat does not match");
	Check(!fe::IsModernControlsGroup("Modern texture pack"), "\"modern\" alone, without \"control\", does not match");
	Check(!fe::IsModernControlsGroup("Controller vibration fix"), "\"control\" alone, without \"modern\", does not match");
	Check(!fe::IsModernControlsGroup(""), "an empty name does not match");
}

} // namespace

int main()
{
	TestRealWorldGroupNamesMatch();
	TestUnrelatedGroupNamesDoNotMatch();
	std::printf(g_fails ? "%d FAILED\n" : "all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
