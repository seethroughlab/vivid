"""Report generation for track analysis.

Three flavors:
  - generate_llm_report() — optimized for LLM consumption: YAML front-matter of
    feature scalars, compacted section map, "Notable" deviations, and Vivid-
    specific translation hints. Drops token-heavy raw-data tables (chord seq,
    MFCCs). This is what analyze_track emits by default.
  - generate_report(for_llm=True) — Familiar's legacy compact format. Preserved
    for backwards-compat / human reading.
  - generate_report(for_llm=False) — Familiar's full format with every
    <details> collapsible (chord sequence, interval histograms, etc.).

Ported from familiar/backend/app/services/track_analysis/reports.py.
"""

from typing import Any

from .constants import INTERVAL_NAMES
from .utils import _format_time

PITCH_CLASS_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# ─── Character summary ─────────────────────────────────────────────────────

def _generate_character_summary(
    harmonic: dict[str, Any],
    melodic: dict[str, Any],
    rhythmic: dict[str, Any],
    spectral: dict[str, Any],
    structural: dict[str, Any],
    energy: dict[str, Any],
    duration: float,
) -> str:
    """Synthesize a 2-3 sentence natural language character summary from analysis data."""
    parts = []

    # Sentence 1: Tempo + harmonic character
    bpm = rhythmic.get("bpm")
    modal = harmonic.get("modal_character", "")
    if bpm and modal:
        if bpm < 80:
            tempo_desc = "slow"
        elif bpm < 110:
            tempo_desc = "mid-tempo"
        elif bpm < 140:
            tempo_desc = "upbeat"
        else:
            tempo_desc = "fast"

        mode_lower = modal.lower()
        if "phrygian" in mode_lower:
            harmonic_desc = "dark, phrygian-inflected harmonic palette"
        elif "dorian" in mode_lower:
            harmonic_desc = "dorian harmonic color"
        elif "mixolydian" in mode_lower:
            harmonic_desc = "mixolydian harmonic feel"
        elif "lydian" in mode_lower:
            harmonic_desc = "bright, lydian-inflected harmony"
        elif "locrian" in mode_lower:
            harmonic_desc = "tense, locrian-inflected harmony"
        elif "aeolian" in mode_lower or "minor" in mode_lower:
            harmonic_desc = "minor-key harmonic palette"
        elif "ionian" in mode_lower or "major" in mode_lower:
            harmonic_desc = "major-key harmonic palette"
        else:
            harmonic_desc = f"{modal} tonality"

        parts.append(f"A {tempo_desc} ({bpm:.0f} BPM) track with a {harmonic_desc}")
    elif bpm:
        parts.append(f"A {bpm:.0f} BPM track")

    # Sentence 2: Rhythmic + timbral character
    sentence2_parts = []
    swing = rhythmic.get("swing_ratio", 50)
    syncopation = rhythmic.get("syncopation_index", 0)
    brightness = spectral.get("brightness", "")

    if rhythmic.get("has_clear_beat", True):
        if abs(swing - 50) < 5:
            feel = "straight"
        elif swing < 60:
            feel = "lightly swung"
        else:
            feel = "swung"

        if syncopation > 0.5:
            feel += " with heavy syncopation"
        elif syncopation > 0.3:
            feel += " with moderate syncopation"

        sentence2_parts.append(f"Rhythmically {feel}")
    else:
        sentence2_parts.append("No clear beat (ambient/drone)")

    if brightness:
        flatness = spectral.get("flatness", 0)
        if flatness > 0.1:
            timbral = f"{brightness} and textural"
        else:
            timbral = f"{brightness} timbral character"
        sentence2_parts.append(timbral)

    if sentence2_parts:
        parts.append(". ".join(sentence2_parts))

    # Sentence 3: Energy shape + structure
    shape = energy.get("energy_shape", "")
    section_count = structural.get("section_count", 0)
    form = structural.get("form", "")

    sentence3_parts = []
    if shape and shape != "insufficient data":
        sentence3_parts.append(f"The arrangement has a {shape} energy profile")

    if section_count > 0:
        unique_sections = len(set(form)) if form else section_count
        if unique_sections <= 2:
            structure_desc = f"across {section_count} sections with a simple structure"
        elif unique_sections <= 4:
            structure_desc = f"across {section_count} sections with moderate structural variety"
        else:
            structure_desc = f"across {section_count} sections with high structural complexity"
        sentence3_parts.append(structure_desc)

    if sentence3_parts:
        parts.append(", ".join(sentence3_parts))

    if not parts:
        return ""

    result = ". ".join(parts)
    if not result.endswith("."):
        result += "."
    return result


# ─── Report generation ─────────────────────────────────────────────────────

