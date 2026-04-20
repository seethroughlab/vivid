"""Regression tests for mcp/analysis — report structure, metadata parsing.

Runs without invoking the heavy analyzers (librosa, basic-pitch). Feeds a
synthetic analysis dict shaped like real pipeline output into generate_report
and asserts the expected sections + collapsibles appear.

Guards against:
  - Accidentally trimming a section out of generate_report.
  - Regression of the Vorbis list-valued tag bug (artist rendered as "['Ochre']").
  - Cache-version drift leaving stale sidecars readable as current.
"""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from analysis.reports import generate_report, generate_llm_report
from analysis.cache import CACHE_VERSION, _unwrap_tag
from analysis.pipeline import extract_feature_scalars


def _synthetic_analysis() -> dict:
    """Fake analysis dict with every field generate_report renders."""
    return {
        "harmonic": {
            "harmonic_content": True,
            "harmonic_rhythm": 5.05,
            "key_stability": "stable",
            "modal_character": "C# Phrygian",
            "modal_confidence": 0.78,
            "roman_numeral_chords": [
                {"roman": "BVImaj7", "chord": "Amaj7", "percentage": 24.8},
                {"roman": "IVmaj7", "chord": "F#maj7", "percentage": 12.0},
            ],
            "detected_progressions": [{"name": "I-V-vi-IV", "occurrences": 1}],
            "key_mode_timeline": [
                {"start": 0, "end": 32, "key_mode": "C# Locrian", "confidence": 0.77},
                {"start": 32, "end": 268, "key_mode": "C# Phrygian", "confidence": 0.78,
                 "conf_min": 0.77, "conf_max": 0.79},
            ],
            "chords": [
                {"time": 0.0, "chord": "Amaj7", "confidence": 0.63},
                {"time": 1.2, "chord": "F#maj7", "confidence": 0.73},
            ],
        },
        "rhythmic": {
            "has_clear_beat": True,
            "bpm": 117.5,
            "has_clear_beat": True,
            "swing_ratio": 54.6,
            "syncopation_index": 0.209,
            "tempo_stability": "breathing",
            "tempo_cv": 0.297,
            "density_shape": "thinning",
        },
        "spectral": {
            "brightness": "neutral",
            "centroid_hz": 2256.3,
            "rolloff_hz": 4976.9,
            "band_energy": {"sub_bass": 38.6, "bass": 39.9, "low_mid": 12.4,
                            "mid": 6.5, "high_mid": 2.0, "high": 0.3},
            "flatness": 0.0295,
            "mfcc_mean": [-1702.459, 278.197, 15.229, 70.304, -12.317, 53.417,
                          4.117, 49.379, -5.387, 39.536, -7.207, 33.430, -6.847],
        },
        "structural": {
            "section_count": 13,
            "form": "AAAAAAAAAAAAB",
            "avg_section_length": 25.53,
            "segments": [
                {"start": 0, "end": 8.22, "duration": 8.22, "label": "A"},
                {"start": 318.85, "end": 331.9, "duration": 13.15, "label": "B"},
            ],
            "section_profiles": [
                {"start": 0, "end": 8.22, "rms_db": -9.8, "brightness": "neutral", "density": "dense"},
                {"start": 318.85, "end": 331.9, "rms_db": -26.3, "brightness": "bright", "density": "dense"},
            ],
        },
        "energy": {
            "dynamic_range_db": 8.9,
            "energy_shape": "dynamic",
            "rms_mean_db": -12.0,
            "rms_peak_db": -5.5,
        },
        "melodic": {
            "pitch_range": {"low_note": "D#1", "high_note": "C#6",
                            "primary_low": "F#1", "primary_high": "B3", "low": 27, "high": 85},
            "interval_character": "leap-heavy",
            "avg_interval_size": 7.32,
            "unison_pct": 21.4,
            "interval_character_no_unison": "leap-heavy",
            "avg_interval_size_no_unison": 9.3,
            "phrase_count": 56,
            "avg_phrase_length_seconds": 4.34,
            "dominant_contour": "flat",
            "contour_summary": {"flat": 68.0, "ascending": 14.0, "descending": 8.0,
                                "valley": 6.0, "arch": 4.0},
            "register_trend": "stable",
            "note_density_per_beat": 0.91,
            "register_intervals": {
                "bass": {"note_count": 384, "interval_character": "leap-heavy",
                         "avg_interval_size": 5.91,
                         "interval_histogram": {"0": 31.6, "-12": 16.6, "12": 16.1},
                         "interval_character_no_unison": "leap-heavy",
                         "avg_interval_size_no_unison": 8.65,
                         "interval_histogram_no_unison": {"-12": 24.3, "12": 23.5, "-1": 8.6}},
                "mid": {"note_count": 203, "interval_character": "leap-heavy",
                        "avg_interval_size": 5.32,
                        "interval_histogram": {"0": 19.9, "7": 8.2, "-7": 8.2}},
                "lead": {"note_count": 2},
            },
            "interval_histogram": {"-12": 21.8, "0": 21.4, "12": 20.6, "-7": 4.6, "7": 3.2},
            "interval_histogram_no_unison": {"-12": 27.8, "12": 26.2, "-7": 5.9},
            "interval_transitions_common": [
                {"from": "Octave down", "to": "Octave up", "count": 40, "percentage": 9.7},
                {"from": "Octave up", "to": "Octave down", "count": 31, "percentage": 7.5},
            ],
            "interval_transitions_unexpected": [
                {"from": "P5 up", "to": "P5 down", "count": 3, "observed_expected_ratio": 4.99},
            ],
            # Dominant pitch class is F# (index 6) — doesn't match C# tonal root.
            # Exercises the tonal-center-vs-dominant-chroma Notable rule.
            "pitch_class_distribution": {
                "C": 0.070, "C#": 0.079, "D": 0.081, "D#": 0.077,
                "E": 0.081, "F": 0.095, "F#": 0.111, "G": 0.102,
                "G#": 0.098, "A": 0.074, "A#": 0.072, "B": 0.060,
            },
        },
    }


