"""Pure music-theory helpers for the Vivid MCP bridge — zero external dependencies.

Notes are MIDI integers 0..127. Convention: C4 = middle C = MIDI 60 (set MIDDLE_C_OCTAVE=3
for Ableton/Logic labelling). Note names are accepted as "C4", "F#3", "Bb5", "Db2" (sharps
`#` or `s`, flats `b`). Clip notes are dicts {p:pitch, s:startBeat, d:durBeats, v:velocity}
— the same shape the control server's set_clip/get_clip use.

Everything here is stateless + pure (functions of their args), so it's unit-tested directly
in test_theory.py without the app.
"""
from __future__ import annotations

import re

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


# --- Scales (pitch-class intervals from the root) ---
SCALES = {
    "major":            [0, 2, 4, 5, 7, 9, 11],
    "minor":            [0, 2, 3, 5, 7, 8, 10],   # natural minor
    "natural_minor":    [0, 2, 3, 5, 7, 8, 10],
    "harmonic_minor":   [0, 2, 3, 5, 7, 8, 11],
    "melodic_minor":    [0, 2, 3, 5, 7, 9, 11],
    "ionian":           [0, 2, 4, 5, 7, 9, 11],
    "dorian":           [0, 2, 3, 5, 7, 9, 10],
    "phrygian":         [0, 1, 3, 5, 7, 8, 10],
    "lydian":           [0, 2, 4, 6, 7, 9, 11],
    "mixolydian":       [0, 2, 4, 5, 7, 9, 10],
    "aeolian":          [0, 2, 3, 5, 7, 8, 10],
    "locrian":          [0, 1, 3, 5, 6, 8, 10],
    "pentatonic_major": [0, 2, 4, 7, 9],
    "pentatonic_minor": [0, 3, 5, 7, 10],
    "blues":            [0, 3, 5, 6, 7, 10],
    "whole_tone":       [0, 2, 4, 6, 8, 10],
    "chromatic":        list(range(12)),
}


def scale_pcs(root, scale: str = "major") -> list[int]:
    """The pitch classes (0..11) of a scale, e.g. scale_pcs('C','dorian')."""
    steps = SCALES.get(scale.lower())
    if steps is None:
        raise ValueError(f"unknown scale: {scale!r}")
    r = pitch_class(root)
    return [(r + s) % 12 for s in steps]


