#!/usr/bin/env python3
"""Audit custom editor top bars for reusable toolbar layout adoption.

This is intentionally a lightweight static audit. It catches drift in the
editors where toolbar layout is a known risk, and reports advisory candidates
without requiring a running Vivid runtime or pixel renderer.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


@dataclass(frozen=True)
class EditorTarget:
    name: str
    path: str
    priority: str
    required_toolbar: bool = False


TARGETS = [
    EditorTarget("MidiClip", "operators/control/midi_clip/midi_clip_editor.cpp", "high", True),
    EditorTarget("DrumSequencer", "operators/control/drum_sequencer/drum_sequencer_editor.cpp", "high"),
    EditorTarget("Tracker", "operators/control/tracker/tracker_editor.cpp", "high"),
    EditorTarget("PatternSeq", "operators/control/pattern_seq/pattern_seq_editor.cpp", "high"),
    EditorTarget("Sequencer", "operators/control/sequencer/sequencer_editor.cpp", "high"),
    EditorTarget("Arpeggiator", "operators/control/arpeggiator/arpeggiator_editor.cpp", "high"),
    EditorTarget("Euclidean", "operators/control/euclidean/euclidean_editor.cpp", "high"),
    EditorTarget("ParametricEQ", "operators/audio/parametric_eq/parametric_eq_editor.cpp", "high"),
    EditorTarget("MSEG", "operators/control/mseg/mseg_editor.cpp", "high"),
]


def classify(source: str) -> tuple[str, str]:
    uses_toolbar = "toolbar_section(" in source or "toolbar_row(" in source
    has_top_bar = bool(re.search(r"Top[- ]bar|Top readout|Header bar", source, re.I))
    has_right_hints = "hints_w" in source and re.search(r"-\s*hints_w", source) is not None
    suppresses_collision = (
        "pattern_block_end" in source
        or re.search(r"hints_x\s*>\s*[^;\n]+", source) is not None
        or re.search(r"if\s*\([^)]*hints_w[^)]*<", source) is not None
    )

    if uses_toolbar:
        return "migrated", "uses editor_ui toolbar helper"
    if has_right_hints and suppresses_collision:
        return "manual-guarded", "manual top bar suppresses right hints before collision"
    if has_right_hints:
        return "advisory", "manual left status + right hint string may collide at narrow widths"
    if has_top_bar:
        return "manual-simple", "manual top bar has no detected right-aligned hint collision pattern"
    return "none", "no custom top bar pattern detected"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".", help="repository root")
    parser.add_argument("--format", choices=("markdown", "json"), default="markdown")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    rows: list[dict[str, str]] = []
    failures: list[str] = []

    for target in TARGETS:
        path = root / target.path
        if not path.exists():
            rows.append({
                "editor": target.name,
                "path": target.path,
                "priority": target.priority,
                "status": "missing",
                "note": "expected editor source file is missing",
            })
            failures.append(f"{target.name}: missing {target.path}")
            continue

        source = path.read_text(encoding="utf-8")
        status, note = classify(source)
        rows.append({
            "editor": target.name,
            "path": target.path,
            "priority": target.priority,
            "status": status,
            "note": note,
        })
        if target.required_toolbar and status != "migrated":
            failures.append(f"{target.name}: expected reusable toolbar helper adoption")

    if args.format == "json":
        import json

        print(json.dumps({"rows": rows, "failures": failures}, indent=2))
    else:
        print("# Editor Toolbar Layout Audit\n")
        print("| Editor | Priority | Status | Note |")
        print("|---|---:|---|---|")
        for row in rows:
            print(f"| {row['editor']} | {row['priority']} | {row['status']} | {row['note']} |")
        if failures:
            print("\n## Strict Failures\n")
            for failure in failures:
                print(f"- {failure}")
        else:
            print("\nNo strict toolbar coverage failures.")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
