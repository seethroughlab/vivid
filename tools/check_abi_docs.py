#!/usr/bin/env python3
"""Operator-ABI doc/version consistency guard (static; no build).

The operator ABI has a single source of truth — `VIVID_OPERATOR_ABI_VERSION` and
`VIVID_OPERATOR_ABI_MIN_LOADABLE` in `app/src/operator_api/types.h`. The public docs restate
that version in prose, and prose drifts: this is exactly how the READMEs came to still claim
"v11" long after the code reached v17. This check parses the two macros from `types.h` and
asserts every public "current ABI" pointer agrees, so a future ABI bump that forgets the docs
fails loudly instead of shipping a stale contract.

It does NOT touch the dated audit snapshots under `docs/audits/**` or ADR/roadmap prose — those
are point-in-time records and are legitimately era-specific.

Run standalone (`uv run tools/check_abi_docs.py`) or under pytest.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TYPES_H = ROOT / "app" / "src" / "operator_api" / "types.h"


def _macro(text: str, name: str) -> int:
    m = re.search(rf"#define\s+{name}\s+(\d+)u", text)
    if not m:
        raise SystemExit(f"FAIL: could not find `#define {name} <N>u` in {TYPES_H}")
    return int(m.group(1))


def check() -> list[str]:
    """Return a list of human-readable failures (empty == all good)."""
    types = TYPES_H.read_text()
    version = _macro(types, "VIVID_OPERATOR_ABI_VERSION")
    floor = _macro(types, "VIVID_OPERATOR_ABI_MIN_LOADABLE")

    # Each entry: (doc path, list of substrings that MUST all be present).
    # Keep these in sync with the prose the docs actually use.
    expectations = [
        (
            ROOT / "docs" / "operator-api" / "README.md",
            [f"Current operator ABI: v{version}"],
        ),
        (
            ROOT / "docs" / "operator-authoring" / "README.md",
            [f"operator ABI **v{version}**"],
        ),
        (
            ROOT / "docs" / "operator-api" / "abi-changelog.md",
            [f"Current: v{version}", f"[v{floor}, v{version}]"],
        ),
    ]

    failures = []
    for path, required in expectations:
        rel = path.relative_to(ROOT)
        if not path.exists():
            failures.append(f"missing doc: {rel}")
            continue
        body = path.read_text()
        for needle in required:
            if needle not in body:
                failures.append(
                    f"{rel}: expected to contain {needle!r} "
                    f"(types.h is at ABI v{version}, floor v{floor})"
                )
    return failures


def test_abi_docs_are_current():
    """pytest entry point."""
    failures = check()
    assert not failures, "ABI doc drift:\n  " + "\n  ".join(failures)


if __name__ == "__main__":
    fails = check()
    if fails:
        print("ABI doc/version drift detected:", file=sys.stderr)
        for f in fails:
            print(f"  - {f}", file=sys.stderr)
        print(
            "\nFix the prose above to match app/src/operator_api/types.h, "
            "or update this guard if the contract intentionally changed.",
            file=sys.stderr,
        )
        sys.exit(1)
    print("ABI docs agree with app/src/operator_api/types.h. OK.")
