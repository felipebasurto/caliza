# PS4 port audit (AI-assisted)

## Completed audit actions tonight

1. Captured git/toolchain/build baseline (`BASELINE.md`).
2. Reproduced pristine compile/link/PKG for commit `d082d64` in Linux x86_64 container.
3. Corrected import auditing: dynamic undefined symbols, not every weak/static undefined name.
4. Audited inherited branch `fork/fix/ps4-14.00-jit-boot` and manually integrated its intent with guards preserving PS5 behavior.
5. Hardened packaging so BIOS files are included only when `PCSX2_PS4_BIOS_DIR` is explicitly set; the silent fallback to `$PCSX2PS4_BUILD/bios` is removed.
6. Added GPL/source/asset notices to the staged package.
7. Fixed player-1 disconnected-controller input state handling.
8. Wired existing PS4 WSI health counters into the once-per-second telemetry ticker.
9. Ran host tests: frame pacing 911/911, keyboard map, pad map, texture roots all pass.
10. Rebuilt candidate package; dynamic import audit: 261 imports, 0 unresolved, no known PS5-only imports.

## Diff classification of inherited `main..ps4-perf`

| Area | Classification | Assessment |
|---|---|---|
| `ps4/Makefile.ps4`, `build-ps4.sh`, `link/` | necessary platform/build adaptation | Keep; import audit now fixed |
| `ps4/ps4_vk_wsi.cpp`, `ps4/ps4_compat.cpp` | necessary platform adaptation | Keep; WSI health now visible; concurrency/statics need future hardening |
| `ps5/coreorbis/main-boot.cpp` PS4 branches | mixed necessary adaptation plus inherited PS5 flow | Keep; PS5 jailbreak now skipped on PS4; pad disconnect fixed |
| Memory/JIT code in `pcsx2/Memory.cpp`, `common/Linux/LnxHostSys.cpp` | critical platform adaptation | Direct-memory aliasing is required for fastmem; do not regress |
| MTVU/thread pinning/settings changes | optimization/configurability | Keep but hardware measurements still absent |
| PS5-only compatibility stubs (`sceHttp2`, VideoOut2, Prospero version) | necessary boot adaptation | Functionally limited; document rather than pretend support |
| implicit BIOS-in-PKG fallback | legal/distribution hazard | Removed; explicit opt-in only |
| unmerged JIT branch | valid direction, needed manual conflict resolution | Integrated conservatively with `#ifdef ORBIS_PS4` |

## Validation evidence

- Baseline compile/link/package: `ps4/logs/baseline-build-metrics.txt`.
- Candidate rebuild: `ps4/logs/candidate1-rebuild-validation.log`.
- Candidate dynamic imports: 261; unresolved: 0; known PS5-only: none.
- Host tests: `ps4/logs/host-tests-candidate1.log`.
- Candidate package includes `COPYING.GPLv3`, `SOURCE.txt`, `ASSET-SOURCES.txt`, `FontAwesome-LICENSE.txt`; no BIOS files.

## Compatibility risk assessment of tonight's code changes

| Change | Compatibility risk | Mitigation/evidence |
|---|---|---|
| Skip PS5 jailbreak/HEN logic on PS4 | Low on PS4; none on PS5 due `#ifndef ORBIS_PS4` | Compile/link validation; logic is PS5-specific |
| Remove PS5 JIT imports on PS4 | Low; direct memory was already default | Dynamic imports confirm removal; console boot still required |
| Player-1 idle input on disconnect | Medium-low: changes behavior only when read fails/disconnected | Mirrors existing extra-player behavior; needs hardware disconnect test |
| WSI telemetry logging | Very low: read-only counters once/sec | No rendering-path mutation |
| Packaging/license changes | Low runtime impact; personal BIOS workflow requires explicit env var | Documented in build script and audit |
| Dynamic import audit | No runtime impact | Build-time only |

## Not verified

- No real PS4 console execution.
- No game, BIOS, memory-card, savestate, renderer, audio, or input runtime test.
- No performance measurement on Jaguar hardware.
- Firmware coverage unknown; direct-memory executability must be validated per loader/firmware.
