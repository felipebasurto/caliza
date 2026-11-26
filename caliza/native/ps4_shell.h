// Caliza native shell adapter: Estratos entry point and orchestrator boundary.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

struct OrbisFrontendPaths;

namespace caliza::ps4shell
{
struct ShellResult
{
	enum class Kind
	{
		Failed,
		Game,
		SystemMenu,
	};
	Kind kind = Kind::Failed;
	std::string path;
	std::string error;
	bool ui_ran = false;
};

// Runs the Estratos interface on the existing Orbis user/pad/Vulkan/font services and returns the validated
// boot selection. A Game result is a menu handoff only: main-boot still owns PCSX2 creation, BIOS execution,
// save-state policy, disc transport and the re-exec return path. Runtime pause/resume/save/load/disc commands
// are deliberately not faked by this shell while that in-game transport remains unbound.
//
// Orchestrator integration (no API imitation of the cover-flow presentation is required):
//
//   #include "caliza/native/ps4_shell.h"
//
//   std::string orbis_frontend_run(const OrbisFrontendPaths& paths, const char* build_tag, bool* ran)
//   {
//       const caliza::ps4shell::ShellResult shell = caliza::ps4shell::RunEstratos(paths, build_tag);
//       if (ran)
//           *ran = shell.ui_ran;
//       if (shell.kind == caliza::ps4shell::ShellResult::Kind::Game)
//           return shell.path;
//       if (shell.kind == caliza::ps4shell::ShellResult::Kind::SystemMenu)
//           return kOrbisSystemMenuPath;  // main-boot's existing system-menu sentinel
//       return {};      // shell.error has been logged; do not invent a boot target
//   }
//
// Build integration still has to compile caliza/core/ui_model.cpp, caliza/core/settings_schema.cpp,
// caliza/core/console_backend.cpp, caliza/core/settings_projection.cpp, caliza/core/catalog_model.cpp,
// the existing storage/data-root files, caliza/native/{draw_list,procedural_covers,estratos_scene,
// vk2d_renderer}.cpp and this file. That Makefile change belongs to the
// orchestrator and is intentionally outside this adapter's owned files.
ShellResult RunEstratos(const OrbisFrontendPaths& paths, const char* build_tag);
} // namespace caliza::ps4shell
