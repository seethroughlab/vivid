# vivid-effects-2d

2D texture operators: generators, effects, and compositing.

## Installation

This addon is included with Vivid by default. No additional installation required.

## Operators

### Generators
| Operator | Description |
|----------|-------------|
| `Noise` | Fractal noise (Perlin/Simplex) with octaves |
| `SolidColor` | Solid color fill |
| `Gradient` | Linear/radial gradients |
| `Shape` | Primitive shapes (circle, rectangle, polygon) |
| `Ramp` | Color ramp/lookup table |

### Effects
| Operator | Description |
|----------|-------------|
| `Blur` | Gaussian blur |
| `Bloom` | Glow/bloom effect |
| `Edge` | Edge detection |
| `Pixelate` | Pixelation effect |
| `Quantize` | Color quantization |
| `Dither` | Dithering patterns |
| `Scanlines` | CRT scanline effect |
| `CRTEffect` | Full CRT simulation |
| `Vignette` | Corner darkening |

### Transforms
| Operator | Description |
|----------|-------------|
| `Transform` | Scale, rotate, translate |
| `Mirror` | Kaleidoscope/mirror effects |
| `Tile` | Texture tiling |
| `Displace` | UV displacement mapping |
| `BarrelDistortion` | Lens distortion |
| `ChromaticAberration` | Color fringing |

### Color
| Operator | Description |
|----------|-------------|
| `Brightness` | Brightness/contrast |
| `HSV` | Hue/saturation/value adjustment |

### Compositing
| Operator | Description |
|----------|-------------|
| `Composite` | Blend layers (Over, Add, Multiply, Screen, etc.) |
| `Output` | Final output to screen |
| `Feedback` | Frame feedback buffer |

### Advanced
| Operator | Description |
|----------|-------------|
| `Particles` | 2D particle system |
| `Plexus` | GPU particle network |
| `PointSprites` | Point sprite rendering |
| `Canvas` | Immediate-mode 2D drawing |
| `Image` | Load static images |

## Examples

| Example | Description |
|---------|-------------|
| [chain-basics](examples/chain-basics) | Core concepts: chaining, inputs, mouse control |
| [feedback](examples/feedback) | Feedback buffer for trails and echoes |
| [kaleidoscope](examples/kaleidoscope) | Mirror and transform effects |
| [retro-crt](examples/retro-crt) | CRT/retro pipeline |
| [particles](examples/particles) | 2D particle systems |
| [canvas-drawing](examples/canvas-drawing) | Immediate-mode drawing API |

## Quick Start

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Generate animated noise
    chain.add<Noise>("noise")
        .scale(4.0f)
        .speed(0.5f)
        .octaves(4);

    // Apply color and bloom
    chain.add<HSV>("color")
        .input("noise")
        .hue(0.6f);

    chain.add<Bloom>("bloom")
        .input("color")
        .intensity(0.5f);

    chain.add<Output>("out")
        .input("bloom");
}

void update(Context& ctx) {
    ctx.chain().process();
}

VIVID_CHAIN(setup, update)
```

## API Reference

See [LLM-REFERENCE.md](../../docs/LLM-REFERENCE.md) for complete operator documentation.

## Dependencies

- vivid-core
- vivid-io (for Image operator)

## License

MIT
