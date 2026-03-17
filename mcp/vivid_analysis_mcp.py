"""Vivid Analysis MCP Server — analyze audio and images for LLM-driven graph creation."""

import asyncio
import json
import logging
from pathlib import Path

from mcp.server.fastmcp import FastMCP

from analysis.cache import (
    get_metadata_from_file,
    has_melodic,
    read_cache,
    update_cache_melodic,
    write_cache,
)
from analysis.pipeline import load_and_analyze, run_melodic, extract_melodic_scalars
from analysis.reports import generate_report
from analysis.utils import _sanitize_for_json

from visual.cache import (
    read_cache as visual_read_cache,
    write_cache as visual_write_cache,
)
from visual.pipeline import (
    load_and_analyze as visual_load_and_analyze,
    IMAGE_EXTENSIONS,
)
from visual.reports import generate_report as visual_generate_report, generate_summary_report
from visual.synthesis import synthesize_collection

logger = logging.getLogger(__name__)

mcp = FastMCP("vivid-analysis", instructions="""Analysis Server for Vivid — analyze audio files and reference images to extract features for building reactive visual graphs.

## Audio Workflow

1. **Analyze** — call `analyze_track` with an audio file path to run comprehensive analysis
2. **Reference** — call `get_analysis` or `get_analysis_report` to retrieve cached results
3. **Build** — use the analysis data with the Vivid graph MCP server to create music-reactive visuals

### Audio Analysis Sections (5 core + optional melodic)
- **Harmonic**: key, mode, chords, progressions, key stability, key/mode timeline
- **Rhythmic**: BPM, swing, syncopation, Euclidean patterns, rhythm classification
- **Spectral**: brightness, 6-band energy, MFCCs, spectral contrast/flatness
- **Structural**: section boundaries, form string (AABACA), section energy/timbral profiles
- **Energy**: RMS curve, dynamic range, energy shape, build/drop detection
- **Melodic** (opt-in): pitch range, intervals, phrases, contour, register separation

Plus **mood/genre/instrumentation tags** via CLAP embeddings (48 semantic descriptors).

## Image Workflow

1. **Analyze** — call `analyze_image` with a reference image path for full visual analysis
2. **Reference** — call `get_image_analysis` or `get_image_report` to retrieve cached results
3. **Folder** — call `analyze_reference_folder` to analyze all images in a directory with cross-image synthesis
4. **Build** — use the visual analysis data to match a reference aesthetic in Vivid graphs

### Image Analysis Sections (4 core + style tags)
- **Color**: dominant palette (hex + RGB floats), harmony classification, HSV distribution, warm/cool ratio
- **Luminance**: key classification (low/balanced/high), shadow/midtone/highlight %, dynamic range
- **Texture**: edge density, detail level, smoothness, grain estimate
- **Composition**: weight quadrants, symmetry scores, complexity, visual center of mass

Plus **visual style tags** via SigLIP embeddings (35 descriptors across 4 categories: visual_style, mood, movement, color_mood).

## Translating Analysis to Vivid Graphs

### Audio → Vivid
- **BPM** → Clock node tempo, LFO rates
- **Key/mode** → oscillator frequencies, sequencer note choices
- **Energy shape + section boundaries** → variation presets (Intro, Verse, Chorus, Drop)
- **Brightness/spectral data** → visual color temperatures, filter settings
- **Rhythmic patterns** (syncopation, swing) → LFO and sequencer behavior
- **Build/drop events** → trigger points for transitions
- **Mood tags** → overall aesthetic direction (color palettes, motion styles, intensity)

### Image → Vivid
- **Dominant palette** (RGB floats) → color node values, gradient stops
- **Warm/cool ratio** → color temperature, filter hue
- **Key classification** → bloom threshold, overall brightness
- **Edge density + detail** → noise octaves, lacunarity, blur radius
- **Smoothness** → Gaussian blur, feedback diffusion
- **Dynamic range** → contrast, levels adjustment
- **Composition weight** → element positioning, focal points
- **Style tags** → aesthetic direction, motion character, visual mood
""")


