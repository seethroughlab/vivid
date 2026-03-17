"""Edge density, detail level, smoothness, and grain estimation.

Uses Canny edge detection and Laplacian variance to quantify
image texture characteristics for mapping to Vivid parameters.
"""

import logging
from typing import Any

import cv2
import numpy as np
from PIL import Image

logger = logging.getLogger(__name__)


def analyze_texture(img: Image.Image) -> dict[str, Any]:
    """Analyze texture characteristics of an image.

    Args:
        img: PIL Image (RGB)

    Returns:
        Dict with edge_density, detail_level, smoothness, grain_estimate.
    """
    # Convert to grayscale uint8 for OpenCV
    gray = np.array(img.convert("L"))

    # Resize for consistent analysis if very large
    max_dim = 1024
    h, w = gray.shape
    if max(h, w) > max_dim:
        scale = max_dim / max(h, w)
        gray = cv2.resize(gray, (int(w * scale), int(h * scale)), interpolation=cv2.INTER_AREA)

    # Edge density: Canny edge pixel percentage
    edges = cv2.Canny(gray, 50, 150)
    edge_density = float(np.mean(edges > 0))

    # Detail level: Laplacian variance (higher = more detail)
    laplacian = cv2.Laplacian(gray, cv2.CV_64F)
    laplacian_var = float(np.var(laplacian))

    # Normalize laplacian variance to a 0-1 scale (empirical mapping)
    # Typical range: 0 (flat) to ~5000+ (very detailed)
    detail_normalized = min(laplacian_var / 3000.0, 1.0)

    # Detail classification
    if detail_normalized < 0.15:
        detail_class = "low"
    elif detail_normalized < 0.4:
        detail_class = "moderate"
    elif detail_normalized < 0.7:
        detail_class = "high"
    else:
        detail_class = "very high"

    # Smoothness: inverse of edge density
    smoothness = 1.0 - edge_density

    # Grain estimate: high-frequency energy ratio
    # Apply Gaussian blur and compare to original
    blurred = cv2.GaussianBlur(gray.astype(np.float64), (5, 5), 1.5)
    high_freq = gray.astype(np.float64) - blurred
    hf_energy = float(np.mean(high_freq**2))
    total_energy = float(np.mean(gray.astype(np.float64) ** 2))

    grain_ratio = hf_energy / total_energy if total_energy > 1e-8 else 0.0

    # Grain classification
    if grain_ratio < 0.005:
        grain_class = "none"
    elif grain_ratio < 0.02:
        grain_class = "low"
    elif grain_ratio < 0.06:
        grain_class = "moderate"
    else:
        grain_class = "high"

    return {
        "edge_density": round(edge_density, 4),
        "detail_level": detail_class,
        "detail_score": round(detail_normalized, 3),
        "laplacian_variance": round(laplacian_var, 1),
        "smoothness": round(smoothness, 4),
        "grain_ratio": round(grain_ratio, 4),
        "grain_estimate": grain_class,
    }
