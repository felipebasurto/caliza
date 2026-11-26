# PS4SX2 autonomous port status

Living board for the PS4 port effort. All work is AI-assisted.

Last updated: 2026-11-26 after commit `3159626`.

## Current repository state

- Workspace: `/Users/felipe/PS4SX2`, branch `ps4-perf`.
- Original frozen baseline: `d082d64139cad10a3faaec2afaa23f0063208808`.
- Current candidate: **34 local commits** on top of the baseline.
- No push, PR, or issue was created.
- Raw evidence is local under `ps4/logs/`; logs are intentionally not committed blindly.

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
11. `cfae184` — document final overnight validation state.
12. `a989ec4` — do not mount unusable NFS shares.
13. `1476b5d` — count audio underrun events accurately.
14. `02c5b6c` — record final artifact and validation evidence.
15. `5e00eea` — validate package staging repeatably.
16. `cdeeefc` — test package validation failure modes.
17. `79bd1f2` — report guest and JIT state in crash logs.
18. `4650ca8` — isolate executable probe faults by thread.
19. `af05de9` — include guest CPU state in hang reports.
20. `fdbc7c6` — fix input release and audio failure edge cases.
21. `aa1dfbc` — keep input alive without initial pad handle.
22. `8eb0c96` — download covers through the mbedTLS HTTPS client.
23. `e5288be` — harden pad detection, WSI teardown, and memory probing.
24. `ce85c44` — stream savestate entries during load.
25. `5bfdb00` — harden package build reproducibility.
26. `8485982` — record final validation evidence and remaining risks.
27. `4598aff` — make crash hooks async-signal-safe.
28. `b128266` — stream synchronous savestate writes.
29. `9b5fb08` — integrate robustness review fixes.
30. `bb0d1ad` — arm executable probe only after sigsetjmp.
31. `8300eb8` — polish streaming savestate failure behavior.
32. `ac6ebed` — expose NFS shares through CEMUPS4 netfs.
33. `385df24` — add savestate validation tooling and PS4Mount folder integration.
34. `3159626` — harden executable memory probing and release tracking.

## Completed work

| ID | Task | State | Evidence |
|---|---|---|---|
| P0-1 | Freeze git/toolchain baseline | done | `BASELINE.md`, `ps4/logs/git-baseline.txt`, `toolchain-inventory.txt` |
| P0-BUILD | Reproduce baseline build/PKG | done | 583 objects, ELF 141M, eboot 40M, PKG 57M; `baseline-build-metrics.txt` |
| P0-HOSTTESTS | Repeatable host validation | done/pass | frame pacing 911/911, kbm, padmap, texture roots, package validator all pass |
| P0-IMPORTS | Correct dynamic import audit | done/pass | current candidate: **257 dynamic imports, 0 unresolved, 0 known PS5-only** |
| P1-JIT | Remove unsafe PS5-only JIT imports on PS4 | implemented, console-unverified | no `sceKernelJitCreate/MapSharedMemory` dynamic imports remain |
| P1-MEMPROBE | Runtime executable-memory validation | implemented, console-unverified | PS4-scoped guarded `ret` probes cover first/middle/last pages; both direct-memory permission transitions are tried; release tracking no longer uses the null shared-mapping path |
| P0-LEGAL | BIOS/package compliance | done/build-validated | no BIOS bundled; GPL/source/asset/third-party notices staged |
| P1-INPUT | Controller disconnect/stuck-input robustness | implemented, console-unverified | neutral input on disconnect, pad thread survives missing initial handle, hotplug retry once per second |
| P1-PADBYTE | PS4 pad connected-byte uncertainty | fail-safe diagnostics added | PS4 fails open until the byte is observed nonzero; first data window is logged for console verification |
| P1-KBM | USB keyboard/mouse risk posture | improved, console-unverified | off by default; `/data/PCSX2/flags/usbkbm` opts in; disconnect/read-failure release logic added |
| P1-WSI | WSI telemetry and lifetime | implemented, console-unverified | semaphore destroy/device teardown tracking; noteworthy-delta `[ps4wsi]` telemetry |
| P1-AUDIO | Audio starvation/error telemetry | implemented, console-unverified | initial fill no longer counted as underrun; output errors back off instead of spinning |
| P1-COVERS | Cover downloads | implemented, console-unverified | cover fetches now use the existing mbedTLS HTTPS client instead of PS5-only `sceHttp2*`; HTTP/SSL system-library dependencies removed |
| P1-CRASH | Crash/hang diagnostics | implemented, console-unverified | guest EE/IOP PCs/cycles/instruction words and JIT range evidence emitted through async-signal-safe fixed-buffer writes |
| P1-DEBUG | Debug allocation protocol corruption | fixed | `ps5dbgclient.cpp` allocation body now matches documented wire layout |
| P2-DOCS | Required technical docs | done, still evolving | all requested docs exist under `ps4/` |

## Current candidate artifact

Built in the Linux x86_64 OpenOrbis container after commit `3159626`:

