# Vivid Chain API

The Chain API is Vivid's declarative system for composing operators into visual effects pipelines. It provides a clean, fluent interface for building operator graphs with automatic dependency resolution.

## Quick Start

```cpp
#include <vivid/vivid.h>
using namespace vivid;

void setup(Chain& chain) {
    // Add operators with fluent configuration
    chain.add<Noise>("noise").scale(4.0f).speed(0.3f);
    chain.add<Feedback>("fb").input("noise").decay(0.9f);
    chain.add<Mirror>("mirror").input("fb").kaleidoscope(6);
    chain.add<HSV>("color").input("mirror").colorize(true).saturation(0.8f);

    // Set final output
    chain.setOutput("color");
}

void update(Chain& chain, Context& ctx) {
    // Dynamic parameter updates each frame
    chain.get<Feedback>("fb").rotate(ctx.mouseNormX() * 0.1f);
    chain.get<HSV>("color").hueShift(ctx.time() * 0.1f);
}

// Export entry points
VIVID_CHAIN(setup, update)
```

## Core Concepts

### The Chain Object

The `Chain` class manages operator instances and their connections:

- **Operators** are added by type and given unique names
- **Connections** flow data from one operator to another via `.input()`
- **Execution** happens automatically in dependency order
- **State** is preserved across hot-reloads

### Entry Points

Chain-based projects export two functions:

| Function | Purpose | Called |
|----------|---------|--------|
| `setup(Chain&)` | Build the operator graph | Once on load/reload |
| `update(Chain&, Context&)` | Dynamic parameter changes | Every frame |

The `VIVID_CHAIN(setup, update)` macro exports these for the runtime.

## Adding Operators

```cpp
// Basic: add operator with name
chain.add<Noise>("myNoise");

// Fluent: configure inline
chain.add<Noise>("noise")
    .scale(4.0f)
    .speed(0.3f)
    .octaves(4);
```

### Built-in Operators

**Generators:**
- `Noise` - Animated fractal noise
- `Gradient` - Linear/radial/angular gradients
- `Shape` - SDF shapes (circle, rect, star, etc.)
- `Constant` - Solid colors

**Effects:**
- `Blur` - Gaussian blur
- `HSV` - Hue/saturation/brightness adjustment
- `Feedback` - Video feedback with transform
- `Mirror` - Mirroring and kaleidoscope
- `Displacement` - Texture-based distortion
- `Transform` - Scale/rotate/translate
- `Edge` - Sobel edge detection
- `ChromaticAberration` - RGB separation
- `Pixelate` - Mosaic effect
- `Scanlines` - CRT effect

**Media:**
- `VideoFile` - Video playback
- `ImageFile` - Static images
- `Webcam` - Camera input

**Utility:**
- `Composite` - Blend two textures
- `Switch` - Choose between inputs
- `Passthrough` - Identity (for organization)

## Connecting Operators

### Using .input() (Recommended)

Most operators have an `input()` method to specify their source:

```cpp
chain.add<Noise>("noise");
chain.add<Blur>("blur").input("noise");  // blur reads from noise
chain.add<HSV>("color").input("blur");   // color reads from blur
```

### Using connect() (Alternative)

For external configuration or complex graphs:

```cpp
chain.add<Noise>("noise");
chain.add<Blur>("blur");
chain.add<HSV>("color");

chain.connect("noise", "blur");   // noise.out -> blur.in
chain.connect("blur", "color");   // blur.out -> color.in
```

### Multiple Inputs

Some operators (like `Composite`) take multiple inputs:

```cpp
chain.add<Noise>("noise");
chain.add<Gradient>("gradient");
chain.add<Composite>("comp")
    .input("gradient")      // Background
    .input2("noise")        // Foreground
    .mode(BlendMode::Add);
```

## Dynamic Updates

Use `update()` for per-frame parameter changes:

```cpp
void update(Chain& chain, Context& ctx) {
    // Mouse control
    float rotation = (ctx.mouseNormX() - 0.5f) * 0.1f;
    chain.get<Feedback>("fb").rotate(rotation);

    // Time-based animation
    float hue = std::fmod(ctx.time() * 0.1f, 1.0f);
    chain.get<HSV>("color").hueShift(hue);

    // Keyboard interaction
    if (ctx.wasKeyPressed(Key::Space)) {
        chain.get<Feedback>("fb").decay(0.0f);  // Clear
    }
}
```

## Setting Output

Designate which operator provides the final output:

```cpp
chain.setOutput("color");  // Explicit
```

If not set, the last added operator is used.

## Dependency Resolution

The Chain automatically handles execution order. When using `connect()`, you can optionally call:

```cpp
chain.computeExecutionOrder();  // Topological sort
```

This ensures operators run in dependency order, even if added out of order.

## State Preservation

Operator state (like Feedback buffers, animation phases) is automatically preserved across hot-reloads. The runtime:

1. Saves state from all operators
2. Reloads the library
3. Calls `setup()` to rebuild the graph
4. Restores state to matching operator names

## Complete Example

```cpp
// Animated kaleidoscope with mouse control
#include <vivid/vivid.h>
using namespace vivid;

void setup(Chain& chain) {
    // Noise as seed pattern
    chain.add<Noise>("noise")
        .scale(4.0f)
        .speed(0.3f)
        .octaves(4);

    // Feedback creates trails
    chain.add<Feedback>("feedback")
        .input("noise")
        .decay(0.92f)
        .zoom(1.02f)
        .rotate(0.01f);

    // Kaleidoscope symmetry
    chain.add<Mirror>("mirror")
        .input("feedback")
        .kaleidoscope(6);

    // Colorize the result
    chain.add<HSV>("color")
        .input("mirror")
        .colorize(true)
        .saturation(0.8f);

    chain.setOutput("color");
}

void update(Chain& chain, Context& ctx) {
    // Mouse X: rotation
    float rot = (ctx.mouseNormX() - 0.5f) * 0.1f;
    chain.get<Feedback>("feedback").rotate(rot);

    // Mouse Y: zoom
    float zoom = 0.98f + ctx.mouseNormY() * 0.06f;
    chain.get<Feedback>("feedback").zoom(zoom);

    // Cycle hue over time
    float hue = std::fmod(ctx.time() * 0.05f, 1.0f);
    chain.get<HSV>("color").hueShift(hue);

    // Click to clear
    if (ctx.wasMousePressed(0)) {
        chain.get<Feedback>("feedback").decay(0.0f);
    } else {
        chain.get<Feedback>("feedback").decay(0.92f);
    }
}

VIVID_CHAIN(setup, update)
```

## Tips

1. **Name operators meaningfully** - You'll reference them in `update()`
2. **Use `colorize(true)`** for grayscale generators feeding into HSV
3. **Call `setOutput()` explicitly** for clarity
4. **Keep `update()` fast** - It runs every frame
5. **State persists** - Animation phases survive hot-reload

## See Also

- [OPERATOR-API.md](OPERATOR-API.md) - Creating custom operators
- [SHADER-CONVENTIONS.md](SHADER-CONVENTIONS.md) - Writing shaders
- `examples/chain-demo/` - Full working example
