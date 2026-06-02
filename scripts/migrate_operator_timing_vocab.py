#!/usr/bin/env python3
"""Rename timing params in operator SOURCE to the consolidated vocabulary.

Ordered, exact-substring replacements per operator group. Clean break: the old
spellings are removed entirely. Run once; re-running is a no-op (substrings gone).

  full   group: rate_mode {free,external,metronome}      -> clock_mode {internal,external,metronome}
  synced group: clock_source {external,metronome}         -> clock_mode (synced)
  tempo  group: rate_mode {free,sync} / sync {Free,Tempo} -> clock_mode {internal,metronome}
  fx     group: rate (Hz) -> frequency  (in addition to full-group mode rename)
  div    group: rate (9-entry division) -> sync_division (12-entry); kMultipliers expanded
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def sub_ordered(text, pairs):
    """Apply (old, new) replacements in order. Returns (text, total_count)."""
    total = 0
    for old, new in pairs:
        n = text.count(old)
        text = text.replace(old, new)
        total += n
    return text, total


def regex_sub(text, pattern, repl):
    return re.subn(pattern, repl, text)


# --- shared ordered fragments -------------------------------------------------
FULL_MODE = [
    ("rate_mode_labels()", "clock_mode_full_labels()"),
    ("kRateModeMetronome", "kClockModeMetronome"),
    ("kRateModeExternal", "kClockModeExternal"),
    ("kRateModeFree", "kClockModeInternal"),
    ("rate_mode", "clock_mode"),
]
SYNCED_MODE = [
    ("clock_source_labels()", "clock_mode_synced_labels()"),
    ("kClockSourceMetronome", "kClockModeSyncedMetronome"),
    ("kClockSourceExternal", "kClockModeSyncedExternal"),
    ("clock_source", "clock_mode"),
]
TEMPO_MODE = [  # Clock: rate_mode {free,sync}; labels already inline {"free","sync"}
    ('{"rate_mode", 0, {"free", "sync"}}', '{"clock_mode", 0, vivid::clock_mode_tempo_labels()}'),
    ("kRateModeMetronome", "kClockModeMetronome"),
    ("rate_mode", "clock_mode"),
]

# Per-file rules. Each entry: (relative_path, [ordered (old,new) pairs], [optional regex (pattern,repl)]).
FILES = {
    # ---- full-shape (rate_mode -> clock_mode) ----
    "operators/control/lfo/lfo.h": FULL_MODE,
    "operators/control/lfo/lfo.cpp": FULL_MODE,
    "operators/control/sequencer/sequencer_core.h": FULL_MODE,
    "operators/audio/audio_clip/audio_clip.h": FULL_MODE,
    "operators/audio/audio_clip/audio_clip.cpp": FULL_MODE,
    "operators/control/note_modulator/note_modulator.cpp": FULL_MODE,
    # ---- synced-shape (clock_source -> clock_mode) ----
    "operators/control/envelope/envelope.h": SYNCED_MODE,
    "operators/control/envelope/envelope.cpp": SYNCED_MODE,
    "operators/control/mseg/mseg.h": SYNCED_MODE,
    "operators/control/mseg/mseg.cpp": SYNCED_MODE,
    "operators/control/drum_sequencer/drum_sequencer_core.h": SYNCED_MODE,
    "operators/control/drum_sequencer/drum_sequencer_core.cpp": SYNCED_MODE,
    "operators/control/drum_sequencer/drum_sequencer.cpp": SYNCED_MODE,
    "operators/control/chord_progression/chord_progression_core.h": SYNCED_MODE,
    "operators/control/note_pattern/note_pattern_core.h": SYNCED_MODE,
    "operators/control/alternate/alternate.cpp": SYNCED_MODE,
    "operators/control/state_machine/state_machine.cpp": SYNCED_MODE,
    "operators/control/phase_to_midi/phase_to_midi.cpp": SYNCED_MODE,
    # ---- tempo-shape ----
    "operators/control/clock/clock.h": TEMPO_MODE,
    # clock.cpp uses a local int var named rate_mode reading param idx 2; rename it.
    "operators/control/clock/clock.cpp": [("rate_mode", "clock_mode")],
    # ---- fx group: mode rename here; rate->frequency done manually ----
    "operators/audio/flanger/flanger.cpp": FULL_MODE,
    "operators/audio/phaser/phaser.cpp": FULL_MODE,
    "operators/audio/chorus/chorus.cpp": FULL_MODE,
    # ---- div group: mode rename here; rate->sync_division + kMultipliers done manually ----
    "operators/control/arpeggiator/arpeggiator_core.h": SYNCED_MODE,
    "operators/control/arpeggiator/arpeggiator.cpp": SYNCED_MODE,
    "operators/control/pattern_seq/pattern_seq_core.h": SYNCED_MODE,
    "operators/control/pattern_seq/pattern_seq.cpp": SYNCED_MODE,
    "operators/control/euclidean/euclidean_core.h": SYNCED_MODE,
    "operators/control/euclidean/euclidean.cpp": SYNCED_MODE,
    "operators/control/tracker/tracker_core.h": SYNCED_MODE,
    "operators/control/tracker/tracker_core.cpp": SYNCED_MODE,
    "operators/control/tracker/tracker.cpp": SYNCED_MODE,
}


def craft_chord_progression_note_pattern():
    """chord_progression/note_pattern .cpp files may also reference clock_source
    in their non-core .cpp; add them if present."""
    extra = {}
    for op in ("chord_progression", "note_pattern"):
        for cpp in (ROOT / "operators/control" / op).glob("*.cpp"):
            txt = cpp.read_text()
            if "clock_source" in txt or "kClockSource" in txt:
                extra[str(cpp.relative_to(ROOT))] = SYNCED_MODE
    return extra


def main():
    rules = dict(FILES)
    rules.update(craft_chord_progression_note_pattern())
    for rel, pairs in rules.items():
        f = ROOT / rel
        if not f.exists():
            print(f"  SKIP (missing): {rel}")
            continue
        text = f.read_text()
        new_text, count = sub_ordered(text, pairs)
        if new_text != text:
            f.write_text(new_text)
            print(f"  migrated {rel} ({count} substitutions)")
        else:
            print(f"  no-op {rel}")


if __name__ == "__main__":
    main()
