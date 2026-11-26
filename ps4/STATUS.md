# PS4SX2 autonomous port status

Living board for the PS4 port effort. All work is AI-assisted.

Last updated: 2026-11-26 after hostile GIF/USB savestate hardening, collision-safe atomic folder metadata writes, CDVD diagnostics, rumble retry correction, pause-race audio correction, and package reproducibility validation; binary artifact validated at `b3c6e4d`.

## Current repository state

- Workspace: `/Users/felipe/PS4SX2`, branch `ps4-perf`.
- Original frozen baseline: `d082d64139cad10a3faaec2afaa23f0063208808`.
- Current candidate code: **162 local commits** on top of the baseline; subsequent documentation-only checkpoint commits do not change the binary.
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
67. `56b1d1c` — record refreshed final candidate artifact.
68. `5866401` — add repeatable host validation runner.
69. `be27858` — document repeatable host validation runner.
70. `3c311de` — invalidate input caches on pad recreation.
71. `8ba9983` — harden small-file and memory-card persistence.
72. `be0a489` — detect storage failures in console log analysis.
73. `4509fe4` — correct current candidate provenance documentation.
74. `12ec337` — integrate build warning triage fixes.
75. `ce00772` — record warning-fix artifact provenance.
76. `3a52d80` — clean dead memory and harden display plane selection.
77. `9126a77` — allocate legacy overlay frame copies lazily.
78. `dff2dbd` — integrate memory-card durability review fixes.
79. `f2b9493` — collect canonical and rotated console logs.
80. `38460cc` — separate rotated logs from current analysis input.
81. `06f7b4b` — allocate large MDEC and CDVD backing lazily.
82. `06cd4e8` — record lazy memory artifact and migration audit.
83. `564a940` — detect lazy backing failures in log analysis.
84. `c7034af` — allocate big Vulkan diagnostic ring lazily.
85. `15e09c5` — record diagnostic ring candidate evidence.
86. `94fabe1` — detect diagnostic ring allocation failure in logs.
87. `f28cf60` — bound MDEC allocation retries during memory exhaustion.
88. `cb4c5cf` — isolate compile artifacts under the selected build root.
89. `9b2f703` — lazily allocate optional flash and profiler backing.
90. `73a1174` — record exact lazy backing candidate evidence.
91. `a4f1075` — bound guest-supplied DEV9 flash addresses.
92. `86264b3` — allocate folder memory-card backing lazily.
93. `e7ede47` — bound DEV9 flash staging data accesses.
94. `4aa999e` — allocate OSD composition backing lazily.
95. `08cf61b` — preserve disabled folder memory-card behavior.
96. `6a6178c` — compact SPU2 PCM cache entries.
97. `f89748e` — cap MTVU ring backing at the PS4 default.
98. `03b893e` — test compacted SPU2 cache invariants.
99. `6584ed0` — record static memory reduction candidate evidence.
100. `3554cab` — analyze MTVU ring-cap pressure evidence.
101. `bbcbff6` — validate folder memory-card geometry.
102. `6b0c621` — scope folder-card IFC validation to implemented slots.
103. `1f9baf3` — bound folder-card file-entry accesses.
104. `6899203` — record final hardened-memory candidate evidence.
105. `3336947` — make folder-card metadata traversal cycle-safe.
106. `9c2c523` — lazily allocate oversized PGIF FIFO backing.
107. `195bb94` — record lazy-PGIF final candidate evidence.
108. `d1e1566` — invalidate SPU2 decode cache on reset.
109. `04d4911` — record SPU2-reset final candidate evidence.
110. `23f86b2` — invalidate SPU2 cache after wrapped DMA writes.
111. `3129df3` — record wrapped-DMA final candidate evidence.
112. `f03bf7c` — tighten reviewed folder-card traversal hardening.
113. `752b833` — bound folder-card FAT and directory traversal.
114. `97b8010` — record bounded folder-card candidate evidence.
115. `d3fa36c` — retain folder-card data after host write failures.
116. `e899b89` — record folder write-durability candidate evidence.
117. `453c9f5` — harden memory-card bounds and write retries.
118. `114f536` — record memory-card hardening candidate evidence.
119. `d15b932` — bound file memory-card writes to card size.
120. `f8541fb` — make folder-card write retries effective.
121. `0fe505b` — record final memory-card hardening evidence.
122. `e0fa9e7` — document remaining memory-card review risks.
123. `f30bac2` — bound folder directory DAG expansion.
124. `ffcfcf4` — report folder-card write retry recovery.
125. `f54832b` — record folder DAG hardening candidate evidence.
126. `5acdbff` — record folder DAG package reproducibility.
127. `23cd341` — avoid shared folder metadata aliasing.
128. `16cfd6d` — rebuild folder metadata targets each flush.
129. `9b5707d` — make CDVD cache failure fallback direct-only.
130. `46be528` — record metadata and CDVD hardening evidence.
131. `0913698` — harden hostile savestate and memory-card inputs.
132. `cd5da76` — propagate folder-card close failure evidence.
133. `04125e1` — record hostile-input hardening artifact evidence.
134. `2bb0446` — bound remaining hostile savestate handles.
135. `1efdae5` — record final savestate hardening evidence.
136. `6991fb8` — test hostile StateWrapper length handling.
137. `72ec0be` — bound shared folder-card file chains.

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
| P1-STORAGE | NVRAM/savestate resilience | implemented, console-unverified | dirty NVRAM persists periodically from the owning CPU thread and writes atomically on PS4; unusable slot-one states fall back to their consistent `.backup`; failed cached memory-card flushes keep dirty blocks for retry/close instead of silently dropping guest saves; tiny mechacon-version files replace atomically; close/checksum and final flush failures are reported rather than silently dropped |
| P1-FAULTS | Concurrent page-fault handling | implemented, console-unverified | only same-thread handler re-entry is fatal; concurrent faults on other EE/IOP/VU/GS threads now serialize through the handler instead of killing the process |
| P1-WSI-DECIDE | WSI decision logic testability | implemented/host-tested | pure acquire/flip decision helpers extracted without behavior change; host tests pin round-robin availability, scanout reuse, fallback, and sign semantics |
| P0-LEGAL | BIOS/package compliance | done/build-validated | no BIOS bundled; GPL/source/asset/third-party notices staged |
| P1-INPUT | Controller disconnect/stuck-input robustness | implemented, console-unverified | neutral input on disconnect, pad thread survives missing initial handle, hotplug retry once per second, host-side input dedup caches invalidate when PCSX2 recreates/resets pad objects; null pad slots cannot crash reset paths |
| P1-PADBYTE | PS4 pad connected-byte uncertainty | fail-safe diagnostics added | PS4 fails open until the byte is observed nonzero; first data window is logged for console verification |
| P1-KBM | USB keyboard/mouse risk posture | improved, console-unverified | off by default; `/data/PCSX2/flags/usbkbm` opts in; disconnect/read-failure release logic added |
| P1-WSI | WSI telemetry and lifetime | implemented, console-unverified | semaphore destroy/device teardown tracking; noteworthy-delta `[ps4wsi]` telemetry; display planes now use supported-display evidence when the driver exposes it |
| P1-AUDIO | Audio starvation/error telemetry | implemented, console-unverified | initial fill no longer counted as underrun; output errors back off instead of spinning |
| P1-COVERS | Cover downloads | implemented, console-unverified | cover fetches now use the existing mbedTLS HTTPS client instead of PS5-only `sceHttp2*`; HTTP/SSL system-library dependencies removed |
| P1-CRASH | Crash/hang diagnostics | implemented, console-unverified | guest EE/IOP PCs/cycles/instruction words and JIT range evidence emitted through async-signal-safe fixed-buffer writes |
| P1-DEBUG | Debug allocation protocol corruption | fixed | `ps5dbgclient.cpp` allocation body now matches documented wire layout |
| P2-DOCS | Required technical docs | done, still evolving | all requested docs exist under `ps4/` |

