"""Cross-image synthesis for folder analysis.

Aggregates analysis from multiple images to produce a unified summary
of a collection's visual character.
"""

import logging
from typing import Any

import numpy as np

logger = logging.getLogger(__name__)


def synthesize_collection(
    analyses: list[dict[str, Any]],
) -> dict[str, Any]:
    """Synthesize cross-image summary from multiple image analyses.

    Args:
        analyses: List of dicts, each containing:
            - analysis: full analysis results
            - feature_scalars: scalar features
            - style_tags: style tag results
            - file_path: source file path

    Returns:
        Dict with aggregate_palette, common_tags, tendencies, summary.
    """
    if not analyses:
        return {"error": "No analyses provided"}

    # Collect all dominant palette colors for re-clustering
    all_colors = []
    all_scalars = []
    all_tags: dict[str, list[float]] = {}
    all_tag_categories: dict[str, str] = {}

    for entry in analyses:
        analysis = entry.get("analysis", {})
        scalars = entry.get("feature_scalars", {})
        tags = entry.get("style_tags", [])

        all_scalars.append(scalars)

        # Collect palette colors weighted by percentage
        palette = analysis.get("color", {}).get("palette", [])
        for color in palette:
            rgb = color.get("rgb_float", [0, 0, 0])
            pct = color.get("percentage", 0)
            all_colors.append((rgb, pct))

        # Accumulate tag confidences
        for tag in tags:
            name = tag["tag"]
            if name not in all_tags:
                all_tags[name] = []
                all_tag_categories[name] = tag["category"]
            all_tags[name].append(tag["confidence"])

    # Aggregate palette: KMeans k=8 on union of all dominant colors
    aggregate_palette = _aggregate_palette(all_colors)

    # Common style tags: averaged confidence, re-ranked
    common_tags = _aggregate_tags(all_tags, all_tag_categories, len(analyses))

    # Tendencies: averages + consensus classifications
    tendencies = _compute_tendencies(all_scalars)

    # Natural language summary
    summary = _generate_collection_summary(
        aggregate_palette, common_tags, tendencies, len(analyses)
    )

    return {
        "image_count": len(analyses),
        "aggregate_palette": aggregate_palette,
        "common_tags": common_tags,
        "tendencies": tendencies,
        "summary": summary,
    }


def _aggregate_palette(
    all_colors: list[tuple[list[float], float]],
    n_colors: int = 8,
) -> list[dict[str, Any]]:
    """Re-cluster all dominant colors into an aggregate palette."""
    if not all_colors:
        return []

    from sklearn.cluster import KMeans

    # Build weighted color array
    rgb_array = np.array([c[0] for c in all_colors], dtype=np.float64)
    weights = np.array([c[1] for c in all_colors], dtype=np.float64)

    # Duplicate colors by weight (approximate weighting for KMeans)
    # Normalize weights to sum to ~1000 samples
    if weights.sum() > 0:
        sample_counts = np.round(weights / weights.sum() * 1000).astype(int)
        sample_counts = np.maximum(sample_counts, 1)
    else:
        sample_counts = np.ones(len(rgb_array), dtype=int)

    expanded = np.repeat(rgb_array, sample_counts, axis=0)

    k = min(n_colors, len(expanded))
    if k < 1:
        return []

    kmeans = KMeans(n_clusters=k, random_state=42, n_init=3, max_iter=100)
    labels = kmeans.fit_predict(expanded)
    centers = kmeans.cluster_centers_

    # Compute percentages
    counts = np.bincount(labels, minlength=k)
    percentages = counts / counts.sum()

    order = np.argsort(-percentages)
    palette = []
    for i in order:
        r, g, b = centers[i]
        r_i, g_i, b_i = int(round(r * 255)), int(round(g * 255)), int(round(b * 255))
        r_i, g_i, b_i = max(0, min(255, r_i)), max(0, min(255, g_i)), max(0, min(255, b_i))
        palette.append({
            "hex": f"#{r_i:02x}{g_i:02x}{b_i:02x}",
            "rgb_float": [round(r, 3), round(g, 3), round(b, 3)],
            "percentage": round(float(percentages[i]) * 100, 1),
        })

    return palette


def _aggregate_tags(
    all_tags: dict[str, list[float]],
    categories: dict[str, str],
    n_images: int,
) -> list[dict[str, Any]]:
    """Aggregate style tags across images."""
    results = []
    for tag, confidences in all_tags.items():
        avg_conf = sum(confidences) / n_images  # average across ALL images (0 for missing)
        presence = len(confidences) / n_images
        results.append({
            "tag": tag,
            "category": categories[tag],
            "avg_confidence": round(avg_conf, 3),
            "presence": round(presence, 3),
        })

    # Sort by avg_confidence descending
    results.sort(key=lambda x: -x["avg_confidence"])
    return results[:12]


def _compute_tendencies(scalars_list: list[dict[str, Any]]) -> dict[str, Any]:
    """Compute average tendencies from scalar features."""
    if not scalars_list:
        return {}

    # Numeric fields to average
    numeric_keys = [
        "warm_cool_ratio", "global_contrast", "saturation_mean", "value_mean",
        "mean_brightness", "dynamic_range", "edge_density", "detail_score",
        "smoothness", "complexity", "symmetry_h", "symmetry_v",
    ]

    tendencies: dict[str, Any] = {}
    for key in numeric_keys:
        values = [s[key] for s in scalars_list if key in s and isinstance(s[key], (int, float))]
        if values:
            tendencies[key] = round(sum(values) / len(values), 3)

    # Consensus classifications (mode)
    categorical_keys = [
        "key", "saturation_profile", "color_harmony", "detail_level",
        "grain_estimate", "histogram_shape",
    ]
    for key in categorical_keys:
        values = [s[key] for s in scalars_list if key in s and s[key] is not None]
        if values:
            # Most common value
            from collections import Counter
            counter = Counter(values)
            tendencies[key] = counter.most_common(1)[0][0]

    return tendencies


def _generate_collection_summary(
    palette: list[dict[str, Any]],
    tags: list[dict[str, Any]],
    tendencies: dict[str, Any],
    n_images: int,
) -> str:
    """Generate natural language summary of a collection's visual character."""
    parts = []

    parts.append(f"Collection of {n_images} reference images")

    # Key tendency
    key = tendencies.get("key")
    if key:
        parts[0] += f" with predominantly {key} exposure"

    # Color character
    harmony = tendencies.get("color_harmony")
    sat = tendencies.get("saturation_profile")
    warm = tendencies.get("warm_cool_ratio")
    if harmony and sat:
        temp = "warm" if warm and warm > 0.6 else "cool" if warm and warm < 0.4 else "neutral"
        parts.append(f"The palette leans {sat} and {harmony}, with a {temp} temperature bias")

    # Texture
    detail = tendencies.get("detail_level")
    if detail:
        parts.append(f"Detail level is consistently {detail}")

    # Common tags
    if tags:
        top = [t["tag"] for t in tags[:5] if t["presence"] > 0.4]
        if top:
            parts.append(f"Common themes: {', '.join(top)}")

    # Dominant colors
    if palette and len(palette) >= 2:
        hex_list = [c["hex"] for c in palette[:4]]
        parts.append(f"Aggregate dominant colors: {', '.join(hex_list)}")

    result = ". ".join(parts)
    if not result.endswith("."):
        result += "."
    return result
