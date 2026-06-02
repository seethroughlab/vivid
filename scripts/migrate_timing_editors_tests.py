#!/usr/bin/env python3
"""Migrate editor index constants and test files to the consolidated timing vocab."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Ordered substitutions safe for tests + editor sources.
COMMON = [
    ("kRateModeMetronome", "kClockModeMetronome"),
    ("kRateModeExternal", "kClockModeExternal"),
    ("kRateModeFree", "kClockModeInternal"),
    ("kClockSourceMetronome", "kClockModeSyncedMetronome"),
    ("kClockSourceExternal", "kClockModeSyncedExternal"),
    ("kRateIndex", "kSyncDivisionIndex"),
    ("kClockSourceIndex", "kClockModeIndex"),
    ("rate_mode", "clock_mode"),     # "rate_mode", "timbre_rate_mode", .rate_mode., etc.
    ("clock_source", "clock_mode"),
]

FILES = {
    "operators/control/arpeggiator/arpeggiator_editor_shared.h": COMMON,
    "operators/control/pattern_seq/pattern_seq_editor_shared.h": COMMON,
    "operators/audio/audio_clip/audio_clip_editor.cpp": [
        ("kRateModeLabels", "kClockModeLabels"),
        ('{"free", "ext", "sync"}', '{"internal", "ext", "metro"}'),
        ("kRateMode", "kClockMode"),       # enum constant + ed::kRateMode refs
        ('set_p("rate_mode"', 'set_p("clock_mode"'),
    ],
    "tests/operators/test_note_modulator.cpp": COMMON,
    "tests/operators/test_euclidean_midi.cpp": COMMON,
    "tests/operators/test_pattern_seq_editor_helpers.cpp": COMMON,
    "tests/operators/test_pattern_seq_editor.cpp": COMMON,
    "tests/operators/test_arpeggiator_editor.cpp": COMMON,
    "tests/operators/test_audio_clip.cpp": COMMON,
    "tests/audio/test_audio_control_timing.cpp": COMMON,
    "tests/operators/test_drum_sequencer_song_mode.cpp": COMMON,
    "tests/operators/test_euclidean_editor.cpp": COMMON,
    "tests/operators/test_drum_sequencer_probability_roll.cpp": COMMON,
}


def main():
    for rel, pairs in FILES.items():
        f = ROOT / rel
        if not f.exists():
            print(f"  SKIP {rel}"); continue
        t = f.read_text()
        nt = t
        for old, new in pairs:
            nt = nt.replace(old, new)
        if nt != t:
            f.write_text(nt); print(f"  migrated {rel}")
        else:
            print(f"  no-op {rel}")


if __name__ == "__main__":
    main()
