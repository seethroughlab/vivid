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


TESTS = [test_notes, test_norm_notes, test_chords, test_scales, test_roman, test_transforms, test_rhythm]


def main() -> int:
    for t in TESTS:
        t()
        print(f"  OK {t.__name__}")
    print(f"theory selftest: OK ({len(TESTS)} groups)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
