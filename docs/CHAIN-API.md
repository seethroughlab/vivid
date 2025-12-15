# Vivid Chain API

The Chain API is Vivid's declarative system for composing operators into visual effects pipelines. It provides a clean interface for building operator graphs with automatic dependency resolution.

## Quick Start

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Add operators and configure parameters
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.3f;

    auto& fb = chain.add<Feedback>("fb");
    fb.input(&noise);
    fb.decay = 0.9f;

    auto& mirror = chain.add<Mirror>("mirror");
    mirror.input(&fb);
    mirror.segments = 6;

    auto& color = chain.add<HSV>("color");
    color.input(&mirror);
    color.saturation = 0.8f;

    // Designate the output operator
    chain.output("color");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Dynamic parameter updates each frame
    chain.get<Feedback>("fb").rotate = ctx.mouseNorm().x * 0.1f;
    chain.get<HSV>("color").hueShift = static_cast<float>(ctx.time()) * 0.1f;
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
// Add operator with name, then configure
auto& noise = chain.add<Noise>("myNoise");
noise.scale = 4.0f;
noise.speed = 0.3f;
noise.octaves = 4;
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

## Output Requirements

Every vivid project must have exactly one texture output:

- Call `chain.output("name")` to designate which operator renders to screen
- The designated operator must produce `OutputKind::Texture`
- Calling `output()` multiple times will warn (only last call takes effect)
- If no output is specified, you'll see a warning and the screen stays black

Audio output is optional:

- Call `chain.audioOutput("name")` if your project produces audio
- The designated operator must produce `OutputKind::Audio`

### Common Errors

| Scenario | What Happens |
|----------|--------------|
| No output specified | Warning: "Screen will be black" |
| Output operator doesn't exist | Error: initialization fails |
| Output is not texture type | Error: "produces X, not Texture" |
| output() called multiple times | Warning: "Only one output allowed" |

## Setting Output

Use `chain.output("name")` to designate the output operator:

```cpp
chain.add<Noise>("noise").scale(4.0f);
chain.add<HSV>("color").input("noise");
chain.output("color");  // Display the color operator
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
    chain->output("color");
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

## Debugging

### Debug Logging

When troubleshooting chain issues (e.g., "why is this operator rendering to screen?"), enable debug logging:

**Option 1: Environment variable**
```bash
VIVID_DEBUG_CHAIN=1 ./build/bin/vivid my-project
```

**Option 2: Programmatic**
```cpp
void setup(Context& ctx) {
    ctx.chain().setDebug(true);
    // ... rest of setup
}
```

This outputs the processing order and shows which operator is the screen output:

```
[Chain Debug] === Processing Chain ===
[Chain Debug] Designated output: composite
[Chain Debug] noise (Noise) -> texture
[Chain Debug] blur (Blur) -> texture
[Chain Debug] color (HSV) -> texture
[Chain Debug] composite (Composite) -> texture -> SCREEN OUTPUT
[Chain Debug] === End Processing ===
```

### Common Issues

1. **Wrong operator rendering to screen** - Check `chain.output("name")` is set correctly
2. **Operator not processing** - Ensure it's connected via `.input()` and not bypassed
3. **Circular dependency** - Chain will report an error; check your input connections

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
- `examples/2d-effects/chain-basics/` - Full working example
- `examples/2d-effects/particles/` - 2D particle system
- `examples/3d-rendering/3d-basics/` - 3D mesh rendering
- `examples/3d-rendering/instancing/` - GPU instanced rendering
