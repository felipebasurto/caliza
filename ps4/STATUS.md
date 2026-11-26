# PS4SX2 autonomous port status

Living board for the PS4 port effort. All work is AI-assisted.

Last updated: 2026-11-26 after commit `364adfd`; binary artifact validated at that commit.

## Current repository state

- Workspace: `/Users/felipe/PS4SX2`, branch `ps4-perf`.
- Original frozen baseline: `d082d64139cad10a3faaec2afaa23f0063208808`.
- Current candidate: **66 local commits** on top of the baseline after this documentation checkpoint.
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
44. `fe5e0ea` — record final boot-trace artifact evidence.
45. `7bc8f23` — harden parallel network-share workers.
46. `a7822cb` — harden memory and fastmem initialization failure paths.
47. `f3e8984` — report persistent boot-trace milestones in log analysis.
48. `038b24f` — extract and host-test WSI present decisions.
49. `d5aa34b` — integrate memory probe and fastmem review fixes.
50. `ddc0f18` — record memory hardening validation evidence.
51. `b2332de` — make packaging deterministic and boot failures fatal in analysis.
52. `b4587fe` — record deterministic package validation evidence.
53. `852de7f` — fall back to interpreters when code memory cannot execute.
54. `3788e33` — synchronize audio pause with in-flight buffer reads.
55. `b51a05a` — fix concurrent fault handling and savestate validation contract.
56. `d4144e2` — harden controller hotplug and rumble handle reuse.
57. `9277d24` — persist dirty NVRAM during gameplay and harden VU0 cycle accounting.
58. `aa8c200` — route guest power-off requests back to the menu.
59. `cb176a9` — release retired swapchain memory when recreation fails.
60. `6b4129d` — use the PS4 title path when re-executing into the menu.
61. `8506f16` — write NVRAM atomically.
62. `a6fbbf6` — fall back to the backup savestate when slot one is unusable.
63. `3882a5f` — enforce RW-only memory in the interpreter fallback.
64. `1846c3b` — record robustness review and candidate evidence.
65. `dabaa3c` — record final candidate hashes and layout risks.
66. `364adfd` — report effective interpreter fallback in performance warnings.

## Completed work

| ID | Task | State | Evidence |
|---|---|---|---|
| P0-1 | Freeze git/toolchain baseline | done | `BASELINE.md`, `ps4/logs/git-baseline.txt`, `toolchain-inventory.txt` |
| P0-BUILD | Reproduce baseline build/PKG | done | 583 objects, ELF 141M, eboot 40M, PKG 57M; `baseline-build-metrics.txt` |
| P0-HOSTTESTS | Repeatable host validation | done/pass | frame pacing 911/911, kbm, padmap, texture roots, package validator all pass |
| P0-IMPORTS | Correct dynamic import audit | done/pass | current candidate: **257 dynamic imports, 0 unresolved, 0 known PS5-only** |
| P1-JIT | Remove unsafe PS5-only JIT imports on PS4 | implemented, console-unverified | no `sceKernelJitCreate/MapSharedMemory` dynamic imports remain |
| P1-MEMPROBE | Runtime executable-memory validation | implemented, console-unverified | PS4-scoped guarded `ret` probes cover first/middle/last pages; both direct-memory permission transitions are tried; release tracking no longer uses the null shared-mapping path; probe-specific signal state is thread-local |
| P1-FASTMEM | Fastmem allocation degradation | implemented, console-unverified | `CHECK_FASTMEM` now includes runtime fastmem availability, so Orbis reservation failure degrades to non-fastmem recompiler paths instead of leaving empty lookup state; the page-fault handler is installed unconditionally for slowmem SMC protection, partial setup rolls back, and desktop fatal-on-reservation-failure behavior is preserved |
| P1-BOOTFAIL | Memory initialization failure behavior | implemented, console-unverified | CPU/memory initialization and pre-reset failure now log persistent boot-trace milestones and terminate the boot cleanly instead of continuing with invalid memory |
| P1-INTERP | Non-executable-memory resilience | implemented, console-unverified | PS4 keeps a validated RW direct mapping as a non-executable fallback, forces EE/IOP/VU interpreters, disables fastmem and generated VIF/scanline code at runtime truth level, and continues boot rather than dying before UI/BIOS diagnostics |
| P1-STORAGE | NVRAM/savestate resilience | implemented, console-unverified | dirty NVRAM persists periodically from the owning CPU thread and writes atomically on PS4; unusable slot-one states fall back to their consistent `.backup`; failed cached memory-card flushes keep dirty blocks for retry/close instead of silently dropping guest saves; tiny mechacon-version files replace atomically |
| P1-FAULTS | Concurrent page-fault handling | implemented, console-unverified | only same-thread handler re-entry is fatal; concurrent faults on other EE/IOP/VU/GS threads now serialize through the handler instead of killing the process |
| P1-WSI-DECIDE | WSI decision logic testability | implemented/host-tested | pure acquire/flip decision helpers extracted without behavior change; host tests pin round-robin availability, scanout reuse, fallback, and sign semantics |
| P0-LEGAL | BIOS/package compliance | done/build-validated | no BIOS bundled; GPL/source/asset/third-party notices staged |
| P1-INPUT | Controller disconnect/stuck-input robustness | implemented, console-unverified | neutral input on disconnect, pad thread survives missing initial handle, hotplug retry once per second, host-side input dedup caches invalidate when PCSX2 recreates/resets pad objects |
| P1-PADBYTE | PS4 pad connected-byte uncertainty | fail-safe diagnostics added | PS4 fails open until the byte is observed nonzero; first data window is logged for console verification |
| P1-KBM | USB keyboard/mouse risk posture | improved, console-unverified | off by default; `/data/PCSX2/flags/usbkbm` opts in; disconnect/read-failure release logic added |
| P1-WSI | WSI telemetry and lifetime | implemented, console-unverified | semaphore destroy/device teardown tracking; noteworthy-delta `[ps4wsi]` telemetry |
| P1-AUDIO | Audio starvation/error telemetry | implemented, console-unverified | initial fill no longer counted as underrun; output errors back off instead of spinning |
| P1-COVERS | Cover downloads | implemented, console-unverified | cover fetches now use the existing mbedTLS HTTPS client instead of PS5-only `sceHttp2*`; HTTP/SSL system-library dependencies removed |
| P1-CRASH | Crash/hang diagnostics | implemented, console-unverified | guest EE/IOP PCs/cycles/instruction words and JIT range evidence emitted through async-signal-safe fixed-buffer writes |
| P1-DEBUG | Debug allocation protocol corruption | fixed | `ps5dbgclient.cpp` allocation body now matches documented wire layout |
| P2-DOCS | Required technical docs | done, still evolving | all requested docs exist under `ps4/` |

