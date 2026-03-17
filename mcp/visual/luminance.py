"""Brightness, dynamic range, and key classification.

Analyzes the luminance channel to classify image key (low/balanced/high),
compute shadow/midtone/highlight distribution, and characterize histogram shape.
"""

import logging
from typing import Any

import numpy as np
from PIL import Image

logger = logging.getLogger(__name__)


def _histogram_shape(values: np.ndarray) -> str:
    """Classify the shape of a luminance histogram."""
    from scipy import stats

    if len(values) < 100:
        return "insufficient data"

    skew = stats.skew(values)
    kurt = stats.kurtosis(values)

    # Check for bimodal: two peaks in histogram
    hist, _ = np.histogram(values, bins=32)
    hist_smooth = np.convolve(hist, np.ones(3) / 3, mode="same")
    peaks = []
    for i in range(1, len(hist_smooth) - 1):
        if hist_smooth[i] > hist_smooth[i - 1] and hist_smooth[i] > hist_smooth[i + 1]:
            if hist_smooth[i] > np.max(hist_smooth) * 0.15:
                peaks.append(i)

    if len(peaks) >= 2:
        return "bimodal"
    elif skew < -0.5:
        return "right-skewed"  # bright-heavy
    elif skew > 0.5:
        return "left-skewed"  # dark-heavy
    elif abs(kurt) < 1.0:
        return "uniform"
    else:
        return "normal"


def analyze_luminance(img: Image.Image) -> dict[str, Any]:
    """Analyze luminance characteristics of an image.

    Args:
        img: PIL Image (RGB)

    Returns:
        Dict with key, brightness, dynamic range, zone distribution, histogram shape.
    """
    # Convert to grayscale (luminance)
    gray = np.array(img.convert("L"), dtype=np.float64) / 255.0

    mean_brightness = float(np.mean(gray))

    # Key classification
    if mean_brightness < 0.35:
        key = "low-key"
    elif mean_brightness > 0.65:
        key = "high-key"
    else:
        key = "balanced"

    # Dynamic range: P95 - P5 of value
    p5 = float(np.percentile(gray, 5))
    p95 = float(np.percentile(gray, 95))
    dynamic_range = p95 - p5

    # Shadow / midtone / highlight percentages
    shadow_pct = float(np.mean(gray < 0.33)) * 100
    midtone_pct = float(np.mean((gray >= 0.33) & (gray < 0.67))) * 100
    highlight_pct = float(np.mean(gray >= 0.67)) * 100

    # Histogram shape
    shape = _histogram_shape(gray.flatten())

    return {
        "key": key,
        "mean_brightness": round(mean_brightness, 3),
        "dynamic_range": round(dynamic_range, 3),
        "shadow_pct": round(shadow_pct, 1),
        "midtone_pct": round(midtone_pct, 1),
        "highlight_pct": round(highlight_pct, 1),
        "histogram_shape": shape,
    }
