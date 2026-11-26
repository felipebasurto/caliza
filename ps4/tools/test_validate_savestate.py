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
    "USB.bin", "PAD.bin",
]


def blob(entries: list[tuple[str, bytes]], version: int = VERSION, trailing: bytes = b"") -> bytes:
    out = bytearray(b"OPS2STAT" + struct.pack("<II", version, len(entries)))
    for name, payload in entries:
        encoded = name.encode("ascii")
        out += struct.pack("<I", len(encoded)) + encoded + struct.pack("<Q", len(payload)) + payload
    return bytes(out) + trailing


def valid() -> bytes:
    return blob([(name, bytes([index + 1]) * (16 + index)) for index, name in enumerate(REQUIRED)])


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
        ok &= expect("trailing bytes", path, 1, "trailing bytes")

        entries = [(name, b"x") for name in REQUIRED[:-1]]
        path = root / "missing.p2s"
        path.write_bytes(blob(entries))
        ok &= expect("missing required entry", path, 1, "missing required entries: PAD.bin")

        entries = [(name, b"x") for name in REQUIRED]
        entries[1] = (REQUIRED[0], b"x")
        path = root / "duplicate.p2s"
        path.write_bytes(blob(entries))
        ok &= expect("duplicate entry", path, 1, "duplicate entry")

        path = root / "truncated.p2s"
        path.write_bytes(valid()[:-3])
        ok &= expect("truncated payload", path, 1, "extends past EOF")

    print("savestate validator tests: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
