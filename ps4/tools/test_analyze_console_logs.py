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

        audio = root / "audio.log"
        audio.write_text(BAD_AUDIO)
        ok &= expect("audio errors warn without failing", audio, ("--expect-audio",), 0, "WARN audio counters")

    print("console-log analyzer tests: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
