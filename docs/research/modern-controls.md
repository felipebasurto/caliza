# "Modern controls" per game: how other PS2 emulators do it, and what Caliza does

Felipe asked for per-game "modern controls" — remapping an old PS2 game's tank controls or camera-on-face-buttons
scheme to a modern dual-stick layout — modeled on how ARMSX2 is believed to handle it. This document records what
was actually found in ARMSX2, upstream PCSX2 and the wider community patch ecosystem, with sources and licenses,
and the design this led to in Caliza (see `caliza/core/console_backend.cpp` `BuildPatchAndCheatRows` and
`LogModernControlsForLaunch`, `ps5/frontend/fe_modern_controls.*`, and `ps5/frontend/fe_web.cpp`).

## 1. Repositories and pages checked

| Source | What it is | License |
| --- | --- | --- |
| [`ARMSX2/ARMSX2`](https://github.com/ARMSX2/ARMSX2) | PCSX2 fork targeting ARM64 (Android, desktop, handhelds) with native JIT recompilers | GPL-3.0 |
| [`PCSX2/pcsx2`](https://github.com/PCSX2/pcsx2) | Upstream PCSX2 (`pcsx2/Patch.cpp`, `pcsx2/GameDB/GameIndex.yaml`) | GPL-3.0+ |
| [`PCSX2/pcsx2_patches`](https://github.com/PCSX2/pcsx2_patches) | The official community-maintained pnach patch collection PCSX2 and its forks (including ARMSX2) fetch online | none declared (all rights reserved by default) |
| [`Gabominated/PCSX2`](https://github.com/Gabominated/PCSX2) | A second community pnach collection, already used by Caliza's existing online-patch downloader | none declared |
| [`xs1l3n7x/pcsx2_cheats_collection`](https://github.com/xs1l3n7x/pcsx2_cheats_collection) | A community cheat (not patch) pnach collection, already used by Caliza's existing online-patch downloader | none declared |
| [`moonlessformless/ps2_cam_acolyte`](https://github.com/moonlessformless/ps2_cam_acolyte) | A Windows PC tool that drives Silent Hill Origins' "Modern Controls" mode over PCSX2's PINE/IPC interface, reading a second controller on the PC | not evaluated further (see §4; architecturally inapplicable) |

License status was read from each repository's GitHub API `license` field (`gh api repos/<owner>/<repo>`), not
guessed from a README.

## 2. How ARMSX2 actually does it

ARMSX2 is a PCSX2 fork, not a separate emulator core. Reading its `pcsx2/Patch.cpp`, `pcsx2/CDVD/` serial/CRC
identification, and its Settings > Patches / Settings > Cheats UI confirms it uses **exactly the same mechanism as
upstream PCSX2**:

- A patch or cheat is an ordinary `.pnach` text file, named `<serial>_<crc>.pnach` (patches) or kept in a
  separate cheats folder, containing one or more `[group name]` sections of `patch=`/`dpatch=` lines (word-level
  EE/IOP memory writes, or "extended" ASM injection for anything more complex than a constant write).
- ARMSX2's Settings > Patches tab lets the user browse and enable patches from an online source (the same
  `PCSX2/pcsx2_patches`-style repositories) or load their own file, per game, exactly like upstream PCSX2's
  patch browser.
- There is **no separate "modern controls" file format, plugin, or runtime input-remap layer** anywhere in
  ARMSX2. A "modern controls" patch is just a pnach group like any other, whose *name* happens to say what it
  does.

This matches upstream PCSX2 itself: `pcsx2/Patch.cpp` only special-cases two exact group names end-to-end
(`WS_PATCH_NAME = "Widescreen 16:9"` and `NI_PATCH_NAME = "No-Interlacing"`, each driving a *global* auto-enable
toggle), and otherwise treats every group as an opaque list of memory patches. There is no
`MC_PATCH_NAME`/"Modern Control" equivalent convention in upstream PCSX2 or ARMSX2 to match by exact name.

**Conclusion:** ARMSX2 needs no runtime input layer beyond pnach patches. Caliza therefore implements this
entirely on top of the patch/cheat mechanism it already has (PR #8's per-game patch/cheat toggles), with no new
patch format and no new runtime input-remap code.

## 3. Real "modern controls" pnach groups that exist today

Three concrete examples were found, each a plain pnach group whose author named it to describe a dual-stick/analog
remap:

1. **[PCSX2/pcsx2_patches pull request #703](https://github.com/PCSX2/pcsx2_patches/pull/703)** — Sprint Car
   Challenge (`SLES-52774`). Adds a `[Modern Control Profiles]` group using static `patch=0,EE,<addr>,extended,<value>`
   ASM patches that remap which physical buttons drive which in-game action. This is a *static button remap*, not
   true continuous analog-stick injection: it changes which digital input fires which action, not how an analog
   stick value is read.
2. **King's Field: The Ancient City**, a community cheat (`36E02E91.pnach`, named `"Modern analog controls"`)
   shared by Reddit user `entrigant`, using static word patches to remap the camera/movement scheme to a stick-
   based one.
3. **Silent Hill Origins' "Modern Controls"**, via `moonlessformless/ps2_cam_acolyte` — this is **not** a pnach
   patch at all. It is a standalone Windows program that connects to a running PCSX2 over PCSX2's PINE/IPC
   interface and a *second* physical controller plugged into the PC, translating that controller's input into
   live memory writes for camera control. It requires a separate PC running alongside the console and has no
   console-only equivalent; it does not fit a standalone PS4 build and was not adopted.

The naming pattern that emerges — a patch or cheat group whose name contains both "modern" and "control(s)" — is
the only concrete, observed convention across the ecosystem. It is a naming heuristic over existing files, not a
file format of its own.

## 4. Licensing conclusion

Every source that has actually produced a "modern controls" pnach file (`PCSX2/pcsx2_patches`, and by extension
anything shared the same way on Reddit/forums) has **no declared license**, which defaults to all rights reserved
and is **not compatible with bundling into Caliza's GPLv3 codebase**. `Gabominated/PCSX2` and
`xs1l3n7x/pcsx2_cheats_collection` (Caliza's two other existing online-patch/cheat sources, from PR #8) are the
same: no declared license. Only `ARMSX2` itself is GPL-3.0, and ARMSX2 contains no modern-controls *content* to
take — only the same mechanism Caliza already has.

Per the project's licensing rule, Caliza does **not** bundle or add a new auto-download source for
modern-controls content. Caliza's **existing** online-patch downloader (`fe_patchdl.cpp`, from before this
change) already fetches from the three unlicensed sources above for ordinary patches/cheats; that is pre-existing
behavior this change does not alter or extend. What this change adds is a name-based *recognition* layer: if a
user's existing patches/cheats folder (whether populated by that downloader or by hand) already contains a group
whose name matches the observed "modern controls" convention, Caliza surfaces it under its own "Modern controls"
toggle category instead of lumping it in with every other patch or cheat. A game with no such group gets no such
category — there is nothing bundled, and nothing is implied to exist for a game where it doesn't.

## 5. What this means for which games get "modern controls" in Caliza

None, out of the box. Caliza ships no modern-controls content. A game shows a "Modern controls" category only
when its `patches/<serial>_<crc>.pnach` or `cheats/<serial>_<crc>.pnach` file (supplied by the user, or fetched by
the existing online-patch service if a matching upstream file happens to exist and to be named accordingly)
contains a group whose name matches case-insensitively on both "modern" and "control" — the same substrings seen
in every real example above (`fe::IsModernControlsGroup`, `ps5/frontend/fe_modern_controls.cpp`). See
`ps4/docs/MODERN-CONTROLS.md` for user-facing instructions on supplying such a file.