## Current candidate artifact

Built in the Linux x86_64 OpenOrbis container at the current uncommitted storage-durability fix (includes the RW-only fallback invariant from `3882a5f`):

- ELF `/tmp/ps4candidate1/build/link/pcsx2`: **148,110,480 bytes**, SHA-256 `9e91b5746e051d10227c17f573bd08d374e83255dcd3ece22d13447ab05e9a09`.
- eboot `/tmp/ps4candidate1/build/link/pcsx2_eboot/eboot.bin`: **41,037,120 bytes**, SHA-256 `f022ab09c227e7f697b67d18c72385b75c065188687f699fb0274965ce0fff96`.
- PKG `/tmp/ps4candidate1/out/IV0000-PCSX00002_00-PCSX2PS400000000.pkg`: **60,751,872 bytes**, SHA-256 `6d0229b5bd6cc2193646865ac210e7726715e1af8ed96508e0fad8d806656f36`.
- macOS copy: `/Users/felipe/ps4-build-work/ps4sx2-candidate-final.pkg`, same PKG SHA-256.
- Import audit: **257 dynamic imports, 0 unresolved, 0 known PS5-only imports**.
- BIOS files: none; redistributable build.
- Package notices: `COPYING.GPLv3`, `SOURCE.txt`, `ASSET-SOURCES.txt`, `FontAwesome-LICENSE.txt`, `ThirdPartyLicenses.html`, `PORT-THIRD-PARTY.txt`, `licenses/`.
- ThinLTO remains a separate experimental artifact only: `/Users/felipe/ps4-build-work/ps4sx2-candidate-lto1-experimental.pkg`, SHA-256 `5001507188009b9cbfe7ca9db04ec6f2cd96b9c6579d69f139da3efceafbd4d2`.

## Validation ledger

| Validation | Result | Evidence |
|---|---|---|
| Baseline compile/link/package at `d082d64` | PASS | `ps4/logs/baseline-build-d082d64.log`, `baseline-build-metrics.txt` |
| Candidate compile/link/package at final documented commit | PASS | `ps4/logs/candidate71-mcd-mec.log` |
| Current strict dynamic import audit | PASS: 257 total, 0 unresolved, no known PS5-only | same build log |
| Package stage compliance validation | PASS: redistributable BIOS-free build, 17 required files | same build log |
| Bit-for-bit package reproducibility | PASS | repeated packaging at `1846c3b` produced identical PKG SHA-256 `e70d618f...89b15f`; the current storage-durability package is `candidate71` evidence |
| Host tests | PASS | `ps4/tools/run-host-tests.sh` passes end-to-end in `ps4/logs/host-tests-mcd-mec.log`; broader older host evidence remains in `ps4/logs/host-tests-candidate45.log` |
| Frame-pacing host test | PASS: 911/911 | same |
| Input mapping host tests | PASS: kbm_map and pad_map | same |
| Texture-root host test | PASS | same |
| WSI decision host test | PASS | round-robin/image availability, scanout reuse, FIFO/immediate mode, GPU-flip fallback, and final-flip sign semantics |
| Package validator negative/positive tests | PASS | same |
| Savestate structural validator negative/positive tests | PASS | same; use `ps4/tools/validate_savestate.py` on states pulled from console |
| Real console boot | NOT RUN | no confirmed console access in this session |
| Real game compatibility/performance | NOT RUN | no confirmed console access in this session |

