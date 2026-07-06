# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Version-guard check (P4.5).

Vivid keeps a single version source — `project(vivid VERSION X.Y.Z)` in
app/CMakeLists.txt, from which version.h is generated — so there's no second surface to
drift. This guard asserts that version parses as semver and, on a release, that it matches
the pushed tag (`vX.Y.Z`). Run in CI by version-guard.yml.

  uv run tools/check_version.py                 # validate the CMake version is semver
  uv run tools/check_version.py --expect 0.2.0  # also assert it equals 0.2.0 (e.g. a tag)
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

CMAKE = Path("app/CMakeLists.txt")
VERSION_RE = re.compile(r"project\s*\(\s*vivid\s+VERSION\s+(\d+\.\d+\.\d+)", re.IGNORECASE)


def cmake_version(text: str) -> str | None:
    m = VERSION_RE.search(text)
    return m.group(1) if m else None


def main() -> int:
    ap = argparse.ArgumentParser(description="Vivid version-guard")
    ap.add_argument("--cmake", type=Path, default=CMAKE)
    ap.add_argument("--expect", help="version (or vX.Y.Z tag) the CMake version must equal")
    args = ap.parse_args()

    if not args.cmake.exists():
        print(f"error: {args.cmake} not found", file=sys.stderr)
        return 2

    ver = cmake_version(args.cmake.read_text())
    if not ver:
        print(f"error: no `project(vivid VERSION X.Y.Z)` in {args.cmake}", file=sys.stderr)
        return 1
    print(f"CMake project version: {ver}")

    if args.expect:
        expect = args.expect[1:] if args.expect.startswith("v") else args.expect
        if expect != ver:
            print(f"error: version mismatch — CMake {ver} != expected {expect}", file=sys.stderr)
            return 1
        print(f"matches expected {expect}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
