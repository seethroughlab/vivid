# Vivid LLM Reference

A compact reference for LLMs helping users build vivid projects.

## Project Structure

```
my-project/
  chain.cpp       # Your visual program (hot-reloaded)
  CLAUDE.md       # Optional: project-specific context for LLMs
```

## Basic Template

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1280, 720);

    // Add operators here
    chain->add<Noise>("noise").scale(4.0f);
    chain->add<Output>("out").input("noise");

    // Register for visualizer (Tab key)
    ctx.registerOperator("noise", &chain->get<Noise>("noise"));
    ctx.registerOperator("out", &chain->get<Output>("out"));
}

void update(Context& ctx) {
    chain->process();
}

VIVID_CHAIN(setup, update)
```

## Keyboard Controls

- `Tab` - Toggle chain visualizer
- `F` - Toggle fullscreen
- `Esc` - Quit

## Operators Quick Reference

### Generators (no input required)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Noise` | Fractal noise | `.scale(4.0)` `.speed(0.5)` `.type(NoiseType::Perlin)` `.octaves(4)` |
| `SolidColor` | Solid color fill | `.color(r, g, b)` |
| `Gradient` | Color gradients | `.mode(GradientMode::Linear)` `.colorA(r,g,b)` `.colorB(r,g,b)` `.angle(0)` |
| `Ramp` | Animated HSV gradient | `.hueSpeed(0.5)` `.saturation(1.0)` `.type(RampType::Linear)` |
| `Shape` | SDF shapes | `.type(ShapeType::Circle)` `.size(0.5)` `.position(0.5, 0.5)` `.color(r,g,b)` |
| `LFO` | Oscillator (value) | `.waveform(LFOWaveform::Sine)` `.frequency(1.0)` `.amplitude(1.0)` |
| `Image` | Load image file | `.path("assets/image.jpg")` |

### Effects (require `.input()`)

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Blur` | Gaussian blur | `.radius(5.0)` `.passes(1)` |
| `HSV` | Color adjustment | `.hueShift(0.0)` `.saturation(1.0)` `.value(1.0)` |
| `Brightness` | Brightness/contrast | `.brightness(1.0)` `.contrast(1.0)` `.gamma(1.0)` |
| `Transform` | Scale/rotate/translate | `.scale(1.0)` `.rotate(radians)` `.translate(x, y)` |
| `Mirror` | Axis mirroring | `.horizontal(true)` `.vertical(false)` `.kaleidoscope(4)` |
| `Displace` | Texture distortion | `.source(op)` `.map(op)` `.strength(0.1)` |
| `Edge` | Edge detection | `.strength(1.0)` |
| `Pixelate` | Mosaic effect | `.size(10)` |
| `Tile` | Texture tiling | `.tilesX(4)` `.tilesY(4)` |
| `ChromaticAberration` | RGB separation | `.amount(0.01)` `.angle(0)` |
| `Bloom` | Glow effect | `.threshold(0.8)` `.intensity(1.0)` `.radius(5.0)` |
| `Feedback` | Frame feedback | `.decay(0.95)` `.mix(0.5)` `.zoom(1.0)` `.rotate(0.01)` |

### Retro Effects

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Dither` | Ordered dithering | `.pattern(DitherPattern::Bayer4x4)` `.levels(8)` |
| `Quantize` | Color reduction | `.levels(8)` |
| `Scanlines` | CRT lines | `.spacing(2)` `.intensity(0.3)` `.thickness(0.5)` |
| `CRTEffect` | Full CRT sim | `.curvature(0.1)` `.vignette(0.3)` `.scanlines(0.2)` |
| `Downsample` | Low-res look | `.factor(4)` |

### Compositing

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Composite` | Blend two textures | `.inputA(op)` `.inputB(op)` `.mode(BlendMode::Over)` `.opacity(1.0)` |
| `Switch` | Select input | `.input0(op)` `.input1(op)` `.select(0)` |
| `Output` | Final output | `.input(op)` |

### Particles

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Particles` | 2D particle system | `.emitRate(50)` `.life(2.0)` `.gravity(0.1)` `.size(0.02)` |
| `PointSprites` | Point-based particles | `.points(vector)` `.size(4.0)` |