def generate_report(
    results: dict[str, Any],
    track_metadata: dict[str, Any],
    for_llm: bool = False,
) -> str:
    """Generate a single-track markdown report from analysis results.

    Args:
        for_llm: If True, strip heavy raw data sections to reduce token usage.
    """
    lines = []

    artist = track_metadata.get("artist", "Unknown Artist")
    title = track_metadata.get("title", "Unknown Title")
    album = track_metadata.get("album", "")
    duration = track_metadata.get("duration_seconds", 0)

    lines.append(f"# Track Analysis: {artist} - {title}")
    lines.append("")

    meta_parts = []
    if album:
        meta_parts.append(f"**Album:** {album}")
    if duration:
        meta_parts.append(f"**Duration:** {_format_time(duration)}")
    if meta_parts:
        lines.append(" | ".join(meta_parts))
        lines.append("")

    lines.append("---")
    lines.append("")

    harmonic = results.get("harmonic", {})
    rhythmic = results.get("rhythmic", {})
    energy = results.get("energy", {})
    spectral = results.get("spectral", {})
    structural = results.get("structural", {})
    melodic = results.get("melodic", {})

    summary = _generate_character_summary(
        harmonic, melodic, rhythmic, spectral, structural, energy, duration
    )
    if summary:
        lines.append(f"> {summary}")
        lines.append("")

    # Overview
    lines.append("## Overview")
    bpm = rhythmic.get("bpm", "?")
    stability = rhythmic.get("tempo_stability", "?")
    lines.append(f"- **Tempo:** {bpm} BPM ({stability})")

    modal = harmonic.get("modal_character", "?")
    modal_conf = harmonic.get("modal_confidence", 0)
    lines.append(f"- **Key/Mode:** {modal} (confidence: {round(modal_conf * 100)}%)")

    shape = energy.get("energy_shape", "?")
    lines.append(f"- **Energy shape:** {shape}")

    dyn = energy.get("dynamic_range_db", "?")
    lines.append(f"- **Dynamic range:** {dyn} dB")
    lines.append("")

    # Harmonic Character
    lines.append("## Harmonic Character")
    if not harmonic.get("harmonic_content", True):
        lines.append(f"- {harmonic.get('note', 'No significant harmonic content detected')}")
    else:
        hr = harmonic.get("harmonic_rhythm", "?")
        lines.append(f"- **Chord change frequency:** {hr} changes/bar")

        ks = harmonic.get("key_stability", "?")
        lines.append(f"- **Key stability:** {ks}")

        lines.append(f"- **Modal quality:** {modal}")

        rnc = harmonic.get("roman_numeral_chords", [])
        mc = harmonic.get("most_common_chords", [])
        if rnc:
            chord_str = ", ".join(
                f"{c['roman']} [{c['chord']}] ({c['percentage']}%)" for c in rnc[:6]
            )
            lines.append(f"- **Most common chords:** {chord_str}")
        elif mc:
            chord_str = ", ".join(f"{c['chord']} ({c['percentage']}%)" for c in mc[:6])
            lines.append(f"- **Most common chords:** {chord_str}")

        progs = harmonic.get("detected_progressions", [])
        if progs:
            prog_str = ", ".join(
                f"{p['name']} ({p['occurrences']}x)" for p in progs[:3]
            )
            lines.append(f"- **Detected progressions:** {prog_str}")

        kmt = harmonic.get("key_mode_timeline", [])
        if kmt:
            lines.append("- **Key/mode over time:**")
            for entry in kmt:
                conf_min = entry.get("conf_min", entry["confidence"])
                conf_max = entry.get("conf_max", entry["confidence"])
                if abs(conf_min - conf_max) < 0.005:
                    conf_str = f"{round(conf_min * 100)}%"
                else:
                    conf_str = f"{round(conf_min * 100)}\u2013{round(conf_max * 100)}%"
                lines.append(
                    f"  - {_format_time(entry['start'])}\u2013{_format_time(entry['end'])}: "
                    f"{entry['key_mode']} ({conf_str})"
                )
    lines.append("")

    # Melodic Character
    lines.append("## Melodic Character")
    if melodic.get("degraded"):
        lines.append(f"> *{melodic.get('error', 'Melodic analysis unavailable')}*")
    elif not melodic:
        lines.append("> *Melodic analysis not run. Call analyze_track with include_melodic=True.*")
    else:
        pr = melodic.get("pitch_range", {})
        if pr:
            lines.append(
                f"- **Pitch range:** {pr.get('low_note', '?')} to {pr.get('high_note', '?')} "
                f"(primary: {pr.get('primary_low', '?')} to {pr.get('primary_high', '?')})"
            )

        ic = melodic.get("interval_character", "?")
        avg_iv = melodic.get("avg_interval_size", "?")
        upct = melodic.get("unison_pct", 0)
        if upct > 5:
            ic_nu = melodic.get("interval_character_no_unison", ic)
            avg_iv_nu = melodic.get("avg_interval_size_no_unison", avg_iv)
            lines.append(
                f"- **Interval character:** {ic} (avg {avg_iv} semitones) \u2014 "
                f"{upct}% repeated pitches; excluding unisons: {ic_nu} "
                f"(avg {avg_iv_nu} semitones)"
            )
        else:
            lines.append(f"- **Interval character:** {ic} (avg {avg_iv} semitones)")

        pc = melodic.get("phrase_count", 0)
        apl = melodic.get("avg_phrase_length_seconds", 0)
        lines.append(f"- **Phrases:** {pc} phrases, avg {apl}s")

        dc = melodic.get("dominant_contour", "?")
        cs = melodic.get("contour_summary", {})
        if cs:
            dist_parts = [f"{k} {v}%" for k, v in sorted(cs.items(), key=lambda x: -x[1])]
            lines.append(f"- **Contour tendency:** {dc} ({', '.join(dist_parts)})")
        else:
            lines.append(f"- **Contour tendency:** {dc}")

        rt = melodic.get("register_trend")
        if rt:
            lines.append(f"- **Register movement:** {rt}")

        nd = melodic.get("note_density_per_beat", "?")
        lines.append(f"- **Note density:** {nd} notes/beat")

        ri = melodic.get("register_intervals", {})
        if ri:
            reg_parts = []
            for reg_name in ("bass", "mid", "lead"):
                reg = ri.get(reg_name, {})
                nc = reg.get("note_count", 0)
                if nc > 0:
                    char = reg.get("interval_character", "")
                    avg = reg.get("avg_interval_size", "")
                    detail = f", {char}, avg {avg}st" if char else ""
                    reg_parts.append(f"{reg_name}: {nc} notes{detail}")
            if reg_parts:
                lines.append(f"- **Register breakdown:** {' | '.join(reg_parts)}")

        itc = melodic.get("interval_transitions_common", [])
        if itc:
            top5 = itc[:5]
            parts = [f"{t['from']}\u2192{t['to']} ({t['percentage']}%)" for t in top5]
            lines.append(f"- **Common transitions:** {', '.join(parts)}")

        itu = melodic.get("interval_transitions_unexpected", [])
        if itu:
            top3 = itu[:3]
            parts = [
                f"{t['from']}\u2192{t['to']} ({t['observed_expected_ratio']}x expected)"
                for t in top3
            ]
            lines.append(f"- **Unexpected transitions:** {', '.join(parts)}")

        sketches = melodic.get("section_sketches", [])
        if sketches:
            lines.append("- **Melodic sketches:**")
            for sk in sketches:
                lines.append(f"  - Section {sk['label']}: {sk['sketch']}")
    lines.append("")

    # Rhythmic Character
    lines.append("## Rhythmic Character")
    if not rhythmic.get("has_clear_beat", True):
        lines.append("- No clear beat detected (ambient/drone)")
    else:
        sw = rhythmic.get("swing_ratio", 50)
        swing_desc = "straight" if abs(sw - 50) < 5 else "light swing" if sw < 60 else "triplet swing"
        lines.append(f"- **Swing:** {sw}% ({swing_desc})")

        si = rhythmic.get("syncopation_index", 0)
        lines.append(f"- **Syncopation:** {si}")

        ts = rhythmic.get("tempo_stability", "?")
        lines.append(f"- **Tempo stability:** {ts}")

        rp = rhythmic.get("rhythm_pattern", "")
        if rp and rp != "unclassified":
            lines.append(f"- **Pattern:** {rp}")

        ds = rhythmic.get("density_shape", "")
        if ds and ds != "insufficient data":
            lines.append(f"- **Rhythmic density:** {ds}")

        ep = rhythmic.get("euclidean_patterns", [])
        if ep:
            for p in ep:
                lines.append(f"- **Euclidean pattern:** {p['pattern']} \u2014 {p['name']} (distance: {p['hamming_distance']})")
    lines.append("")

    # Timbral Character
    lines.append("## Timbral Character")
    br = spectral.get("brightness", "?")
    lines.append(f"- **Brightness:** {br} (centroid: {spectral.get('centroid_hz', '?')} Hz)")

    be = spectral.get("band_energy", {})
    if be:
        parts = [f"{k.replace('_', ' ')}: {v}%" for k, v in be.items()]
        lines.append(f"- **Band energy:** {' / '.join(parts)}")

    fl = spectral.get("flatness", 0)
    tonal_desc = "tonal" if fl < 0.01 else "mixed" if fl < 0.1 else "noisy/textural"
    lines.append(f"- **Spectral character:** {tonal_desc} (flatness: {fl})")
    lines.append("")

    # Structure
    lines.append("## Structure")
    sc = structural.get("section_count", 0)
    form = structural.get("form", "?")
    lines.append(f"- **Sections:** {sc} detected")
    lines.append(f"- **Form:** {form}")
    asl = structural.get("avg_section_length", 0)
    lines.append(f"- **Average section length:** {asl}s")

    segs = structural.get("segments", [])
    section_profiles = structural.get("section_profiles", [])
    profile_by_key = {}
    for sp in section_profiles:
        key = (sp["start"], sp["end"])
        profile_by_key[key] = sp

    if segs:
        lines.append("- **Section map:**")
        for seg in segs:
            profile = profile_by_key.get((seg["start"], seg["end"]))
            if profile:
                lines.append(
                    f"  - {_format_time(seg['start'])}\u2013{_format_time(seg['end'])} \u2014 "
                    f"Section {seg.get('label', '?')} ({seg['duration']}s) \u2014 "
                    f"{profile['rms_db']}dB, {profile['brightness']}, {profile['density']}"
                )
            else:
                lines.append(
                    f"  - {_format_time(seg['start'])}\u2013{_format_time(seg['end'])} \u2014 "
                    f"Section {seg.get('label', '?')} ({seg['duration']}s)"
                )
    lines.append("")

    # Energy
    lines.append("## Energy & Dynamics")
    lines.append(f"- **Dynamic range:** {dyn} dB")
    lines.append(f"- **Energy shape:** {shape}")
    lines.append(f"- **Mean RMS:** {energy.get('rms_mean_db', '?')} dB")
    lines.append(f"- **Peak RMS:** {energy.get('rms_peak_db', '?')} dB")

    builds = energy.get("builds", [])
    if builds:
        for b in builds[:5]:
            lines.append(f"- **{b['type'].title()}** at {_format_time(b['time'])} (ratio: {b['ratio']})")
    lines.append("")

    # Raw data reference — omitted for LLM to save tokens
    if not for_llm:
        lines.append("## Raw Data Reference")
        lines.append("")

        chord_seq = harmonic.get("chords", [])
        if chord_seq:
            lines.append("<details>")
            lines.append("<summary>Chord sequence (click to expand)</summary>")
            lines.append("")
            lines.append("| Time | Chord | Confidence |")
            lines.append("|------|-------|------------|")
            display_limit = 200
            total = len(chord_seq)
            for c in chord_seq[:display_limit]:
                lines.append(f"| {_format_time(c['time'])} | {c['chord']} | {c['confidence']} |")
            if total > display_limit:
                lines.append(f"\n*Showing first {display_limit} of {total} total chord changes.*")
            lines.append("")
            lines.append("</details>")
            lines.append("")

        ih = melodic.get("interval_histogram", {})
        if ih:
            lines.append("<details>")
            lines.append("<summary>Interval histogram</summary>")
            lines.append("")
            lines.append("| Interval (semitones) | Name | Frequency |")
            lines.append("|---------------------|------|-----------|")
            for iv_str, pct in sorted(ih.items(), key=lambda x: -x[1]):
                iv = int(iv_str)
                name = INTERVAL_NAMES.get(iv, f"{iv:+d}")
                lines.append(f"| {iv:+d} | {name} | {pct}% |")
            lines.append("")
            lines.append("</details>")
            lines.append("")

        upct = melodic.get("unison_pct", 0)
        ih_nu = melodic.get("interval_histogram_no_unison", {})
        if upct > 5 and ih_nu:
            lines.append("<details>")
            lines.append("<summary>Interval histogram (excluding unisons)</summary>")
            lines.append("")
            lines.append(f"*{upct}% of intervals were unisons (repeated pitches) \u2014 "
                         f"this table shows the distribution of the remaining intervals.*")
            lines.append("")
            lines.append("| Interval (semitones) | Name | Frequency |")
            lines.append("|---------------------|------|-----------|")
            for iv_str, pct in sorted(ih_nu.items(), key=lambda x: -x[1]):
                iv = int(iv_str)
                name = INTERVAL_NAMES.get(iv, f"{iv:+d}")
                lines.append(f"| {iv:+d} | {name} | {pct}% |")
            lines.append("")
            lines.append("</details>")
            lines.append("")

        ri = melodic.get("register_intervals", {})
        has_register_data = any(
            ri.get(r, {}).get("interval_histogram") for r in ("bass", "mid", "lead")
        )
        if has_register_data:
            lines.append("<details>")
            lines.append("<summary>Per-register interval histograms</summary>")
            lines.append("")
            for reg_name in ("bass", "mid", "lead"):
                reg = ri.get(reg_name, {})
                reg_hist = reg.get("interval_histogram", {})
                if not reg_hist:
                    continue
                lines.append(f"**{reg_name.title()}** ({reg.get('note_count', 0)} notes, "
                             f"{reg.get('interval_character', '?')}, avg {reg.get('avg_interval_size', '?')}st)")
                lines.append("")
                lines.append("| Interval | Name | Frequency |")
                lines.append("|----------|------|-----------|")
                for iv_str, pct in sorted(reg_hist.items(), key=lambda x: -x[1]):
                    iv = int(iv_str)
                    name = INTERVAL_NAMES.get(iv, f"{iv:+d}")
                    lines.append(f"| {iv:+d} | {name} | {pct}% |")
                lines.append("")

                reg_hist_nu = reg.get("interval_histogram_no_unison", {})
                if reg_hist_nu and "0" in reg_hist and float(reg_hist.get("0", 0)) > 5:
                    lines.append(f"*Excluding unisons ({reg.get('interval_character_no_unison', '?')}, "
                                 f"avg {reg.get('avg_interval_size_no_unison', '?')}st):*")
                    lines.append("")
                    lines.append("| Interval | Name | Frequency |")
                    lines.append("|----------|------|-----------|")
                    for iv_str, pct in sorted(reg_hist_nu.items(), key=lambda x: -x[1]):
                        iv = int(iv_str)
                        name = INTERVAL_NAMES.get(iv, f"{iv:+d}")
                        lines.append(f"| {iv:+d} | {name} | {pct}% |")
                    lines.append("")

            lines.append("</details>")
            lines.append("")

        itc = melodic.get("interval_transitions_common", [])
        if itc:
            lines.append("<details>")
            lines.append("<summary>Interval transitions</summary>")
            lines.append("")
            lines.append("**Most common transitions (top 10):**")
            lines.append("")
            lines.append("| From | To | Count | Percentage |")
            lines.append("|------|----|-------|------------|")
            for t in itc:
                lines.append(f"| {t['from']} | {t['to']} | {t['count']} | {t['percentage']}% |")
            lines.append("")
            itu = melodic.get("interval_transitions_unexpected", [])
            if itu:
                lines.append("**Most unexpected transitions (top 5):**")
                lines.append("")
                lines.append("| From | To | Count | Observed/Expected |")
                lines.append("|------|----|-------|-------------------|")
                for t in itu:
                    lines.append(f"| {t['from']} | {t['to']} | {t['count']} | {t['observed_expected_ratio']}x |")
                lines.append("")
            lines.append("</details>")
            lines.append("")

        kmt = harmonic.get("key_mode_timeline", [])
        if kmt:
            lines.append("<details>")
            lines.append("<summary>Key/mode timeline</summary>")
            lines.append("")
            lines.append("| Time Range | Key/Mode | Confidence |")
            lines.append("|------------|----------|------------|")
            for entry in kmt:
                conf_min = entry.get("conf_min", entry["confidence"])
                conf_max = entry.get("conf_max", entry["confidence"])
                if abs(conf_min - conf_max) < 0.005:
                    conf_str = f"{round(conf_min * 100)}%"
                else:
                    conf_str = f"{round(conf_min * 100)}\u2013{round(conf_max * 100)}%"
                lines.append(
                    f"| {_format_time(entry['start'])}\u2013{_format_time(entry['end'])} "
                    f"| {entry['key_mode']} | {conf_str} |"
                )
            lines.append("")
            lines.append("</details>")
            lines.append("")

        mfcc = spectral.get("mfcc_mean", [])
        if mfcc:
            lines.append("<details>")
            lines.append("<summary>MFCC coefficients</summary>")
            lines.append("")
            lines.append("| Coefficient | Value |")
            lines.append("|------------|-------|")
            for i, v in enumerate(mfcc):
                lines.append(f"| MFCC-{i} | {v} |")
            lines.append("")
            lines.append("</details>")
            lines.append("")

    return "\n".join(lines)