def scale_notes(root, scale: str = "major", octave: int = 4, count: int | None = None) -> list[int]:
    """MIDI notes of a scale ascending from `root` at `octave`. count defaults to one octave+1."""
    steps = SCALES.get(scale.lower())
    if steps is None:
        raise ValueError(f"unknown scale: {scale!r}")
    base = 60 + (octave - MIDDLE_C_OCTAVE) * 12 + pitch_class(root)
    n = count if count is not None else len(steps) + 1
    return [_clamp(base + steps[i % len(steps)] + 12 * (i // len(steps))) for i in range(n)]


# --- Chords ---
CHORD_INTERVALS = {
    "":      [0, 4, 7],        "maj":   [0, 4, 7],
    "m":     [0, 3, 7],        "dim":   [0, 3, 6],        "aug": [0, 4, 8],
    "sus2":  [0, 2, 7],        "sus4":  [0, 5, 7],        "sus": [0, 5, 7],
    "6":     [0, 4, 7, 9],     "m6":    [0, 3, 7, 9],
    "7":     [0, 4, 7, 10],    "maj7":  [0, 4, 7, 11],    "m7":  [0, 3, 7, 10],
    "m7b5":  [0, 3, 6, 10],    "dim7":  [0, 3, 6, 9],     "7sus4": [0, 5, 7, 10],
    "9":     [0, 4, 7, 10, 14], "maj9": [0, 4, 7, 11, 14], "m9": [0, 3, 7, 10, 14],
    "add9":  [0, 4, 7, 14],    "madd9": [0, 3, 7, 14],
    "11":    [0, 4, 7, 10, 14, 17], "m11": [0, 3, 7, 10, 14, 17],
    "13":    [0, 4, 7, 10, 14, 21], "maj13": [0, 4, 7, 11, 14, 21], "m13": [0, 3, 7, 10, 14, 21],
}


def _norm_quality(q: str) -> str:
    q = q.strip()
    q = q.replace("Δ", "maj7").replace("△", "maj7").replace("°", "dim").replace("ø", "m7b5")
    q = q.replace("Major", "maj").replace("major", "maj").replace("Maj", "maj").replace("MAJ", "maj")
    q = q.replace("Minor", "m").replace("minor", "m").replace("Min", "m").replace("MIN", "m").replace("min", "m")
    q = q.replace("Aug", "aug").replace("+", "aug")
    q = re.sub(r"M(?=7|9|11|13|6|$)", "maj", q)   # capital-M = major-7th marker (M7, bare M)
    if q.startswith("-"):
        q = "m" + q[1:]
    return q


def chord(symbol: str, octave: int = 4, inversion: int = 0, voicing: str = "close") -> list[int]:
    """A chord symbol -> MIDI pitches. Supports root (+#/b), quality (maj/m/dim/aug/sus2/sus4),
    extensions (6/7/maj7/m7/9/maj9/m9/add9/11/13/m7b5/dim7), a slash bass ("Cmaj7/G"), inversion,
    and voicing (close/open/drop2). Unknown qualities fall back to a major/minor triad."""
    sym = symbol.strip()
    bass = None
    if "/" in sym:
        sym, bass = (p.strip() for p in sym.split("/", 1))
    m = re.match(r"^([A-Ga-g])([#b]?)(.*)$", sym)
    if not m:
        raise ValueError(f"bad chord symbol: {symbol!r}")
    root = m.group(1).upper() + m.group(2)
    q = _norm_quality(m.group(3))
    intervals = CHORD_INTERVALS.get(q)
    if intervals is None:
        intervals = [0, 3, 7] if q.startswith("m") else [0, 4, 7]
    root_midi = parse_note(root + str(octave))
    pitches = [root_midi + iv for iv in intervals]
    for _ in range(inversion % max(1, len(pitches))):     # invert: raise the lowest note an octave
        pitches = pitches[1:] + [pitches[0] + 12]
    if voicing == "drop2" and len(pitches) >= 2:
        s = sorted(pitches); s[-2] -= 12; pitches = sorted(s)
    elif voicing == "open" and len(pitches) >= 3:
        s = sorted(pitches); s[1] += 12; pitches = sorted(s)
    if bass:
        low = min(pitches)
        bnote = pitch_class(bass) + 12 * (low // 12)
        while bnote >= low:
            bnote -= 12
        pitches = [bnote] + pitches
    return [_clamp(p) for p in pitches]


_ROMAN = {"i": 0, "ii": 1, "iii": 2, "iv": 3, "v": 4, "vi": 5, "vii": 6}


def roman(numeral: str, key, scale: str = "major", octave: int = 4) -> list[int]:
    """A roman-numeral degree -> MIDI pitches, diatonic to key/scale. A DIATONIC numeral (I..vii)
    stacks scale thirds, so the quality is automatic (I=maj, ii=min, vii°=dim in major). A
    trailing "7" adds the diatonic seventh ("V7"). An ACCIDENTAL prefix marks a borrowed chord
    ("bVII", "bIII"): a triad on the chromatic degree, major if the numeral is UPPER-case, minor
    if lower — e.g. bVII in C = Bb major."""
    s = numeral.strip()
    shift = 0
    while s and s[0] in "b#":
        shift += -1 if s[0] == "b" else 1
        s = s[1:]
    seventh = s.endswith("7")
    if seventh:
        s = s[:-1]
    deg = _ROMAN.get(s.lower())
    if deg is None:
        raise ValueError(f"bad roman numeral: {numeral!r}")
    steps = SCALES.get(scale.lower(), SCALES["major"])
    key_root = 60 + (octave - MIDDLE_C_OCTAVE) * 12 + pitch_class(key)

    def off(d):   # absolute semitone offset of scale degree d (wraps octaves)
        return steps[d % len(steps)] + 12 * (d // len(steps))

    if shift != 0:                                  # borrowed chord: quality from case
        root = key_root + off(deg) + shift
        out = [root + i for i in ([0, 4, 7] if s[:1].isupper() else [0, 3, 7])]
        if seventh:
            out.append(root + 10)
    else:                                           # diatonic: stack scale thirds
        idxs = [deg, deg + 2, deg + 4] + ([deg + 6] if seventh else [])
        out = [key_root + off(d) for d in idxs]
    return [_clamp(p) for p in out]


# --- Transforms (operate on clip-note lists; pure) ---
def transpose(notes, semitones: int) -> list[dict]:
    return [{**n, "p": _clamp(int(n["p"]) + int(semitones))} for n in notes]


def quantize_pitch(p: int, root, scale: str) -> int:
    """Snap a pitch to the nearest member of the scale (ties go up)."""
    pcs = set(scale_pcs(root, scale))
    if p % 12 in pcs:
        return p
    for d in (1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6):
        if (p + d) % 12 in pcs:
            return _clamp(p + d)
    return p


def quantize_to_scale(notes, root, scale: str = "major") -> list[dict]:
    return [{**n, "p": quantize_pitch(int(n["p"]), root, scale)} for n in notes]


def _scale_ladder(root, scale: str):
    pcs = set(scale_pcs(root, scale))
    ladder = [m for m in range(128) if m % 12 in pcs]
    return ladder, {m: i for i, m in enumerate(ladder)}


def harmonize(notes, degree: int = 2, root="C", scale: str = "major") -> list[dict]:
    """Add a diatonic harmony voice `degree` SCALE steps from each note (2 = a third above,
    4 = a fifth; negative = below). Off-scale notes fall back to a chromatic ~third. Returns
    the originals + the harmony voice."""
    ladder, pos = _scale_ladder(root, scale)
    fallback = 4 if degree > 0 else -4

    def harm(p):
        if p in pos:
            j = pos[p] + degree
            return ladder[j] if 0 <= j < len(ladder) else _clamp(p + fallback)
        return _clamp(p + fallback)

    return notes + [{**n, "p": harm(int(n["p"]))} for n in notes]


def invert(notes, axis=None) -> list[dict]:
    """Mirror pitches around `axis` (default = the first note): new = 2*axis - p."""
    ps = [int(n["p"]) for n in notes]
    if not ps:
        return notes
    a = int(axis) if axis is not None else ps[0]
    return [{**n, "p": _clamp(2 * a - int(n["p"]))} for n in notes]


def retrograde(notes, length: float) -> list[dict]:
    """Reverse in time within a clip of `length` beats (start -> length - start - dur)."""
    out = [{**n, "s": round(float(length) - float(n["s"]) - float(n["d"]), 6)} for n in notes]
    return sorted(out, key=lambda n: n["s"])


def arpeggiate(pitches, pattern: str = "up", rate: float = 0.25, octaves: int = 1,
               length: float = 4.0, vel: float = 0.8, start: float = 0.0) -> list[dict]:
    """Turn chord pitches into an arpeggio filling `length` beats. pattern = up|down|updown|
    downup; rate = beats per step; octaves stacks copies upward."""
    base = sorted({int(p) for p in pitches})
    if not base:
        return []
    seq = [p + 12 * o for o in range(max(1, octaves)) for p in base]
    if pattern == "down":
        seq = seq[::-1]
    elif pattern == "updown":
        seq = seq + seq[-2:0:-1]
    elif pattern == "downup":
        seq = seq[::-1] + seq[1:-1]
    steps = int(round(length / rate)) if rate > 0 else 0
    return [{"p": _clamp(seq[k % len(seq)]), "s": round(start + k * rate, 6), "d": rate, "v": vel}
            for k in range(steps)]