### Math/Logic

| Operator | Description | Key Parameters |
|----------|-------------|----------------|
| `Math` | Math operations | `.operation(MathOp::Add)` `.value(0.5)` |
| `Logic` | Comparisons | `.operation(LogicOp::GreaterThan)` `.threshold(0.5)` |

## Enum Types

```cpp
// Noise types
NoiseType::Perlin, NoiseType::Simplex, NoiseType::Worley, NoiseType::Value

// Shape types
ShapeType::Circle, ShapeType::Rectangle, ShapeType::RoundedRect,
ShapeType::Triangle, ShapeType::Star, ShapeType::Ring, ShapeType::Polygon

// LFO waveforms
LFOWaveform::Sine, LFOWaveform::Triangle, LFOWaveform::Saw,
LFOWaveform::Square, LFOWaveform::Noise

// Blend modes
BlendMode::Over, BlendMode::Add, BlendMode::Multiply,
BlendMode::Screen, BlendMode::Overlay, BlendMode::Difference

// Gradient modes
GradientMode::Linear, GradientMode::Radial, GradientMode::Angular, GradientMode::Diamond

// Dither patterns
DitherPattern::Bayer2x2, DitherPattern::Bayer4x4, DitherPattern::Bayer8x8
```

## Common Patterns

### Chain operators together

```cpp
chain->add<Noise>("noise").scale(4.0f);
chain->add<HSV>("color").input("noise").hueShift(0.3f);
chain->add<Blur>("blur").input("color").radius(3.0f);
chain->add<Output>("out").input("blur");
```

### Feedback loop (trails/echoes)

```cpp
chain->add<Noise>("noise").scale(4.0f);
chain->add<Feedback>("fb").input("noise").decay(0.95f).zoom(1.01f);
chain->add<Output>("out").input("fb");
```

### Blend two sources

```cpp
chain->add<Noise>("noise");
chain->add<Image>("img").path("assets/photo.jpg");
chain->add<Composite>("blend")
    .inputA("img")
    .inputB("noise")
    .mode(BlendMode::Overlay)
    .opacity(0.5f);
chain->add<Output>("out").input("blend");
```

### Displace with noise

```cpp
chain->add<Image>("img").path("assets/photo.jpg");
chain->add<Noise>("noise").scale(10.0f).speed(0.3f);
chain->add<Displace>("warp")
    .source(&chain->get<Image>("img"))
    .map(&chain->get<Noise>("noise"))
    .strength(0.05f);
chain->add<Output>("out").input("warp");
```

### Retro/VHS look

```cpp
chain->add<Video>("vid").path("assets/video.mov");
chain->add<ChromaticAberration>("chroma").input("vid").amount(0.005f);
chain->add<Scanlines>("lines").input("chroma").spacing(3).intensity(0.2f);
chain->add<Quantize>("quant").input("lines").levels(32);
chain->add<Output>("out").input("quant");
```

### Particle system

```cpp
chain->add<Particles>("particles")
    .emitRate(100.0f)
    .life(3.0f)
    .gravity(0.05f)
    .size(0.01f, 0.005f)  // start, end
    .color(1.0f, 0.5f, 0.2f)
    .velocity(0.0f, -0.1f)
    .spread(30.0f);
chain->add<Output>("out").input("particles");
```

## Context API

```cpp
// In update() - available on ctx:
float time = ctx.time();      // Seconds since start
float dt = ctx.dt();          // Delta time (seconds)
int frame = ctx.frame();      // Frame number
int width = ctx.width();      // Output width
int height = ctx.height();    // Output height
```

## Troubleshooting

**"No output showing"**
- Make sure you have an `Output` operator with `.input()` connected
- Check that all operators are connected in a chain

**"Operator not found"**
- Verify the operator name matches exactly (case-sensitive)
- Make sure you're using `chain->get<Type>("name")`

**"Hot reload not working"**
- Save the file
- Check for compile errors in the terminal

**"Black screen"**
- Check if input connections are correct
- Verify image/video paths are relative to project folder