# ─── LLM-optimized report ──────────────────────────────────────────────────
#
# Design rationale (from an LLM consumer's POV):
#   - YAML front-matter up top so scalars are retrievable without prose parsing.
#   - Qualitative labels anchored with numbers when the raw value is in the
#     analysis dict (`breathing (tempo_cv=0.30)` beats `breathing` alone).
#   - No <details> collapsibles — they're browser idioms; to an LLM they're
#     just extra tokens.
#   - Chord sequence (hundreds of rows) and MFCC vector dropped — unreadable
#     in bulk and already summarized elsewhere.
#   - Section map compacted when consecutive sections share a label; expanded
#     only when they differ.
#   - Deduplicated: dynamic range / energy shape appear once, not in both
#     Overview and Energy sections.
#   - New sections: "Notable" (rule-based deviations worth flagging) and
#     "Vivid translation hints" (explicit per-feature design cues keyed to
#     Vivid's reactive-graph use case).


def _dominant_pitch_class(pitch_distribution: Any) -> str | None:
    """Return the pitch-class name with the highest distribution weight, or None."""
    if not isinstance(pitch_distribution, dict) or not pitch_distribution:
        return None
    try:
        top = max(pitch_distribution.items(), key=lambda kv: float(kv[1]))
        # Keys may be int indices or pitch-class name strings depending on version.
        key = top[0]
        if isinstance(key, int) or (isinstance(key, str) and key.isdigit()):
            idx = int(key) % 12
            return PITCH_CLASS_NAMES[idx]
        return str(key).strip()
    except (TypeError, ValueError):
        return None


