# PS4SX2 technical roadmap (AI-assisted)

## P0 immediate correctness/reproducibility

1. Console-verify candidate boot on target firmware/loader and preserve boot/emulog.
2. Verify direct-memory executable path on real hardware; implement runtime execution probe/failure reporting if absent.
3. Confirm dynamic import audit remains: 0 unresolved, no known PS5-only imports.
4. Hardware-test player-1 disconnect/reconnect and back-to-menu flow.
5. Fill firmware/loader matrix in `PS4_PLATFORM.md`.

## P1 platform robustness

1. Console-validate the executable-memory probe now implemented before recompiler memory use.
2. Add actionable failure UI/log if no executable memory source exists; consider explicit interpreter fallback.
3. Hardware-test opt-in USB keyboard/mouse using `/data/PCSX2/flags/usbkbm`; enable by default only if the console matrix proves it safe.
4. Either port the NFS file wrapping layer to PS4 or hide/report NFS as unsupported.
5. Console-validate the new mbedTLS cover-download path, including offline behavior and certificate failure handling.
6. Use the new audio underrun/output telemetry on hardware; honor latency settings where possible.
7. Console-validate streamed savestate loading and then reduce the save path's large serialized buffer if flexible memory remains tight.
8. Finish WSI lifetime hardening for multi-device aliasing and swapchain recreation assumptions.

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
