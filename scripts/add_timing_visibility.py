#!/usr/bin/env python3
"""Re-add conditional-visibility wiring for timing controls so each operator
shows only the sub-control relevant to its current clock_mode. Inserts a wiring
line at the end of the constructor's description block (anchored on a known
existing line), and ensures the clock_block.h include is present.

LFO and Sequencer already wire visibility (preserved through the rename), so
they are not touched here.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = '#include "shared/timing/clock_block.h"'


def ensure_include(text):
    if INCLUDE in text:
        return text
    # insert after the operator_api/operator.h include if present, else metronome_sync.h
    for anchor in ('#include "operator_api/operator.h"',
                   '#include "operator_api/metronome_sync.h"'):
        if anchor in text:
            return text.replace(anchor, anchor + "\n" + INCLUDE, 1)
    return text


def insert_after(text, anchor_line, new_line):
    """Insert new_line right after the first line containing anchor_line."""
    idx = text.find(anchor_line)
    if idx < 0 or new_line.strip() in text:
        return text, False
    eol = text.find("\n", idx)
    if eol < 0:
        return text, False
    indent = text[text.rfind("\n", 0, idx) + 1: idx]
    return text[:eol + 1] + indent + new_line + "\n" + text[eol + 1:], True


# (file, needs_include, anchor_line_substring, wiring_line)
JOBS = [
    ("operators/audio/flanger/flanger.cpp", True,
     'vivid::description(sync_division,',
     'vivid::wire_clock_visibility(frequency, sync_division, clock_mode);'),
    ("operators/audio/phaser/phaser.cpp", True,
     'vivid::description(sync_division,',
     'vivid::wire_clock_visibility(frequency, sync_division, clock_mode);'),
    ("operators/audio/chorus/chorus.cpp", True,
     'vivid::description(sync_division,',
     'vivid::wire_clock_visibility(frequency, sync_division, clock_mode);'),
    # arp family: synced shape, hide sync_division unless metronome
    ("operators/control/arpeggiator/arpeggiator_core.h", True,
     'vivid::description(sync_division,',
     'vivid::wire_clock_visibility_synced(sync_division, clock_mode);'),
    ("operators/control/pattern_seq/pattern_seq_core.h", True,
     'vivid::description(sync_division,',
     'vivid::wire_clock_visibility_synced(sync_division, clock_mode);'),
    ("operators/control/euclidean/euclidean_core.h", True,
     'vivid::description(sync_division,',
     'vivid::wire_clock_visibility_synced(sync_division, clock_mode);'),
    ("operators/control/tracker/tracker_core.cpp", True,
     'vivid::description(sync_division,',
     'vivid::wire_clock_visibility_synced(sync_division, clock_mode);'),
]


def main():
    for rel, needs_inc, anchor, wiring in JOBS:
        f = ROOT / rel
        if not f.exists():
            print(f"  SKIP {rel}"); continue
        t = f.read_text()
        if needs_inc:
            t = ensure_include(t)
        t, ok = insert_after(t, anchor, wiring)
        f.write_text(t)
        print(f"  {'wired' if ok else 'no-op'} {rel}")


if __name__ == "__main__":
    main()