def _tonal_root(modal_character: str | None) -> str | None:
    """Extract the tonal root pitch class from 'C# Phrygian', 'Ab Dorian', etc."""
    if not modal_character or not isinstance(modal_character, str):
        return None
    head = modal_character.strip().split()[0]
    # Normalize to our pitch-class spelling
    if head in PITCH_CLASS_NAMES:
        return head
    # Try flat-to-sharp equivalence
    flats = {"Db": "C#", "Eb": "D#", "Gb": "F#", "Ab": "G#", "Bb": "A#"}
    return flats.get(head, head)


def _front_matter(analysis: dict[str, Any], metadata: dict[str, Any]) -> list[str]:
    """YAML front-matter block with the scalars an LLM is likely to retrieve."""
    h = analysis.get("harmonic", {})
    r = analysis.get("rhythmic", {})
    s = analysis.get("spectral", {})
    st = analysis.get("structural", {})
    e = analysis.get("energy", {})
    m = analysis.get("melodic", {}) or {}

    band = s.get("band_energy", {}) or {}
    band_dominant = max(band.items(), key=lambda kv: kv[1])[0].replace("_", " ") if band else None

    lines: list[str] = ["---"]

    def add(key: str, value: Any) -> None:
        if value is None or value == "":
            return
        if isinstance(value, str):
            # Quote strings that contain YAML special chars; otherwise emit plain.
            needs_quote = any(c in value for c in ":#[]{},&*!|>'\"%@`")
            lines.append(f'{key}: "{value}"' if needs_quote else f"{key}: {value}")
        else:
            lines.append(f"{key}: {value}")

    add("artist", metadata.get("artist"))
    add("title", metadata.get("title"))
    add("album", metadata.get("album"))
    add("duration_seconds", round(metadata.get("duration_seconds", 0), 2) or None)
    add("bpm", r.get("bpm"))
    add("tempo_cv", r.get("tempo_cv"))
    add("tempo_stability", r.get("tempo_stability"))
    add("key", h.get("modal_character"))
    add("modal_confidence", h.get("modal_confidence"))
    add("key_stability", h.get("key_stability"))
    add("harmonic_rhythm_per_bar", h.get("harmonic_rhythm"))
    add("form", st.get("form"))
    add("section_count", st.get("section_count"))
    add("energy_shape", e.get("energy_shape"))
    add("dynamic_range_db", e.get("dynamic_range_db"))
    add("rms_mean_db", e.get("rms_mean_db"))
    add("rms_peak_db", e.get("rms_peak_db"))
    add("centroid_hz", s.get("centroid_hz"))
    add("rolloff_hz", s.get("rolloff_hz"))
    add("spectral_flatness", s.get("flatness"))
    add("brightness_label", s.get("brightness"))
    add("band_dominant", band_dominant)
    add("swing_ratio_pct", r.get("swing_ratio"))
    add("syncopation_index", r.get("syncopation_index"))
    add("has_clear_beat", r.get("has_clear_beat"))
    if not m.get("degraded", False):
        add("note_density_per_beat", m.get("note_density_per_beat"))
        pr = m.get("pitch_range") or {}
        if pr:
            lo, hi = pr.get("low_note"), pr.get("high_note")
            if lo and hi:
                add("pitch_range", f"{lo}-{hi}")
            plo, phi = pr.get("primary_low"), pr.get("primary_high")
            if plo and phi:
                add("primary_pitch_range", f"{plo}-{phi}")
        add("interval_character", m.get("interval_character"))
    lines.append("---")
    return lines