## Current candidate artifact

Built in the Linux x86_64 OpenOrbis container at exactly `b3c6e4d` (includes hostile-savestate hardening with element-size-scaled container bounds, GIF packet-queue validation, USB OHCI/control-transfer/packet-queue hardening, bounded folder-card shared file chains plus collision-safe atomic metadata writes, CDVD/ISO read-failure propagation, bounded mode-2368 transfers, retry-exhaustion error completion, partial asynchronous ISO-read rejection, bounded rumble retry, and pause-race audio silence):

- ELF `/root/pcsx2ps4-build/build/link/pcsx2`: **148,251,776 bytes**, SHA-256 `17115a89470b497f8d06a4372740b80d577aab22df5960d6aae55ef580fe6e34` (`.bss` remains approximately **23.9 MiB**, down about **75.8 MiB** from the original 99,733,824-byte reference).
- eboot `/root/pcsx2ps4-build/build/link/pcsx2_eboot/eboot.bin`: **41,053,632 bytes**, SHA-256 `d2a6f7e0421afefd7a45705d3169046428b49659da235f14c03baf46299aeb6a`.
- PKG `/root/pcsx2ps4-build/out/IV0000-PCSX00002_00-PCSX2PS400000000.pkg`: **60,751,872 bytes**, SHA-256 `88d569d8a292595804c567728946a8bedf6ffebc2fe7335d69a65ff892750a60`.
- macOS copy: `/Users/felipe/ps4-build-work/ps4sx2-candidate-final.pkg`, same PKG SHA-256.
- Embedded package provenance in staged `SOURCE.txt`: `Build commit: b3c6e4dabd02`.
- Import audit: **257 dynamic imports, 0 unresolved, 0 known PS5-only imports**.
- BIOS files: none; redistributable build.
- Package notices: `COPYING.GPLv3`, `SOURCE.txt`, `ASSET-SOURCES.txt`, `FontAwesome-LICENSE.txt`, `ThirdPartyLicenses.html`, `PORT-THIRD-PARTY.txt`, `licenses/`.
- ThinLTO remains a separate stale experimental artifact only; do not treat it as the current candidate.

