"""Weight distribution, symmetry, and complexity analysis.

Analyzes spatial composition by computing luminance-weighted quadrant
distribution, horizontal/vertical symmetry, complexity, and visual
center of mass.
"""

import logging
from typing import Any

import cv2
import numpy as np
from PIL import Image

logger = logging.getLogger(__name__)


def analyze_composition(img: Image.Image) -> dict[str, Any]:
    """Analyze spatial composition of an image.

    Args:
        img: PIL Image (RGB)

    Returns:
        Dict with weight_quadrants, symmetry, complexity, center_of_mass, aspect_ratio.
    """
    gray = np.array(img.convert("L"), dtype=np.float64) / 255.0
    h, w = gray.shape

    # Resize for consistent analysis
    max_dim = 512
    if max(h, w) > max_dim:
        scale = max_dim / max(h, w)
        gray = cv2.resize(gray, (int(w * scale), int(h * scale)), interpolation=cv2.INTER_AREA)
        h, w = gray.shape

    # Edge map for weighting
    gray_u8 = (gray * 255).astype(np.uint8)
    edges = cv2.Canny(gray_u8, 50, 150).astype(np.float64) / 255.0

    # Combined weight: luminance * (1 + edges) to emphasize detailed bright areas
    weight = gray * (1.0 + edges)

    # Weight quadrants
    mid_h, mid_w = h // 2, w // 2
    tl = float(np.sum(weight[:mid_h, :mid_w]))
    tr = float(np.sum(weight[:mid_h, mid_w:]))
    bl = float(np.sum(weight[mid_h:, :mid_w]))
    br = float(np.sum(weight[mid_h:, mid_w:]))
    total = tl + tr + bl + br

    if total > 1e-8:
        tl_pct = round(tl / total * 100, 1)
        tr_pct = round(tr / total * 100, 1)
        bl_pct = round(bl / total * 100, 1)
        br_pct = round(br / total * 100, 1)
    else:
        tl_pct = tr_pct = bl_pct = br_pct = 25.0

    # Horizontal symmetry: compare left half to flipped right half
    left = gray[:, :mid_w]
    right = gray[:, -mid_w:]  # handle odd widths
    right_flipped = np.fliplr(right)
    min_w_sym = min(left.shape[1], right_flipped.shape[1])
    h_diff = np.mean(np.abs(left[:, :min_w_sym] - right_flipped[:, :min_w_sym]))
    h_symmetry = max(0.0, 1.0 - h_diff * 2.5)  # scale so 0.4 diff -> ~0 symmetry

    # Vertical symmetry: compare top half to flipped bottom half
    top = gray[:mid_h, :]
    bottom = gray[-mid_h:, :]
    bottom_flipped = np.flipud(bottom)
    min_h_sym = min(top.shape[0], bottom_flipped.shape[0])
    v_diff = np.mean(np.abs(top[:min_h_sym, :] - bottom_flipped[:min_h_sym, :]))
    v_symmetry = max(0.0, 1.0 - v_diff * 2.5)

    # Complexity: combination of edge density and color variance
    edge_density = float(np.mean(edges > 0))
    local_var = cv2.blur(gray_u8, (16, 16)).astype(np.float64) / 255.0
    var_of_local = float(np.std(local_var))
    complexity = min(1.0, edge_density * 2 + var_of_local)

    # Visual center of mass
    y_coords, x_coords = np.mgrid[0:h, 0:w]
    weight_sum = np.sum(weight)
    if weight_sum > 1e-8:
        cx = float(np.sum(x_coords * weight) / weight_sum) / w
        cy = float(np.sum(y_coords * weight) / weight_sum) / h
    else:
        cx, cy = 0.5, 0.5

    # Aspect ratio
    orig_w, orig_h = img.size
    aspect = orig_w / orig_h if orig_h > 0 else 1.0

    # Aspect ratio label
    if abs(aspect - 1.0) < 0.05:
        aspect_label = "1:1"
    elif abs(aspect - 16 / 9) < 0.1:
        aspect_label = "16:9"
    elif abs(aspect - 4 / 3) < 0.1:
        aspect_label = "4:3"
    elif abs(aspect - 21 / 9) < 0.15:
        aspect_label = "21:9"
    elif abs(aspect - 9 / 16) < 0.1:
        aspect_label = "9:16"
    elif abs(aspect - 3 / 4) < 0.1:
        aspect_label = "3:4"
    else:
        aspect_label = f"{aspect:.2f}:1"

    return {
        "weight_quadrants": {
            "top_left": tl_pct,
            "top_right": tr_pct,
            "bottom_left": bl_pct,
            "bottom_right": br_pct,
        },
        "symmetry": {
            "horizontal": round(float(h_symmetry), 3),
            "vertical": round(float(v_symmetry), 3),
        },
        "complexity": round(float(complexity), 3),
        "center_of_mass": {
            "x": round(cx, 3),
            "y": round(cy, 3),
        },
        "aspect_ratio": round(aspect, 3),
        "aspect_label": aspect_label,
        "resolution": {"width": orig_w, "height": orig_h},
    }
