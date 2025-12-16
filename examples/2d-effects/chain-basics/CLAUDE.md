# Chain Basics

Demonstrates the core operator chain concept: connecting generators, processors, and compositors to create layered effects.

## Vision

Show how multiple operators connect together: an image is distorted by animated noise displacement, then composited with a color ramp for tinting. Demonstrates the fundamental building blocks of any Vivid chain.

## Operators

| Operator | Role |
|----------|------|
| Image | Load source image |
| Noise | Generate displacement map |
| Displace | Warp image using noise |
| Ramp | HSV color gradient |
| Composite | Blend displaced image with ramp |

## Interaction

- **Mouse X** - Displacement strength (0.02 to 0.15)
- **Mouse Y** - Color saturation (0.3 to 1.0)
- **V** - Toggle vsync

## Key Concepts

- Generators (Noise, Ramp) have their own resolution
- Processors (Displace, Composite) inherit resolution from inputs
- Operators connect via input() setter methods