_METADATA = {"artist": "Ochre", "title": "Skyway", "album": "Oversail", "duration_seconds": 331.92}


class ReportSectionShapeTests(unittest.TestCase):
    """Every major section must appear; compact mode drops collapsibles."""

    # Top-level section headings that must always be present.
    REQUIRED_SECTIONS = [
        "## Overview",
        "## Harmonic Character",
        "## Melodic Character",
        "## Rhythmic Character",
        "## Timbral Character",
        "## Structure",
        "## Energy & Dynamics",
    ]

    # Collapsibles that must appear only when for_llm=False.
    REQUIRED_COLLAPSIBLES = [
        "<summary>Chord sequence (click to expand)</summary>",
        "<summary>Interval histogram</summary>",
        "<summary>Interval histogram (excluding unisons)</summary>",
        "<summary>Per-register interval histograms</summary>",
        "<summary>Interval transitions</summary>",
        "<summary>Key/mode timeline</summary>",
        "<summary>MFCC coefficients</summary>",
    ]

    def setUp(self):
        self.analysis = _synthetic_analysis()

    def test_compact_report_has_every_section(self):
        report = generate_report(self.analysis, _METADATA, for_llm=True)
        for heading in self.REQUIRED_SECTIONS:
            self.assertIn(heading, report, f"missing {heading} in compact report")
        # Character summary should be present as blockquote.
        self.assertIn("> A", report)
        # Title + metadata line render cleanly (no Python list repr).
        self.assertIn("# Track Analysis: Ochre - Skyway", report)
        self.assertIn("**Album:** Oversail", report)
        self.assertNotIn("['", report)

    def test_compact_report_omits_collapsibles(self):
        report = generate_report(self.analysis, _METADATA, for_llm=True)
        self.assertNotIn("## Raw Data Reference", report)
        for summary in self.REQUIRED_COLLAPSIBLES:
            self.assertNotIn(summary, report,
                             f"compact report leaked collapsible: {summary}")

    def test_full_report_has_every_collapsible(self):
        report = generate_report(self.analysis, _METADATA, for_llm=False)
        self.assertIn("## Raw Data Reference", report)
        for summary in self.REQUIRED_COLLAPSIBLES:
            self.assertIn(summary, report,
                          f"missing collapsible in full report: {summary}")

    def test_full_report_preserves_compact_sections(self):
        """Full mode is a superset of compact — every compact section still present."""
        report = generate_report(self.analysis, _METADATA, for_llm=False)
        for heading in self.REQUIRED_SECTIONS:
            self.assertIn(heading, report, f"full report dropped section: {heading}")

    def test_degraded_melodic_renders_error_and_skips_collapsibles(self):
        analysis = _synthetic_analysis()
        analysis["melodic"] = {"degraded": True, "error": "basic-pitch unavailable"}
        report = generate_report(analysis, _METADATA, for_llm=False)
        self.assertIn("basic-pitch unavailable", report)
        # Interval-histogram collapsibles rely on melodic data, so they must
        # silently drop when melodic degraded rather than render empty tables.
        self.assertNotIn("<summary>Interval histogram</summary>", report)
        self.assertNotIn("<summary>Per-register interval histograms</summary>", report)


