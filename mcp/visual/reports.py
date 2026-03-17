"""Markdown report generation for visual analysis.

Produces LLM-optimized reports with Vivid parameter suggestions.
"""

from pathlib import Path
from typing import Any


def _generate_visual_summary(
    color: dict[str, Any],
    luminance: dict[str, Any],
    texture: dict[str, Any],
    composition: dict[str, Any],
    style_tags: list[dict[str, Any]],
) -> str:
    """Synthesize a 2-3 sentence natural language summary from visual analysis data."""
    parts = []

    # Sentence 1: Key + palette character
    key = luminance.get("key", "balanced")
    harmony = color.get("harmony", "")
    sat_profile = color.get("saturation_profile", "")
    warm_ratio = color.get("warm_cool_ratio", 0.5)

    temp = "warm" if warm_ratio > 0.6 else "cool" if warm_ratio < 0.4 else "neutral-temperature"

    s1_parts = [f"A {key} image"]
    if harmony and sat_profile:
        s1_parts.append(f"with a {sat_profile} {harmony} palette centered on {temp} tones")
    parts.append(" ".join(s1_parts))

    # Sentence 2: Texture + composition
    detail = texture.get("detail_level", "")
    grain = texture.get("grain_estimate", "")
    complexity = composition.get("complexity", 0)

    s2_parts = []
    if detail:
        s2_parts.append(f"{detail} detail")
    if grain and grain != "none":
        s2_parts.append(f"{grain} grain")
    if complexity > 0.6:
        s2_parts.append("high visual complexity")
    elif complexity < 0.3:
        s2_parts.append("simple composition")

    if s2_parts:
        parts.append("Features " + ", ".join(s2_parts))

    # Sentence 3: Style tags summary
    if style_tags:
        top_tags = [t["tag"] for t in style_tags[:4]]
        parts.append("Reads as " + ", ".join(top_tags))

    if not parts:
        return ""

    result = ". ".join(parts)
    if not result.endswith("."):
        result += "."
    return result


def _suggest_vivid_params(
    color: dict[str, Any],
    luminance: dict[str, Any],
    texture: dict[str, Any],
    composition: dict[str, Any],
) -> list[str]:
    """Generate Vivid parameter suggestions based on analysis data."""
    suggestions = []

    # Primary/secondary color from palette
    palette = color.get("palette", [])
    if len(palette) >= 2:
        p0 = palette[0]
        p1 = palette[1]
        r0 = p0.get("rgb_float", [0, 0, 0])
        r1 = p1.get("rgb_float", [0, 0, 0])
        suggestions.append(
            f"Color: rgb({r0[0]}, {r0[1]}, {r0[2]}) primary, "
            f"rgb({r1[0]}, {r1[1]}, {r1[2]}) secondary"
        )

    # Bloom based on key
    key = luminance.get("key", "balanced")
    brightness = luminance.get("mean_brightness", 0.5)
    if key == "low-key":
        suggestions.append(f"Bloom: threshold 0.7+ (dark image, bloom should be selective)")
    elif key == "high-key":
        suggestions.append(f"Bloom: threshold 0.3-0.5 (bright image, widespread bloom)")
    else:
        suggestions.append(f"Bloom: threshold 0.5 (balanced)")

    # Noise octaves from texture detail
    edge_density = texture.get("edge_density", 0.1)
    detail_score = texture.get("detail_score", 0.3)
    if detail_score < 0.2:
        suggestions.append("Noise: octaves 1-2, lacunarity 1.5 (low detail, smooth)")
    elif detail_score < 0.5:
        suggestions.append("Noise: octaves 3-4, lacunarity 2.0 (moderate detail)")
    else:
        suggestions.append("Noise: octaves 5-6, lacunarity 2.5+ (high detail)")

    # Feedback/persistence from brightness
    if brightness < 0.35:
        suggestions.append("Feedback: decay 0.85 (dark imagery suits slow persistence)")
    elif brightness > 0.65:
        suggestions.append("Feedback: decay 0.6 (bright imagery suits faster refresh)")
    else:
        suggestions.append("Feedback: decay 0.7 (balanced persistence)")

    # Blur from smoothness
    smoothness = texture.get("smoothness", 0.5)
    if smoothness > 0.85:
        suggestions.append(f"Blur: radius 3-5px (very smooth image)")
    elif smoothness > 0.7:
        suggestions.append(f"Blur: radius 1-2px (moderately smooth)")
    else:
        suggestions.append(f"Blur: radius 0 (detailed/sharp image)")

    return suggestions


