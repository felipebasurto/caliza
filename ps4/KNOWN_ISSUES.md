# Known issues and risks (AI-assisted)

## Critical/platform

1. **Baseline ELF imports PS5-only JIT shared-memory symbols.** `sceKernelJitCreateSharedMemory` and `sceKernelJitMapSharedMemory` appear as dynamic imports even though direct memory is the default. Candidate removes those references under `ORBIS_PS4`; verify with dynamic import audit.
2. **Executable memory is firmware/loader dependent.** Direct memory mapped/mprotected executable is the chosen path. It must be proven on each supported firmware/homebrew loader. If execution fails but RW memory exists, the PS4 candidate now falls back to interpreters; expect a severe performance cost and validate boot/UI behavior on hardware.
3. **OpenOrbis stubs can overstate available symbols.** Export-index checks are necessary but insufficient; keep an explicit PS5-only denylist.

## Renderer/GPU

1. WSI semaphore destruction is now tracked for the normal single-device flow, but the tracking sets are not keyed by `VkDevice`; concurrent live devices could theoretically alias handles.
2. One global VideoOut/device/queue/swapchain state assumes sequential frontend/GS use, not concurrent independent swapchains.
3. Acquire can wait indefinitely if VideoOut flip status stops reporting valid state.
4. CPU fallback present path serializes with `vkQueueWaitIdle`; new `[ps4wsi]` telemetry should expose it.
5. Fixed 1920x1080 60 Hz mode only; no 120 Hz frame-generation display mode on PS4.

## Input/audio/filesystem

1. Player-1 disconnect stuck-input handling and extra-controller hotplug retry are improved, but the PS4 pad connected-byte layout is unverified; the candidate fails open and logs the byte window for first console validation.
2. USB keyboard/mouse remains off by default on PS4. `/data/PCSX2/flags/usbkbm` opts in for hardware testing; behavior is still console-unverified.
3. Cover downloads now use the mbedTLS HTTPS client on PS4, but DNS/TLS/redirect behavior is still console-unverified.
4. NFS game shares now use CEMUPS4 `/net/shareN` rather than the PS5 `/nfs` libc-wrap layer. Only plain NFSv3 `nfs://host/export` addresses are supported, behavior is console-unverified, and SMB is not exposed in the UI yet.
5. NVRAM lives beside BIOS; read-only custom BIOS folders can break NVRAM persistence. Writes are now periodic, dirty-only, and atomic on PS4, but a read-only location still cannot persist them.

## Memory/robustness

1. Savestate loading streams entry payloads, and synchronous saves stream entries without a second full-state buffer; both paths still need console memory/correctness validation. Threaded saves intentionally keep the full-snapshot contract.
2. Baseline compiler warnings have been triaged and the highest-confidence correctness issues integrated; remaining warning-volume reduction is non-critical cleanup, not evidence of a clean runtime.
3. The non-executable-memory interpreter fallback is compile-validated only. It should allow boot/UI diagnostics, but its real speed and correctness on Jaguar are unknown.
4. The legacy non-PS4 direct-code fallback still maps `0x33` (CPU/GPU RW, no CPU execute) and lacks ownership tracking. It is PS5-only, was not changed without PS5 hardware validation, and should either use the validated RW-to-mprotect path or fail cleanly in a future narrow patch.
5. JIT shared-memory ownership remains incomplete on the PS5-only opt-in path because the kernel handle is lost; do not reintroduce those imports on PS4.


## Memory-pressure tradeoffs

1. The PS4 MTVU backing is capped at the existing 2048 KiB default lap, removing the ability to test larger 4/8/16 MiB laps on the normal PS4 candidate. This preserves default behavior while avoiding 14 MiB of always-resident BSS, but heavy-MTVU games still need hardware comparison before the cap is treated as final.
2. Folder memory-card corrupt-superblock validation remains incomplete. Malformed host-side `_pcsx2_superblock` data can still drive some out-of-range FAT/cluster indexing in upstream folder-card logic; do not treat folder cards as hardened untrusted input yet.

## User-data layout

1. `OrbisDir()` still uses a flat `/data/PCSX2` fallback when a subdirectory does not exist. Existing flat installs must not be migrated blindly: memory cards, savestates, BIOS/NVRAM, and custom filenames need a validated one-time migration plan on real hardware. Do not create `memcards/`, `savestates/`, or `bios/` casually on a console with flat user data, or the layout can switch and orphan existing files. The audited design is rename-only, per-category transactional, idempotent after power loss, creates a target directory only when it will receive files, classifies BIOS before `.bin` memory cards, moves BIOS sidecars as families, honors configured custom card names, skips destination collisions, and leaves ambiguous patches/covers/disc images flat. It should be implemented flag-gated off and console-validated before becoming default.
2. Forced interpreter configuration is a runtime execution gate, not a durable user-visible configuration policy. `CHECK_*JITMEM` prevents generated code execution even if settings are later edited, but some non-fatal decisions may still read raw config values; audit them before adding new JIT consumers.

## Documentation/evidence gaps

1. README game compatibility is inherited and not backed by logs in this repository.
2. No structured per-title compatibility dataset exists yet.
3. No real console performance measurement was available tonight.
