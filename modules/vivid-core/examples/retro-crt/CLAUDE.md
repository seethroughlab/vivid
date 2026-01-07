# Retro CRT

A complete retro graphics pipeline: low resolution, dithering, scanlines, and CRT monitor simulation.

## Vision

Recreate the look of vintage computer graphics and CRT monitors. A pulsing star shape is processed through a chain of effects that simulate the limitations and characteristics of old display technology.

## Effect Pipeline

| Operator | Purpose |
|----------|---------|
| Shape | Animated star generator |
| Gradient | Radial purple background |
| Composite | Layer star over gradient |
| HSV | Hue cycling animation |
| Downsample | Reduce to 320x240 |
| Dither | Bayer 4x4 pattern, 16 color levels |
| Scanlines | Horizontal CRT lines |
| CRTEffect | Curvature, vignette, chromatic aberration |

## Interaction

- **Mouse X** - Screen curvature (0-0.3)
- **Mouse Y** - Chromatic aberration (0-0.05)

## Visual Style

- Low resolution pixelated graphics
- Ordered dithering for limited color palette feel
- Curved screen edges like a CRT tube
- Subtle color fringing at edges
