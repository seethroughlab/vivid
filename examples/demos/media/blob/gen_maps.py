"""Generate the hero blob's PBR maps — a LIQUID / MOLTEN look (organic flow, glossy sheen, no grid).

Domain-warped integer-frequency sinusoids give a seamless, tileable height field that FLOWS like the
surface of mercury / oil (the domain warp breaks the regular egg-carton grid of the old test maps). The
four maps derive from that one height field:
  • normal    — gradient of the flow field → rippling liquid surface (subtle in the demo via normal_strength)
  • roughness — glossy (low) in the flow valleys, a touch rougher on the ridges → a moving wet sheen
  • albedo    — near-neutral light, so the coloured Light3D rig still paints the colour (not the texture)
  • metallic  — moderate, so the magenta/cyan lights throw sharp oily speculars (no IBL, so not too high)

Run:  uv run --with numpy --with pillow examples/demos/projects/blob/textures/gen_maps.py
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
N = 512
u = (np.arange(N) + 0.5) / N
X, Y = np.meshgrid(u, u)
TAU = 2 * np.pi


def wave(fx, fy, ph):   # a periodic (integer-frequency → seamless) plane wave
    return np.sin(TAU * (fx * X + fy * Y) + ph)


# --- domain warp: push the sample coords around with low-frequency (still integer) noise, so the
#     flow field bends and swirls instead of gridding up ---
wx = 0.14 * (wave(1, 2, 0.0) + 0.5 * wave(2, 1, 1.3))
wy = 0.14 * (wave(2, 1, 2.1) + 0.5 * wave(1, 3, 0.7))
Xw, Yw = X + wx, Y + wy


def swave(fx, fy, ph):   # a warped plane wave (organic once summed)
    return np.sin(TAU * (fx * Xw + fy * Yw) + ph)


# --- organic FLOW height: several warped octaves at irregular directions/frequencies → liquid ripples ---
h = (1.00 * swave(2, 1, 0.0) + 0.70 * swave(1, 3, 1.7) + 0.55 * swave(3, 2, 3.1)
     + 0.38 * swave(4, 1, 0.6) + 0.27 * swave(2, 4, 2.4) + 0.18 * swave(5, 3, 1.1))
h = (h - h.min()) / (h.max() - h.min())

# --- normal from the gradient (tileable via np.roll) ---
strength = 2.2
dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
nz = np.ones_like(h)
nl = np.sqrt(dx * dx + dy * dy + nz * nz)
normal = np.stack([(-dx / nl) * 0.5 + 0.5, (-dy / nl) * 0.5 + 0.5, (nz / nl) * 0.5 + 0.5], axis=-1)
Image.fromarray((normal * 255).astype(np.uint8)).save(os.path.join(HERE, "normal.png"))

# --- roughness: WIDE range so the wet sheen is legible — mirror-glossy in the flow valleys (0.10) to
#     soft-satin on the ridge tops (~0.62). The blob sets base roughness = 1.0, so this map IS m_roughness;
#     low values = tight bright highlights that slide across the surface as the camera orbits. ---
rough = 0.10 + 0.52 * h
Image.fromarray((np.clip(rough, 0, 1) * 255).astype(np.uint8)).save(os.path.join(HERE, "roughness.png"))

# --- albedo: near-neutral light with only a whisper of cool/warm by height, so the coloured lights read
#     as the colour (matches the blob's dark-albedo philosophy — the texture adds form, not hue) ---
alb = np.stack([0.80 + 0.06 * h, 0.81 + 0.05 * h, 0.85 + 0.05 * (1 - h)], axis=-1)
Image.fromarray((np.clip(alb, 0, 1) * 255).astype(np.uint8)).save(os.path.join(HERE, "albedo.png"))

# --- metallic: fairly high + higher in the valleys (pooled "liquid metal") so the coloured lights throw
#     oily speculars; the blob sets base metallic = 1.0, so this map IS m_metallic. Not a full 1.0 anywhere
#     (SDF3D lighting is direct-only, no reflections — pure chrome would read dark) ---
met = np.clip(0.45 + 0.30 * (1 - h), 0, 1)
Image.fromarray((met * 255).astype(np.uint8)).convert("L").save(os.path.join(HERE, "metallic.png"))

print("wrote liquid/molten maps ->", HERE, "(albedo/roughness/metallic/normal .png, 512² tileable)")
