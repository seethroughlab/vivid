#!/usr/bin/env python3
"""Vendored-snapshot consistency guard (static; no build, no running app).

Example projects are self-contained: `tools/operator_audit/gen_examples.py` COPIES each moved-out
operator's source and its vendored SDK headers (gpu_3d.h, instance_algorithms.h, linmath.h, …) out
of the canonical package under `app/operators/packages/**` into every `examples/**` project that
uses it. Those copies are snapshots — they are supposed to be byte-identical to their origin. Nothing
enforces that today, so editing the canonical (e.g. `content-3d/instance_grid.cpp` or
`vivid-3d/operator_api/gpu_3d.h`) without re-running the generator silently leaves the example copies
stale, and an example that no longer matches the op it demonstrates is worse than no example.

This guard groups every source-tree copy of each snapshot file by basename and fails if any group
holds more than one distinct content hash — i.e. some copy has drifted. It does not need to know
which copy is "canonical": the invariant is simply that all copies of a snapshot agree, so a genuine
update (regenerate → every copy moves together) stays green while a forgotten regenerate splits the
group and fails.

Fix on failure: re-run `tools/operator_audit/gen_examples.py` (needs a running app) to refresh the
example copies from the canonical package sources, or hand-sync the drifted file.

Run standalone (`uv run tools/check_vendored_snapshots.py`) or under pytest.
"""
import hashlib
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Roots that hold canonical package sources AND their vendored/example snapshot copies.
SCAN_ROOTS = [
    ROOT / "app" / "operators" / "packages",
    ROOT / "examples" / "operators",
    ROOT / "examples" / "demos" / "projects",
]
SNAPSHOT_SUFFIXES = {".h", ".hpp", ".inl", ".c", ".cpp", ".cc"}
# Per-project generated files legitimately differ between projects — never snapshot-compared.
EXCLUDE_NAMES = {"project.json", "vivid-package.json"}


def _is_build_path(p: Path) -> bool:
    return any(part == "build" or part.startswith("build-") for part in p.parts)


def _sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def snapshot_groups() -> dict[str, list[Path]]:
    """basename -> every source-tree copy of it (build outputs excluded)."""
    groups: dict[str, list[Path]] = defaultdict(list)
    for root in SCAN_ROOTS:
        if not root.exists():
            continue
        for p in root.rglob("*"):
            if not p.is_file() or _is_build_path(p):
                continue
            if p.suffix not in SNAPSHOT_SUFFIXES or p.name in EXCLUDE_NAMES:
                continue
            groups[p.name].append(p)
    return groups


def check() -> tuple[dict[str, dict[str, list[str]]], int]:
    """Returns (drifted, n_multi) where drifted maps basename -> {hash: [rel paths]} for any
    basename whose copies disagree, and n_multi is how many multi-copy groups were checked."""
    drifted: dict[str, dict[str, list[str]]] = {}
    n_multi = 0
    for name, paths in snapshot_groups().items():
        if len(paths) < 2:
            continue  # a single copy can't drift
        n_multi += 1
        by_hash: dict[str, list[str]] = defaultdict(list)
        for p in paths:
            by_hash[_sha(p)].append(str(p.relative_to(ROOT)))
        if len(by_hash) > 1:
            drifted[name] = {h[:12]: sorted(v) for h, v in by_hash.items()}
    return drifted, n_multi


def test_vendored_snapshots_consistent():
    drifted, _ = check()
    assert not drifted, "vendored snapshot drift:\n" + "\n".join(
        f"  {name}: {len(versions)} distinct versions\n"
        + "\n".join(f"    [{h}] {', '.join(files)}" for h, files in versions.items())
        for name, versions in sorted(drifted.items())
    )


if __name__ == "__main__":
    drifted, n_multi = check()
    print(f"vendored snapshot files checked (2+ copies): {n_multi}")
    if drifted:
        print("\nFAIL — these snapshot files have drifted (copies disagree):", file=sys.stderr)
        for name, versions in sorted(drifted.items()):
            print(f"  {name}: {len(versions)} distinct versions", file=sys.stderr)
            for h, files in versions.items():
                print(f"    [{h}] {', '.join(files)}", file=sys.stderr)
        print(
            "\nRe-run tools/operator_audit/gen_examples.py to refresh example copies from the "
            "canonical package sources, or hand-sync the drifted file.",
            file=sys.stderr,
        )
        sys.exit(1)
    print("PASS — every vendored snapshot copy matches its peers")
