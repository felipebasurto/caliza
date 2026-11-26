#!/usr/bin/env python3
"""Positive/negative host tests for analyze_console_logs.py."""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TOOL = HERE / "analyze_console_logs.py"

GOOD = """[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (flexible)
[pad] player 1 data[56..84]: 1 2 3
[pad] player 1 connected byte observed nonzero
[ps4wsi] presents=10 failures=0 flip_fallbacks=0 acquire_timeouts=0 acquire_not_ready=0
[audio] grains=10 underruns=0 errors=0 last=0x0
[state] wrote /data/PCSX2/sstates/test.p2s: 13 entries, 41943040 bytes (streaming)
[state] loaded /data/PCSX2/sstates/test.p2s: 13 entries
[boot] boot: network share nfs://192.168.1.2/ps2 at /net/share1: Connected
[threads] draws=120 gs_idle_ms=1 ee_waitgs_ms=2(3) ee_ringfull_ms=0(0) ticker_cpu=1
[copies] draw=1(1.0MB)
[pcsx2] VM stage: Opening SPU2
[pcsx2] first VMManager::Execute begin
"""

BAD_MEM = """[dbg] memmap: direct alloc failed, falling through
"""

BAD_AUDIO = """[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (flexible)
[audio] grains=10 underruns=3 errors=1 last=0x80020001
"""


def run(path: pathlib.Path, *flags: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *flags, str(path)], text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )


def expect(name: str, path: pathlib.Path, flags: tuple[str, ...], code: int, needle: str) -> bool:
    proc = run(path, *flags)
    combined = proc.stdout + proc.stderr
    ok = proc.returncode == code and needle in combined
    print(f"{'PASS' if ok else 'FAIL'}: {name}")
    if not ok:
        print(f"  rc={proc.returncode}, wanted {code}; output missing {needle!r}:\n{combined}")
    return ok


