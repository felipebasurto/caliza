# PS4SX2 technical roadmap (AI-assisted)

> **Rebrand note (2026-11-26):** this product is now **Caliza** (repository: <https://github.com/felipebasurto/caliza>), a separate GPL-3.0-or-later product built on the PCSX2 core - no longer branded as a PCSX2 fork variant. Evidence, hashes and commit references dated before the rebrand use the old PS4SX2/PS5SX2 identity and `/data/PCSX2` paths (shown here already renamed where operational); they remain valid historical records. The data root is now `/data/caliza` and the first boot after upgrading copies the user's settings, memory cards, saves, BIOS and patches from the old folder automatically (games and texture packs are read from the old folder in place - nothing is moved or deleted). See `ps4/BRAND.md` for the identity guide.


## P0 immediate correctness/reproducibility

1. Console-verify candidate boot on target firmware/loader and preserve boot/emulog.
2. Verify direct-memory executable path on real hardware; implement runtime execution probe/failure reporting if absent.
3. Confirm dynamic import audit remains: 0 unresolved, no known PS5-only imports.
4. Hardware-test player-1 disconnect/reconnect and back-to-menu flow.
5. Fill firmware/loader matrix in `PS4_PLATFORM.md`.

## P1 platform robustness

1. Console-validate the executable-memory probe now implemented before recompiler memory use.
2. Console-validate the non-executable-memory interpreter fallback; measure its speed and confirm generated-code paths stay disabled.
3. Hardware-test opt-in USB keyboard/mouse using `/data/caliza/flags/usbkbm`; enable by default only if the console matrix proves it safe.
4. Validate the new CEMUPS4 `/net` NFSv3 path on hardware, then decide whether SMB deserves a separate credential-aware network-shares UI.
5. Console-validate the new mbedTLS cover-download path, including offline behavior and certificate failure handling.
6. Use the new audio underrun/output telemetry on hardware; honor latency settings where possible.
7. Console-validate streamed savestate loading/saving, backup fallback, memory cards, and atomic NVRAM persistence under real power-loss/crash scenarios.
8. Finish WSI lifetime hardening for multi-device aliasing; failed swapchain recreation now releases the retired Orbis swapchain.

## P0.5 brand/UX alignment (deferred to the design pass)

1. Full alignment of the frontend with `ps4/BRAND.md` section 3: game-state taxonomy (Ready / Needs startup files / Missing file / Not checked yet / May slow down), plain-language relabeling of the remaining technical rows, "Tune for this game" copy and result states, first-run teaching flow, reduce-motion option, regional confirm/cancel detection. The visual refinement pass is scheduled separately (design-owner review), then this UX alignment should be implemented against the final designs.
2. Wire-format decision for the texture-pack magic `PS5SX2TP` and frame-cap magic `PS5FCAP1` (rename breaks existing packs; keep for now).
3. Fix the RetroAchievements user-agent platform string (reports PS5 on PS4; their rules ask for accurate identity) once the correct PS4 identification format is confirmed.
4. Optional `calizaemu` GitHub organization: create in the web UI and transfer the repository (links redirect automatically).

## P2 compatibility

1. Create structured per-game override layer (`ps4_overrides.yaml` concept) after upstream GameDB load.
2. Build per-title compatibility database with exact settings, serial, region, commit hash, console model, firmware, renderer, fps evidence.
3. Test savestates, memory cards, disc change, multitap/input edge cases.
4. Audit upstream PCSX2 microVU/game fixes applicable to x86-64 and cherry-pick only focused correctness fixes.

## P3 measured performance

1. Use `ps4/tools/eeprof.py` to measure EE thread composition.
2. Validate MTVU ring/spin/batch settings on hardware with before/after metrics.
3. Investigate IOP threading only if measurements show meaningful IOP share.
4. Reduce shader compilation stutter after measuring `[shaders]`.
5. Reduce GPU/CPU copy and readback costs after `[copies]`/`[readbacks]` evidence.
6. Evaluate order-file/LTO/PGO only with controlled benchmarks; keep correctness as the constraint.

## P4 completeness/experience

1. Per-game settings UI/config persistence.
2. Better controller-driven OSD/error messages.
3. User-facing compatibility/report export without copyrighted data.
4. Release discipline: dated commit-named packages, changelog, tested-on matrix.
5. Crash hooks (`ps4_crash_*`) and startup capability report.

## Ideas explicitly deprioritized

- ARM64 dynarecs or ARM-specific ARMSX2 implementation details.
- Mali/Turnip/Android driver-pack concepts.
- Broad rewrites of EE/IOP/VU/GS/SPU2 cores.
- EE pipeline timing simulation as a near-term project.
