"""Color palette, harmony, HSV distribution, and warm/cool analysis.

Extracts dominant colors via KMeans on downsampled pixel data,
classifies color harmony, and computes HSV statistics.
"""

import logging
from typing import Any

import numpy as np
from PIL import Image

logger = logging.getLogger(__name__)

# Hue ranges for warm/cool classification (HSV hue 0-360)
_WARM_RANGES = [(0, 60), (300, 360)]  # reds, oranges, yellows, magentas
_COOL_RANGES = [(60, 300)]  # greens, cyans, blues, purples


def _rgb_to_hex(r: int, g: int, b: int) -> str:
    return f"#{r:02x}{g:02x}{b:02x}"


def _rgb_to_hsv(rgb: np.ndarray) -> np.ndarray:
    """Convert RGB (0-255) array to HSV (H: 0-360, S: 0-1, V: 0-1)."""
    rgb_f = rgb.astype(np.float64) / 255.0
    r, g, b = rgb_f[..., 0], rgb_f[..., 1], rgb_f[..., 2]

    v = np.maximum(np.maximum(r, g), b)
    c = v - np.minimum(np.minimum(r, g), b)

    s = np.where(v > 0, c / v, 0.0)

    h = np.zeros_like(v)
    mask_r = (c > 0) & (v == r)
    mask_g = (c > 0) & (v == g) & ~mask_r
    mask_b = (c > 0) & ~mask_r & ~mask_g

    h[mask_r] = 60.0 * (((g[mask_r] - b[mask_r]) / c[mask_r]) % 6)
    h[mask_g] = 60.0 * (((b[mask_g] - r[mask_g]) / c[mask_g]) + 2)
    h[mask_b] = 60.0 * (((r[mask_b] - g[mask_b]) / c[mask_b]) + 4)

    return np.stack([h, s, v], axis=-1)


def _circular_mean(angles_deg: np.ndarray, weights: np.ndarray | None = None) -> float:
    """Compute circular mean of angles in degrees."""
    rad = np.deg2rad(angles_deg)
    if weights is not None:
        sin_mean = np.average(np.sin(rad), weights=weights)
        cos_mean = np.average(np.cos(rad), weights=weights)
    else:
        sin_mean = np.mean(np.sin(rad))
        cos_mean = np.mean(np.cos(rad))
    return float(np.rad2deg(np.arctan2(sin_mean, cos_mean)) % 360)


def _circular_std(angles_deg: np.ndarray, weights: np.ndarray | None = None) -> float:
    """Compute circular standard deviation of angles in degrees."""
    rad = np.deg2rad(angles_deg)
    if weights is not None:
        sin_mean = np.average(np.sin(rad), weights=weights)
        cos_mean = np.average(np.cos(rad), weights=weights)
    else:
        sin_mean = np.mean(np.sin(rad))
        cos_mean = np.mean(np.cos(rad))
    r = np.sqrt(sin_mean**2 + cos_mean**2)
    r = min(r, 1.0)
    return float(np.rad2deg(np.sqrt(-2.0 * np.log(r)))) if r > 1e-8 else 180.0


def _classify_harmony(hues: np.ndarray) -> str:
    """Classify color harmony from dominant palette hues."""
    if len(hues) < 2:
        return "monochromatic"

    # Normalize hues to 0-360
    hues = hues % 360

    # Compute pairwise angular distances
    diffs = []
    for i in range(len(hues)):
        for j in range(i + 1, len(hues)):
            d = abs(hues[i] - hues[j])
            d = min(d, 360 - d)
            diffs.append(d)

    diffs = np.array(diffs)
    max_diff = np.max(diffs)
    mean_diff = np.mean(diffs)

    if max_diff < 30:
        return "monochromatic"
    elif max_diff < 60:
        return "analogous"
    elif len(hues) >= 3 and abs(mean_diff - 120) < 20:
        return "triadic"
    elif any(abs(d - 180) < 25 for d in diffs):
        if len(hues) >= 3 and any(abs(d - 150) < 20 for d in diffs):
            return "split-complementary"
        return "complementary"
    elif mean_diff < 90:
        return "analogous"
    else:
        return "complementary"


