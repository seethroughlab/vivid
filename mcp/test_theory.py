# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""Dependency-free selftest for theory.py. Run: uv run --directory mcp test_theory.py"""
import theory as T


def test_notes():
    assert T.parse_note(60) == 60
    assert T.parse_note("C4") == 60
    assert T.parse_note("A4") == 69          # A440
    assert T.parse_note("C5") == 72
    assert T.parse_note("C3") == 48
    assert T.parse_note("F#3") == 54
    assert T.parse_note("Gb3") == 54         # enharmonic with F#3
    assert T.parse_note("Bb4") == 70
    assert T.parse_note("Cs4") == 61         # 's' spelling for sharp
    assert T.note_name(60) == "C4"
    assert T.note_name(69) == "A4"
    assert T.note_name(61) == "C#4"
    for m in range(128):                     # round-trip every MIDI note
        assert T.parse_note(T.note_name(m)) == m
    assert T.pitch_class("F#") == 6 and T.pitch_class(72) == 0


def test_norm_notes():
    ns = T.norm_notes([{"p": "C4"}, {"p": 67, "s": 1, "d": 0.5, "v": 0.6}])
    assert ns[0] == {"p": 60, "s": 0.0, "d": 0.25, "v": 0.8}
    assert ns[1] == {"p": 67, "s": 1.0, "d": 0.5, "v": 0.6}


def test_chords():
    assert T.chord("C") == [60, 64, 67]
    assert T.chord("Cm") == [60, 63, 67]
    assert T.chord("C7") == [60, 64, 67, 70]
    assert T.chord("Cmaj7") == [60, 64, 67, 71]
    assert T.chord("CM7") == [60, 64, 67, 71]        # capital-M maj7 spelling
    assert T.chord("Cm7") == [60, 63, 67, 70]
    assert T.chord("Cdim") == [60, 63, 66]
    assert T.chord("Csus4") == [60, 65, 67]
    assert T.chord("Cadd9") == [60, 64, 67, 74]
    assert T.chord("Am") == [69, 72, 76]             # A4 root
    assert T.chord("G7") == [67, 71, 74, 77]
    assert T.chord("F#m7b5") == [66, 69, 72, 76]
    assert T.chord("C/G") == [55, 60, 64, 67]        # slash bass G3 below
    assert T.chord("C", inversion=1) == [64, 67, 72] # first inversion
    assert T.chord("Bb") == [70, 74, 77]             # flat root


def test_extended_chords():
    """The Lydian / altered / minor-major vocabulary. Every one of these used to fall through to
    a bare triad, and the "maj*" spellings came back MINOR because the fallback tested
    startswith("m") — Cmaj7#11 returned C minor with no error."""
    assert T.chord("Cmaj7#11") == [60, 64, 67, 71, 78]     # C E G B F# — was [60,63,67] (C MINOR)
    assert T.chord("Dbmaj7#11") == [61, 65, 68, 72, 79]    # was [61,64,68] (Db minor)
    assert T.chord("Fmaj9#11") == [65, 69, 72, 76, 79, 83]
    assert T.chord("G7#9") == [67, 71, 74, 77, 82]         # was a bare G major triad
    assert T.chord("E7b9") == [64, 68, 71, 74, 77]
    assert T.chord("C7#5") == [60, 64, 68, 70] == T.chord("Caug7")
    assert T.chord("C7b5") == [60, 64, 66, 70]
    assert T.chord("Galt") == [67, 71, 77, 82, 87]         # 1 3 b7 #9 b13
    assert T.chord("C9sus4") == [60, 65, 67, 70, 74]
    assert T.chord("Cadd11") == [60, 64, 67, 77]
    # Parenthesised spellings normalise to the bare quality.
    assert T.chord("Cm(maj7)") == T.chord("Cmmaj7") == [60, 63, 67, 71]
    assert T.chord("Am(maj7)") == [69, 72, 76, 80]
    assert T.chord("Cdim(maj7)") == [60, 63, 66, 71]
    # A slash INSIDE a quality is not a slash bass — "C6/9" used to raise "bad note name: '9'".
    assert T.chord("C6/9") == [60, 64, 67, 69, 74]
    assert T.chord("Cm6/9") == [60, 63, 67, 69, 74]
    # ...but a real slash bass still splits.
    assert T.chord("Cmaj7/E") == [52, 60, 64, 67, 71]
    assert T.chord("Bb/D") == [62, 70, 74, 77]


def test_unknown_quality_raises():
    """An unrecognised quality must ERROR, never resolve to a plausible-looking wrong chord —
    a caller can fix a bad symbol, but cannot tell that silently-wrong notes are wrong."""
    for bad in ("Cwobble", "Cmaj77", "Cxyz", "Dbogus"):
        try:
            T.chord(bad)
        except ValueError:
            pass
        else:
            raise AssertionError(f"chord({bad!r}) should have raised, not guessed a triad")


