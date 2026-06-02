#!/usr/bin/env python3
"""Structural rename of the `rate` param for FX and sequencer operators.

  FX  (Flanger/Phaser/Chorus): rate (Hz) -> frequency, rate_cv port -> freq_cv
  DIV (Arp/PatternSeq/Euclidean/Tracker): rate (9-entry division enum) ->
      sync_division (12-entry metronome_division_labels); the local kMultipliers
      lookup is replaced with 1/sync_cycle_beats(idx) and the array removed; the
      index clamp widens from 0..8 to 0..11.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FX = [
    "operators/audio/flanger/flanger.cpp",
    "operators/audio/phaser/phaser.cpp",
    "operators/audio/chorus/chorus.cpp",
]
DIV = [
    "operators/control/arpeggiator/arpeggiator_core.h",
    "operators/control/pattern_seq/pattern_seq_core.h",
    "operators/control/euclidean/euclidean_core.h",
    "operators/control/tracker/tracker_core.h",
    "operators/control/tracker/tracker_core.cpp",
]

RATE_DECL = re.compile(r'\{"rate",(\s*)(\d+),\s*\{"1/1"[^}]*"1/16T"\}\}')
KMULT_ARRAY = re.compile(r'\n[ \t]*static constexpr float kMultipliers\[\][^;]*?\};')


def migrate_fx(text):
    text = re.sub(r'\brate\b', 'frequency', text)   # standalone rate -> frequency
    text = text.replace("rate_cv", "freq_cv")
    return text


def migrate_div(text):
    text = RATE_DECL.sub(r'{"sync_division",\1\2, vivid::metronome_division_labels()}', text)
    text = text.replace("description(rate,", "description(sync_division,")
    text = text.replace("&rate)", "&sync_division)")
    text = text.replace("&rate);", "&sync_division);")
    text = text.replace("rate.int_value()", "sync_division.int_value()")
    # kMultipliers[r] -> 1/sync_cycle_beats(r); the two multiplication idioms used:
    text = text.replace("* kMultipliers[r]", "/ vivid::sync_cycle_beats(r)")
    text = text.replace("kMultipliers[r]", "(1.0f / vivid::sync_cycle_beats(r))")
    text = KMULT_ARRAY.sub("", text)
    # widen the rate index clamp (specific param-index forms only)
    for idx in ("17", "4", "0"):
        text = text.replace(f"static_cast<int>(params[{idx}]), 0, 8)",
                            f"static_cast<int>(params[{idx}]), 0, 11)")
    # arp reads `int r = sync_division.int_value();` with no clamp; array now 12 wide.
    # comment fixups (cosmetic, keep the source honest)
    text = text.replace("//  2       = rate", "//  2       = sync_division")
    text = text.replace("rate=17,", "sync_division=17,")
    return text


def main():
    for rel in FX:
        f = ROOT / rel
        t = f.read_text(); nt = migrate_fx(t)
        if nt != t:
            f.write_text(nt); print(f"  fx  {rel}")
    for rel in DIV:
        f = ROOT / rel
        t = f.read_text(); nt = migrate_div(t)
        if nt != t:
            f.write_text(nt); print(f"  div {rel}")


if __name__ == "__main__":
    main()
