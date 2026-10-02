"""Pure music-theory helpers for the Vivid MCP bridge — zero external dependencies.

Notes are MIDI integers 0..127. Convention: C4 = middle C = MIDI 60 (set MIDDLE_C_OCTAVE=3
for Ableton/Logic labelling). Note names are accepted as "C4", "F#3", "Bb5", "Db2" (sharps
`#` or `s`, flats `b`). Clip notes are dicts {p:pitch, s:startBeat, d:durBeats, v:velocity}
— the same shape the control server's set_clip/get_clip use.

Everything here is stateless + pure (functions of their args), so it's unit-tested directly
in test_theory.py without the app.
"""
from __future__ import annotations

import random
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
        d = {
            "p": parse_note(n["p"]),
            "s": float(n.get("s", 0.0)),
            "d": float(n.get("d", 0.25)),
            "v": float(n.get("v", 0.8)),
        }
        # Pass through optional painted per-note expression curves (M3): each axis is a
        # list of [t, v] pairs (t=0..1 within the note; bend v=semitones, others 0..1).
        for axis in ("bend", "pressure", "timbre"):
            if axis in n and n[axis]:
                d[axis] = [[float(t), float(v)] for t, v in n[axis]]
        out.append(d)
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
    # Lydian / altered / minor-major colour. This is the vocabulary the reference literature on
    # Depeche Mode, Beach House, Reznor-Ross and Boards of Canada actually trades in; without it
    # `chord()` fell through to a bare triad (and, worse, "maj*" tripped the startswith("m")
    # minor test, so Cmaj7#11 returned C MINOR).
    "maj7#11": [0, 4, 7, 11, 18], "maj9#11": [0, 4, 7, 11, 14, 18], "7#11": [0, 4, 7, 10, 18],
    "maj7b5":  [0, 4, 6, 11],
    "7#9":   [0, 4, 7, 10, 15], "7b9": [0, 4, 7, 10, 13],
    "7#5":   [0, 4, 8, 10],     "7b5": [0, 4, 6, 10],   "aug7": [0, 4, 8, 10],
    "alt":   [0, 4, 10, 15, 20],                        # 1 3 b7 #9 b13
    "mmaj7": [0, 3, 7, 11],     "mmaj9": [0, 3, 7, 11, 14], "dimmaj7": [0, 3, 6, 11],
    "69":    [0, 4, 7, 9, 14],  "m69": [0, 3, 7, 9, 14],
    "add11": [0, 4, 7, 17],     "add13": [0, 4, 7, 21],
    "9sus4": [0, 5, 7, 10, 14], "sus2sus4": [0, 2, 5, 7],
}


def _norm_quality(q: str) -> str:
    q = q.strip()
    # Parenthesised qualities are a spelling, not structure: "m(maj7)" == "mmaj7", "C(add9)" ==
    # "Cadd9". Strip before the substitutions below so the table lookup sees one canonical form.
    q = q.replace("(", "").replace(")", "")
    q = q.replace("6/9", "69")          # a slash INSIDE a quality, not a slash bass
    q = q.replace("ø7", "ø").replace("°7", "dim7")   # ø already implies the 7th ("Bø7" == "Bø")
    q = q.replace("Δ", "maj7").replace("△", "maj7").replace("°", "dim").replace("ø", "m7b5")
    q = q.replace("Major", "maj").replace("major", "maj").replace("Maj", "maj").replace("MAJ", "maj")
    q = q.replace("Minor", "m").replace("minor", "m").replace("Min", "m").replace("MIN", "m").replace("min", "m")
    q = q.replace("Aug", "aug").replace("+", "aug")
    q = re.sub(r"M(?=7|9|11|13|6|$)", "maj", q)   # capital-M = major-7th marker (M7, bare M)
    if q.startswith("-"):
        q = "m" + q[1:]
    return q