## Validation ledger

| Validation | Result | Evidence |
|---|---|---|
| Baseline compile/link/package at `d082d64` | PASS | `ps4/logs/baseline-build-d082d64.log`, `baseline-build-metrics.txt` |
| Candidate compile/link/package at final documented commit | PASS | container log `/tmp/ps4-package-b3c6.log` |
| Current strict dynamic import audit | PASS: 257 total, 0 unresolved, no known PS5-only | same build log |
| Package stage compliance validation | PASS: redistributable BIOS-free build, 17 required files | same build log |
| Bit-for-bit package reproducibility | PASS | repeated packaging at `1846c3b` produced identical PKG SHA-256 `e70d618f...89b15f`; the folder-DAG candidate reproduced identical PKG SHA-256 `e2c61513...16bc` in `ps4/logs/pkg-repro-folder-dag.log`, and the current candidate reproduced identical PKG SHA-256 `88d569d8...0a60` in the container reproducibility check |
| Host tests | PASS | `ps4/tools/run-host-tests.sh` passes end-to-end in `ps4/logs/host-tests-mcd-mec.log` and, after the analyzer-only storage diagnostics, `ps4/logs/host-tests-analyzer-storage.log` and `ps4/logs/host-tests-wsi-plane.log` and `ps4/logs/host-tests-lazy-overlay.log` and `ps4/logs/host-tests-mcd-review.log`, and after lazy memory backing in `ps4/logs/host-tests-memory-lazy.log`, and after diagnostic-ring splitting in `ps4/logs/host-tests-vkring.log` and after optional backing/profiler allocation in `ps4/logs/host-tests-lazy-profiler.log`, and after SPU2/MTVU memory reduction in `ps4/logs/host-tests-memory-reduction.log`, folder-card hardening in `ps4/logs/host-tests-folder-geometry.log`, lazy PGIF backing in `ps4/logs/host-tests-pgif-final.log`, SPU2 reset invalidation in `ps4/logs/host-tests-spu2-final.log`, wrapped-DMA invalidation in `ps4/logs/host-tests-spu2-wrap.log`, bounded folder-card traversal in `ps4/logs/host-tests-folder-bounded.log`, write-failure retention in `ps4/logs/host-tests-folder-writefail.log`, memory-card security hardening in `ps4/logs/host-tests-mcd-security.log`, final memory-card durability/bounds hardening in `ps4/logs/host-tests-mcd-final.log`, directory-DAG/recovery diagnostics in `ps4/logs/host-tests-folder-dag.log`, and metadata/CDVD fallback hardening in `ps4/logs/host-tests-cdvd-qa.log`, and hostile-savestate/memory-card close hardening in `ps4/logs/host-tests-cd5da76.log`, and hostile-handle hardening in `ps4/logs/host-tests-2bb0446.log`, and shared file-chain/StateWrapper regression coverage in `ps4/logs/host-tests-72ec0be.log`, and exact-size savestate/folder-metadata diagnostics plus CDVD hardening in `ps4/logs/host-tests-474b844.log`; broader older host evidence remains in `ps4/logs/host-tests-candidate45.log` |
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
| P2-PERF | Hardware performance measurements | not started | requires console or user-run measurement protocol; ThinLTO experimental package builds but is completely unmeasured; host-measured static memory footprint reduced by about 75.8 MiB of BSS from the original reference through dead worker-stack removal, lazy optional backing, SPU2 PCM cache compaction, and the PS4 MTVU backing cap; hardware performance impact remains unmeasured |

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