def generate_report(
    analysis: dict[str, Any],
    style_tags: list[dict[str, Any]],
    file_path: str,
) -> str:
    """Generate a visual analysis markdown report.

    Args:
        analysis: Full analysis results from pipeline
        style_tags: Style tag results
        file_path: Path to the analyzed image
    """
    lines = []
    filename = Path(file_path).name

    color = analysis.get("color", {})
    luminance = analysis.get("luminance", {})
    texture = analysis.get("texture", {})
    composition = analysis.get("composition", {})
    meta = analysis.get("metadata", {})

    w = meta.get("width", "?")
    h = meta.get("height", "?")
    aspect_label = composition.get("aspect_label", "?")

    lines.append(f"# Visual Analysis: {filename}")
    lines.append(f"**Resolution:** {w}x{h} | **Aspect:** {aspect_label}")
    lines.append("")
    lines.append("---")
    lines.append("")

    # Character summary
    summary = _generate_visual_summary(color, luminance, texture, composition, style_tags)
    if summary:
        lines.append(f"> {summary}")
        lines.append("")

    # Color Palette
    lines.append("## Color Palette")
    palette = color.get("palette", [])
    if palette:
        hex_parts = [f"{c['hex']} ({c['percentage']}%)" for c in palette[:6]]
        lines.append(f"- **Dominant:** {', '.join(hex_parts)}")
        rgb_parts = [f"({c['rgb_float'][0]}, {c['rgb_float'][1]}, {c['rgb_float'][2]})" for c in palette[:6]]
        lines.append(f"  - RGB floats: {', '.join(rgb_parts)}")

    harmony = color.get("harmony", "?")
    sat_profile = color.get("saturation_profile", "?")
    sat_mean = color.get("hsv_distribution", {}).get("saturation_mean", "?")
    lines.append(f"- **Harmony:** {harmony} | **Saturation:** {sat_profile} ({sat_mean})")

    warm_ratio = color.get("warm_cool_ratio", 0.5)
    warm_pct = round(warm_ratio * 100)
    cool_pct = 100 - warm_pct
    contrast = color.get("global_contrast", "?")
    lines.append(f"- **Warm/cool:** {warm_pct}% / {cool_pct}% | **Contrast:** {contrast}")
    lines.append("")

    # Luminance
    lines.append("## Luminance")
    lum_key = luminance.get("key", "?")
    brightness = luminance.get("mean_brightness", "?")
    dyn_range = luminance.get("dynamic_range", "?")
    lines.append(f"- **Key:** {lum_key} ({brightness}) | **Range:** {dyn_range}")

    shadow = luminance.get("shadow_pct", "?")
    midtone = luminance.get("midtone_pct", "?")
    highlight = luminance.get("highlight_pct", "?")
    lines.append(f"- Shadow {shadow}% / Midtone {midtone}% / Highlight {highlight}%")

    hist_shape = luminance.get("histogram_shape", "?")
    lines.append(f"- **Histogram:** {hist_shape}")
    lines.append("")

    # Texture
    lines.append("## Texture")
    detail = texture.get("detail_level", "?")
    edges = texture.get("edge_density", "?")
    smoothness = texture.get("smoothness", "?")
    grain = texture.get("grain_estimate", "?")
    lines.append(f"- **Detail:** {detail} | **Edges:** {_pct(edges)} | **Smoothness:** {smoothness} | **Grain:** {grain}")
    lines.append("")

    # Composition
    lines.append("## Composition")
    wq = composition.get("weight_quadrants", {})
    lines.append(
        f"- **Weight:** TL {wq.get('top_left', '?')}% / TR {wq.get('top_right', '?')}% / "
        f"BL {wq.get('bottom_left', '?')}% / BR {wq.get('bottom_right', '?')}%"
    )
    sym = composition.get("symmetry", {})
    lines.append(
        f"- **Symmetry:** H {sym.get('horizontal', '?')} / V {sym.get('vertical', '?')} | "
        f"**Complexity:** {composition.get('complexity', '?')}"
    )
    com = composition.get("center_of_mass", {})
    lines.append(f"- **Visual center:** ({com.get('x', '?')}, {com.get('y', '?')})")
    lines.append("")

    # Style Tags
    if style_tags:
        lines.append("## Style Tags")
        tag_parts = [f"{t['tag']} ({t['confidence']})" for t in style_tags]
        lines.append(", ".join(tag_parts))
        lines.append("")

    # Vivid Parameter Suggestions
    lines.append("## Vivid Parameter Suggestions")
    suggestions = _suggest_vivid_params(color, luminance, texture, composition)
    for s in suggestions:
        lines.append(f"- {s}")
    lines.append("")

    return "\n".join(lines)


def generate_summary_report(
    analysis: dict[str, Any],
    style_tags: list[dict[str, Any]],
    feature_scalars: dict[str, Any],
    file_path: str,
) -> str:
    """Generate a compact summary for folder analysis (lighter on tokens).

    Returns a shorter report with just key metrics and tags.
    """
    lines = []
    filename = Path(file_path).name

    color = analysis.get("color", {})
    luminance = analysis.get("luminance", {})
    texture = analysis.get("texture", {})
    composition = analysis.get("composition", {})

    summary = _generate_visual_summary(color, luminance, texture, composition, style_tags)

    lines.append(f"### {filename}")
    if summary:
        lines.append(f"> {summary}")

    # Key scalars
    parts = []
    if luminance.get("key"):
        parts.append(f"Key: {luminance['key']}")
    if color.get("harmony"):
        parts.append(f"Harmony: {color['harmony']}")
    if color.get("saturation_profile"):
        parts.append(f"Sat: {color['saturation_profile']}")
    if texture.get("detail_level"):
        parts.append(f"Detail: {texture['detail_level']}")

    if parts:
        lines.append(f"- {' | '.join(parts)}")

    # Top 4 style tags
    if style_tags:
        tag_str = ", ".join(f"{t['tag']} ({t['confidence']})" for t in style_tags[:4])
        lines.append(f"- Tags: {tag_str}")

    # Dominant color
    palette = color.get("palette", [])
    if palette:
        lines.append(f"- Dominant: {palette[0]['hex']} ({palette[0]['percentage']}%)")

    lines.append("")
    return "\n".join(lines)


def _pct(value) -> str:
    """Format a float as percentage string."""
    if isinstance(value, (int, float)):
        return f"{value * 100:.0f}%"
    return str(value)
