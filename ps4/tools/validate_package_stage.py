#!/usr/bin/env python3
"""Validate a staged PS4SX2 package tree before it is treated as a release artifact.

This is a reproducibility/compliance gate, not a substitute for console testing. It checks
that required runtime/legal files are present, redistributable packages do not carry BIOS or
Sony SPRX modules, and the packaging step left the expected metadata behind.
"""
from __future__ import annotations

import argparse
import pathlib
import sys

REQUIRED_FILES = [
    "eboot.bin",
    "sce_sys/icon0.png",
    "sce_sys/param.sfo",
    "resources/GameIndex.yaml",
    "COPYING.GPLv3",
    "SOURCE.txt",
    "ASSET-SOURCES.txt",
    "FontAwesome-LICENSE.txt",
    "ThirdPartyLicenses.html",
    "PORT-THIRD-PARTY.txt",
    "licenses/zlib-LICENSE.txt",
    "licenses/zstd-LICENSE.txt",
    "licenses/lz4-LICENSE.txt",
    "licenses/libarchive-COPYING.txt",
    "licenses/mbedtls-LICENSE.txt",
    "licenses/libnfs-LGPL-2.1.txt",
    "licenses/libnfs-BSD.txt",
]

# Extensions/names commonly used by PS2 BIOS dumps. The supported personal-build location is
# stage/bios; this scan catches accidental drops elsewhere without inspecting copyrighted content.
BIOS_SUFFIXES = {".bin", ".rom", ".erom", ".nvm", ".mec"}
BIOS_PREFIXES = ("scph", "ps2-", "ps2_")


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("stage", type=pathlib.Path)
    parser.add_argument("--pkg", type=pathlib.Path)
    args = parser.parse_args()

    stage = args.stage
    errors: list[str] = []
    warnings: list[str] = []
    if not stage.is_dir():
        parser.error(f"stage directory not found: {stage}")

    for rel in REQUIRED_FILES:
        path = stage / rel
        if not path.is_file():
            fail(errors, f"missing required file: {rel}")
        elif path.stat().st_size == 0:
            fail(errors, f"required file is empty: {rel}")

    source = stage / "SOURCE.txt"
    if source.is_file():
        text = source.read_text(errors="replace")
        if "https://github.com/felipebasurto/PS4SX2" not in text:
            fail(errors, "SOURCE.txt lacks the PS4SX2 corresponding-source URL")
        if "https://github.com/PCSX2/pcsx2" not in text:
            fail(errors, "SOURCE.txt lacks the upstream PCSX2 source URL")
        if "Build commit:" not in text:
            fail(errors, "SOURCE.txt lacks a build-commit line")

    bios_dir = stage / "bios"
    personal = bios_dir.is_dir()
    if personal:
        if not (stage / "PERSONAL-BUILD.txt").is_file():
            fail(errors, "bios/ exists without PERSONAL-BUILD.txt redistribution warning")
        if not any(bios_dir.iterdir()):
            fail(errors, "bios/ exists but is empty")
    else:
        for path in stage.rglob("*"):
            if not path.is_file() or "resources" in path.relative_to(stage).parts:
                continue
            name = path.name.lower()
            if path.suffix.lower() in BIOS_SUFFIXES and name.startswith(BIOS_PREFIXES):
                fail(errors, f"possible BIOS file outside bios/: {path.relative_to(stage)}")

    # Public packages must not contain Sony SPRX modules. OpenOrbis .prx sample modules are
    # expected because make-pkg.sh stages them for module loading.
    sprx = sorted((stage / "sce_module").glob("*.sprx")) if (stage / "sce_module").is_dir() else []
    if sprx and not personal:
        fail(errors, "redistributable stage contains Sony SPRX module(s): " + ", ".join(p.name for p in sprx))
    prx = sorted((stage / "sce_module").glob("*.prx")) if (stage / "sce_module").is_dir() else []
    if not prx:
        fail(errors, "sce_module contains no OpenOrbis .prx modules")
    if (stage / "pkg.gp4").exists():
        fail(errors, "temporary pkg.gp4 was left in the stage")

    if args.pkg:
        if not args.pkg.is_file():
            fail(errors, f"PKG not found: {args.pkg}")
        elif args.pkg.stat().st_size < 1024 * 1024:
            fail(errors, f"PKG is implausibly small: {args.pkg}")
        if args.pkg.name != "IV0000-PCSX00002_00-PCSX2PS400000000.pkg":
            warnings.append(f"nonstandard PKG name: {args.pkg.name}")

    for warning in warnings:
        print(f"warning: {warning}")
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    mode = "personal BIOS build" if personal else "redistributable BIOS-free build"
    print(f"package stage validation: PASS ({mode}, {len(REQUIRED_FILES)} required files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