## Active/unresolved work

| ID | Task | State | Blocker/next step |
|---|---|---|---|
| P1-CONSOLE | Real PS4 boot/render/input/audio validation | not started | install only the current candidate through the user's existing safe flow |
| P1-FW | Firmware/loader matrix | empty | requires console evidence per firmware/loader |
| P1-INTERPRETER | Automatic fallback when no executable memory exists | implemented, console-unverified | validate on hardware; expected severe performance cost but boot/UI diagnostics should survive |
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

- **Memory/fastmem hardening:** repeated Orbis Reset/CPU allocation is now idempotent for the memory map, IOP LUT, and VTLB core; fastmem availability is a runtime predicate rather than a configuration-only assumption; Orbis fastmem reservation failure therefore degrades coherently instead of leaving an empty reservation in use, while the fault handler required by slowmem SMC protection is still installed. This is compile/link/package validated only and still requires hardware fault-injection or real console evidence.
- **Boot failure contract:** `CPUThreadInitialize` and pre-initialize `SysMemory::Reset` failures now terminate the boot cleanly after persistent boot-trace milestones. The analyzer reports the latest persistent `[pcsx2]` milestone and warns when it is a failure milestone.
- **Memory-probe review integration:** the production probe no longer faults inside libc `memcpy`, disarms before handler restoration, and the PS4 fallback tracks the exact successful in/out mapping pointer rather than relying on failed kernel calls to preserve hints. The diagnostic memprobe now uses PS4 direct-memory type 0, fails closed if its guard cannot install, confines longjmp to the probing thread, chains unrelated faults, and releases JIT/alias test reservations.
- **WSI decision extraction:** pure helpers now pin the existing acquire/reuse and flip-fallback semantics. No behavior change is intended; the extraction is host-tested and the PS4 package still builds with 257 dynamic imports, 0 unresolved, and no known PS5-only imports.
- **Interpreter resilience:** adversarial review of commit `852de7f` found the runtime memory-truth gating sound: settings cannot re-enable JIT when code memory is nonexecutable, generated VIF/scanline paths are forced to C, and ordering selects interpreters before use. One residual VU0 raw-config check was fixed in `9277d24`; the retained fallback mapping is explicitly demoted back to RW in `3882a5f` so the non-executable invariant is real rather than nominal.
- **Storage/input/WSI review integration:** savestate validation now matches loader-required entries (`GS.bin` required, `USB.bin` optional); extra controllers retry hotplug; rumble state resets on handle changes; guest power-off routes back to menu; retired swapchains are destroyed on failed recreation; NVRAM persistence is periodic and atomic; backup savestates are reachable if slot one is missing or unusable.

- **OpenOrbis libc/netfs interposition:** object-code inspection of `/opt/pacbrew/ps4/openorbis/lib/libc.a` shows `opendir` references public `open`, `readdir64` references public `getdents`, `__stdio_read` references wrapped `_read`/`_readv`, and even hidden `__lseek`/`getdents64` are thunks to public `lseek`/`getdents`. CEMUPS4's `--wrap` coverage therefore does reach the stdio/dirent paths PCSX2 uses in this specific libc build. This is strong static evidence, not a substitute for console playback validation.
- **Hot-order file:** exact symbol coverage in the current non-LTO candidate is 362/397. Most missing entries are stale PS5 recorder symbols (`ps5vk_record_*`) or STL hash-table instantiations whose mangled names changed when Orbis node allocators/libc++ details changed; `V_VolumeSlide::Update` is now `UpdateSlide`, and `__emutls_get_address` is absent. The order file still must not be treated as Jaguar-tuned.
- **SMB:** remains correctly unexposed because there is no credential/UI design and guest-only SMB would fail on many real servers.


## Compatibility preservation rules for this session

- No push, no PRs, no issues.
- No BIOS, games, keys, or proprietary data in repo or redistributable packages.
- No `3rdparty/` edits.
- Compatibility is sacred: no performance claim without measured evidence.
