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


TESTS = [test_notes, test_norm_notes]


def main() -> int:
    for t in TESTS:
        t()
        print(f"  OK {t.__name__}")
    print(f"theory selftest: OK ({len(TESTS)} groups)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
