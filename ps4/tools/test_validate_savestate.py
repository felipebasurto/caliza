#!/usr/bin/env python3
"""Positive/negative host tests for validate_savestate.py."""
from __future__ import annotations

import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TOOL = HERE / "validate_savestate.py"
VERSION = 0x9A59 << 16
REQUIRED = [
    "PCSX2 Internal Structures.dat", "eeMemory.bin", "iopMemory.bin", "eeHwRegs.bin", "iopHwRegs.bin",
    "Scratchpad.bin", "vu0Memory.bin", "vu1Memory.bin", "vu0MicroMem.bin", "vu1MicroMem.bin", "SPU2.bin",
    "PAD.bin", "GS.bin",
]


def blob(entries: list[tuple[str, bytes]], version: int = VERSION, trailing: bytes = b"") -> bytes:
    out = bytearray(b"OPS2STAT" + struct.pack("<II", version, len(entries)))
    for name, payload in entries:
        encoded = name.encode("ascii")
        out += struct.pack("<I", len(encoded)) + encoded + struct.pack("<Q", len(payload)) + payload
    return bytes(out) + trailing


MEMORY_SIZES = {
    "eeMemory.bin": 32 * 1024 * 1024,
    "iopMemory.bin": 2 * 1024 * 1024,
    "eeHwRegs.bin": 64 * 1024,
    "iopHwRegs.bin": 64 * 1024,
    "Scratchpad.bin": 16 * 1024,
    "vu0Memory.bin": 4 * 1024,
    "vu1Memory.bin": 16 * 1024,
    "vu0MicroMem.bin": 4 * 1024,
    "vu1MicroMem.bin": 16 * 1024,
}


def payload_for(name: str, index: int) -> bytes:
    return bytes([index + 1]) * MEMORY_SIZES.get(name, 16 + index)


def valid() -> bytes:
    return blob([(name, payload_for(name, index)) for index, name in enumerate(REQUIRED)])


def run(path: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), "--require-current-version", str(path)],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )


def expect(name: str, path: pathlib.Path, code: int, needle: str) -> bool:
    proc = run(path)
    combined = proc.stdout + proc.stderr
    ok = proc.returncode == code and needle in combined
    print(f"{'PASS' if ok else 'FAIL'}: {name}")
    if not ok:
        print(f"  rc={proc.returncode}, wanted {code}; output missing {needle!r}:\n{combined}")
    return ok


def main() -> int:
    ok = True
    with tempfile.TemporaryDirectory(prefix="ps4-savestate-validator-test.") as td:
        root = pathlib.Path(td)

        path = root / "good.p2s"
        path.write_bytes(valid())
        ok &= expect("valid savestate", path, 0, "PASS: 13 entries")

        path = root / "bad-magic.p2s"
        path.write_bytes(b"BADMAGIC" + valid()[8:])
        ok &= expect("bad magic", path, 1, "bad magic")

        path = root / "trailing.p2s"
        path.write_bytes(valid() + b"x")
        ok &= expect("trailing bytes are tolerated like the loader", path, 0, "trailing bytes ignored")

        entries = [(name, payload_for(name, index)) for index, name in enumerate(REQUIRED) if name != "PAD.bin"]
        path = root / "missing.p2s"
        path.write_bytes(blob(entries))
        ok &= expect("missing required entry", path, 1, "missing required entries: PAD.bin")

        entries = [(name, payload_for(name, index)) for index, name in enumerate(REQUIRED)] + [(REQUIRED[0], b"x")]
        path = root / "duplicate.p2s"
        path.write_bytes(blob(entries))
        ok &= expect("duplicate entries are tolerated like the loader", path, 0, "duplicate")

        entries = [(name, payload_for(name, index)) for index, name in enumerate(REQUIRED) if name != "GS.bin"] + [("USB.bin", b"x")]
        path = root / "missing-gs.p2s"
        path.write_bytes(blob(entries))
        ok &= expect("missing GS entry is fatal", path, 1, "missing required entries: GS.bin")

        entries = [(name, payload_for(name, index)) for index, name in enumerate(REQUIRED)]
        path = root / "no-usb.p2s"
        path.write_bytes(blob(entries))
        ok &= expect("missing USB entry is optional", path, 0, "PASS: 13 entries")

        entries = [(name, payload_for(name, index)) for index, name in enumerate(REQUIRED)]
        entries[1] = ("eeMemory.bin", b"short")
        path = root / "short-memory.p2s"
        path.write_bytes(blob(entries))
        ok &= expect("short fixed memory entry is fatal", path, 1, "entry 'eeMemory.bin' has size")

        entries = [(name, payload_for(name, index)) for index, name in enumerate(REQUIRED)]
        entries[2] = ("iopMemory.bin", bytes(8 * 1024 * 1024))
        path = root / "large-iop.p2s"
        path.write_bytes(blob(entries))
        ok &= expect("expanded IOP memory size is accepted", path, 0, "PASS: 13 entries")

        entries = [(name, payload_for(name, index)) for index, name in enumerate(REQUIRED)]
        entries[0] = ("PCSX2 Internal Structures.dat", bytes(16 * 1024 * 1024 + 1))
        path = root / "oversized-internals.p2s"
        path.write_bytes(blob(entries))
        ok &= expect("oversized internal structures are fatal", path, 1, "internal structures entry is too large")

        path = root / "truncated.p2s"
        path.write_bytes(valid()[:-3])
        ok &= expect("truncated payload", path, 1, "extends past EOF")

    print("savestate validator tests: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