@mcp.tool()
async def analyze_track(
    file_path: str,
    include_melodic: bool = False,
    include_mood_tags: bool = True,
) -> str:
    """Run comprehensive music analysis on an audio file.

    Returns an LLM-optimized markdown report plus structured JSON with all analysis
    results, feature scalars, and mood tags. Results are cached as a sidecar JSON file
    next to the audio — subsequent calls return cached data unless the file has changed.

    Args:
        file_path: Absolute path to an audio file (MP3, WAV, FLAC, OGG, etc.)
        include_melodic: Run basic-pitch MIDI transcription for note-level analysis.
            Disabled by default because it's 5-10x slower (~60-120s vs ~10-30s).
            Enable when you need pitch ranges, interval patterns, or melodic contour.
        include_mood_tags: Run CLAP-based mood/genre/instrumentation tagging.
            Enabled by default — adds ~5s on first call (model cached after).
    """
    path = Path(file_path).resolve()
    if not path.exists():
        return json.dumps({"ok": False, "error": f"File not found: {file_path}"})

    # Check cache
    cached = read_cache(str(path))
    if cached is not None:
        # If melodic was requested but not in cache, run just melodic
        need_melodic = include_melodic and not has_melodic(str(path))

        if not need_melodic:
            return json.dumps({
                "ok": True,
                "cached": True,
                "report": cached.get("report", ""),
                "analysis": cached.get("analysis", {}),
                "feature_scalars": cached.get("feature_scalars", {}),
                "mood_tags": cached.get("mood_tags", []),
                "metadata": cached.get("metadata", {}),
                "duration_seconds": cached.get("duration_seconds", 0),
            })

        # Run just melodic and merge into cache
        try:
            melodic_result = await asyncio.to_thread(
                _run_melodic_only, str(path)
            )
            if melodic_result.get("degraded"):
                return json.dumps({
                    "ok": True,
                    "cached": True,
                    "melodic_error": melodic_result.get("error"),
                    "report": cached.get("report", ""),
                    "analysis": cached.get("analysis", {}),
                    "feature_scalars": cached.get("feature_scalars", {}),
                    "mood_tags": cached.get("mood_tags", []),
                    "metadata": cached.get("metadata", {}),
                    "duration_seconds": cached.get("duration_seconds", 0),
                })

            # Merge melodic into cached analysis for report generation
            analysis_with_melodic = dict(cached.get("analysis", {}))
            analysis_with_melodic["melodic"] = melodic_result

            metadata = cached.get("metadata", {})
            report = generate_report(
                analysis_with_melodic, {
                    **metadata,
                    "duration_seconds": cached.get("duration_seconds", 0),
                },
                for_llm=True,
            )

            update_cache_melodic(str(path), melodic_result, report=report)

            # Merge melodic scalars
            feature_scalars = dict(cached.get("feature_scalars", {}))
            feature_scalars.update(extract_melodic_scalars(melodic_result))

            return json.dumps({
                "ok": True,
                "cached": False,
                "melodic_added": True,
                "report": report,
                "analysis": analysis_with_melodic,
                "feature_scalars": feature_scalars,
                "mood_tags": cached.get("mood_tags", []),
                "metadata": metadata,
                "duration_seconds": cached.get("duration_seconds", 0),
            })

        except Exception as e:
            logger.error(f"Melodic analysis failed: {e}")
            return json.dumps({
                "ok": True,
                "cached": True,
                "melodic_error": str(e),
                "report": cached.get("report", ""),
                "analysis": cached.get("analysis", {}),
                "feature_scalars": cached.get("feature_scalars", {}),
                "mood_tags": cached.get("mood_tags", []),
                "metadata": cached.get("metadata", {}),
                "duration_seconds": cached.get("duration_seconds", 0),
            })

    # Full analysis (CPU-bound, run in thread)
    try:
        analysis, feature_scalars, duration = await asyncio.to_thread(
            load_and_analyze, str(path), include_melodic
        )
    except (FileNotFoundError, ValueError) as e:
        return json.dumps({"ok": False, "error": str(e)})
    except Exception as e:
        logger.error(f"Analysis failed for {file_path}: {e}", exc_info=True)
        return json.dumps({"ok": False, "error": f"Analysis failed: {e}"})

    # Mood tags (also CPU-bound due to CLAP inference)
    mood_tags: list = []
    if include_mood_tags:
        try:
            mood_tags = await asyncio.to_thread(_compute_mood_tags, str(path))
        except Exception as e:
            logger.warning(f"Mood tagging failed: {e}")

    # Extract metadata from file tags
    try:
        metadata = get_metadata_from_file(str(path))
    except Exception:
        metadata = {}

    # If no metadata from tags, use filename
    if not metadata.get("title"):
        metadata["title"] = path.stem
    if not metadata.get("artist"):
        metadata["artist"] = "Unknown Artist"

    # Generate report
    report = generate_report(
        analysis,
        {**metadata, "duration_seconds": duration},
        for_llm=True,
    )

    # Write cache
    try:
        write_cache(
            str(path),
            analysis=analysis,
            feature_scalars=feature_scalars,
            duration_seconds=duration,
            mood_tags=mood_tags,
            report=report,
            metadata=metadata,
        )
    except Exception as e:
        logger.warning(f"Failed to write cache: {e}")

    return json.dumps({
        "ok": True,
        "cached": False,
        "report": report,
        "analysis": analysis,
        "feature_scalars": feature_scalars,
        "mood_tags": mood_tags,
        "metadata": metadata,
        "duration_seconds": duration,
    })


