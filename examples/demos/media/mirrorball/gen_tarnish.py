"""Generate a subtle TARNISH set for the mirror-ball example — faint smudges + fine scratches so a
near-mirror chrome ball reads as a real, ROTATING physical object (a perfect featureless mirror shows
no rotation) and the slight roughness hides the single-mip prefilter rings.

Run:  uv run --with numpy --with pillow examples/demos/media/mirrorball/gen_tarnish.py
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
N = 512
u = (np.arange(N) + 0.5) / N
X, Y = np.meshgrid(u, u)
TAU = 2 * np.pi


def fbm(seed_phases):   # tileable band-limited noise from summed integer-frequency sinusoids
    acc = np.zeros_like(X)
    amp = 1.0
    for (fx, fy, ph) in seed_phases:
        acc += amp * np.sin(TAU * (fx * X + fy * Y) + ph)
        amp *= 0.6
    return (acc - acc.min()) / (acc.max() - acc.min())

# --- roughness: mostly mirror-glossy (0.06) with faint larger tarnish smudges up to ~0.20 ---
smudge = fbm([(2, 3, 0.4), (3, 2, 1.9), (5, 4, 3.1), (7, 5, 0.7)])
rough = 0.06 + 0.16 * (smudge ** 2)
Image.fromarray((np.clip(rough, 0, 1) * 255).astype(np.uint8)).save(os.path.join(HERE, "tarnish_roughness.png"))

# --- normal: very fine scratches/dust (high-freq, low amplitude) so the surface has micro-character ---
h = 0.5 * fbm([(17, 13, 0.2), (23, 19, 1.3), (31, 29, 2.6)]) + 0.5 * smudge
strength = 0.9
dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
nz = np.ones_like(h)
nl = np.sqrt(dx * dx + dy * dy + nz * nz)
normal = np.stack([(-dx / nl) * 0.5 + 0.5, (-dy / nl) * 0.5 + 0.5, (nz / nl) * 0.5 + 0.5], axis=-1)
Image.fromarray((normal * 255).astype(np.uint8)).save(os.path.join(HERE, "tarnish_normal.png"))

print("wrote tarnish_roughness.png + tarnish_normal.png ->", HERE)
