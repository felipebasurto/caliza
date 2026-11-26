#!/usr/bin/env python3
"""Strict dead-brand audit: fail if the retired product name appears in the tree.

The audit pattern is assembled at runtime so the audit tool itself does not reintroduce the word.
Technical compatibility must use indirect runtime construction too; user-facing copy has no exception.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SKIP_DIRS = {".git", "build", "deps", "obj", "out"}
LEGACY = "zer" + "uko"
WORD = re.compile(re.escape(LEGACY), re.IGNORECASE)


def skip(path: pathlib.Path) -> bool:
    return bool(set(path.parts) & SKIP_DIRS)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    rows = []
    scanned = 0
    for path in ROOT.rglob("*"):
        if not path.is_file() or skip(path.relative_to(ROOT)):
            continue
        try:
            data = path.read_bytes()
        except OSError:
            continue
        scanned += 1
        if WORD.search(data.decode("utf-8", errors="ignore")):
            text = data.decode("utf-8", errors="ignore")
            for lineno, line in enumerate(text.splitlines(), 1):
                if WORD.search(line):
                    rows.append((str(path.relative_to(ROOT)), lineno, line.strip()[:180]))
    for file, line, excerpt in rows:
        print(f"dead-brand reference: {file}:{line}: {excerpt}")
    print(f"dead-brand audit: {scanned} files scanned, {len(rows)} references")
    if args.verbose and not rows:
        print("no retired product-name references found")
    return 1 if rows else 0


if __name__ == "__main__":
    sys.exit(main())