@mcp.tool()
async def get_analysis(file_path: str) -> str:
    """Return cached analysis for a previously analyzed track. No computation.

    Args:
        file_path: Absolute path to the audio file
    """
    path = Path(file_path).resolve()
    cached = read_cache(str(path))

    if cached is None:
        return json.dumps({
            "ok": False,
            "error": f"No cached analysis found for {file_path}. Run analyze_track first.",
        })

    return json.dumps({
        "ok": True,
        "analysis": cached.get("analysis", {}),
        "feature_scalars": cached.get("feature_scalars", {}),
        "mood_tags": cached.get("mood_tags", []),
        "metadata": cached.get("metadata", {}),
        "duration_seconds": cached.get("duration_seconds", 0),
    })


@mcp.tool()
async def get_analysis_report(file_path: str) -> str:
    """Return just the LLM-optimized markdown report for a previously analyzed track.

    Lighter than get_analysis — returns only the human-readable report without
    the full structured JSON. Good for refreshing context without burning tokens.

    Args:
        file_path: Absolute path to the audio file
    """
    path = Path(file_path).resolve()
    cached = read_cache(str(path))

    if cached is None:
        return json.dumps({
            "ok": False,
            "error": f"No cached analysis found for {file_path}. Run analyze_track first.",
        })

    report = cached.get("report", "")
    if not report:
        # Regenerate report from cached data
        analysis = cached.get("analysis", {})
        metadata = cached.get("metadata", {})
        metadata["duration_seconds"] = cached.get("duration_seconds", 0)
        report = generate_report(analysis, metadata, for_llm=True)

    return json.dumps({
        "ok": True,
        "report": report,
        "metadata": cached.get("metadata", {}),
    })


# ─── Image analysis tools ──────────────────────────────────────────────────


@mcp.tool()
async def analyze_image(
    file_path: str,
    include_style_tags: bool = True,
) -> str:
    """Run comprehensive visual analysis on a reference image.

    Returns an LLM-optimized markdown report plus structured JSON with color palette,
    luminance, texture, composition analysis, feature scalars, and style tags.
    Results are cached as a sidecar JSON file next to the image.

    Args:
        file_path: Absolute path to an image file (PNG, JPG, BMP, TIFF, WebP, GIF)
        include_style_tags: Run SigLIP-based visual style/mood tagging.
            Enabled by default — adds ~5s on first call (model cached after).
    """
    path = Path(file_path).resolve()
    if not path.exists():
        return json.dumps({"ok": False, "error": f"File not found: {file_path}"})

    # Check cache
    cached = visual_read_cache(str(path))
    if cached is not None:
        return json.dumps({
            "ok": True,
            "cached": True,
            "report": cached.get("report", ""),
            "analysis": cached.get("analysis", {}),
            "feature_scalars": cached.get("feature_scalars", {}),
            "style_tags": cached.get("style_tags", []),
        })

    # Full analysis (CPU-bound, run in thread)
    try:
        analysis, feature_scalars, style_tags = await asyncio.to_thread(
            visual_load_and_analyze, str(path), include_style_tags
        )
    except (FileNotFoundError, ValueError) as e:
        return json.dumps({"ok": False, "error": str(e)})
    except Exception as e:
        logger.error(f"Image analysis failed for {file_path}: {e}", exc_info=True)
        return json.dumps({"ok": False, "error": f"Image analysis failed: {e}"})

    # Generate report
    report = visual_generate_report(analysis, style_tags, str(path))

    # Write cache
    try:
        visual_write_cache(
            str(path),
            analysis=analysis,
            feature_scalars=feature_scalars,
            style_tags=style_tags,
            report=report,
        )
    except Exception as e:
        logger.warning(f"Failed to write visual cache: {e}")

    return json.dumps({
        "ok": True,
        "cached": False,
        "report": report,
        "analysis": analysis,
        "feature_scalars": feature_scalars,
        "style_tags": style_tags,
    })


