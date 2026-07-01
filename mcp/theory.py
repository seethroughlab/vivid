"""Pure music-theory helpers for the Vivid MCP bridge — zero external dependencies.

Notes are MIDI integers 0..127. Convention: C4 = middle C = MIDI 60 (set MIDDLE_C_OCTAVE=3
for Ableton/Logic labelling). Note names are accepted as "C4", "F#3", "Bb5", "Db2" (sharps
`#` or `s`, flats `b`). Clip notes are dicts {p:pitch, s:startBeat, d:durBeats, v:velocity}
— the same shape the control server's set_clip/get_clip use.

Everything here is stateless + pure (functions of their args), so it's unit-tested directly
in test_theory.py without the app.
"""
from __future__ import annotations

MIDDLE_C_OCTAVE = 4   # octave label for MIDI 60

_PC = {"C": 0, "C#": 1, "DB": 1, "D": 2, "D#": 3, "EB": 3, "E": 4, "FB": 4, "E#": 5,
       "F": 5, "F#": 6, "GB": 6, "G": 7, "G#": 8, "AB": 8, "A": 9, "A#": 10, "BB": 10,
       "B": 11, "CB": 11, "B#": 0}
_NAMES_SHARP = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def _clamp(m: int) -> int:
    return 0 if m < 0 else (127 if m > 127 else m)


def parse_note(x) -> int:
    """A MIDI int (passed through, clamped) or a name like 'C4' / 'F#3' / 'Bb5' -> MIDI int."""
    if isinstance(x, bool):
        raise ValueError(f"bad note: {x!r}")
    if isinstance(x, (int, float)):
        return _clamp(int(x))
    s = str(x).strip()
    i = len(s)
    while i > 0 and (s[i - 1].isdigit() or s[i - 1] == "-"):
        i -= 1
    letter, octs = s[:i], s[i:]
    key = letter.upper().replace("S", "#")
    if key not in _PC or octs in ("", "-"):
        raise ValueError(f"bad note name: {x!r}")
    return _clamp(60 + (int(octs) - MIDDLE_C_OCTAVE) * 12 + _PC[key])


def pitch_class(x) -> int:
    """The pitch class 0..11 of a note (int or name). 'C'/'F#' with no octave also work."""
    if isinstance(x, str):
        key = x.strip().upper().replace("S", "#")
        if key in _PC:
            return _PC[key]
    return parse_note(x) % 12


def note_name(m: int) -> str:
    """MIDI int -> 'C4' style name (sharps)."""
    m = _clamp(int(m))
    return _NAMES_SHARP[m % 12] + str(m // 12 - 5 + MIDDLE_C_OCTAVE)


def norm_notes(notes) -> list[dict]:
    """Normalize a list of clip-note dicts: parse `p` (int or name) to a MIDI int, coerce the
    beat/velocity fields, and fill defaults. Returns fresh dicts ready for set_clip."""
    out = []
    for n in notes:
        out.append({
            "p": parse_note(n["p"]),
            "s": float(n.get("s", 0.0)),
            "d": float(n.get("d", 0.25)),
            "v": float(n.get("v", 0.8)),
        })
    return out