def _compact_section_map(segments: list[dict], profiles: list[dict]) -> list[str]:
    """Compress runs of consecutive sections that share label/brightness/density.

    Output lines either expand a single section or collapse a run like:
        `A × 12 across 0:00–5:18 (lengths 8.2–55.9s, -9.9 to -9.0 dB, neutral/dense)`
    """
    profile_by_key = {(p["start"], p["end"]): p for p in profiles or []}

    def _sig(seg: dict) -> tuple:
        p = profile_by_key.get((seg["start"], seg["end"]), {})
        return (seg.get("label", "?"), p.get("brightness"), p.get("density"))

    def _format_run(run: list[dict]) -> str:
        first, last = run[0], run[-1]
        label = first.get("label", "?")
        p_first = profile_by_key.get((first["start"], first["end"]), {})
        if len(run) == 1:
            dur = first["duration"]
            body = (f"Section {label} — {_format_time(first['start'])}-"
                    f"{_format_time(first['end'])} ({dur}s)")
            if p_first:
                body += f", {p_first.get('rms_db', '?')}dB, {p_first.get('brightness', '?')}/{p_first.get('density', '?')}"
            return f"- {body}"
        durations = [s["duration"] for s in run]
        rmses = [profile_by_key.get((s["start"], s["end"]), {}).get("rms_db") for s in run]
        rmses = [v for v in rmses if v is not None]
        rms_hi = max(rmses) if rmses else None
        rms_lo = min(rmses) if rmses else None
        bright = p_first.get("brightness", "?")
        dens = p_first.get("density", "?")
        span = f"{_format_time(first['start'])}-{_format_time(last['end'])}"
        head = f"Section {label} × {len(run)} across {span}"
        details = [f"lengths {min(durations)}-{max(durations)}s"]
        if rms_hi is not None and rms_lo is not None:
            details.append(f"RMS {rms_lo} to {rms_hi} dB")
        details.append(f"{bright}/{dens}")
        return f"- {head} ({', '.join(details)})"

    if not segments:
        return ["- (no segments)"]

    lines: list[str] = []
    run: list[dict] = [segments[0]]
    run_sig = _sig(segments[0])
    for seg in segments[1:]:
        if _sig(seg) == run_sig:
            run.append(seg)
        else:
            lines.append(_format_run(run))
            run = [seg]
            run_sig = _sig(seg)
    lines.append(_format_run(run))
    return lines