@mcp.tool()
async def get_image_analysis(file_path: str) -> str:
    """Return cached visual analysis for a previously analyzed image. No computation.

    Args:
        file_path: Absolute path to the image file
    """
    path = Path(file_path).resolve()
    cached = visual_read_cache(str(path))

    if cached is None:
        return json.dumps({
            "ok": False,
            "error": f"No cached visual analysis found for {file_path}. Run analyze_image first.",
        })

    return json.dumps({
        "ok": True,
        "analysis": cached.get("analysis", {}),
        "feature_scalars": cached.get("feature_scalars", {}),
        "style_tags": cached.get("style_tags", []),
    })


@mcp.tool()
async def get_image_report(file_path: str) -> str:
    """Return just the LLM-optimized markdown report for a previously analyzed image.

    Lighter than get_image_analysis — returns only the human-readable report without
    the full structured JSON. Good for refreshing context without burning tokens.

    Args:
        file_path: Absolute path to the image file
    """
    path = Path(file_path).resolve()
    cached = visual_read_cache(str(path))

    if cached is None:
        return json.dumps({
            "ok": False,
            "error": f"No cached visual analysis found for {file_path}. Run analyze_image first.",
        })

    report = cached.get("report", "")
    if not report:
        # Regenerate report from cached data
        analysis = cached.get("analysis", {})
        style_tags = cached.get("style_tags", [])
        report = visual_generate_report(analysis, style_tags, str(path))

    return json.dumps({
        "ok": True,
        "report": report,
    })


