# Known issues and risks (AI-assisted)

## Critical/platform

1. **Baseline ELF imports PS5-only JIT shared-memory symbols.** `sceKernelJitCreateSharedMemory` and `sceKernelJitMapSharedMemory` appear as dynamic imports even though direct memory is the default. Candidate removes those references under `ORBIS_PS4`; verify with dynamic import audit.
2. **Executable memory is firmware/loader dependent.** Direct memory mapped/mprotected executable is the chosen path. It must be proven on each supported firmware/homebrew loader. If it fails, there is currently no clean automatic interpreter fallback.
3. **OpenOrbis stubs can overstate available symbols.** Export-index checks are necessary but insufficient; keep an explicit PS5-only denylist.

## Renderer/GPU

1. WSI semaphore handle reuse is not fully tracked; acquire/present semaphore sets are insert-only and no destroy hook exists yet.
2. One global VideoOut/device/queue/swapchain state assumes sequential frontend/GS use, not concurrent independent swapchains.
3. Acquire can wait indefinitely if VideoOut flip status stops reporting valid state.
4. CPU fallback present path serializes with `vkQueueWaitIdle`; new `[ps4wsi]` telemetry should expose it.
5. Fixed 1920x1080 60 Hz mode only; no 120 Hz frame-generation display mode on PS4.

## Input/audio/filesystem

1. Player-1 disconnect previously left inputs stuck; candidate fixes it, needs console verification.
2. USB keyboard/mouse is disabled on PS4 because module resolution previously failed; worth retesting now that modules are explicitly loaded.
3. Cover downloads are unimplemented on PS4 (`sceHttp2*` stubs fail by design).
4. NFS game shares are effectively dead on PS4 because the PS5 file-wrap layer is not linked in; either port or explicitly hide/report unsupported.
5. NVRAM lives beside BIOS; read-only custom BIOS folders can break NVRAM persistence.

## Memory/robustness

1. Savestate loading can transiently allocate about twice the state file size through normal heap containers.
2. Compiler warning count is 148 in baseline and not triaged.
3. No automatic fallback to interpreter if executable memory cannot be validated.

## Documentation/evidence gaps

1. README game compatibility is inherited and not backed by logs in this repository.
2. No structured per-title compatibility dataset exists yet.
3. No real console performance measurement was available tonight.
