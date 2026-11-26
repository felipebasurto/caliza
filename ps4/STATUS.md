# PS4SX2 autonomous port status

Living board for the PS4 port effort. All work is AI-assisted.

Last updated: 2026-11-26 after commit `ae30f34`.

## Current repository state

- Workspace: `/Users/felipe/PS4SX2`, branch `ps4-perf`.
- Original frozen baseline: `d082d64139cad10a3faaec2afaa23f0063208808`.
- Current candidate: **43 local commits** on top of the baseline.
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
30. `a5ae086` — finalize probe safety and validation evidence.
31. `8300eb8` — polish streaming savestate failure behavior.
32. `ac6ebed` — expose NFS shares through CEMUPS4 netfs.
33. `385df24` — add savestate validation tooling and PS4Mount folder integration.
34. `3159626` — harden executable memory probing and release tracking.
35. `22132cc` — record netfs and memory-hardening checkpoint evidence.
36. `af53105` — make ThinLTO experimental builds link and package.
37. `d88194e` — add console log smoke-test analyzer.
38. `51c3f70` — fail boot cleanly when VM memory initialization fails.
39. `6c5b260` — report early memory reset failure accurately.
40. `60c3a75` — add read-only console log collection helper.
41. `587d2f8` — bound network-share boot checks with parallelism.
42. `194568a` — record parallel-netfs and libc interposition evidence.
43. `ae30f34` — persist VM boot stages in boot trace.

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

Built in the Linux x86_64 OpenOrbis container after commit `ae30f34`:

- ELF `/tmp/ps4candidate1/build/link/pcsx2`: **147,974,720 bytes**, SHA-256 `77764032caff372e8fff28284106e9a0871fc40b1e7e06cc391e74b525f9bbec`.
- eboot `/tmp/ps4candidate1/build/stage/eboot.bin`: **41,020,656 bytes**, SHA-256 `34080e800fa7a34bd5a867fcf5db215568ce3da57f7655d3b9099637b5d4da2a`.
- PKG `/tmp/ps4candidate1/out/IV0000-PCSX00002_00-PCSX2PS400000000.pkg`: **60,686,336 bytes**, SHA-256 `56f6b7885ebcec93253d40420f9dec848c65107d3514dd90ac1444a8de8b7665`.
- macOS copy: `/Users/felipe/ps4-build-work/ps4sx2-candidate-final.pkg`, same PKG SHA-256.
- Import audit: **257 dynamic imports, 0 unresolved, 0 known PS5-only imports**.
- BIOS files: none; redistributable build.
- Package notices: `COPYING.GPLv3`, `SOURCE.txt`, `ASSET-SOURCES.txt`, `FontAwesome-LICENSE.txt`, `ThirdPartyLicenses.html`, `PORT-THIRD-PARTY.txt`, `licenses/`.
- ThinLTO remains a separate experimental artifact only: `/Users/felipe/ps4-build-work/ps4sx2-candidate-lto1-experimental.pkg`, SHA-256 `5001507188009b9cbfe7ca9db04ec6f2cd96b9c6579d69f139da3efceafbd4d2`.

## Validation ledger

| Validation | Result | Evidence |
|---|---|---|
| Baseline compile/link/package at `d082d64` | PASS | `ps4/logs/baseline-build-d082d64.log`, `baseline-build-metrics.txt` |
| Candidate compile/link/package at final documented commit | PASS | `ps4/logs/candidate36-boottrace.log` |
| Current strict dynamic import audit | PASS: 257 total, 0 unresolved, no known PS5-only | same build log |
| Package stage compliance validation | PASS: redistributable BIOS-free build, 17 required files | same build log |
| Host tests | PASS | `ps4/logs/host-tests-boottrace.log` |
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
| P1-NFS | NFS support on PS4 | implemented, console-unverified | plain NFSv3 shares mount through CEMUPS4 `/net/shareN`; per-share boot checks now run concurrently (maximum four workers) with deterministic result logging; failed shares stay registered for explicit retry but are excluded from automatic scans; query options/ports unsupported; SMB remains deliberately unexposed |
| P1-SAVESTATE | Savestate memory peak | load and synchronous save paths improved, console-unverified | load streams entries from disk; synchronous saves stream entries without materializing a second full state; threaded saves keep the snapshot contract |
| P1-HTTP | HTTPS/covers on console | implemented but unverified | test DNS, TCP, RDRAND/urandom, TLS roots, redirects, and offline behavior on hardware |
| P2-PERF | Hardware performance measurements | not started | requires console or user-run measurement protocol; ThinLTO experimental package builds but is completely unmeasured |

## Console smoke-test priority

1. Install only `/Users/felipe/ps4-build-work/ps4sx2-candidate-final.pkg` using the already existing safe flow.
2. Collect logs read-only with `PS4_HOST=<console-ip> bash ps4/tools/collect-console-logs.sh /tmp/ps4-logs` (or manually preserve `/data/PCSX2/boot.log`, `emulog.txt`, `settings.log`, `/data/Crema/boot-trace.log`, and `/data/Crema/crash.log`).
3. Confirm `[dbg] memmap`, `[jitprobe]`/memprobe messages, `[ps4wsi]`, `[audio]`, `[pad]`, `[threads]`, `[copies]`, and `[readbacks]`.
4. Test boot to shelf, BIOS detection, one legal user-owned game, controller disconnect/reconnect, memory card, savestate, and one minutes-long gameplay sample.
5. Test cover download both online and offline; preserve `[covers]`/`[https]` lines.
6. Test a plain NFSv3 share with `PS5SX2/NfsShares=nfs://<server>/<export>`; preserve `[boot] ... network share` lines, scan one ISO, and note throughput/stutter.
7. After a pad-combo savestate, pull the `.p2s` and run `python3 ps4/tools/validate_savestate.py --require-current-version <file>` on the host.
8. Optional USB keyboard/mouse test: create empty flag file `/data/PCSX2/flags/usbkbm`, restart, and preserve `[kbm]` lines.
9. Analyze the collected logs with `python3 ps4/tools/analyze_console_logs.py --expect-input --expect-wsi --expect-audio --expect-state --expect-gameplay /tmp/ps4-logs/*` and investigate every FAIL before changing performance settings.

## New audit evidence since the previous checkpoint

- **OpenOrbis libc/netfs interposition:** object-code inspection of `/opt/pacbrew/ps4/openorbis/lib/libc.a` shows `opendir` references public `open`, `readdir64` references public `getdents`, `__stdio_read` references wrapped `_read`/`_readv`, and even hidden `__lseek`/`getdents64` are thunks to public `lseek`/`getdents`. CEMUPS4's `--wrap` coverage therefore does reach the stdio/dirent paths PCSX2 uses in this specific libc build. This is strong static evidence, not a substitute for console playback validation.
- **Hot-order file:** exact symbol coverage in the current non-LTO candidate is 362/397. Most missing entries are stale PS5 recorder symbols (`ps5vk_record_*`) or STL hash-table instantiations whose mangled names changed when Orbis node allocators/libc++ details changed; `V_VolumeSlide::Update` is now `UpdateSlide`, and `__emutls_get_address` is absent. The order file still must not be treated as Jaguar-tuned.
- **SMB:** remains correctly unexposed because there is no credential/UI design and guest-only SMB would fail on many real servers.


## Compatibility preservation rules for this session

- No push, no PRs, no issues.
- No BIOS, games, keys, or proprietary data in repo or redistributable packages.
- No `3rdparty/` edits.
- Compatibility is sacred: no performance claim without measured evidence.
