"""Visual analysis pipeline — orchestrate all analyzers and extract feature scalars."""

import logging
import time
from pathlib import Path
from typing import Any

from PIL import Image

logger = logging.getLogger(__name__)

# Supported image extensions
IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".tiff", ".tif", ".webp", ".gif"}


def _load_image(file_path: str) -> Image.Image:
    """Load and validate an image file."""
    path = Path(file_path)
    if not path.exists():
        raise FileNotFoundError(f"Image file not found: {file_path}")

    suffix = path.suffix.lower()
    if suffix not in IMAGE_EXTENSIONS:
        raise ValueError(f"Unsupported image format: {suffix}")

    img = Image.open(str(path))
    img = img.convert("RGB")
    return img


def run_analyzers(img: Image.Image) -> dict[str, Any]:
    """Run all 4 core analyzers (color, luminance, texture, composition).

    Returns dict with keys: color, luminance, texture, composition.
    """
    from .color import analyze_color
    from .composition import analyze_composition
    from .luminance import analyze_luminance
    from .texture import analyze_texture

    results: dict[str, Any] = {}
    errors: list[dict[str, str]] = []

    for name, analyzer in [
        ("color", analyze_color),
        ("luminance", analyze_luminance),
        ("texture", analyze_texture),
        ("composition", analyze_composition),
    ]:
        try:
            results[name] = analyzer(img)
        except Exception as e:
            logger.error(f"Visual analyzer '{name}' failed: {e}")
            errors.append({"section": name, "error": str(e)})
            results[name] = {"error": str(e)}

    if errors:
        results["_errors"] = errors

    return results


def run_style_tags(img: Image.Image) -> list[dict[str, Any]]:
    """Run SigLIP-based style tag analysis."""
    from .style_tags import compute_style_tags

    try:
        return compute_style_tags(img)
    except Exception as e:
        logger.warning(f"Style tag analysis failed: {e}")
        return []


def extract_feature_scalars(analysis: dict[str, Any]) -> dict[str, Any]:
    """Extract typed scalar values from visual analysis results.

    Returns a flat dict of key metrics suitable for quick reference and
    mapping to Vivid node parameters.
    """
    scalars: dict[str, Any] = {}

    # Color
    color = analysis.get("color", {})
    if isinstance(color, dict) and "error" not in color:
        scalars["warm_cool_ratio"] = color.get("warm_cool_ratio")
        scalars["saturation_profile"] = color.get("saturation_profile")
        scalars["color_harmony"] = color.get("harmony")
        scalars["global_contrast"] = color.get("global_contrast")

        hsv = color.get("hsv_distribution", {})
        scalars["hue_mean"] = hsv.get("hue_mean")
        scalars["saturation_mean"] = hsv.get("saturation_mean")
        scalars["value_mean"] = hsv.get("value_mean")

        palette = color.get("palette", [])
        if palette:
            scalars["dominant_color_hex"] = palette[0].get("hex")
            scalars["dominant_color_rgb"] = palette[0].get("rgb_float")

    # Luminance
    luminance = analysis.get("luminance", {})
    if isinstance(luminance, dict) and "error" not in luminance:
        scalars["key"] = luminance.get("key")
        scalars["mean_brightness"] = luminance.get("mean_brightness")
        scalars["dynamic_range"] = luminance.get("dynamic_range")
        scalars["histogram_shape"] = luminance.get("histogram_shape")

    # Texture
    texture = analysis.get("texture", {})
    if isinstance(texture, dict) and "error" not in texture:
        scalars["edge_density"] = texture.get("edge_density")
        scalars["detail_level"] = texture.get("detail_level")
        scalars["detail_score"] = texture.get("detail_score")
        scalars["smoothness"] = texture.get("smoothness")
        scalars["grain_estimate"] = texture.get("grain_estimate")

    # Composition
    composition = analysis.get("composition", {})
    if isinstance(composition, dict) and "error" not in composition:
        scalars["complexity"] = composition.get("complexity")
        scalars["aspect_ratio"] = composition.get("aspect_ratio")
        sym = composition.get("symmetry", {})
        scalars["symmetry_h"] = sym.get("horizontal")
        scalars["symmetry_v"] = sym.get("vertical")
        com = composition.get("center_of_mass", {})
        scalars["center_x"] = com.get("x")
        scalars["center_y"] = com.get("y")

    # Remove None values
    return {k: v for k, v in scalars.items() if v is not None}


def load_and_analyze(
    file_path: str,
    include_style_tags: bool = True,
) -> tuple[dict[str, Any], dict[str, Any], list[dict[str, Any]]]:
    """Load image and run full visual analysis pipeline.

    Returns (analysis_results, feature_scalars, style_tags).
    """
    start_time = time.time()

    img = _load_image(file_path)

    # Run core analyzers
    analysis = run_analyzers(img)

    # Add image metadata
    w, h = img.size
    analysis["metadata"] = {
        "width": w,
        "height": h,
        "aspect_ratio": round(w / h, 3) if h > 0 else 1.0,
        "file_name": Path(file_path).name,
    }

    # Extract scalars
    feature_scalars = extract_feature_scalars(analysis)

    # Style tags
    style_tags: list[dict[str, Any]] = []
    if include_style_tags:
        style_tags = run_style_tags(img)

    elapsed = time.time() - start_time
    logger.info(f"Visual analysis completed in {elapsed:.1f}s: {file_path}")

    return analysis, feature_scalars, style_tags
