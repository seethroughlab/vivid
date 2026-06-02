#!/usr/bin/env python3
"""Add semantic_tag to timing INPUT ports so the node-graph editor badges them
(beat_phase -> clock triangle, trigger -> ring, gate -> filled disc). Purely
additive; the UI badging is inert until these tags are present.

Tags only INPUT scalar ports named beat_phase / gate / reset. Output ports and
already-tagged (multi-field) ports are left alone.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Every operator file that declares a timing input port.
FILES = [
    "operators/control/lfo/lfo.h",
    "operators/control/sequencer/sequencer_core.h",
    "operators/control/envelope/envelope.h",
    "operators/control/mseg/mseg.h",
    "operators/control/arpeggiator/arpeggiator_core.h",
    "operators/control/pattern_seq/pattern_seq_core.h",
    "operators/control/euclidean/euclidean_core.h",
    "operators/control/tracker/tracker_core.cpp",
    "operators/control/drum_sequencer/drum_sequencer_core.cpp",
    "operators/control/chord_progression/chord_progression_core.h",
    "operators/control/note_pattern/note_pattern_core.h",
    "operators/control/note_modulator/note_modulator.cpp",
    "operators/control/phase_to_midi/phase_to_midi.cpp",
    "operators/control/alternate/alternate.cpp",
    "operators/control/state_machine/state_machine.cpp",
    "operators/control/macro/macro.h",
    "operators/control/step_counter/step_counter.cpp",
    "operators/audio/flanger/flanger.cpp",
    "operators/audio/phaser/phaser.cpp",
    "operators/audio/chorus/chorus.cpp",
    "operators/audio/audio_clip/audio_clip.cpp",
]

TAG = {"beat_phase": "beat_phase", "gate": "gate", "reset": "trigger"}

# 3-arg scalar INPUT aggregate: {"<name>", VIVID_PORT_SCALAR, VIVID_PORT_INPUT}
def three_arg(name, tag):
    pat = re.compile(r'\{"' + name + r'",(\s*)VIVID_PORT_SCALAR,(\s*)VIVID_PORT_INPUT\}')
    repl = (r'{"' + name + r'",\1VIVID_PORT_SCALAR,\2VIVID_PORT_INPUT, '
            r'VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "' + tag + r'"}')
    return pat, repl

# FX 8-field form: {"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f}
FX8 = re.compile(r'(\{"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,\s*'
                 r'VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0\.0f)\}')
# audio_clip multiline: {"beat_phase", VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,\n   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f}
AC = re.compile(r'(\{"beat_phase", VIVID_PORT_SCALAR,\s*VIVID_PORT_INPUT,\s*'
                r'VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0\.0f)\}')


def main():
    for rel in FILES:
        f = ROOT / rel
        if not f.exists():
            print(f"  SKIP {rel}"); continue
        t = f.read_text()
        orig = t
        for name, tag in TAG.items():
            pat, repl = three_arg(name, tag)
            t = pat.sub(repl, t)
        t = FX8.sub(r'\1, nullptr, "beat_phase"}', t)
        t = AC.sub(r'\1, nullptr, "beat_phase"}', t)
        if t != orig:
            f.write_text(t)
            print(f"  tagged {rel}")
        else:
            print(f"  no-op  {rel}")


if __name__ == "__main__":
    main()