class MetadataUnwrapTests(unittest.TestCase):
    """Regression for the Vorbis list-tag bug: artist must not render as '['Ochre']'."""

    def test_list_valued_tag_unwraps_to_first_string(self):
        self.assertEqual(_unwrap_tag(["Ochre"]), "Ochre")
        self.assertEqual(_unwrap_tag(["Ochre", "Other"]), "Ochre")

    def test_empty_list_returns_empty_string(self):
        self.assertEqual(_unwrap_tag([]), "")

    def test_none_returns_empty_string(self):
        self.assertEqual(_unwrap_tag(None), "")

    def test_scalar_tag_coerces_to_str(self):
        self.assertEqual(_unwrap_tag("Skyway"), "Skyway")


class FeatureScalarTests(unittest.TestCase):
    def test_extract_feature_scalars_contains_core_metrics(self):
        scalars = extract_feature_scalars(_synthetic_analysis())
        # Harmonic
        self.assertEqual(scalars.get("key_stability"), "stable")
        self.assertEqual(scalars.get("modal_character"), "C# Phrygian")
        # Rhythmic
        self.assertEqual(scalars.get("bpm"), 117.5)
        self.assertEqual(scalars.get("tempo_character"), "breathing")
        # Spectral (brightness is derived, not raw Hz)
        self.assertAlmostEqual(scalars.get("brightness"), 2256.3 / 8000.0, places=4)
        # Energy
        self.assertEqual(scalars.get("dynamic_range_db"), 8.9)
        self.assertEqual(scalars.get("energy_shape"), "dynamic")
        # Structural
        self.assertEqual(scalars.get("form_string"), "AAAAAAAAAAAAB")
        self.assertEqual(scalars.get("section_count"), 13)


class CacheVersionTests(unittest.TestCase):
    def test_cache_version_at_least_3(self):
        """v3 replaces the cached report with the LLM-optimized format.
        v<3 caches hold a Familiar-style compact report and must be rejected."""
        self.assertGreaterEqual(CACHE_VERSION, 3)


# ─── LLM report format regression tests ───────────────────────────────────