def _warm_cool_ratio(hsv: np.ndarray) -> float:
    """Compute warm ratio (0-1) from HSV array. 1.0 = all warm."""
    hues = hsv[..., 0]
    sats = hsv[..., 1]

    # Weight by saturation — desaturated pixels are neutral
    warm_mask = np.zeros_like(hues, dtype=bool)
    for lo, hi in _WARM_RANGES:
        warm_mask |= (hues >= lo) & (hues < hi)

    warm_weight = np.sum(sats[warm_mask])
    total_weight = np.sum(sats)

    if total_weight < 1e-8:
        return 0.5  # neutral for achromatic images
    return float(warm_weight / total_weight)


def analyze_color(img: Image.Image, n_colors: int = 8) -> dict[str, Any]:
    """Analyze color palette and distribution of an image.

    Args:
        img: PIL Image (RGB)
        n_colors: Number of dominant colors to extract

    Returns:
        Dict with palette, harmony, hsv_distribution, warm_cool_ratio, etc.
    """
    from sklearn.cluster import KMeans

    # Convert to RGB if needed
    img_rgb = img.convert("RGB")

    # Downsample to ~10k pixels for speed
    pixels = np.array(img_rgb).reshape(-1, 3)
    if len(pixels) > 10000:
        rng = np.random.default_rng(42)
        indices = rng.choice(len(pixels), 10000, replace=False)
        pixels = pixels[indices]

    # KMeans clustering for dominant colors
    kmeans = KMeans(n_clusters=n_colors, random_state=42, n_init=3, max_iter=100)
    labels = kmeans.fit_predict(pixels)
    centers = kmeans.cluster_centers_.astype(np.uint8)

    # Compute percentages
    counts = np.bincount(labels, minlength=n_colors)
    percentages = counts / counts.sum()

    # Sort by percentage descending
    order = np.argsort(-percentages)
    centers = centers[order]
    percentages = percentages[order]

    # Build palette
    palette = []
    for i in range(n_colors):
        r, g, b = int(centers[i][0]), int(centers[i][1]), int(centers[i][2])
        palette.append({
            "hex": _rgb_to_hex(r, g, b),
            "rgb_float": [round(r / 255.0, 3), round(g / 255.0, 3), round(b / 255.0, 3)],
            "percentage": round(float(percentages[i]) * 100, 1),
        })

    # HSV analysis on sampled pixels
    hsv = _rgb_to_hsv(pixels)

    # Filter out near-achromatic pixels for hue analysis
    chromatic_mask = hsv[..., 1] > 0.1
    chromatic_hues = hsv[chromatic_mask, 0] if np.any(chromatic_mask) else hsv[..., 0]

    # HSV distribution
    hue_mean = _circular_mean(chromatic_hues) if len(chromatic_hues) > 0 else 0.0
    hue_std = _circular_std(chromatic_hues) if len(chromatic_hues) > 0 else 0.0
    sat_mean = float(np.mean(hsv[..., 1]))
    sat_std = float(np.std(hsv[..., 1]))
    val_mean = float(np.mean(hsv[..., 2]))
    val_std = float(np.std(hsv[..., 2]))

    # Saturation profile
    if sat_mean < 0.2:
        sat_profile = "muted"
    elif sat_mean < 0.5:
        sat_profile = "moderate"
    else:
        sat_profile = "vibrant"

    # Color harmony from palette hues
    palette_hsv = _rgb_to_hsv(centers)
    # Use only chromatic palette entries for harmony
    chromatic_palette = palette_hsv[palette_hsv[:, 1] > 0.1]
    harmony = _classify_harmony(chromatic_palette[:, 0]) if len(chromatic_palette) >= 2 else "monochromatic"

    # Warm/cool ratio
    warm_ratio = _warm_cool_ratio(hsv)

    # Global contrast (std of value channel)
    global_contrast = float(np.std(hsv[..., 2]))

    return {
        "palette": palette,
        "harmony": harmony,
        "hsv_distribution": {
            "hue_mean": round(hue_mean, 1),
            "hue_std": round(hue_std, 1),
            "saturation_mean": round(sat_mean, 3),
            "saturation_std": round(sat_std, 3),
            "value_mean": round(val_mean, 3),
            "value_std": round(val_std, 3),
        },
        "warm_cool_ratio": round(warm_ratio, 3),
        "saturation_profile": sat_profile,
        "global_contrast": round(global_contrast, 3),
    }