def _notable(analysis: dict[str, Any]) -> list[str]:
    """Rule-based deviations worth flagging to an LLM consumer."""
    h = analysis.get("harmonic", {})
    r = analysis.get("rhythmic", {})
    s = analysis.get("spectral", {})
    st = analysis.get("structural", {})
    e = analysis.get("energy", {})
    m = analysis.get("melodic", {}) or {}

    notes: list[str] = []

    # R1 — tonal-center vs dominant-chroma mismatch (modal inflection).
    modal = h.get("modal_character") or ""
    root = _tonal_root(modal)
    top_pc = _dominant_pitch_class(m.get("pitch_class_distribution"))
    if root and top_pc and top_pc != root:
        mode_word = modal.split()[-1] if " " in modal else modal
        notes.append(
            f"Dominant pitch class is **{top_pc}** but tonal center is **{root}** — "
            f"characteristic of {mode_word} (pedal/drone on a non-tonic scale degree). "
            "Don't treat most-frequent pitch as the tonal root."
        )

    # R2 — monolithic form: one section dominates duration.
    form = st.get("form") or ""
    seg_total = sum(s.get("duration", 0) for s in (st.get("segments") or []))
    if form and seg_total > 0:
        unique = set(form)
        if len(unique) <= 2:
            # Find the dominant label's share
            durs: dict[str, float] = {}
            for seg in (st.get("segments") or []):
                durs[seg.get("label", "?")] = durs.get(seg.get("label", "?"), 0) + seg.get("duration", 0)
            dom_label, dom_dur = max(durs.items(), key=lambda kv: kv[1]) if durs else ("?", 0)
            share = dom_dur / seg_total if seg_total else 0
            if share > 0.8:
                notes.append(
                    f"Form is monolithic: `{form}` — **{dom_label}** covers "
                    f"{round(share * 100)}% of the track. Macro arc lives in "
                    "spectral drift / micro-variation, not section changes."
                )

    # R3/R4 — band-energy skew.
    band = s.get("band_energy") or {}
    if band:
        low = (band.get("sub_bass", 0) or 0) + (band.get("bass", 0) or 0)
        high = (band.get("high_mid", 0) or 0) + (band.get("high", 0) or 0)
        if low > 70:
            notes.append(
                f"Spectrum is strongly bass-weighted: sub-bass+bass = {round(low, 1)}%. "
                "Bass-coupled visuals will dominate; treble couplings need explicit gain."
            )
        if high < 5:
            notes.append(
                f"Very little top-end: high-mid+high = {round(high, 1)}%. "
                "Any visual driven from treble/air will be near-silent."
            )

    # R5 — noisy/textural character.
    flat = s.get("flatness")
    if isinstance(flat, (int, float)) and flat > 0.15:
        notes.append(
            f"High spectral flatness ({flat:.3f}) — track has a noisy/textural "
            "character; grain/scanline/chromatic-aberration visuals match better "
            "than clean geometric shapes."
        )

    # R6 — flat amplitude arc.
    dyn = e.get("dynamic_range_db")
    if isinstance(dyn, (int, float)) and dyn < 6:
        notes.append(
            f"Dynamic range is tight ({dyn} dB). Amplitude-coupled visuals will "
            "read monotone; drive the macro arc from spectral centroid or chord-"
            "change rate instead."
        )

    # R7 — modal-shifting intro (from key_mode_timeline).
    kmt = h.get("key_mode_timeline") or []
    if len(kmt) >= 3:
        intro = [e for e in kmt if e.get("end", 0) <= 45]
        if len({e.get("key_mode") for e in intro}) >= 3:
            mode_list = [e.get("key_mode") for e in intro]
            notes.append(
                f"Intro passes through multiple modes before settling: "
                f"{' → '.join(mode_list[:4])}. Visual state should allow "
                "transitional character in the first ~30-45s."
            )

    # R8 — unusually dense or sparse melodic activity (if melodic ran).
    if not m.get("degraded", False):
        nd = m.get("note_density_per_beat")
        if isinstance(nd, (int, float)):
            if nd > 2.5:
                notes.append(f"High note density ({nd} notes/beat) — melody "
                             "is dense; per-note triggers would be busy.")
            elif nd < 0.3 and r.get("has_clear_beat", True):
                notes.append(f"Sparse melody ({nd} notes/beat) — per-note "
                             "triggers have wide gaps; consider envelope sustain.")

    return notes


def _translation_hints(analysis: dict[str, Any]) -> list[str]:
    """Vivid-specific per-feature design cues derived from the analysis."""
    h = analysis.get("harmonic", {})
    r = analysis.get("rhythmic", {})
    s = analysis.get("spectral", {})
    st = analysis.get("structural", {})
    e = analysis.get("energy", {})

    hints: list[str] = []

    # Band-driven visuals.
    band = s.get("band_energy") or {}
    if band:
        low = (band.get("sub_bass", 0) or 0) + (band.get("bass", 0) or 0)
        high = (band.get("high_mid", 0) or 0) + (band.get("high", 0) or 0)
        if low > 60:
            hints.append(
                f"Drive brightness/scale/mass from the bass band ({round(low, 1)}% "
                "of energy). Envelope-follower on kick/bass rather than broadband RMS."
            )
        if high < 10:
            hints.append(
                "Treble-band couplings need explicit gain — track has very little "
                "high-frequency energy for an FFT-band driver to latch onto."
            )

    # Mode → palette mapping.
    modal = (h.get("modal_character") or "").lower()
    if any(k in modal for k in ("phrygian", "locrian")):
        hints.append(
            "Mode is dark/tense (Phrygian/Locrian family) — desaturated palette, "
            "cool or violet bias; avoid warm/bright primaries."
        )
    elif any(k in modal for k in ("lydian", "mixolydian")) or "major" in modal or "ionian" in modal:
        hints.append(
            "Mode is bright/open (Lydian/Mixolydian/Major family) — warmer or "
            "higher-saturation palette, allow brighter hues."
        )
    elif any(k in modal for k in ("aeolian", "minor", "dorian")):
        hints.append(
            "Mode is minor/Dorian family — mid-key palette, cool neutrals with "
            "occasional warmer accent; avoid fully-lit majors."
        )

    # Structural cues.
    form = st.get("form") or ""
    if form:
        unique = set(form)
        if len(unique) <= 2:
            # Find the boundary where the minority label appears.
            segs = st.get("segments") or []
            if segs:
                majority = max(set(s.get("label") for s in segs), key=lambda lb: sum(1 for s in segs if s.get("label") == lb))
                first_minority = next((s for s in segs if s.get("label") != majority), None)
                if first_minority:
                    hints.append(
                        f"Single natural structural cue at {_format_time(first_minority['start'])} "
                        f"(transition into section **{first_minority.get('label')}**). "
                        "Don't design around section variation that doesn't exist elsewhere."
                    )
        elif len(unique) >= 5:
            hints.append(
                f"Rich structure ({len(unique)} unique section labels across {form}) — "
                "use variation presets keyed to section boundaries for strong per-section identity."
            )

    # Dynamics-driven vs spectral-driven macro arc.
    dyn = e.get("dynamic_range_db")
    if isinstance(dyn, (int, float)) and dyn < 8:
        hints.append(
            f"Low dynamic range ({dyn} dB) — RMS-coupled brightness will look "
            "flat. Prefer spectral centroid or harmonic-rhythm rate as the "
            "macro-arc driver."
        )

    # Tempo character.
    ts = r.get("tempo_stability")
    if ts == "breathing" or ts == "drifting":
        hints.append(
            f"Tempo stability is **{ts}** — avoid hard-locking LFOs to the beat "
            "grid; small free-running detune (±1–2%) matches the track's feel."
        )

    # Noise character.
    flat = s.get("flatness")
    if isinstance(flat, (int, float)) and flat > 0.1:
        hints.append(
            f"Track carries noise/texture content (flatness {flat:.3f}) — grain, "
            "scanlines, chromatic aberration, noise-displaced shapes read better "
            "than clean edges."
        )

    # Harmonic-rhythm pacing.
    hr = h.get("harmonic_rhythm")
    if isinstance(hr, (int, float)):
        if hr > 4:
            hints.append(
                f"Fast harmonic rhythm ({hr} chord changes/bar) — chord-change-"
                "triggered visuals will feel busy; consider smoothing or subsampling."
            )
        elif hr < 1:
            hints.append(
                f"Slow harmonic rhythm ({hr} chord changes/bar) — chord-change "
                "triggers are rare; don't rely on them as a primary drive source."
            )

    return hints


