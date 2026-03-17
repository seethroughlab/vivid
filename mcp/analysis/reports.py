"""Report generation for track analysis.

Markdown report generation for single-track analysis,
character summary synthesis.

Ported from familiar/backend/app/services/track_analysis/reports.py
with imports rewritten to relative. Comparative report removed (not needed for MCP).
"""

from typing import Any

from .constants import INTERVAL_NAMES
from .utils import _format_time

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

    return "\n".join(lines)