def _apply_voicing(pitches: list[int], inversion: int = 0, voicing: str = "close") -> list[int]:
    """Invert (raise the lowest note an octave, `inversion` times), then voice: drop2 drops the
    second-highest note an octave; open raises the second-lowest an octave."""
    for _ in range(inversion % max(1, len(pitches))):
        pitches = pitches[1:] + [pitches[0] + 12]
    if voicing == "drop2" and len(pitches) >= 2:
        s = sorted(pitches); s[-2] -= 12; pitches = sorted(s)
    elif voicing == "open" and len(pitches) >= 3:
        s = sorted(pitches); s[1] += 12; pitches = sorted(s)
    return pitches


def _build(root_midi: int, quality: str, inversion: int = 0, voicing: str = "close",
           symbol: str = "") -> list[int]:
    """root MIDI note + a raw quality string ("maj7", "m9", "add9", …) -> voiced pitches (unclamped).
    The one place qualities are resolved, shared by chord() and roman()."""
    q = _norm_quality(quality)
    intervals = CHORD_INTERVALS.get(q)
    if intervals is None:
        # RAISE rather than guess. The old fallback returned a plausible-looking triad for any
        # unrecognised quality, so a wrong chord was indistinguishable from a right one — and
        # because the test was startswith("m"), every unknown "maj…" became a MINOR triad
        # (Cmaj7#11 -> C minor). A caller that gets an error can fix the symbol; a caller that
        # gets the wrong notes cannot even tell.
        raise ValueError(
            f"unknown chord quality {q!r} in {symbol or quality!r}; supported: "
            + ", ".join(sorted(k for k in CHORD_INTERVALS if k))
        )
    return _apply_voicing([root_midi + iv for iv in intervals], inversion, voicing)