def main() -> int:
    ok = True
    with tempfile.TemporaryDirectory(prefix="ps4-console-log-test.") as td:
        root = pathlib.Path(td)
        good = root / "good.log"
        good.write_text(GOOD)
        flags = ("--expect-input", "--expect-wsi", "--expect-audio", "--expect-state", "--expect-nfs", "--expect-gameplay")
        ok &= expect("complete smoke evidence passes", good, flags, 0, "console log analysis: PASS")

        ok &= expect("missing optional evidence fails when requested", good, ("--expect-kbm",), 1, "missing required evidence for USB keyboard/mouse")

        bad = root / "badmem.log"
        bad.write_text(BAD_MEM)
        ok &= expect("direct-memory failure is fatal", bad, (), 1, "direct memory allocation failed")

        reset = root / "badreset.log"
        reset.write_text("[boot] SysMemory::Reset FAILED\n")
        ok &= expect("memory reset failure is fatal", reset, (), 1, "VM memory initialization failed")

        audio = root / "audio.log"
        audio.write_text(BAD_AUDIO)
        ok &= expect("audio errors warn without failing", audio, ("--expect-audio",), 0, "WARN audio counters")

        nfsfail = root / "nfsfail.log"
        nfsfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                           "[boot] boot: network share nfs://192.168.1.2/ps2 at /net/share1: doesn't answer (not added to scan)\n")
        ok &= expect("unchecked NFS share fails expectation", nfsfail, ("--expect-nfs",), 1, "missing required evidence for NFS share")

        ok &= expect("latest boot-trace milestone is reported", good, (), 0,
                     "INFO boot trace: last persistent milestone: first VMManager::Execute begin")

        plain = root / "plain.log"
        plain.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n")
        ok &= expect("missing boot-trace milestones are informational", plain, (), 0,
                     "INFO boot trace: no persistent [pcsx2] milestones found")

        failedboot = root / "failedboot.log"
        failedboot.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                              "[pcsx2] VMManager::Initialize failed\n")
        ok &= expect("failure boot-trace milestone is fatal", failedboot, (), 1,
                     "boot trace ended at failure milestone: VMManager::Initialize failed")

        cpufail = root / "cpufail.log"
        cpufail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                           "[boot] CPUThreadInitialize FAILED\n")
        ok &= expect("CPUThreadInitialize failure is fatal", cpufail, (), 1,
                     "VM memory initialization failed")

        allocatefail = root / "allocatefail.log"
        allocatefail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                "[dbg] sysmem: Allocate failed\n")
        ok &= expect("canonical SysMemory allocation failure is fatal", allocatefail, (), 1,
                     "VM memory initialization failed")

        noexec = root / "noexec.log"
        noexec.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x680000000 (direct, executable=0)\n"
                          "[boot] recompiler memory is not executable: forcing EE/IOP/VU interpreters, C VIF unpack, and C scanline\n")
        ok &= expect("nonexecutable fallback warns without failing", noexec, (), 0,
                     "slow interpreter compatibility fallback")

        mcdfail = root / "mcdfail.log"
        mcdfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                           "[mcd] slot 0: wrote back 1 block(s) (16 KiB) in 1 write(s), 2.0 ms (idle) -- WRITE FAILED\n")
        ok &= expect("memory-card write failure is fatal", mcdfail, (), 1,
                     "memory-card cache write failure evidence")

        mcdclosefail = root / "mcdclosefail.log"
        mcdclosefail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                "[mcd] slot 0: close/checksum write failed\n")
        ok &= expect("memory-card close failure is fatal", mcdclosefail, (), 1,
                     "memory-card cache write failure evidence")

        folderclosefail = root / "folderclosefail.log"
        folderclosefail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                   "FolderMcd: 1 file handle(s) failed to close on slot 0\n")
        ok &= expect("folder memory-card close failure is fatal", folderclosefail, (), 1,
                     "folder memory-card close failure evidence")

        foldermetadatafail = root / "foldermetadatafail.log"
        foldermetadatafail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                      "FolderMcd: metadata write failed; could not open '/data/PCSX2/memcards/card/_pcsx2_index'\n")
        ok &= expect("folder memory-card metadata write failure warns", foldermetadatafail, (), 0,
                     "folder memory-card metadata write failure evidence")

        mtvu = root / "mtvu.log"
        mtvu.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                        "[mtvu] per s: kicks=10 coalesced=0 ringstalls=2 laps ee/vu=1/1 wakeups=3 spin/sleep=1/0 | backlog hwm 1792 KB\n")
        ok &= expect("MTVU cap pressure warns without failing", mtvu, (), 0,
                     "fixed 2048 KiB PS4 cap may be pressuring this scene")

        nvramfail = root / "nvramfail.log"
        nvramfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                             "Failed to save NVRAM to scph10000.nvm: read-only file system\n")
        ok &= expect("NVRAM save failure warns without failing", nvramfail, (), 0,
                     "WARN NVRAM save failure")

        statecorrupt = root / "statecorrupt.log"
        statecorrupt.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                "Save state corruption in eeMemory.bin.\n")
        ok &= expect("savestate corruption is fatal", statecorrupt, (), 1,
                     "savestate corruption evidence present")

        folderbackfail = root / "folderbackfail.log"
        folderbackfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                  "McdSlot 0: [Folder] backing allocation failed; folder memory card unavailable\n")
        ok &= expect("folder memory-card backing failure is fatal", folderbackfail, (), 1,
                     "FAIL folder memory-card backing allocation failed")

        folderwritefail = root / "folderwritefail.log"
        folderwritefail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                   "FolderMcd: host file write failed; dirty memory-card pages are kept for retry\n")
        ok &= expect("folder memory-card write failure is fatal", folderwritefail, (), 1,
                     "FAIL folder memory-card host write failure")

        folderwriterecover = root / "folderwriterecover.log"
        folderwriterecover.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                      "FolderMcd: host file write failed; dirty memory-card pages are kept for retry\n"
                                      "FolderMcd: host file write succeeded after retry\n")
        ok &= expect("recovered folder memory-card write failure warns", folderwriterecover, (), 0,
                     "WARN folder memory-card host write failure occurred but a later retry succeeded")

        folderwriterefail = root / "folderwriterefail.log"
        folderwriterefail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                     "FolderMcd: host file write failed; dirty memory-card pages are kept for retry\n"
                                     "FolderMcd: host file write succeeded after retry\n"
                                     "FolderMcd: host file write failed; dirty memory-card pages are kept for retry\n")
        ok &= expect("post-recovery folder memory-card write failure is fatal", folderwriterefail, (), 1,
                     "without later recovery")

        mdecfail = root / "mdecfail.log"
        mdecfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                            "MDEC backing allocation failed; MDEC DMAs will complete without decoded data\n")
        ok &= expect("MDEC backing failure is fatal", mdecfail, (), 1,
                     "MDEC backing allocation failed")

        cdvdcachefail = root / "cdvdcachefail.log"
        cdvdcachefail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                 "CDVD: sector cache allocation failed; using direct reads only\n")
        ok &= expect("CDVD cache backing failure warns without failing", cdvdcachefail, (), 0,
                     "WARN CDVD sector cache allocation failed")


        cdvdreaderr = root / "cdvdreaderr.log"
        cdvdreaderr.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                               "CDVD read err, retrying... (attempt 1 of 256)\n"
                               "CDVD READ ERROR, sector = 123\n")
        ok &= expect("CDVD retry exhaustion warns without failing", cdvdreaderr, (), 0,
                     "WARN CDVD read retry exhaustion evidence")

        isoerr = root / "isoerr.log"
        isoerr.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                          "isoFile error: Block index is past the end of file!\n")
        ok &= expect("ISO reader error warns without failing", isoerr, (), 0,
                     "WARN ISO reader error evidence")


        nfstimeout = root / "nfstimeout.log"
        nfstimeout.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                              "[boot] test: network share check budget expired after 8 s; pending shares stay registered for manual retry\n")
        ok &= expect("NFS boot-check timeout warns", nfstimeout, (), 0,
                     "WARN network share boot-check budget expired")

        audioinitfail = root / "audioinitfail.log"
        audioinitfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                 "[audio] sceAudioOutInit=80260001\n")
        ok &= expect("audio initialization failure warns", audioinitfail, (), 0,
                     "WARN audio initialization failure evidence")

        audioopenfail = root / "audioopenfail.log"
        audioopenfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                 "[audio] sceAudioOutOpen rate=48000 ch=2 -> 80260002\n")
        ok &= expect("audio open failure warns", audioopenfail, (), 0,
                     "WARN audio output open failure evidence")

        vkringfail = root / "vkringfail.log"
        vkringfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                              "[vkhw] big draw ring allocation failed; using the small ring only\n")
        ok &= expect("big Vulkan diagnostic ring failure warns without failing", vkringfail, (), 0,
                     "WARN big Vulkan diagnostic ring allocation failed")

        dev9flashfail = root / "dev9flashfail.log"
        dev9flashfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                                 "DEV9: flash backing allocation failed; flash memory is read-only erased\n")
        ok &= expect("DEV9 flash backing failure warns without failing", dev9flashfail, (), 0,
                     "WARN DEV9 flash backing allocation failed")

        gpproffail = root / "gpproffail.log"
        gpproffail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                              "[gpuprof] backing allocation failed: the profiler stays off\n")
        ok &= expect("GPU profiler backing failure warns without failing", gpproffail, (), 0,
                     "WARN GPU profiler backing allocation failed")

        proffail = root / "proffail.log"
        proffail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                            "[prof] backing allocation failed: profiler stays off\n")
        ok &= expect("sampling profiler backing failure warns without failing", proffail, (), 0,
                     "WARN sampling profiler backing allocation failed")

        osdfail = root / "osdfail.log"
        osdfail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                           "[osd] backing allocation failed: overlay composition disabled\n")
        ok &= expect("OSD backing failure warns without failing", osdfail, (), 0,
                     "WARN OSD composition backing allocation failed")

        pgiffail = root / "pgiffail.log"
        pgiffail.write_text("[dbg] memmap: data=0x600000000 (direct) code=0x900000000 (direct)\n"
                            "PGIF GP0 FIFO backing allocation failed; dropping PS1 GPU FIFO data\n")
        ok &= expect("PGIF backing failure is fatal", pgiffail, (), 1,
                     "FAIL PGIF GP0 FIFO backing allocation failed")

    print("console-log analyzer tests: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
