"""Sidecar JSON cache for analysis results.

For /path/to/track.mp3, cache at /path/to/track.vivid-analysis.json.
Cache invalidated when source file mtime or size changes.
Supports incremental melodic addition to existing cache.
"""

import json
import logging
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

CACHE_VERSION = 1
CACHE_SUFFIX = ".vivid-analysis.json"


def _cache_path(source_path: str) -> Path:
    """Get the sidecar cache path for an audio file."""
    p = Path(source_path)
    return p.parent / (p.stem + CACHE_SUFFIX)


def _source_fingerprint(source_path: str) -> tuple[float, int]:
    """Get (mtime, size) for cache invalidation."""
    stat = os.stat(source_path)
    return (stat.st_mtime, stat.st_size)


def read_cache(source_path: str) -> dict[str, Any] | None:
    """Read cached analysis for a source file.

    Returns the full cache dict if valid, or None if missing/stale.
    """
    cp = _cache_path(source_path)
    if not cp.exists():
        return None

    try:
        data = json.loads(cp.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as e:
        logger.warning(f"Failed to read cache {cp}: {e}")
        return None

    # Version check
    if data.get("version") != CACHE_VERSION:
        logger.info(f"Cache version mismatch for {source_path}, will re-analyze")
        return None

    # Staleness check: compare mtime + size
    try:
        mtime, size = _source_fingerprint(source_path)
    except OSError:
        return None

    if (abs(data.get("source_mtime", 0) - mtime) > 0.01
            or data.get("source_size", 0) != size):
        logger.info(f"Cache stale for {source_path} (file changed)")
        return None

    return data


def write_cache(
    source_path: str,
    analysis: dict[str, Any],
    feature_scalars: dict[str, Any],
    duration_seconds: float,
    mood_tags: list[dict[str, Any]] | None = None,
    report: str | None = None,
    metadata: dict[str, Any] | None = None,
) -> Path:
    """Write analysis results to sidecar JSON cache.

    Returns the cache file path.
    """
    mtime, size = _source_fingerprint(source_path)

    cache_data = {
        "version": CACHE_VERSION,
        "source_file": Path(source_path).name,
        "source_mtime": mtime,
        "source_size": size,
        "analyzed_at": datetime.now(timezone.utc).isoformat(),
        "duration_seconds": duration_seconds,
        "analysis": analysis,
        "feature_scalars": feature_scalars,
        "mood_tags": mood_tags or [],
        "report": report or "",
        "metadata": metadata or {},
    }

    cp = _cache_path(source_path)
    cp.write_text(json.dumps(cache_data, indent=2, ensure_ascii=False), encoding="utf-8")
    logger.info(f"Wrote analysis cache: {cp}")
    return cp


def update_cache_melodic(
    source_path: str,
    melodic_results: dict[str, Any],
    report: str | None = None,
) -> bool:
    """Merge melodic results into an existing cache.

    Returns True if successful, False if no existing cache found.
    """
    data = read_cache(source_path)
    if data is None:
        return False

    # Merge melodic into analysis
    data["analysis"]["melodic"] = melodic_results

    # Update report if provided
    if report is not None:
        data["report"] = report

    # Update timestamp
    data["analyzed_at"] = datetime.now(timezone.utc).isoformat()

    cp = _cache_path(source_path)
    cp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    logger.info(f"Updated cache with melodic data: {cp}")
    return True


def has_melodic(source_path: str) -> bool:
    """Check if cached analysis includes melodic data."""
    data = read_cache(source_path)
    if data is None:
        return False
    melodic = data.get("analysis", {}).get("melodic", {})
    return bool(melodic) and not melodic.get("degraded", False)


def get_metadata_from_file(file_path: str) -> dict[str, Any]:
    """Try to extract metadata (artist, title, album) from audio file tags."""
    try:
        import mutagen
        audio = mutagen.File(file_path)
        if audio is None:
            return {}

        metadata: dict[str, Any] = {}

        # Try common tag formats
        # ID3 (MP3)
        if hasattr(audio, "tags") and audio.tags:
            tags = audio.tags
            for key in ("TIT2", "title"):
                if key in tags:
                    metadata["title"] = str(tags[key])
                    break
            for key in ("TPE1", "artist"):
                if key in tags:
                    metadata["artist"] = str(tags[key])
                    break
            for key in ("TALB", "album"):
                if key in tags:
                    metadata["album"] = str(tags[key])
                    break

        # Vorbis comments (FLAC, OGG)
        if not metadata and hasattr(audio, "tags") and audio.tags:
            tags = audio.tags
            for key in ("title", "TITLE"):
                if key in tags:
                    val = tags[key]
                    metadata["title"] = val[0] if isinstance(val, list) else str(val)
                    break
            for key in ("artist", "ARTIST"):
                if key in tags:
                    val = tags[key]
                    metadata["artist"] = val[0] if isinstance(val, list) else str(val)
                    break
            for key in ("album", "ALBUM"):
                if key in tags:
                    val = tags[key]
                    metadata["album"] = val[0] if isinstance(val, list) else str(val)
                    break

        return metadata

    except Exception as e:
        logger.debug(f"Could not read metadata from {file_path}: {e}")
        return {}