def test_scales():
    assert T.scale_pcs("C", "major") == [0, 2, 4, 5, 7, 9, 11]
    assert T.scale_pcs("A", "minor") == [9, 11, 0, 2, 4, 5, 7]
    assert T.scale_pcs("C", "dorian") == [0, 2, 3, 5, 7, 9, 10]
    assert T.scale_notes("C", "major", 4, 8) == [60, 62, 64, 65, 67, 69, 71, 72]
    assert T.scale_notes("C", "pentatonic_minor", 4, 5) == [60, 63, 65, 67, 70]


def test_roman():
    assert T.roman("I", "C", "major") == [60, 64, 67]     # C
    assert T.roman("ii", "C", "major") == [62, 65, 69]    # Dm
    assert T.roman("V7", "C", "major") == [67, 71, 74, 77]  # G7
    assert T.roman("vi", "C", "major") == [69, 72, 76]    # Am
    assert T.roman("i", "A", "minor") == [69, 72, 76]     # Am in A-minor
    assert T.roman("bVII", "C", "major") == [70, 74, 77]  # Bb major (borrowed)
    assert T.roman("bIII", "C", "major") == [63, 67, 70]  # Eb major (borrowed)


def test_roman_extended():
    assert T.roman("IVmaj7", "C") == [65, 69, 72, 76]       # F A C E
    assert T.roman("bVIImaj7", "C") == [70, 74, 77, 81]     # Bb D F A — was impossible
    assert T.roman("bVII7", "C") == [70, 74, 77, 80]        # dominant Bb7
    assert T.roman("bVImaj7", "C") == [68, 72, 75, 79]      # Ab C Eb G
    assert T.roman("vi9", "C") == T.chord("Am9", octave=4)  # case sets the third
    assert T.roman("ii11", "C") == T.chord("Dm11")
    assert T.roman("Iadd9", "C") == T.chord("Cadd9")
    assert T.roman("Vsus4", "C") == T.chord("Gsus4")
    assert T.roman("viiø7", "C") == T.chord("Bm7b5")       # suffix already sets the third
    assert T.chord("Bø7") == T.chord("Bø") == T.chord("Bm7b5")  # was "m7b57" -> ValueError
    assert T.chord("B°7") == T.chord("Bdim7")
    assert T.roman("bvi", "C") == T.chord("Abm")            # lower-case borrowed = minor
    assert T.roman("I", "C", voicing="open") == T.chord("C", voicing="open")
    assert T.roman("V7", "C", voicing="drop2") == T.chord("G7", voicing="drop2")
    for bad in ("IVxyz", "X", "bQ7"):
        try:
            T.roman(bad, "C")
        except ValueError:
            continue
        raise AssertionError(f"roman({bad!r}) should have raised")


def test_voice_leading():
    prog = T.progression(["IVmaj7", "V", "iii", "vi"], key="C")
    assert prog[0] == T.roman("IVmaj7", "C")                # first chord kept as voiced
    for a, b in zip(prog, prog[1:]):
        # the brief's bar: no voice moves more than a 4th between adjacent chords
        assert all(min(abs(n - p) for p in a) <= 5 for n in b), (a, b)
        assert all(min(abs(p - n) for n in b) <= 5 for p in a), (a, b)
        if {p % 12 for p in a} & {p % 12 for p in b}:       # common tones are held in place
            assert set(a) & set(b), (a, b)
    for c in prog[1:]:
        assert 48 <= min(c) <= 72
    # same pitch classes as the unvoiced chords
    for c, sym in zip(prog, ["IVmaj7", "V", "iii", "vi"]):
        assert {p % 12 for p in c} == {p % 12 for p in T.roman(sym, "C")}
    # no adjacent minor seconds in the longer progression from the brief
    long = T.progression(["IVmaj7", "V", "iii", "vi", "bVIImaj7", "IV", "ii9", "Iadd9"],
                         key="D", voicing="open", octave=3)
    for c in long:
        assert all(b - a != 1 for a, b in zip(c, c[1:])), c
    # voice_lead=False is the old block-chord output
    assert T.progression(["ii", "V7", "I"], key="C", voice_lead=False) == \
        [T.roman("ii", "C"), T.roman("V7", "C"), T.roman("I", "C")]
    # bass: the root as its own voice in C2..B2, under the voice-led upper structure
    withbass = T.progression(["IVmaj7", "V", "iii", "vi"], key="C", bass=True)
    assert [c[0] for c in withbass] == [41, 43, 40, 45]     # F2 G2 E2 A2
    assert [c[1:] for c in withbass] == prog
    # slash bass stays underneath and isn't voice-led
    sl = T.progression(["C", "Cmaj7/E", "F/A"])
    assert sl[1][0] % 12 == 4 and sl[1][0] < min(sl[1][1:])
    assert sl[2][0] % 12 == 9 and sl[2][0] < min(sl[2][1:])