def chord_parts(symbol: str, octave: int = 4, inversion: int = 0,
                voicing: str = "close") -> tuple[list[int], int | None, int]:
    """A chord symbol -> (upper-structure pitches, slash-bass MIDI note or None, root pitch class).
    chord() is `[bass] + upper`; voice leading moves only `upper` and keeps the bass underneath."""
    sym = symbol.strip()
    bass = None
    if "/" in sym:
        head, tail = (p.strip() for p in sym.rsplit("/", 1))
        # Only a slash BASS, never a slash inside a quality ("C6/9", "Cmaj7#11/9"): split just when
        # the right side is a note name. Previously "C6/9" split into quality "6" + bass "9" and
        # raised "bad note name: '9'".
        if re.fullmatch(r"[A-Ga-g][#b]?", tail):
            sym, bass = head, tail
    m = re.match(r"^([A-Ga-g])([#b]?)(.*)$", sym)
    if not m:
        raise ValueError(f"bad chord symbol: {symbol!r}")
    root = m.group(1).upper() + m.group(2)
    root_midi = parse_note(root + str(octave))
    pitches = _build(root_midi, m.group(3), inversion, voicing, symbol)
    bnote = None
    if bass:
        low = min(pitches)
        bnote = pitch_class(bass) + 12 * (low // 12)
        while bnote >= low:
            bnote -= 12
    return [_clamp(p) for p in pitches], (None if bnote is None else _clamp(bnote)), root_midi % 12


def chord(symbol: str, octave: int = 4, inversion: int = 0, voicing: str = "close") -> list[int]:
    """A chord symbol -> MIDI pitches. Supports root (+#/b), quality (maj/m/dim/aug/sus2/sus4),
    extensions (6/7/maj7/m7/9/maj9/m9/add9/11/13/m7b5/dim7), a slash bass ("Cmaj7/G"), inversion,
    and voicing (close/open/drop2). Unknown qualities raise ValueError."""
    upper, bass, _ = chord_parts(symbol, octave, inversion, voicing)
    return ([bass] if bass is not None else []) + upper


_ROMAN = {"i": 0, "ii": 1, "iii": 2, "iv": 3, "v": 4, "vi": 5, "vii": 6}
_ROMAN_RE = re.compile(r"^([b#]*)(VII|VI|V|IV|III|II|I|vii|vi|v|iv|iii|ii|i)(.*)$")
# Suffixes that already fix the third, so a lower-case numeral must NOT prepend "m".
_THIRD_SET = ("m", "dim", "°", "ø", "sus", "aug", "+")


def roman_parts(numeral: str, key, scale: str = "major", octave: int = 4, inversion: int = 0,
                voicing: str = "close") -> tuple[list[int], int]:
    """roman() plus the chord's root pitch class: (pitches, root_pc)."""
    m = _ROMAN_RE.match(numeral.strip())
    if not m:
        raise ValueError(f"bad roman numeral: {numeral!r}")
    acc, num, suffix = m.groups()
    shift = acc.count("#") - acc.count("b")
    deg = _ROMAN[num.lower()]
    steps = SCALES.get(scale.lower(), SCALES["major"])
    key_root = 60 + (octave - MIDDLE_C_OCTAVE) * 12 + pitch_class(key)

    def off(d):   # absolute semitone offset of scale degree d (wraps octaves)
        return steps[d % len(steps)] + 12 * (d // len(steps))

    root = key_root + off(deg) + shift
    if shift == 0 and suffix in ("", "7"):          # diatonic: stack scale thirds
        idxs = [deg, deg + 2, deg + 4] + ([deg + 6] if suffix == "7" else [])
        out = _apply_voicing([key_root + off(d) for d in idxs], inversion, voicing)
    else:                                           # explicit quality: case sets the third
        q = suffix
        if num.islower() and not suffix.startswith(_THIRD_SET):
            q = "m" + suffix
        out = _build(root, q, inversion, voicing, numeral)
    return [_clamp(p) for p in out], root % 12


def roman(numeral: str, key, scale: str = "major", octave: int = 4, inversion: int = 0,
          voicing: str = "close") -> list[int]:
    """A roman-numeral degree -> MIDI pitches in key/scale.

    - A bare DIATONIC numeral (I..vii, optional trailing "7") stacks scale thirds, so the quality
      is automatic: I=maj, ii=min, vii=dim, V7=dominant, IV7=maj7 (in major).
    - Any other suffix names the quality explicitly, using chord()'s vocabulary: "IVmaj7", "vi9",
      "ii11", "Iadd9", "Vsus4", "bVIImaj7". The numeral's CASE sets the third: lower-case prepends
      "m" (vi9 = m9, ii11 = m11) unless the suffix already sets it (dim/°/ø/m7b5/sus/aug).
    - An ACCIDENTAL prefix marks a borrowed chord on the chromatic degree ("bVII" = Bb in C, "bvi"
      = Ab minor); quality from case + suffix, so "bVII7" is a dominant Bb7 and "bVIImaj7" Bbmaj7.
    `inversion` / `voicing` (close/open/drop2) apply as in chord()."""
    return roman_parts(numeral, key, scale, octave, inversion, voicing)[0]


# --- Voice leading ---
def voice_lead(prev: list[int], chord: list[int], low: int = 48, high: int = 72,
               clash_penalty: int = 4) -> list[int]:
    """Re-voice `chord` (any octave placement of its pitch classes) to move least from `prev`.

    Cost = symmetric nearest-voice distance (each new note to its nearest old note + each old note
    to its nearest new note), so it handles different voice counts and keeps common tones (cost 0).
    Each adjacent minor 2nd adds `clash_penalty` (semitones of movement) — clusters are mud in a
    pad. The lowest note must lie in [low, high] (default C3..C5). Ties prefer keeping prev's span,
    so an open voicing stays open."""
    import itertools
    if not prev:
        return sorted(chord)
    pcs = list(dict.fromkeys(p % 12 for p in chord))
    lo_b = min(min(prev) - 12, low)
    hi_b = max(prev) + 12
    opts = [[m for m in range(lo_b, hi_b + 1) if m % 12 == pc] for pc in pcs]
    prev_span = max(prev) - min(prev)
    prev_mid = sum(prev) / len(prev)

    def cost(c):
        move = (sum(min(abs(n - p) for p in prev) for n in c)
                + sum(min(abs(p - n) for n in c) for p in prev))
        s = sorted(c)
        # Adjacent minor seconds read as mud in a sustained pad: worth a few semitones of movement.
        clash = sum(1 for a, b in zip(s, s[1:]) if b - a == 1)
        return (move + clash_penalty * clash, abs((max(c) - min(c)) - prev_span),
                abs(sum(c) / len(c) - prev_mid))

    best = None
    for combo in itertools.product(*opts):
        if not low <= min(combo) <= high:
            continue
        k = cost(combo)
        if best is None or k < best[0]:
            best = (k, combo)
    if best is None:                       # prev far out of range: fall back to the chord as given
        return sorted(chord)
    return sorted(_clamp(p) for p in best[1])


def voice_lead_progression(chords: list[list[int]], low: int = 48,
                           high: int = 72) -> list[list[int]]:
    """Keep the first chord exactly as voiced; voice-lead each next chord from the previous one."""
    out: list[list[int]] = []
    for c in chords:
        out.append(sorted(c) if not out else voice_lead(out[-1], c, low, high))
    return out


def progression(chords: list[str], key: str = "", scale: str = "major", octave: int = 4,
                voicing: str = "close", voice_lead: bool = True, bass: bool = False) -> list[list[int]]:
    """A list of roman numerals (if `key`) or chord symbols -> one pitch list per chord.

    `voicing` shapes the FIRST chord; with `voice_lead` each later chord is re-voiced to move least
    from the one before (so the opening shape carries through). A slash bass stays underneath and
    is not voice-led. `bass` adds the chord root (or slash bass) as its own voice in C2..B2."""
    uppers, basses, roots = [], [], []
    for sym in chords:
        if key:
            up, root = roman_parts(sym, key, scale, octave, voicing=voicing)
            b = None
        else:
            up, b, root = chord_parts(sym, octave=octave, voicing=voicing)
        uppers.append(up); basses.append(b); roots.append(root)
    if voice_lead:
        uppers = voice_lead_progression(uppers)
    out = []
    for up, b, root in zip(uppers, basses, roots):
        if bass:
            pc = b % 12 if b is not None else root
            out.append([36 + pc] + sorted(up))          # C2..B2
        elif b is not None:
            bn = b
            while bn >= min(up):
                bn -= 12
            while bn + 12 < min(up):
                bn += 12
            out.append([_clamp(bn)] + sorted(up))
        else:
            out.append(sorted(up))
    return out


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


# --- Rhythm ---
def euclidean(pulses: int, steps: int, rotation: int = 0) -> list[bool]:
    """A maximally-even (Euclidean) onset pattern: `pulses` hits spread over `steps`.
    E(3,8) -> the tresillo x..x..x. . `rotation` rotates the pattern right."""
    if steps <= 0:
        return []
    pulses = max(0, min(pulses, steps))
    pat = [(i * pulses) % steps < pulses for i in range(steps)]
    r = rotation % steps
    return pat[-r:] + pat[:-r] if r else pat


GM_DRUMS = {
    "kick": 36, "bd": 36, "bassdrum": 36,
    "snare": 38, "sd": 38,
    "rim": 37, "rimshot": 37, "sidestick": 37,
    "clap": 39,
    "hat": 42, "closedhat": 42, "hihat": 42, "hh": 42, "ch": 42,
    "openhat": 46, "oh": 46,
    "tom_lo": 45, "lowtom": 45, "tom_mid": 47, "midtom": 47, "tom_hi": 50, "hitom": 50,
    "crash": 49, "ride": 51, "cowbell": 56, "tambourine": 54,
}


def drum_note(name) -> int:
    """A drum name (kick/snare/hat/openhat/…) or a raw MIDI int -> MIDI note."""
    if isinstance(name, (int, float)) and not isinstance(name, bool):
        return _clamp(int(name))
    key = str(name).strip().lower().replace(" ", "").replace("-", "").replace("_", "")
    # try the normalized key, then with underscores preserved for tom_lo etc.
    if key in GM_DRUMS:
        return GM_DRUMS[key]
    k2 = str(name).strip().lower().replace(" ", "_").replace("-", "_")
    if k2 in GM_DRUMS:
        return GM_DRUMS[k2]
    raise ValueError(f"unknown drum: {name!r}")


def drum_steps(pattern: str, note: int, bar_beats: float = 4.0, vel: float = 0.8,
               dur: float = 0.1, start: float = 0.0) -> list[dict]:
    """A step-string ("x..x..x.", digit = velocity 1..9, '.'/'-'/' ' = rest) -> notes on `note`,
    spread evenly across `bar_beats`."""
    n = len(pattern)
    if n == 0:
        return []
    step = bar_beats / n
    out = []
    for i, ch in enumerate(pattern):
        if ch in ".-_ ":
            continue
        if ch.isdigit():
            if int(ch) == 0:
                continue
            v = int(ch) / 9.0
        else:
            v = vel
        out.append({"p": note, "s": round(start + i * step, 6), "d": dur, "v": round(v, 4)})
    return out


def humanize(notes, timing: float = 0.02, velocity: float = 0.1, seed: int = 0) -> list[dict]:
    """Add small (seeded, reproducible) random offsets to start + velocity for feel."""
    rng = random.Random(seed)
    out = []
    for n in notes:
        s = max(0.0, float(n["s"]) + rng.uniform(-timing, timing))
        v = min(1.0, max(0.0, float(n["v"]) + rng.uniform(-velocity, velocity)))
        out.append({**n, "s": round(s, 6), "v": round(v, 4)})
    return out


def quantize_rhythm(notes, grid: float = 0.25) -> list[dict]:
    """Snap note starts to a beat grid (0.25 = 16ths at 4/4)."""
    g = grid if grid > 0 else 0.25
    return [{**n, "s": round(round(float(n["s"]) / g) * g, 6)} for n in notes]


# --- Analysis (heuristic) ---
_KS_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
_KS_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]


def _corr(a, b) -> float:
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    num = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    da = sum((x - ma) ** 2 for x in a) ** 0.5
    db = sum((x - mb) ** 2 for x in b) ** 0.5
    return num / (da * db) if da and db else 0.0


def pc_histogram(notes) -> list[float]:
    """12-bin pitch-class histogram weighted by note duration."""
    h = [0.0] * 12
    for n in notes:
        h[parse_note(n["p"]) % 12] += float(n.get("d", 0.25))
    return h


def detect_key(notes) -> dict:
    """Best-fit key by Krumhansl–Schmuckler profile correlation. Accepts clip notes or a
    12-bin histogram. Heuristic — short/ambiguous clips are unreliable."""
    h = notes if (len(notes) == 12 and all(isinstance(x, (int, float)) for x in notes)) \
        else pc_histogram(notes)
    best = None
    for root in range(12):
        rot = [h[(root + i) % 12] for i in range(12)]
        for mode, prof in (("major", _KS_MAJOR), ("minor", _KS_MINOR)):
            c = _corr(rot, prof)
            if best is None or c > best[0]:
                best = (c, root, mode)
    return {"root": _NAMES_SHARP[best[1]], "scale": best[2], "confidence": round(best[0], 3)}


_TRIADS = {"": [0, 4, 7], "m": [0, 3, 7], "dim": [0, 3, 6]}


def best_chord(pcs) -> str | None:
    """The triad (root+quality) that best covers a set of pitch classes."""
    pset = set(pcs)
    if not pset:
        return None
    best = None
    for root in range(12):
        for suf, iv in _TRIADS.items():
            tones = {(root + i) % 12 for i in iv}
            score = len(tones & pset) - 0.25 * len(pset - tones)   # coverage, mild penalty for extras
            if best is None or score > best[0]:
                best = (score, root, suf)
    return _NAMES_SHARP[best[1]] + best[2]


def chords_per_bar(notes, length: float, bar_beats: float = 4.0) -> list:
    """A best-fit chord label per bar (heuristic)."""
    nbars = max(1, int((float(length) + bar_beats - 1e-3) // bar_beats))
    out = []
    for b in range(nbars):
        lo, hi = b * bar_beats, (b + 1) * bar_beats
        pcs = [parse_note(n["p"]) % 12 for n in notes if lo <= float(n["s"]) < hi]
        out.append(best_chord(pcs))
    return out
