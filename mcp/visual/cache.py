"""Sidecar JSON cache for visual analysis results.

For /path/to/image.png, cache at /path/to/image.vivid-visual.json.
Cache invalidated when source file mtime or size changes.
"""

import json
import logging
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

CACHE_VERSION = 1
CACHE_SUFFIX = ".vivid-visual.json"


def _cache_path(source_path: str) -> Path:
    """Get the sidecar cache path for an image file."""
    p = Path(source_path)
    return p.parent / (p.stem + CACHE_SUFFIX)


def _source_fingerprint(source_path: str) -> tuple[float, int]:
    """Get (mtime, size) for cache invalidation."""
    stat = os.stat(source_path)
    return (stat.st_mtime, stat.st_size)


def read_cache(source_path: str) -> dict[str, Any] | None:
    """Read cached visual analysis for a source file.

    Returns the full cache dict if valid, or None if missing/stale.
    """
    cp = _cache_path(source_path)
    if not cp.exists():
        return None

    try:
        data = json.loads(cp.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as e:
        logger.warning(f"Failed to read visual cache {cp}: {e}")
        return None

    # Version check
    if data.get("version") != CACHE_VERSION:
        logger.info(f"Visual cache version mismatch for {source_path}, will re-analyze")
        return None

    # Staleness check
    try:
        mtime, size = _source_fingerprint(source_path)
    except OSError:
        return None

    if (abs(data.get("source_mtime", 0) - mtime) > 0.01
            or data.get("source_size", 0) != size):
        logger.info(f"Visual cache stale for {source_path} (file changed)")
        return None

    return data


def write_cache(
    source_path: str,
    analysis: dict[str, Any],
    feature_scalars: dict[str, Any],
    style_tags: list[dict[str, Any]] | None = None,
    report: str | None = None,
) -> Path:
    """Write visual analysis results to sidecar JSON cache.

    Returns the cache file path.
    """
    mtime, size = _source_fingerprint(source_path)

    cache_data = {
        "version": CACHE_VERSION,
        "source_file": Path(source_path).name,
        "source_mtime": mtime,
        "source_size": size,
        "analyzed_at": datetime.now(timezone.utc).isoformat(),
        "analysis": analysis,
        "feature_scalars": feature_scalars,
        "style_tags": style_tags or [],
        "report": report or "",
    }

    cp = _cache_path(source_path)
    cp.write_text(json.dumps(cache_data, indent=2, ensure_ascii=False), encoding="utf-8")
    logger.info(f"Wrote visual analysis cache: {cp}")
    return cp
