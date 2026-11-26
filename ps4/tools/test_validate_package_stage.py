#!/usr/bin/env python3
"""Negative/positive host tests for validate_package_stage.py."""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TOOL = HERE / "validate_package_stage.py"

REQUIRED_FILES = [
    "eboot.bin",
    "sce_sys/icon0.png",
    "sce_sys/param.sfo",
    "resources/GameIndex.yaml",
    "COPYING.GPLv3",
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


def make_stage(root: pathlib.Path) -> pathlib.Path:
    stage = root / "stage"
    for rel in REQUIRED_FILES:
        path = stage / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"x")
    (stage / "SOURCE.txt").write_text(
        "Corresponding source: https://github.com/felipebasurto/zeruko\n"
        "Upstream PCSX2 source: https://github.com/PCSX2/pcsx2\n"
        "Build commit: 0123456789ab\n"
    )
    (stage / "sce_module").mkdir(parents=True, exist_ok=True)
    (stage / "sce_module" / "libSceFios2.prx").write_bytes(b"x")
    return stage


def run(stage: pathlib.Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), str(stage), *extra], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )


def expect(name: str, proc: subprocess.CompletedProcess[str], code: int, needle: str) -> bool:
    combined = proc.stdout + proc.stderr
    ok = proc.returncode == code and needle in combined
    print(f"{'PASS' if ok else 'FAIL'}: {name}")
    if not ok:
        print(f"  rc={proc.returncode}, wanted {code}; output missing {needle!r}:\n{combined}")
    return ok


def main() -> int:
    ok = True
    with tempfile.TemporaryDirectory(prefix="ps4-stage-validator-test.") as td:
        root = pathlib.Path(td)

        stage = make_stage(root / "good")
        ok &= expect("valid redistributable stage", run(stage), 0, "PASS (redistributable BIOS-free build")

        stage = make_stage(root / "dirty-provenance")
        (stage / "SOURCE.txt").write_text((stage / "SOURCE.txt").read_text().replace("Build commit: 0123456789ab", "Build commit: 0123456789ab + uncommitted changes"))
        ok &= expect("dirty build provenance is rejected", run(stage), 1, "exact clean build-commit")

        stage = make_stage(root / "commit-match")
        ok &= expect("matching expected commit passes", run(stage, "--expect-commit", "0123456789ab"), 0, "PASS (redistributable BIOS-free build")

        stage = make_stage(root / "commit-mismatch")
        ok &= expect("mismatched expected commit is rejected", run(stage, "--expect-commit", "fedcba987654"), 1, "does not match expected")

        stage = make_stage(root / "missing-license")
        (stage / "licenses/libnfs-LGPL-2.1.txt").unlink()
        ok &= expect("missing LGPL license is rejected", run(stage), 1, "missing required file: licenses/libnfs-LGPL-2.1.txt")

        stage = make_stage(root / "bios-no-warning")
        (stage / "bios").mkdir()
        (stage / "bios" / "scph10000.bin").write_bytes(b"x")
        ok &= expect("BIOS without personal warning is rejected", run(stage), 1, "bios/ exists without PERSONAL-BUILD.txt")

        stage = make_stage(root / "sprx")
        (stage / "sce_module" / "libScePiglet.sprx").write_bytes(b"x")
        ok &= expect("SPRX in redistributable stage is rejected", run(stage), 1, "contains Sony SPRX module")

        stage = make_stage(root / "loose-bios")
        (stage / "scph10000.bin").write_bytes(b"x")
        ok &= expect("BIOS outside bios directory is rejected", run(stage), 1, "possible BIOS file outside bios/")

        stage = make_stage(root / "personal")
        (stage / "bios").mkdir()
        (stage / "bios" / "scph10000.bin").write_bytes(b"x")
        (stage / "PERSONAL-BUILD.txt").write_text("personal\n")
        ok &= expect("explicit personal BIOS stage passes", run(stage), 0, "PASS (personal BIOS build")

    print("package-stage validator tests: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