@mcp.tool()
async def analyze_reference_folder(
    folder_path: str,
    extensions: list[str] | None = None,
    include_style_tags: bool = True,
) -> str:
    """Analyze all images in a folder and produce per-image summaries plus cross-image synthesis.

    Returns a summary for each image (compact — scalars + tags + brief report) plus an
    aggregate synthesis showing the collection's overall palette, common style tags,
    luminance/texture/color tendencies, and a natural language summary.

    Args:
        folder_path: Absolute path to a folder containing reference images
        extensions: File extensions to include (default: common image formats).
            Example: [".png", ".jpg"]
        include_style_tags: Run SigLIP style tagging per image (default True)
    """
    folder = Path(folder_path).resolve()
    if not folder.exists():
        return json.dumps({"ok": False, "error": f"Folder not found: {folder_path}"})
    if not folder.is_dir():
        return json.dumps({"ok": False, "error": f"Not a directory: {folder_path}"})

    # Determine valid extensions
    valid_exts = set(extensions) if extensions else IMAGE_EXTENSIONS

    # Find image files
    image_files = sorted([
        f for f in folder.iterdir()
        if f.is_file() and f.suffix.lower() in valid_exts
    ])

    if not image_files:
        return json.dumps({
            "ok": False,
            "error": f"No image files found in {folder_path} with extensions {sorted(valid_exts)}",
        })

    # Analyze each image
    per_image = []
    for img_path in image_files:
        try:
            # Check cache first
            cached = visual_read_cache(str(img_path))
            if cached is not None:
                analysis = cached.get("analysis", {})
                feature_scalars = cached.get("feature_scalars", {})
                style_tags = cached.get("style_tags", [])
            else:
                analysis, feature_scalars, style_tags = await asyncio.to_thread(
                    visual_load_and_analyze, str(img_path), include_style_tags
                )
                # Generate full report for caching
                report = visual_generate_report(analysis, style_tags, str(img_path))
                try:
                    visual_write_cache(
                        str(img_path),
                        analysis=analysis,
                        feature_scalars=feature_scalars,
                        style_tags=style_tags,
                        report=report,
                    )
                except Exception as e:
                    logger.warning(f"Failed to write visual cache for {img_path}: {e}")

            # Compact summary for folder results (lighter on tokens)
            summary_report = generate_summary_report(
                analysis, style_tags, feature_scalars, str(img_path)
            )

            per_image.append({
                "file_path": str(img_path),
                "file_name": img_path.name,
                "summary_report": summary_report,
                "feature_scalars": feature_scalars,
                "style_tags": style_tags,
                "analysis": analysis,
            })

        except Exception as e:
            logger.error(f"Failed to analyze {img_path}: {e}")
            per_image.append({
                "file_path": str(img_path),
                "file_name": img_path.name,
                "error": str(e),
            })

    # Cross-image synthesis
    valid_analyses = [entry for entry in per_image if "error" not in entry]
    synthesis = synthesize_collection(valid_analyses) if valid_analyses else {}

    # Build combined summary report
    report_lines = [f"# Reference Folder Analysis: {folder.name}", ""]
    report_lines.append(f"**Images analyzed:** {len(valid_analyses)} / {len(image_files)}")
    report_lines.append("")

    if synthesis.get("summary"):
        report_lines.append("## Collection Summary")
        report_lines.append(f"> {synthesis['summary']}")
        report_lines.append("")

    # Aggregate palette
    agg_palette = synthesis.get("aggregate_palette", [])
    if agg_palette:
        report_lines.append("## Aggregate Palette")
        hex_parts = [f"{c['hex']} ({c['percentage']}%)" for c in agg_palette[:6]]
        report_lines.append(f"- {', '.join(hex_parts)}")
        report_lines.append("")

    # Common tags
    common_tags = synthesis.get("common_tags", [])
    if common_tags:
        report_lines.append("## Common Style Tags")
        tag_parts = [
            f"{t['tag']} (avg {t['avg_confidence']}, {round(t['presence'] * 100)}% of images)"
            for t in common_tags[:8]
        ]
        report_lines.append(f"- {', '.join(tag_parts)}")
        report_lines.append("")

    # Per-image summaries
    report_lines.append("## Per-Image Summaries")
    report_lines.append("")
    for entry in per_image:
        if "error" in entry:
            report_lines.append(f"### {entry['file_name']}")
            report_lines.append(f"> Error: {entry['error']}")
            report_lines.append("")
        else:
            report_lines.append(entry.get("summary_report", ""))

    combined_report = "\n".join(report_lines)

    return json.dumps({
        "ok": True,
        "image_count": len(image_files),
        "analyzed_count": len(valid_analyses),
        "report": combined_report,
        "synthesis": synthesis,
        "per_image": [
            {
                "file_path": e.get("file_path"),
                "file_name": e.get("file_name"),
                "feature_scalars": e.get("feature_scalars", {}),
                "style_tags": e.get("style_tags", []),
                "error": e.get("error"),
            }
            for e in per_image
        ],
    })


# ─── Internal helpers (run in thread pool) ────────────────────────────────

def _compute_mood_tags(file_path: str) -> list:
    """Compute mood tags for an audio file. Runs synchronously."""
    from analysis.embedding import extract_embedding
    from analysis.mood_tags import compute_mood_tags

    embedding = extract_embedding(file_path)
    if embedding is None:
        return []
    return compute_mood_tags(embedding)


def _run_melodic_only(file_path: str) -> dict:
    """Run just the melodic analyzer on an audio file. Runs synchronously."""
    import librosa
    import numpy as np

    from analysis.analyzers import _precompute_shared

    y, sr = librosa.load(file_path, sr=22050, mono=True)
    shared = _precompute_shared(y, sr)
    return run_melodic(y, sr, shared, file_path)


if __name__ == "__main__":
    mcp.run()
