# Changelog

All notable changes to Caliza are documented in this file.

Caliza is free software under the GNU General Public License, version 3 or later (`COPYING.GPLv3`), based on modified PCSX2 code. Caliza does not include BIOS files, firmware, decryption keys, or games.

## [0.4.0] - 2026-11-27

> Alpha build tested on host only; on-console PS4 testing is pending.

### In-game pause menu

- **Native pause menu overlay (#13):** Pause a running session at any time without exiting. Inside the pause menu, move with the D-pad or left stick to resume play, save or load across **10 save slots** (pick a slot with Left/Right or L1/R1, with automatic backup fallback on load), swap discs in multi-disc titles, or return to the library (with Cancel selected first to prevent accidental exits). Controller input stays isolated from the running title while the menu is open.
- **How to open it and first-launch trigger choice (#13):** Press and release **L3 + R3** on their own to open or close the pause menu (`L3 + R3 + D-pad` quick-save, quick-load, and disc-swap shortcuts still work while held). On first launch—or when upgrading from an older Share/Create binding—Caliza prompts you to pick one of six trigger presets (`L3 + R3`, `Touchpad click`, `L3 + R3 + L1`, `Hold Touchpad (0.5 s)`, `L1 + R1 + Touchpad`, or `Hold Options (0.5 s)`), with `L3 + R3` pre-selected. You can change your choice anytime in **Controls** or the companion web panel.
- **No clash with GoldHEN's Share menu (#13):** All six pause triggers avoid Share/Create (used by GoldHEN's cheat menu and the PS4 Share menu) and the PS button.
- **Touchpad = PS2 Select warning (#13):** Because Touchpad acts as PS2 Select, choosing `Touchpad click` warns that PS2 Select must be mapped to another button for titles that use it, and offers a one-step option to map R3 to Select.

### Interface and library

- **Modern controls category per title (#12):** When a title's `patches/` or `cheats/` folder contains a modern-controls patch or cheat group, Caliza displays it under its own **Modern controls** category (off by default) in both Estratos and the companion web panel instead of mixing it in with other patches or cheats. Titles without such a patch do not show the category. Caliza does not bundle or download these patches; `boot.log` records whether a matching patch was applied, available but off, or absent on launch.

## [0.3.0] - 2026-11-27

> Host-tested build; on-console PS4 testing is pending.

### Performance and stability

- **Jaguar scheduling, fastmem hardening, and UI frame times (#7):** Enabled Jaguar-aware CPU core pinning by default so EE, VU1, GS, and background helper threads avoid contending on the same cores. Added MTVU wait backoff, fastmem reservation and `mprotect` hardening, faster EE self-modifying-code checks, per-game VU clamp override wiring, sequential CHD read buffering, hot-loop logging cleanup, and Estratos rendering optimizations.
- **CPU, VIF, and MTVU fast paths with diagnostics and off-switches (#9):** Enabled the x86 SSE/AVX VIF unpack recompiler at runtime when code memory is executable, removed redundant COP0 event checks, reduced VIF1 dispatch overhead, and skipped redundant VU1 microprogram clears. Added startup status lines and periodic counters in `/data/caliza/logs/boot.log`, with per-feature off-switch files under `/data/caliza/flags/` (including `novifrec`, `nopin`, `noeeevent`, `novif1fast`, and `nompgskip`) so any optimization can be disabled individually without rebuilding.
- **Accuracy-preserving CPU, VU1, GS, and I/O optimizations (#10):** Adopted ten accuracy-preserving optimizations and stability fixes: branchless `LZCNT` for `recPLZCW`, constant-address `recVTLB` stack-traffic reduction, per-block VU1 memory-base register caching, 3-operand AVX FMAC emission in microVU, skipping unused 64-bit CLUT expansion, single-pass vertex-trace deduplication for Gouraud triangle strips, `GetThreadId` cache invalidation on VM reset and save-state load, GS reset/surface safety guards, upstream CPU/VU/FPU edge-case fixes (`LWR`, `SQRT.S`/`RSQRT.S`, `CTC2`, `FBRST`), and USB OHCI / DEV9 ICMP hang guards. Every change logs its status in `boot.log` and can be turned off via `/data/caliza/flags/` (`noplzcw`, `rec_vtlb_pushpop`, `novumemreg`, `novu3op`, `noclutskip`, `nogsvtonce`, `notidreset`, `nogsresetflush`, `noswextbufguard`, `nomipinv`, `noupstreamcpufixes`, `noiohangguard`).

### Interface and library

- **Per-patch and per-cheat toggles in Estratos (#8):** Per-game settings now show individual on/off toggles for each downloaded patch and cheat group directly in Estratos, staying in sync with the companion web panel and automatically keeping cheat support enabled when a cheat is toggled.
- **Patches and cheats folder fix (#8):** Fixed downloaded patch and cheat files being written into `/data/caliza/` instead of `/data/caliza/patches/` and `/data/caliza/cheats/`, with automatic relocation of previously misplaced files when unambiguous.
- **Simpler default FPS counter (#6):** The default on-screen Info box now displays a clean, outlined FPS number in the top-right corner without a background box. The detailed EE/GS/VU load and resolution readout remains available under **FPS + load**.
- **Cover art display and download diagnostics (#11):** Fixed covers never appearing in the library. Catalog scans now resolve each game's cover path (manual, beside-disc, USB `covers/`, download cache, and OPL `ART`), and Estratos now decodes and displays real cover files before falling back to procedural placeholders. Added per-fetch lines and a scan-end summary (`found`, `downloaded`, `failed`, `missing`) to `boot.log`.

## [0.2.0] - 2026-11-27

- First public release under the Caliza name with the Estratos 2D Vulkan interface, `/data/caliza/` storage layout with resumable first-run migration, 112 global and per-game settings, memory card and save-state management, accessibility controls, and LAN HTTPS companion web panel.
- Added startup hardening for malformed library folders and async-signal-safe crash logging.

## [0.1.0] - 2026-11-26

- Initial pre-release PS4 package with OpenOrbis build, dynamic import auditing, BIOS-free package validation, and first-run data migration.
