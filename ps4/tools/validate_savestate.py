#!/usr/bin/env python3
"""Structural validator for PCSX2 PS4 flat OPS2STAT savestates.

This checks container structure only; it cannot prove that guest serialization
contents are correct. It is intended for states pulled back from a console.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys

MAGIC = b"OPS2STAT"
CURRENT_VERSION = 0x9A59 << 16
MAX_SIZE = 256 * 1024 * 1024
MAX_COUNT = 1024
MAX_NAME = 1024

REQUIRED = [
    "PCSX2 Internal Structures.dat",
    "eeMemory.bin",
    "iopMemory.bin",
    "eeHwRegs.bin",
    "iopHwRegs.bin",
    "Scratchpad.bin",
    "vu0Memory.bin",
    "vu1Memory.bin",
    "vu0MicroMem.bin",
    "vu1MicroMem.bin",
    "SPU2.bin",
    "PAD.bin",
    "GS.bin",
]
OPTIONAL = {"USB.bin", "Achievements.bin"}
KNOWN = set(REQUIRED) | OPTIONAL


def fail(message: str) -> int:
    print(f"savestate validation: FAIL: {message}", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", type=pathlib.Path)
    parser.add_argument("--require-current-version", action="store_true", help="fail unless the state's version is the current Orbis save version")
    args = parser.parse_args()

    try:
        data = args.state.read_bytes()
    except OSError as exc:
        return fail(f"cannot read {args.state}: {exc}")
    if len(data) > MAX_SIZE:
        return fail(f"file is too large ({len(data)} bytes, cap {MAX_SIZE})")
    if len(data) < 16:
        return fail(f"file is too small for the header ({len(data)} bytes)")
    if data[:8] != MAGIC:
        return fail("bad magic (not OPS2STAT)")
    version, count = struct.unpack_from("<II", data, 8)
    if args.require_current_version and version != CURRENT_VERSION:
        return fail(f"save version {version:#x}, expected {CURRENT_VERSION:#x}")
    if count == 0 or count > MAX_COUNT:
        return fail(f"unreasonable entry count {count}")

    pos = 16
    entries: list[tuple[str, int, int]] = []
    seen: set[str] = set()
    for index in range(count):
        if pos + 4 > len(data):
            return fail(f"truncated name length for entry {index}")
        (name_len,) = struct.unpack_from("<I", data, pos)
        pos += 4
        if name_len == 0 or name_len > MAX_NAME or pos + name_len + 8 > len(data):
            return fail(f"unreasonable/truncated name for entry {index} ({name_len} bytes)")
        try:
            name = data[pos:pos + name_len].decode("ascii")
        except UnicodeDecodeError:
            return fail(f"non-ASCII entry name for entry {index}")
        pos += name_len
        (size,) = struct.unpack_from("<Q", data, pos)
        pos += 8
        if size > len(data) - pos:
            return fail(f"entry {name!r} extends past EOF ({size} bytes at {pos})")
        if name not in seen:
            seen.add(name)
        entries.append((name, pos, size))
        pos += int(size)
    trailing = len(data) - pos

    missing = [name for name in REQUIRED if name not in seen]
    unknown = [name for name in seen if name not in KNOWN]
    if missing:
        return fail("missing required entries: " + ", ".join(missing))

    total_payload = sum(size for _, _, size in entries)
    print(f"savestate validation: PASS: {count} entries, {total_payload} payload bytes, version {version:#x}")
    for name, offset, size in entries:
        print(f"  {name}: {size} bytes @ {offset}")
    duplicates = count - len(seen)
    if duplicates:
        print(f"savestate validation: note: {duplicates} duplicate entr(y/ies) present; loader uses the first")
    if trailing:
        print(f"savestate validation: note: {trailing} trailing bytes ignored by loader")
    if unknown:
        print("savestate validation: note: unknown entries present: " + ", ".join(sorted(unknown)))
    if version != CURRENT_VERSION:
        print(f"savestate validation: note: version {version:#x} differs from current {CURRENT_VERSION:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