- **Static-memory reduction wave:** folder memory cards and OSD composition backing are lazy, DEV9 flash guest/staging indexing is bounded, the SPU2 PCM cache entry is 62 bytes with host-tested predictor-width invariants, the PS4 MTVU backing is capped at the pre-existing 2048 KiB default lap, and the oversized PGIF GP0 FIFO backing is lazy. ELF `.bss` is now 23,921,664 bytes. The MTVU cap intentionally removes larger user-selected laps on the normal PS4 candidate; document this as a memory-pressure tradeoff until hardware evidence says whether heavy-MTVU games need a separate wide-ring experiment.

- **Folder memory-card hardening:** host-controlled superblocks must now have geometry the fixed backing arrays can represent before they are treated as formatted; data-cluster mapping uses overflow-safe range checks, file reads/writes reject out-of-range clusters, and file-entry page/offset indexing is bounded. Metadata-cluster traversal is iterative and cycle-safe, direct FAT-chain indexing is bounded, and directory recursion is depth-capped. Malformed metadata is still not a complete untrusted-input proof although residual close-time metadata errors and broader folder-card behavior remain under review. Quick-access metadata targets are now rebuilt each flush rather than surviving delete/recreate retry windows. Out-of-range sector/file-card writes are bounded, upstream PS1 protocol state bounds are ported, and failed folder-card writes now retry with uncached null handles no longer poisoning recovery. Remaining review targets are residual close-time metadata/write behavior and console validation of the folder-card state machine. Host file write failures now retain dirty pages for retry instead of silently dropping them.

- **PGIF backing:** the oversized 512 KiB PS1 GPU FIFO backing is no longer static BSS. It is allocated lazily on the first actual GP0 FIFO write, retries after VM reset if allocation failed, keeps FIFO size/DREQ semantics visible before allocation, and is surfaced as a fatal analyzer condition if allocation fails.

- **SPU2 reset correctness:** resetting SPU2 RAM now invalidates every validated ADPCM decode-cache entry. This closes a stale-audio edge case where a freshly reset game could hit decode cache left by the previous session; the compacted cache layout itself was independently reviewed as lossless. Wrapped DMA writes that pass the dynamic/register region now invalidate the wrapped cacheable prefix too.

- **Hostile-savestate hardening:** fixed-size memory entries now fail the load when truncated rather than silently preserving stale tail bytes; memory-state reads reject negative and overflowing sizes; VIF partial-transfer sizes are bounded to their 4 KiB staging buffers; GIF path geometry is validated against the fixed runtime allocation before packet bytes are copied; StateWrapper and memory-state string/container lengths cannot exceed bytes actually remaining in their backing stream; HostFS handle counts are capped at the real descriptor-table limit; USB packet port indexes, packet state/token relationships, queue membership, and fixed staging-buffer sizes are validated before endpoint pointers are reconstructed. This is a fail-closed corrupted-state policy and needs hardware validation with legitimate states.
- **StateWrapper regression coverage:** the host runner now compiles the production `StateWrapper` implementation with small logging stubs and proves valid string/vector roundtrips plus rejection of exact-boundary-violating and huge attacker-controlled string/vector/deque lengths. This is stronger host evidence than compile-only review, although it still does not prove legitimate savestate compatibility on hardware.

- **File/folder memory-card follow-up:** uncached file-card erase operations now use the same fixed-card-size bound as writes, closing the remaining host-image extension path found by adversarial review. Folder-card close failures are logged and analyzed as storage-risk failures, and metadata-write state is cleared whenever quick-access metadata is discarded.

- **OpenOrbis libc/netfs interposition:** object-code inspection of `/opt/pacbrew/ps4/openorbis/lib/libc.a` shows `opendir` references public `open`, `readdir64` references public `getdents`, `__stdio_read` references wrapped `_read`/`_readv`, and even hidden `__lseek`/`getdents64` are thunks to public `lseek`/`getdents`. CEMUPS4's `--wrap` coverage therefore does reach the stdio/dirent paths PCSX2 uses in this specific libc build. This is strong static evidence, not a substitute for console playback validation.
- **Hot-order file:** exact symbol coverage in the current non-LTO candidate is 362/397. Most missing entries are stale PS5 recorder symbols (`ps5vk_record_*`) or STL hash-table instantiations whose mangled names changed when Orbis node allocators/libc++ details changed; `V_VolumeSlide::Update` is now `UpdateSlide`, and `__emutls_get_address` is absent. The order file still must not be treated as Jaguar-tuned.
- **SMB:** remains correctly unexposed because there is no credential/UI design and guest-only SMB would fail on many real servers.


## Compatibility preservation rules for this session

- No push, no PRs, no issues.
- No BIOS, games, keys, or proprietary data in repo or redistributable packages.
- No `3rdparty/` edits.
- Compatibility is sacred: no performance claim without measured evidence.
