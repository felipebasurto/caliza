# PS4SX2 autonomous port status

Living board for the overnight PS4 port effort. All work is AI-assisted.

Last updated: 2026-11-26 00:34 UTC by orchestrator.

## Current repository state

- Workspace: `/Users/felipe/PS4SX2`, branch `ps4-perf`.
- Original frozen baseline: `d082d64139cad10a3faaec2afaa23f0063208808`.
- Current candidate: 10 local commits on top of the baseline, newest `6345b2c` at this documentation update.
- No push, PR, or issue was created.
- Raw evidence is local under `ps4/logs/`; logs are intentionally not meant for blind commits.

## Local checkpoint commits

1. `9c7e1ea` — harden boot imports, packaging, input, and audit baseline.
2. `fb40d34` — probe executable memory before JIT use.
3. `e90c923` — record overnight checkpoint state.
4. `3b070bf` — add repeatable validation tooling.
5. `b90d490` — fix WSI semaphore lifetime tracking.
6. `4033d6f` — fix debug allocation wire layout.
7. `797349f` — clean up direct memory on allocation failure.
8. `a29f8b9` — make USB keyboard/mouse testable without default risk.
9. `4926d25` — add audio starvation and output telemetry.
10. `6345b2c` — complete package third-party notices.

## Completed work

| ID | Task | State | Evidence |
|---|---|---|---|
| P0-1 | Freeze git/toolchain baseline | done | `BASELINE.md`, `ps4/logs/git-baseline.txt`, `toolchain-inventory.txt` |
| P0-BUILD | Reproduce baseline build/PKG | done | 583 objects, ELF 141M, eboot 40M, PKG 57M; `baseline-build-metrics.txt` |
| P0-HOSTTESTS | Repeatable host validation | done/pass | frame pacing 911/911, kbm, padmap, texture roots all pass; `host-tests-usbkbm.log` |
| P0-IMPORTS | Correct dynamic import audit | done/pass | final candidate: 260 dynamic imports, 0 unresolved, no known PS5-only imports |
| P1-JIT | Remove unsafe PS5-only JIT imports on PS4 | implemented, console-unverified | no `sceKernelJitCreate/MapSharedMemory` dynamic imports remain |
| P1-MEMPROBE | Runtime executable-memory validation | implemented, console-unverified | RW-then-mprotect and initial-RWX direct-memory paths are probed before use |
| P0-LEGAL | BIOS/package compliance | done/build-validated | no BIOS bundled; GPL/source/asset/third-party notices staged |
| P1-INPUT | Player-1 disconnect stuck-input fix | implemented, console-unverified | neutral pad state applied when disconnected |
| P1-KBM | USB keyboard/mouse risk posture | improved, console-unverified | off by default; `/data/PCSX2/flags/usbkbm` opts in; loaded-module symbol resolution preferred |
| P1-WSI | WSI telemetry and semaphore lifetime | implemented, console-unverified | destroy hook removes stale handles; `[ps4wsi]` noteworthy-delta telemetry |
| P1-AUDIO | Audio starvation/error telemetry | implemented, console-unverified | `[audio]` reports underrun grains/output errors only when nonzero |
| P1-DEBUG | Debug allocation protocol corruption | fixed | `ps5dbgclient.cpp` allocation body now matches documented wire layout |
| P2-DOCS | Required technical docs | done, still evolving | all requested docs exist under `ps4/` |

## Final candidate artifact validated locally

Built in the Linux x86_64 OpenOrbis container after the licensing changes:

- ELF `/tmp/ps4candidate1/build/link/pcsx2`: 147,760,744 bytes, SHA-256 `94b4c1b2522a5c8979c5ea11936999667737803244ec691e9370f5559b789938`.
- eboot `/tmp/ps4candidate1/build/stage/eboot.bin`: 40,987,712 bytes, SHA-256 `b71d5cb1f115e4df38fd5bd59202d653389911a66ab29f98812be984f5726789`.
- PKG `/tmp/ps4candidate1/out/IV0000-PCSX00002_00-PCSX2PS400000000.pkg`: 60,686,336 bytes, SHA-256 `b306f94022d14fa52c9bcca30d5bace6ca3c52246f145ad60c3481464fb4e783`.
- macOS copy: `/Users/felipe/ps4-build-work/ps4sx2-candidate-final.pkg`.
- Import audit: 260 dynamic imports, 0 unresolved, no known PS5-only imports.
- BIOS files: none; redistributable build.
- Package notices: `COPYING.GPLv3`, `SOURCE.txt`, `ASSET-SOURCES.txt`, `FontAwesome-LICENSE.txt`, `ThirdPartyLicenses.html`, `PORT-THIRD-PARTY.txt`, `licenses/`.

## Active/unresolved work

| ID | Task | State | Blocker/next step |
|---|---|---|---|
| P1-CONSOLE | Real PS4 boot/render/input/audio validation | not started | install only this candidate through the user's existing safe flow |
| P1-FW | Firmware/loader matrix | empty | requires console evidence per firmware/loader |
| P1-INTERPRETER | Automatic fallback when no executable memory exists | absent | currently rejects memory and fails clearly; implement explicit fallback policy if needed |
| P1-NFS | NFS support on PS4 | documented as effectively dead | port wrap layer or hide/report unsupported |
| P1-COVERS | Cover downloads | unsupported | `sceHttp2*` stubs fail; needs PS4 HTTP implementation |
| P1-SAVESTATE | Savestate memory peak | documented, not fixed | audit large-state allocation behavior before changing format/loading |
| P2-PERF | Hardware performance measurements | not started | requires console or user-run measurement protocol |

## Validation ledger

| Validation | Result | Evidence |
|---|---|---|
| Baseline compile/link/package at `d082d64` | PASS | `ps4/logs/baseline-build-d082d64.log`, `baseline-build-metrics.txt` |
| Final candidate compile/link/package | PASS | `ps4/logs/candidate9-license-validation.log` |
| Final dynamic import audit | PASS: 260 total, 0 unresolved, no known PS5-only | same |
| Final package BIOS content | PASS: no BIOS files | same |
| Final package license/source notices | PASS: staged | `license-stage-validation.txt`, `source-stamp-validation.txt` |
| Host tests | PASS | `host-tests-usbkbm.log` |
| Real console boot | NOT RUN | no confirmed console access |
| Real game compatibility/performance | NOT RUN | no confirmed console access |

## Console smoke-test priority

1. Install only `/Users/felipe/ps4-build-work/ps4sx2-candidate-final.pkg` using the already existing safe flow.
2. Preserve `/data/PCSX2/boot.log`, `emulog.txt`, and `settings.log` after first boot.
3. Confirm `[dbg] memmap`, `[jitprobe]`/memprobe messages, `[ps4wsi]`, `[audio]`, `[threads]`, `[copies]`, `[readbacks]`.
4. Test boot to shelf, BIOS detection, one legal user-owned game, controller disconnect/reconnect, memory card, savestate, and one minutes-long gameplay sample.
5. Optional USB keyboard/mouse test: create empty flag file `/data/PCSX2/flags/usbkbm`, restart, and preserve `[kbm]` lines.

## Compatibility preservation rules for this session

- No push, no PRs, no issues.
- No BIOS, games, keys, or proprietary data in repo or redistributable packages.
- No `3rdparty/` edits.
- Compatibility is sacred: no performance claim without measured evidence.