class LLMReportTests(unittest.TestCase):
    """Contract for the new LLM-optimized output.

    Guards the format that analyze_track(detail_level='compact') produces
    against regressions on: front-matter presence, collapsible/MFCC absence,
    Notable + Translation-hints section wiring, section-map compaction,
    numeric-anchoring of qualitative labels.
    """

    def setUp(self):
        self.analysis = _synthetic_analysis()

    def test_front_matter_at_top(self):
        report = generate_llm_report(self.analysis, _METADATA)
        # Must start with a YAML block: '---' … '---'.
        self.assertTrue(report.startswith("---\n"))
        head_end = report.index("---\n", 4)  # second '---'
        front = report[:head_end]
        # Spot-check scalars an LLM would retrieve. Strings with special chars
        # get quoted, others emit bare — both are valid YAML.
        for needle in ("bpm: 117.5", 'key: "C# Phrygian"', "form: AAAAAAAAAAAAB",
                       "band_dominant:", "energy_shape: dynamic",
                       "tempo_cv: 0.297"):
            self.assertIn(needle, front, f"missing front-matter: {needle}")

    def test_no_collapsibles_in_llm_report(self):
        report = generate_llm_report(self.analysis, _METADATA)
        self.assertNotIn("<details>", report)
        self.assertNotIn("<summary>", report)

    def test_no_chord_sequence_or_mfcc_table(self):
        report = generate_llm_report(self.analysis, _METADATA)
        # The chord sequence table's header row.
        self.assertNotIn("| Time | Chord | Confidence |", report)
        # MFCC coefficients row pattern.
        self.assertNotIn("| MFCC-0 |", report)

    def test_notable_section_fires_on_mode_mismatch(self):
        """F# dominant chroma vs C# tonal root → Notable should flag it."""
        report = generate_llm_report(self.analysis, _METADATA)
        self.assertIn("## Notable", report)
        self.assertIn("F#", report)
        self.assertIn("C#", report)
        # Must name the mode so the LLM knows what kind of mismatch this is.
        lower = report.lower()
        self.assertTrue("phrygian" in lower or "pedal" in lower or "drone" in lower)

    def test_notable_bass_dominance_rule(self):
        """sub-bass+bass = 78.5% should fire the bass-weighted notable."""
        report = generate_llm_report(self.analysis, _METADATA)
        # R3 rule text mentions "bass-weighted" and the percentage.
        self.assertIn("bass-weighted", report.lower())

    def test_notable_treble_starved_rule(self):
        """high-mid+high = 2.3% should fire the treble-starved notable."""
        report = generate_llm_report(self.analysis, _METADATA)
        self.assertIn("top-end", report.lower())

    def test_notable_monolithic_form_rule(self):
        """Form AAAAAAAAAAAAB with A covering ~96% → monolithic flag."""
        # Extend synthetic segments so the ratio test has enough data.
        analysis = _synthetic_analysis()
        analysis["structural"]["segments"] = [
            *[{"start": i * 25, "end": (i + 1) * 25, "duration": 25, "label": "A"} for i in range(12)],
            {"start": 300, "end": 320, "duration": 20, "label": "B"},
        ]
        analysis["structural"]["section_profiles"] = [
            *[{"start": i * 25, "end": (i + 1) * 25, "rms_db": -9.5, "brightness": "neutral", "density": "dense"} for i in range(12)],
            {"start": 300, "end": 320, "rms_db": -26.0, "brightness": "bright", "density": "dense"},
        ]
        report = generate_llm_report(analysis, _METADATA)
        self.assertIn("monolithic", report.lower())

    def test_translation_hints_section_present(self):
        report = generate_llm_report(self.analysis, _METADATA)
        self.assertIn("## Vivid translation hints", report)

    def test_translation_hints_phrygian_suggests_desaturated_palette(self):
        report = generate_llm_report(self.analysis, _METADATA)
        lower = report.lower()
        self.assertIn("desaturated", lower)
        # The mode-class bullet should mention Phrygian or the darker-palette cue.
        self.assertTrue("phrygian" in lower or "dark" in lower)

    def test_translation_hints_bass_drive(self):
        """Bass-dominant track → hint to drive from bass."""
        report = generate_llm_report(self.analysis, _METADATA)
        self.assertIn("bass band", report.lower())

    def test_section_map_compacted_on_repeated_labels(self):
        """12×A runs must collapse to a single line, not print 12 rows."""
        analysis = _synthetic_analysis()
        analysis["structural"]["segments"] = [
            *[{"start": i * 20, "end": (i + 1) * 20, "duration": 20, "label": "A"} for i in range(12)],
            {"start": 240, "end": 260, "duration": 20, "label": "B"},
        ]
        analysis["structural"]["section_profiles"] = [
            *[{"start": i * 20, "end": (i + 1) * 20, "rms_db": -9.5, "brightness": "neutral", "density": "dense"} for i in range(12)],
            {"start": 240, "end": 260, "rms_db": -26.0, "brightness": "bright", "density": "dense"},
        ]
        report = generate_llm_report(analysis, _METADATA)
        # Must mention "A × 12" (collapsed run) and "Section B" (singleton).
        self.assertIn("A × 12", report)
        # The 12 A sections must not each get their own "Section A —" line.
        self.assertLessEqual(report.count("Section A —"), 0)

    def test_tempo_stability_gets_numeric_anchor(self):
        """'breathing' label should be followed by (CV=...) from tempo_cv."""
        report = generate_llm_report(self.analysis, _METADATA)
        self.assertIn("breathing (CV=0.297)", report)

    def test_character_summary_retained(self):
        """The prose hook is still useful — don't drop it in the LLM format."""
        report = generate_llm_report(self.analysis, _METADATA)
        # Blockquote character summary line.
        self.assertIn("> A", report)

    def test_required_top_level_sections_present(self):
        report = generate_llm_report(self.analysis, _METADATA)
        for heading in ("## Harmonic", "## Melodic", "## Rhythmic", "## Timbral",
                        "## Structure", "## Energy"):
            self.assertIn(heading, report, f"missing {heading}")

    def test_degraded_melodic_handled_gracefully(self):
        analysis = _synthetic_analysis()
        analysis["melodic"] = {"degraded": True, "error": "basic-pitch unavailable"}
        report = generate_llm_report(analysis, _METADATA)
        self.assertIn("basic-pitch unavailable", report)
        # Notable / translation hints should still render for the non-melodic parts.
        self.assertIn("## Notable", report)
        self.assertIn("## Vivid translation hints", report)


if __name__ == "__main__":
    unittest.main()
