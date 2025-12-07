# Vivid Chain API

The Chain API is Vivid's declarative system for composing operators into visual effects pipelines. It provides a clean, fluent interface for building operator graphs with automatic dependency resolution.

## Quick Start

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

// Chain must be global/static for hot-reload
static Chain* chain = nullptr;

void setup(Context& ctx) {
    // Clean up on hot-reload
    delete chain;
    chain = new Chain();

    // Add operators with fluent configuration
    auto& noise = chain->add<Noise>("noise").scale(4.0f).speed(0.3f);
    auto& fb = chain->add<Feedback>("fb").input(&noise).decay(0.9f);
    auto& mirror = chain->add<Mirror>("mirror").input(&fb).kaleidoscope(6);
    auto& color = chain->add<HSV>("color").input(&mirror).colorize(true).saturation(0.8f);

    // Output operator - every chain needs one
    chain->add<Output>("output").input(&color);
    chain->setOutput("output");
    chain->init(ctx);  // Auto-registers all operators for visualization
}

void update(Context& ctx) {
    if (!chain) return;

    // Dynamic parameter updates each frame
    chain->get<Feedback>("fb").rotate(ctx.mouseNormX() * 0.1f);
    chain->get<HSV>("color").hueShift(ctx.time() * 0.1f);

    // Process chain - handles execution order and output automatically
    chain->process(ctx);
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
- **Auto-registration** - all operators in a Chain are automatically registered for the visualizer (Tab key)
- **State** is preserved across hot-reloads

### Entry Points

Chain-based projects export two functions:

| Function | Purpose | Called |
|----------|---------|--------|
| `setup(Context&)` | Build the operator graph | Once on load/reload |
| `update(Context&)` | Dynamic parameter changes | Every frame |

The `VIVID_CHAIN(setup, update)` macro exports these for the runtime.

### Architecture Notes

- **Always use Chain** - All operators should be managed by a Chain, not created manually
- **Output operator required** - Every chain needs an Output operator to display results
- **No manual registration** - Don't call `ctx.registerOperator()` - Chain does this automatically in `init()`
- **No direct output** - Don't call `ctx.setOutputTexture()` - the Output operator handles this

## Adding Operators

```cpp
// Basic: add operator with name
chain->add<Noise>("myNoise");

// Fluent: configure inline
chain->add<Noise>("noise")
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

### Using .input() with Pointers (Recommended)

Use `chain->add<>()` to get a reference, then connect with pointers:

```cpp
auto& noise = chain->add<Noise>("noise");
auto& blur = chain->add<Blur>("blur");
auto& color = chain->add<HSV>("color");

blur.input(&noise);   // blur reads from noise
color.input(&blur);   // color reads from blur
```

Or use references captured during setup:

```cpp
auto& noise = chain->add<Noise>("noise").scale(4.0f);
chain->add<Blur>("blur").input(&noise);
```

### Two Inputs

Some operators (like `Composite`) take two inputs:

```cpp
auto& noise = chain->add<Noise>("noise");
auto& gradient = chain->add<Gradient>("gradient");
auto& comp = chain->add<Composite>("comp");
comp.inputA(&gradient)     // Background
    .inputB(&noise)        // Foreground
    .mode(BlendMode::Add);
```

## Dynamic Updates

Use `update()` for per-frame parameter changes:

```cpp
void update(Context& ctx) {
    if (!chain) return;

    // Mouse control
    float rotation = (ctx.mouseNormX() - 0.5f) * 0.1f;
    chain->get<Feedback>("fb").rotate(rotation);

    // Time-based animation
    float hue = std::fmod(ctx.time() * 0.1f, 1.0f);
    chain->get<HSV>("color").hueShift(hue);

    // Process chain - this handles all operator execution
    chain->process(ctx);
}
```

## Setting Output

Every chain requires an Output operator. The Output operator handles `ctx.setOutputTexture()` internally:

```cpp
chain->add<Output>("output").input("color");
chain->setOutput("output");
```

## Dependency Resolution

The Chain automatically handles execution order via topological sort when `init()` is called.
Operators are processed in dependency order regardless of the order they were added.

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
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

static Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain();

    // Noise as seed pattern
    auto& noise = chain->add<Noise>("noise")
        .scale(4.0f)
        .speed(0.3f)
        .octaves(4);

    // Feedback creates trails
    auto& feedback = chain->add<Feedback>("feedback")
        .input(&noise)
        .decay(0.92f)
        .zoom(1.02f)
        .rotate(0.01f);

    // Kaleidoscope symmetry
    auto& mirror = chain->add<Mirror>("mirror")
        .input(&feedback)
        .kaleidoscope(6);

    // Colorize the result
    auto& color = chain->add<HSV>("color")
        .input(&mirror)
        .colorize(true)
        .saturation(0.8f);

    // Output to screen
    chain->add<Output>("output").input(&color);
    chain->setOutput("output");
    chain->init(ctx);
}

void update(Context& ctx) {
    if (!chain) return;

    // Mouse X: rotation
    float rot = (ctx.mouseNormX() - 0.5f) * 0.1f;
    chain->get<Feedback>("feedback").rotate(rot);

    // Mouse Y: zoom
    float zoom = 0.98f + ctx.mouseNormY() * 0.06f;
    chain->get<Feedback>("feedback").zoom(zoom);

    // Cycle hue over time
    float hue = std::fmod(ctx.time() * 0.05f, 1.0f);
    chain->get<HSV>("color").hueShift(hue);

    // Click to clear
    if (ctx.wasMousePressed(0)) {
        chain->get<Feedback>("feedback").decay(0.0f);
    } else {
        chain->get<Feedback>("feedback").decay(0.92f);
    }

    chain->process(ctx);
}

VIVID_CHAIN(setup, update)
```

## Tips

1. **Name operators meaningfully** - You'll reference them in `update()`
2. **Use `colorize(true)`** for grayscale generators feeding into HSV
3. **Always use Output operator** - Required for displaying results
4. **Call `chain->init(ctx)`** after building the graph
5. **Call `chain->process(ctx)`** every frame in update()
6. **Keep `update()` fast** - It runs every frame
7. **State persists** - Animation phases survive hot-reload

## See Also

- [OPERATOR-API.md](OPERATOR-API.md) - Creating custom operators
- [SHADER-CONVENTIONS.md](SHADER-CONVENTIONS.md) - Writing shaders
- `examples/chain-demo/` - Full working example
- `examples/multi-composite/` - Multi-input compositing example
- `examples/2d-instancing/` - GPU-instanced 2D physics simulation
- `examples/3d-demo/` - 3D mesh rendering
