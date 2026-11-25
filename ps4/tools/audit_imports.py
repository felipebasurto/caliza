#!/usr/bin/env python3
"""Audit dynamic imports of a PS4 ELF against OpenOrbis stub-library exports.

This intentionally inspects dynamic undefined symbols, not every undefined symbol
reported by llvm-nm. The latter includes weak/static archive references that are
not runtime imports and produced thousands of false positives in this tree.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys

PS5_ONLY = re.compile(
    r"^(?:"
    r"sceKernelJit(?:CreateSharedMemory|MapSharedMemory|CreateAliasOfSharedMemory)"
    r"|sceKernelGetProsperoSystemSwVersion"
    r"|sceHttp2.*"
    r"|sceVideoOut(?:SetBufferAttribute2|RegisterBuffers2)"
    r")$"
)


def tool(candidates: list[str]) -> str:
    for name in candidates:
        found = shutil.which(name)
        if found:
            return found
    raise SystemExit(f"missing tool; tried: {', '.join(candidates)}")


def run(cmd: list[str]) -> str:
    return subprocess.run(cmd, check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout


def dynamic_undefined(readelf: str, elf: pathlib.Path) -> list[str]:
    out = run([readelf, "--dyn-syms", "--wide", str(elf)])
    names: set[str] = set()
    for line in out.splitlines():
        parts = line.split()
        # Typical: "  12: 000... 0 FUNC GLOBAL DEFAULT UND symbol@ver"
        if len(parts) >= 8 and parts[6] == "UND":
            names.add(parts[7].split("@", 1)[0])
    return sorted(names)


def exports(nm: str, libdir: pathlib.Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for so in sorted(libdir.glob("*.so")):
        out = run([nm, "-D", "--defined-only", str(so)])
        for line in out.splitlines():
            parts = line.split()
            if parts:
                result.setdefault(parts[-1], so.stem)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=pathlib.Path)
    parser.add_argument("openorbis_lib", type=pathlib.Path, help="directory containing OpenOrbis *.so stubs")
    parser.add_argument("--outdir", type=pathlib.Path, default=pathlib.Path("."))
    parser.add_argument("--strict", action="store_true", help="fail if unresolved or known PS5-only imports exist")
    args = parser.parse_args()

    if not args.elf.is_file():
        parser.error(f"ELF not found: {args.elf}")
    if not args.openorbis_lib.is_dir():
        parser.error(f"OpenOrbis lib directory not found: {args.openorbis_lib}")
    args.outdir.mkdir(parents=True, exist_ok=True)

    readelf = tool(["llvm-readelf-21", "llvm-readelf"])
    nm = tool(["llvm-nm-21", "llvm-nm"])
    undefined = dynamic_undefined(readelf, args.elf)
    exported = exports(nm, args.openorbis_lib)
    unresolved = [name for name in undefined if name not in exported and not name.startswith("annotate_")]
    ps5_only = [name for name in undefined if PS5_ONLY.match(name)]

    (args.outdir / "dynamic-undefined.txt").write_text("".join(f"{x}\n" for x in undefined))
    (args.outdir / "unresolved.txt").write_text("".join(f"{x}\n" for x in unresolved))
    (args.outdir / "ps5-only-imports.txt").write_text("".join(f"{x}\n" for x in ps5_only))
    with (args.outdir / "import-audit.txt").open("w") as f:
        f.write(f"elf={args.elf}\n")
        f.write(f"dynamic_undefined={len(undefined)}\n")
        f.write(f"unresolved={len(unresolved)}\n")
        f.write(f"known_ps5_only={len(ps5_only)}\n")

    print(f"dynamic imports: {len(undefined)}")
    print(f"unresolved by OpenOrbis export index: {len(unresolved)}")
    print(f"known PS5-only imports: {len(ps5_only)}")
    for name in ps5_only:
        print(f"PS5-only: {name}")
    return 1 if args.strict and (unresolved or ps5_only) else 0


if __name__ == "__main__":
    sys.exit(main())