def _anchor(label: str, numeric: Any, fmt: str = "{}") -> str:
    """Attach a numeric anchor to a qualitative label when the value is present."""
    if numeric is None or numeric == "":
        return label
    try:
        return f"{label} ({fmt.format(numeric)})"
    except (ValueError, TypeError):
        return label


def generate_llm_report(
    analysis: dict[str, Any],
    track_metadata: dict[str, Any],
) -> str:
    """Emit the LLM-optimized markdown report.

    Format targets LLM retrieval + decision-making, not human browser reading:
    YAML front-matter of scalars up top, qualitative labels anchored with
    numbers, compacted section map, "Notable" deviations section, Vivid
    translation hints section. No <details> wrappers, no chord-sequence table,
    no MFCC vector.
    """
    h = analysis.get("harmonic", {})
    r = analysis.get("rhythmic", {})
    s = analysis.get("spectral", {})
    st = analysis.get("structural", {})
    e = analysis.get("energy", {})
    m = analysis.get("melodic", {}) or {}

    artist = track_metadata.get("artist", "Unknown Artist")
    title = track_metadata.get("title", "Unknown Title")
    album = track_metadata.get("album", "")
    duration = track_metadata.get("duration_seconds", 0)

    lines: list[str] = []

    # YAML front-matter at the very top.
    lines.extend(_front_matter(analysis, track_metadata))
    lines.append("")

    # Header + character summary (retained — genuinely useful hook).
    lines.append(f"# {artist} — {title}")
    if album or duration:
        meta_parts = []
        if album:
            meta_parts.append(f"**Album:** {album}")
        if duration:
            meta_parts.append(f"**Duration:** {_format_time(duration)}")
        lines.append(" | ".join(meta_parts))
    lines.append("")

    summary = _generate_character_summary(h, m, r, s, st, e, duration)
    if summary:
        lines.append(f"> {summary}")
        lines.append("")

    # Harmonic
    lines.append("## Harmonic")
    if not h.get("harmonic_content", True):
        lines.append(f"- {h.get('note', 'No significant harmonic content detected')}")
    else:
        lines.append(_anchor(
            f"- Chord changes/bar: {h.get('harmonic_rhythm', '?')}",
            None,
        ))
        lines.append(f"- Key stability: {h.get('key_stability', '?')}")
        modal = h.get("modal_character", "?")
        conf = h.get("modal_confidence")
        conf_str = f"{round(conf * 100)}% conf" if isinstance(conf, (int, float)) else "?"
        lines.append(f"- Modal quality: {modal} ({conf_str})")

        rnc = h.get("roman_numeral_chords", [])
        mc = h.get("most_common_chords", [])
        if rnc:
            chord_str = ", ".join(
                f"{c['roman']} [{c['chord']}] ({c['percentage']}%)" for c in rnc[:6]
            )
            lines.append(f"- Most common chords: {chord_str}")
        elif mc:
            chord_str = ", ".join(f"{c['chord']} ({c['percentage']}%)" for c in mc[:6])
            lines.append(f"- Most common chords: {chord_str}")

        progs = h.get("detected_progressions", [])
        if progs:
            prog_str = ", ".join(f"{p['name']} ({p['occurrences']}x)" for p in progs[:3])
            lines.append(f"- Detected progressions: {prog_str}")

        kmt = h.get("key_mode_timeline", [])
        if kmt:
            lines.append("- Key/mode timeline:")
            for entry in kmt:
                conf_min = entry.get("conf_min", entry.get("confidence", 0))
                conf_max = entry.get("conf_max", entry.get("confidence", 0))
                if abs(conf_min - conf_max) < 0.005:
                    c_str = f"{round(conf_min * 100)}%"
                else:
                    c_str = f"{round(conf_min * 100)}\u2013{round(conf_max * 100)}%"
                lines.append(
                    f"  - {_format_time(entry['start'])}\u2013{_format_time(entry['end'])} "
                    f"{entry['key_mode']} ({c_str})"
                )
    lines.append("")

    # Melodic (gated on include_melodic)
    lines.append("## Melodic")
    if m.get("degraded"):
        lines.append(f"> *{m.get('error', 'Melodic analysis unavailable')}*")
    elif not m:
        lines.append("> *Melodic analysis not run. Call analyze_track with include_melodic=True for note-level detail.*")
    else:
        pr = m.get("pitch_range", {})
        if pr:
            lines.append(
                f"- Pitch range: {pr.get('low_note', '?')} to {pr.get('high_note', '?')} "
                f"(primary {pr.get('primary_low', '?')} to {pr.get('primary_high', '?')})"
            )
        ic = m.get("interval_character", "?")
        avg = m.get("avg_interval_size", "?")
        upct = m.get("unison_pct", 0)
        if upct and upct > 5:
            lines.append(
                f"- Interval character: {ic} (avg {avg}st, {upct}% unisons; "
                f"excluding unisons: {m.get('interval_character_no_unison', ic)} "
                f"avg {m.get('avg_interval_size_no_unison', avg)}st)"
            )
        else:
            lines.append(f"- Interval character: {ic} (avg {avg}st)")

        pc = m.get("phrase_count", 0)
        apl = m.get("avg_phrase_length_seconds", 0)
        if pc:
            lines.append(f"- Phrases: {pc} × avg {apl}s")

        dc = m.get("dominant_contour")
        cs = m.get("contour_summary", {}) or {}
        if dc and cs:
            dist = ", ".join(f"{k} {v}%" for k, v in sorted(cs.items(), key=lambda x: -x[1]))
            lines.append(f"- Contour: {dc} ({dist})")
        elif dc:
            lines.append(f"- Contour: {dc}")

        rt = m.get("register_trend")
        if rt:
            lines.append(f"- Register movement: {rt}")

        nd = m.get("note_density_per_beat")
        if nd is not None:
            lines.append(f"- Note density: {nd} notes/beat")

        ri = m.get("register_intervals", {}) or {}
        reg_parts = []
        for name in ("bass", "mid", "lead"):
            reg = ri.get(name, {})
            nc = reg.get("note_count", 0)
            if nc:
                char = reg.get("interval_character", "")
                rav = reg.get("avg_interval_size", "")
                detail = f", {char}, avg {rav}st" if char else ""
                reg_parts.append(f"{name}: {nc} notes{detail}")
        if reg_parts:
            lines.append(f"- Register breakdown: {' | '.join(reg_parts)}")

        itc = m.get("interval_transitions_common", [])
        if itc:
            top = ", ".join(f"{t['from']}\u2192{t['to']} ({t['percentage']}%)" for t in itc[:5])
            lines.append(f"- Common transitions: {top}")
        itu = m.get("interval_transitions_unexpected", [])
        if itu:
            top = ", ".join(f"{t['from']}\u2192{t['to']} ({t['observed_expected_ratio']}x)" for t in itu[:3])
            lines.append(f"- Unexpected transitions: {top}")
    lines.append("")

    # Rhythmic
    lines.append("## Rhythmic")
    if not r.get("has_clear_beat", True):
        lines.append("- No clear beat detected (ambient/drone)")
    else:
        sw = r.get("swing_ratio", 50)
        swing_desc = "straight" if abs(sw - 50) < 5 else "light swing" if sw < 60 else "triplet swing"
        lines.append(f"- Swing: {sw}% ({swing_desc}; straight ≈ 50%)")
        si = r.get("syncopation_index", 0)
        lines.append(f"- Syncopation index: {si} (0 = none, 1 = max)")
        ts = r.get("tempo_stability", "?")
        tcv = r.get("tempo_cv")
        anchor = f" (CV={tcv:.3f})" if isinstance(tcv, (int, float)) else ""
        lines.append(f"- Tempo stability: {ts}{anchor}")
        rp = r.get("rhythm_pattern")
        if rp and rp != "unclassified":
            lines.append(f"- Pattern: {rp}")
        ds = r.get("density_shape")
        if ds and ds != "insufficient data":
            lines.append(f"- Rhythmic density: {ds}")
        ep = r.get("euclidean_patterns", [])
        for p in ep:
            lines.append(f"- Euclidean: {p['pattern']} — {p['name']} (hd {p['hamming_distance']})")
    lines.append("")

    # Timbral
    lines.append("## Timbral")
    br = s.get("brightness", "?")
    c_hz = s.get("centroid_hz")
    ro_hz = s.get("rolloff_hz")
    parts = [f"centroid {c_hz} Hz"]
    if isinstance(ro_hz, (int, float)):
        parts.append(f"rolloff85 {ro_hz} Hz")
    lines.append(f"- Brightness: {br} ({', '.join(parts)})")

    be = s.get("band_energy", {}) or {}
    if be:
        bstr = " / ".join(f"{k.replace('_', ' ')}: {v}%" for k, v in be.items())
        lines.append(f"- Band energy: {bstr}")
    fl = s.get("flatness")
    if fl is not None:
        tonal = "tonal" if fl < 0.01 else "mixed" if fl < 0.1 else "noisy/textural"
        lines.append(f"- Spectral character: {tonal} (flatness {fl:.4f})")
    lines.append("")

    # Structure (compacted)
    lines.append("## Structure")
    form = st.get("form", "?")
    sc = st.get("section_count", 0)
    asl = st.get("avg_section_length")
    asl_str = f", avg section {asl}s" if asl else ""
    lines.append(f"- Form: `{form}` ({sc} sections{asl_str})")
    segs = st.get("segments", []) or []
    profs = st.get("section_profiles", []) or []
    if segs:
        lines.append("- Section map:")
        for ml in _compact_section_map(segs, profs):
            lines.append(f"  {ml}")
    lines.append("")

    # Energy
    lines.append("## Energy")
    dyn = e.get("dynamic_range_db", "?")
    shape = e.get("energy_shape", "?")
    lines.append(f"- Dynamic range: {dyn} dB")
    lines.append(f"- Energy shape: {shape}")
    lines.append(f"- Mean RMS: {e.get('rms_mean_db', '?')} dB / Peak RMS: {e.get('rms_peak_db', '?')} dB")
    builds = e.get("builds", []) or []
    for b in builds[:5]:
        lines.append(f"- {b['type'].title()} at {_format_time(b['time'])} (ratio {b['ratio']})")
    lines.append("")

    # Notable — deviations worth flagging
    notes = _notable(analysis)
    if notes:
        lines.append("## Notable")
        for n in notes:
            lines.append(f"- {n}")
        lines.append("")

    # Vivid translation hints
    hints = _translation_hints(analysis)
    if hints:
        lines.append("## Vivid translation hints")
        for hint in hints:
            lines.append(f"- {hint}")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"