def test_transforms():
    n = [{"p": 60, "s": 0.0, "d": 1.0, "v": 0.8}]
    assert T.transpose(n, 12)[0]["p"] == 72
    # quantize into C major
    assert T.quantize_pitch(60, "C", "major") == 60      # in-scale, unchanged
    assert T.quantize_pitch(61, "C", "major") == 62      # C# -> D
    assert T.quantize_pitch(66, "C", "major") == 67      # F# -> G
    # harmonize a diatonic third above C -> E
    h = T.harmonize([{"p": 60, "s": 0, "d": 1, "v": 0.8}], degree=2, root="C", scale="major")
    assert len(h) == 2 and h[1]["p"] == 64
    # invert around the first note
    inv = T.invert([{"p": 60, "s": 0, "d": 1, "v": 0.8}, {"p": 64, "s": 1, "d": 1, "v": 0.8}])
    assert [x["p"] for x in inv] == [60, 56]
    # retrograde within a 4-beat clip
    r = T.retrograde([{"p": 60, "s": 0.0, "d": 1.0, "v": 0.8}, {"p": 64, "s": 2.0, "d": 1.0, "v": 0.8}], 4.0)
    assert [(x["s"], x["p"]) for x in r] == [(1.0, 64), (3.0, 60)]
    # arpeggiate a C triad, up, 4 steps over 2 beats
    a = T.arpeggiate([60, 64, 67], "up", rate=0.5, octaves=1, length=2.0)
    assert [(x["s"], x["p"]) for x in a] == [(0.0, 60), (0.5, 64), (1.0, 67), (1.5, 60)]


def test_rhythm():
    # E(3,8) = the tresillo x..x..x. (hits at 0,3,6)
    assert T.euclidean(3, 8) == [True, False, False, True, False, False, True, False]
    assert T.euclidean(4, 8) == [True, False, True, False, True, False, True, False]
    assert T.euclidean(5, 8) == [True, False, True, False, True, True, False, True]
    assert T.euclidean(1, 4) == [True, False, False, False]
    # drum names
    assert T.drum_note("kick") == 36 and T.drum_note("hat") == 42
    assert T.drum_note("openhat") == 46 and T.drum_note("tom_lo") == 45
    assert T.drum_note(38) == 38
    # step-string -> notes (8 steps over 4 beats = 0.5/step; hits at 0,3,6)
    ds = T.drum_steps("x..x..x.", 42, 4.0)
    assert [(n["s"], n["p"]) for n in ds] == [(0.0, 42), (1.5, 42), (3.0, 42)]
    # velocity digit
    assert T.drum_steps("9", 36, 4.0)[0]["v"] == round(9 / 9.0, 4)
    # humanize keeps count + bounds
    src = [{"p": 36, "s": 1.0, "d": 0.1, "v": 0.8}]
    h = T.humanize(src, 0.05, 0.1, seed=1)
    assert len(h) == 1 and 0.0 <= h[0]["v"] <= 1.0 and h[0]["s"] >= 0.0
    # rhythm quantize
    assert T.quantize_rhythm([{"p": 60, "s": 0.13, "d": 0.1, "v": 0.8}], 0.25)[0]["s"] == 0.25


def test_analysis():
    # detect_key: a C-major-triad-heavy clip -> C major
    cmaj = [{"p": p, "s": i * 0.5, "d": 0.5, "v": 0.8} for i, p in enumerate([60, 64, 67, 60, 67, 72, 65, 67])]
    k = T.detect_key(cmaj)
    assert k["root"] == "C" and k["scale"] == "major", k
    # an A-minor clip -> A minor
    amin = [{"p": p, "s": i * 0.5, "d": 0.5, "v": 0.8} for i, p in enumerate([69, 72, 76, 69, 72, 67, 65, 69])]
    ka = T.detect_key(amin)
    assert ka["root"] == "A" and ka["scale"] == "minor", ka
    # best_chord + chords_per_bar: bar0 = C major, bar1 = G major
    assert T.best_chord([0, 4, 7]) == "C"
    assert T.best_chord([9, 0, 4]) == "Am"
    two = [{"p": 60, "s": 0, "d": 1, "v": 0.8}, {"p": 64, "s": 0, "d": 1, "v": 0.8}, {"p": 67, "s": 0, "d": 1, "v": 0.8},
           {"p": 67, "s": 4, "d": 1, "v": 0.8}, {"p": 71, "s": 4, "d": 1, "v": 0.8}, {"p": 74, "s": 4, "d": 1, "v": 0.8}]
    assert T.chords_per_bar(two, 8.0) == ["C", "G"]


TESTS = [test_notes, test_norm_notes, test_chords, test_extended_chords,
         test_unknown_quality_raises, test_scales, test_roman, test_roman_extended,
         test_voice_leading, test_transforms,
         test_rhythm, test_analysis]


def main() -> int:
    for t in TESTS:
        t()
        print(f"  OK {t.__name__}")
    print(f"theory selftest: OK ({len(TESTS)} groups)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
