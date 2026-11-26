# Known issues and risks (AI-assisted)

## Critical/platform

1. **Baseline ELF imports PS5-only JIT shared-memory symbols.** `sceKernelJitCreateSharedMemory` and `sceKernelJitMapSharedMemory` appear as dynamic imports even though direct memory is the default. Candidate removes those references under `ORBIS_PS4`; verify with dynamic import audit.
2. **Executable memory is firmware/loader dependent.** Direct memory mapped/mprotected executable is the chosen path. It must be proven on each supported firmware/homebrew loader. If it fails, there is currently no clean automatic interpreter fallback.
3. **OpenOrbis stubs can overstate available symbols.** Export-index checks are necessary but insufficient; keep an explicit PS5-only denylist.

## Renderer/GPU

1. WSI semaphore destruction is now tracked for the normal single-device flow, but the tracking sets are not keyed by `VkDevice`; concurrent live devices could theoretically alias handles.
2. One global VideoOut/device/queue/swapchain state assumes sequential frontend/GS use, not concurrent independent swapchains.
3. Acquire can wait indefinitely if VideoOut flip status stops reporting valid state.
4. CPU fallback present path serializes with `vkQueueWaitIdle`; new `[ps4wsi]` telemetry should expose it.
5. Fixed 1920x1080 60 Hz mode only; no 120 Hz frame-generation display mode on PS4.

## Input/audio/filesystem

1. Player-1 disconnect stuck-input handling is improved, but the PS4 pad connected-byte layout is unverified; the candidate fails open and logs the byte window for first console validation.
2. USB keyboard/mouse remains off by default on PS4. `/data/PCSX2/flags/usbkbm` opts in for hardware testing; behavior is still console-unverified.
3. Cover downloads now use the mbedTLS HTTPS client on PS4, but DNS/TLS/redirect behavior is still console-unverified.
4. NFS game shares are effectively dead on PS4 because the PS5 file-wrap layer is not linked in; either port or explicitly hide/report unsupported.
5. NVRAM lives beside BIOS; read-only custom BIOS folders can break NVRAM persistence.

## Memory/robustness

1. Savestate loading streams entry payloads, and synchronous saves stream entries without a second full-state buffer; both paths still need console memory/correctness validation. Threaded saves intentionally keep the full-snapshot contract.
2. Compiler warning count is 148 in baseline and not triaged.
3. No automatic fallback to interpreter if executable memory cannot be validated; allocation now fails clearly instead of accepting non-executable memory.

## Documentation/evidence gaps

1. README game compatibility is inherited and not backed by logs in this repository.
2. No structured per-title compatibility dataset exists yet.
3. No real console performance measurement was available tonight.