- ELF `/tmp/ps4candidate1/build/link/pcsx2`: **147,877,864 bytes**, SHA-256 `5f9f6762de08ec2696767de33cad7432cfa6166a8a3bd9fda2b2b96964e3d73f`.
- eboot `/tmp/ps4candidate1/build/link/pcsx2_eboot/eboot.bin`: **41,004,240 bytes**, SHA-256 `adf3ef04beb18a335a0c4d364829d64bb3d42d05d68775042fa21c2882f3b608`.
- PKG `/tmp/ps4candidate1/out/IV0000-PCSX00002_00-PCSX2PS400000000.pkg`: **60,686,336 bytes**, SHA-256 `8f2303a23ed31f5f73deabaa18c5d962c8bdeafd9de53a22a1083036165ce656`.
- macOS copy: `/Users/felipe/ps4-build-work/ps4sx2-candidate-final.pkg`, same PKG SHA-256.
- Import audit: **257 dynamic imports, 0 unresolved, 0 known PS5-only imports**.
- BIOS files: none; redistributable build.
- Package notices: `COPYING.GPLv3`, `SOURCE.txt`, `ASSET-SOURCES.txt`, `FontAwesome-LICENSE.txt`, `ThirdPartyLicenses.html`, `PORT-THIRD-PARTY.txt`, `licenses/`.

## Validation ledger

| Validation | Result | Evidence |
|---|---|---|
| Baseline compile/link/package at `d082d64` | PASS | `ps4/logs/baseline-build-d082d64.log`, `baseline-build-metrics.txt` |
| Candidate compile/link/package at final documented commit | PASS | `ps4/logs/candidate31-memrelease.log` |
| Current strict dynamic import audit | PASS: 257 total, 0 unresolved, no known PS5-only | same build log |
| Package stage compliance validation | PASS: redistributable BIOS-free build, 17 required files | same build log |
| Host tests | PASS | `ps4/logs/host-tests-memrelease.log` |
| Frame-pacing host test | PASS: 911/911 | same |
| Input mapping host tests | PASS: kbm_map and pad_map | same |
| Texture-root host test | PASS | same |
| Package validator negative/positive tests | PASS | same |
| Savestate structural validator negative/positive tests | PASS | same; use `ps4/tools/validate_savestate.py` on states pulled from console |
| Real console boot | NOT RUN | no confirmed console access in this session |
| Real game compatibility/performance | NOT RUN | no confirmed console access in this session |

## Active/unresolved work

| ID | Task | State | Blocker/next step |
|---|---|---|---|
| P1-CONSOLE | Real PS4 boot/render/input/audio validation | not started | install only the current candidate through the user's existing safe flow |
| P1-FW | Firmware/loader matrix | empty | requires console evidence per firmware/loader |
| P1-INTERPRETER | Automatic fallback when no executable memory exists | absent | currently rejects memory and fails clearly; implement explicit fallback policy if needed |
| P1-NFS | NFS support on PS4 | implemented, console-unverified | plain NFSv3 `nfs://host/export` shares mount through CEMUPS4 `/net/shareN`; query options/ports unsupported; SMB API exists but is not exposed yet |
| P1-SAVESTATE | Savestate memory peak | load and synchronous save paths improved, console-unverified | load streams entries from disk; synchronous saves stream entries without materializing a second full state; threaded saves keep the snapshot contract |
| P1-HTTP | HTTPS/covers on console | implemented but unverified | test DNS, TCP, RDRAND/urandom, TLS roots, redirects, and offline behavior on hardware |
| P2-PERF | Hardware performance measurements | not started | requires console or user-run measurement protocol; ThinLTO experimental package builds but is completely unmeasured |

## Console smoke-test priority

1. Install only `/Users/felipe/ps4-build-work/ps4sx2-candidate-final.pkg` using the already existing safe flow.
2. Preserve `/data/PCSX2/boot.log`, `emulog.txt`, and `settings.log` after first boot; also check `/data/Crema/boot-trace.log` and `/data/Crema/crash.log` when present.
3. Confirm `[dbg] memmap`, `[jitprobe]`/memprobe messages, `[ps4wsi]`, `[audio]`, `[pad]`, `[threads]`, `[copies]`, and `[readbacks]`.
4. Test boot to shelf, BIOS detection, one legal user-owned game, controller disconnect/reconnect, memory card, savestate, and one minutes-long gameplay sample.
5. Test cover download both online and offline; preserve `[covers]`/`[https]` lines.
6. Test a plain NFSv3 share with `PS5SX2/NfsShares=nfs://<server>/<export>`; preserve `[boot] ... network share` lines, scan one ISO, and note throughput/stutter.
7. After a pad-combo savestate, pull the `.p2s` and run `python3 ps4/tools/validate_savestate.py --require-current-version <file>` on the host.
8. Optional USB keyboard/mouse test: create empty flag file `/data/PCSX2/flags/usbkbm`, restart, and preserve `[kbm]` lines.

## Compatibility preservation rules for this session

- No push, no PRs, no issues.
- No BIOS, games, keys, or proprietary data in repo or redistributable packages.
- No `3rdparty/` edits.
- Compatibility is sacred: no performance claim without measured evidence.
