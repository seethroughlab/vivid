"""Analysis pipeline entry points for Vivid.

Standalone functions (no database dependencies) for running the analysis
pipeline on audio files.

Ported from familiar/backend/app/services/track_analysis/pipeline.py
with all DB/SQLAlchemy code stripped.
"""

import logging
import time
from pathlib import Path
from typing import Any

import numpy as np

from .analyzers import (
    _add_melodic_sketches,
    _analyze_energy,
    _analyze_harmonic,
    _analyze_melodic,
    _analyze_rhythmic,
    _analyze_spectral,
    _analyze_structural,
    _precompute_shared,
)
from .constants import MIN_DURATION_SECONDS
from .utils import _sanitize_for_json

logger = logging.getLogger(__name__)


def run_cheap_sections(
    y: np.ndarray,
    sr: int,
    shared: dict[str, Any],
    file_path: str,
) -> tuple[dict[str, Any], dict[str, Any], list[dict[str, str]]]:
    """Run the 5 cheap section analyzers (everything except melodic/basic-pitch).

    Returns (analysis_detail, feature_scalars, section_errors).
    """
    results: dict[str, Any] = {}
    section_errors: list[dict[str, str]] = []

    # Use file path basename as track_id for the analyzers
    track_id = Path(file_path).stem

    for section_name, analyzer in [
        ("harmonic", _analyze_harmonic),
        ("rhythmic", _analyze_rhythmic),
        ("spectral", _analyze_spectral),
        ("structural", _analyze_structural),
        ("energy", _analyze_energy),
    ]:
        try:
            results[section_name] = analyzer(y, sr, shared, file_path, track_id)
        except Exception as e:
            logger.error(f"Section '{section_name}' failed for {file_path}: {e}")
            section_errors.append({"section": section_name, "error": str(e)})
            results[section_name] = {"error": str(e)}

    # Post-processing: melodic sketches per section (uses chroma, not basic-pitch)
    try:
        _add_melodic_sketches(results, shared)
    except Exception as e:
        logger.warning(f"Melodic sketch generation failed: {e}")

    results = _sanitize_for_json(results)
    section_errors = _sanitize_for_json(section_errors)

    feature_scalars = extract_feature_scalars(results)
    return results, feature_scalars, section_errors


def run_melodic(
    y: np.ndarray,
    sr: int,
    shared: dict[str, Any],
    file_path: str,
) -> dict[str, Any]:
    """Run melodic analysis (basic-pitch) on an audio file.

    Returns the melodic analysis dict, or a degraded dict on error.
    """
    track_id = Path(file_path).stem

    # Cap at 6 min for basic-pitch to prevent OOM
    MELODIC_DURATION_CAP = 360
    duration = len(y) / sr

    needs_truncation = duration > MELODIC_DURATION_CAP

    try:
        melodic_result = _analyze_melodic(
            y, sr, shared, file_path, track_id,
            truncate_duration=MELODIC_DURATION_CAP if needs_truncation else None,
        )
        return _sanitize_for_json(melodic_result)
    except Exception as e:
        logger.error(f"Melodic analysis failed for {file_path}: {e}")
        return {"degraded": True, "error": str(e)}


def extract_feature_scalars(results: dict[str, Any]) -> dict[str, Any]:
    """Extract typed scalar values from analysis results.

    Returns a flat dict of key metrics suitable for quick reference.
    """
    scalars: dict[str, Any] = {}

    # Harmonic section
    harmonic = results.get("harmonic", {})
    if harmonic.get("harmonic_content"):
        scalars["harmonic_complexity"] = harmonic.get("harmonic_rhythm")
        scalars["key_stability"] = harmonic.get("key_stability")
        scalars["modal_character"] = harmonic.get("modal_character")
        scalars["modal_confidence"] = harmonic.get("modal_confidence")

    # Rhythmic section
    rhythmic = results.get("rhythmic", {})
    scalars["bpm"] = rhythmic.get("bpm")
    raw_swing = rhythmic.get("swing_ratio")
    if raw_swing is not None:
        scalars["swing_ratio"] = raw_swing / 100.0 if raw_swing > 1 else raw_swing
    syncopation = rhythmic.get("syncopation_index")
    if syncopation is not None:
        scalars["syncopation"] = syncopation
    scalars["tempo_character"] = rhythmic.get("tempo_stability")

    # Spectral section
    spectral = results.get("spectral", {})
    centroid_hz = spectral.get("centroid_hz")
    if centroid_hz is not None:
        scalars["brightness"] = min(centroid_hz / 8000.0, 1.0)

    # Energy section
    energy = results.get("energy", {})
    scalars["dynamic_range_db"] = energy.get("dynamic_range_db")
    raw_shape = energy.get("energy_shape")
    if raw_shape:
        scalars["energy_shape"] = raw_shape.replace(" ", "_").replace("-", "_")

    # Structural section
    structural = results.get("structural", {})
    scalars["section_count"] = structural.get("section_count")
    scalars["form_string"] = structural.get("form")

    # Remove None values
    return {k: v for k, v in scalars.items() if v is not None}


def load_and_analyze(
    file_path: str,
    include_melodic: bool = False,
) -> tuple[dict[str, Any], dict[str, Any], float]:
    """Load audio and run full analysis pipeline.

    Returns (analysis_results, feature_scalars, duration_seconds).
    """
    import librosa

    start_time = time.time()
    path = Path(file_path)

    if not path.exists():
        raise FileNotFoundError(f"Audio file not found: {file_path}")

    # Load audio
    y, sr = librosa.load(str(path), sr=22050, mono=True)
    duration = len(y) / sr

    if duration < MIN_DURATION_SECONDS:
        raise ValueError(f"Track too short ({duration:.1f}s). Minimum: {MIN_DURATION_SECONDS}s")

    # Check for near-silence
    rms_all = librosa.feature.rms(y=y)[0]
    if np.mean(rms_all) < 1e-6:
        raise ValueError("Track is near-silence")

    # Pre-compute shared representations
    shared = _precompute_shared(y, sr)

    # Run cheap sections (5 analyzers)
    results, feature_scalars, section_errors = run_cheap_sections(
        y, sr, shared, str(path)
    )

    # Optionally run melodic analysis
    if include_melodic:
        melodic_result = run_melodic(y, sr, shared, str(path))
        results["melodic"] = melodic_result

        # Add melodic scalars
        if not melodic_result.get("degraded"):
            melodic_scalars = extract_melodic_scalars(melodic_result)
            feature_scalars.update(melodic_scalars)

    elapsed = time.time() - start_time

    if section_errors:
        logger.warning(f"Analysis completed with {len(section_errors)} section errors: {section_errors}")

    return results, feature_scalars, duration


def extract_melodic_scalars(melodic_results: dict[str, Any]) -> dict[str, Any]:
    """Extract typed scalar values from melodic analysis results."""
    scalars: dict[str, Any] = {}

    if melodic_results.get("degraded"):
        return scalars

    scalars["note_density"] = melodic_results.get("note_density_per_beat")
    scalars["interval_character"] = melodic_results.get("interval_character")

    pitch_range = melodic_results.get("pitch_range")
    if isinstance(pitch_range, dict):
        low = pitch_range.get("low")
        high = pitch_range.get("high")
        if low is not None and high is not None:
            scalars["pitch_range"] = high - low

    return {k: v for k, v in scalars.items() if v is not None}
