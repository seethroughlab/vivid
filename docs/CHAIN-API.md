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
    fb.input("noise");
    fb.decay = 0.9f;

    auto& mirror = chain.add<Mirror>("mirror");
    mirror.input("fb");
    mirror.segments = 6;

    auto& color = chain.add<HSV>("color");
    color.input("mirror");
    color.saturation = 0.8f;

    // Designate the output operator
    chain.output("color");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Dynamic parameter updates each frame
    // mouseNorm() returns 0-1 range, center at (0.5, 0.5)
    chain.get<Feedback>("fb").rotate = (ctx.mouseNorm().x - 0.5f) * 0.2f;
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

The `VIVID_CHAIN(setup, update)` macro exports these for the runtime with default window settings (1280x720, resizable).

### Window Configuration

Use `VIVID_CHAIN_CONFIG` to set window properties before creation:

```cpp
void setup(Context& ctx) { /* ... */ }
void update(Context& ctx) { /* ... */ }

VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false,
    .fullscreen = false
}))
```

| Option | Default | Description |
|--------|---------|-------------|
| `windowWidth` | 1280 | Initial window width |
| `windowHeight` | 720 | Initial window height |
| `resizable` | true | Allow window resizing |
| `fullscreen` | false | Start in fullscreen mode |

**Note:** Window configuration is read at startup and cannot be hot-reloaded. Changing these values requires restarting the application.

### Resolution Behavior

Operator resolution follows these rules:

1. **Generators** (Noise, Gradient, Shape, etc.):
   - Default to window size at initialization
   - Use `setResolution(w, h)` for explicit dimensions
   - Resolution is locked after init - window resize doesn't affect them

2. **Processors** (Blur, HSV, Mirror, etc.):
   - Inherit resolution from their input operator
   - Automatically match input dimensions

3. **Output/Display**:
   - The only operators that scale to window size
   - Always match current window dimensions

```cpp
auto& noise = chain.add<Noise>("noise");
// Uses window size (e.g., 1920x1080 from config)

auto& texture = chain.add<Noise>("texture");
texture.setResolution(512, 512);  // Explicit 512x512
```

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
- `SolidColor` - Solid colors

**Effects:**
- `Blur` - Gaussian blur
- `HSV` - Hue/saturation/brightness adjustment
- `Feedback` - Video feedback with transform
- `Mirror` - Mirroring and kaleidoscope
- `Displace` - Texture-based distortion
- `Transform` - Scale/rotate/translate
- `Edge` - Sobel edge detection
- `ChromaticAberration` - RGB separation
- `Pixelate` - Mosaic effect
- `Scanlines` - CRT effect

**Media (require vivid-video module):**
- `VideoPlayer` - Video playback
- `Image` - Static images
- `Webcam` - Camera input

**Utility:**
- `Composite` - Blend two textures
- `Switch` - Choose between inputs
- `Passthrough` - Identity (for organization)

## Connecting Operators

### Using .input() with Names

Connect operators by referencing their string names:

```cpp
auto& chain = ctx.chain();

auto& noise = chain.add<Noise>("noise");
noise.scale = 4.0f;

auto& blur = chain.add<Blur>("blur");
blur.input("noise");   // blur reads from noise

auto& color = chain.add<HSV>("color");
color.input("blur");   // color reads from blur

chain.output("color");
```

### Two Inputs

Some operators (like `Composite`) take two inputs:

```cpp
auto& noise = chain.add<Noise>("noise");
auto& gradient = chain.add<Gradient>("gradient");

auto& comp = chain.add<Composite>("comp");
comp.inputA("gradient");      // Background
comp.inputB("noise");         // Foreground
comp.mode = BlendMode::Add;
```

## Dynamic Updates

Use `update()` for per-frame parameter changes:

```cpp
void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Mouse control - mouseNorm() returns 0-1 range
    float rotation = (ctx.mouseNorm().x - 0.5f) * 0.1f;
    chain.get<Feedback>("fb").rotate = rotation;

    // Time-based animation
    float hue = std::fmod(ctx.time() * 0.1f, 1.0f);
    chain.get<HSV>("color").hueShift = hue;

    // Process chain - this handles all operator execution
    chain.process(ctx);
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
auto& noise = chain.add<Noise>("noise");
noise.scale = 4.0f;

auto& color = chain.add<HSV>("color");
color.input("noise");

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

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Noise as seed pattern
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.3f;
    noise.octaves = 4;

    // Feedback creates trails
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input("noise");
    feedback.decay = 0.92f;
    feedback.zoom = 1.02f;
    feedback.rotate = 0.01f;

    // Kaleidoscope symmetry
    auto& mirror = chain.add<Mirror>("mirror");
    mirror.input("feedback");
    mirror.segments = 6;

    // Color adjustment
    auto& color = chain.add<HSV>("color");
    color.input("mirror");
    color.saturation = 0.8f;

    // Output to screen
    chain.output("color");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Mouse X: rotation - mouseNorm() returns 0-1 range
    float rot = (ctx.mouseNorm().x - 0.5f) * 0.1f;
    chain.get<Feedback>("feedback").rotate = rot;

    // Mouse Y: zoom
    float zoom = 0.98f + ctx.mouseNorm().y * 0.06f;
    chain.get<Feedback>("feedback").zoom = zoom;

    // Cycle hue over time
    float hue = std::fmod(ctx.time() * 0.05f, 1.0f);
    chain.get<HSV>("color").hueShift = hue;

    // Click to clear
    if (ctx.mouseButton(0).pressed) {
        chain.get<Feedback>("feedback").decay = 0.0f;
    } else {
        chain.get<Feedback>("feedback").decay = 0.92f;
    }

    chain.process(ctx);
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
2. **Use HSV for color manipulation** - Adjust hue, saturation, and brightness
3. **Set output** - Call `chain.output("name")` to designate the display operator
4. **Call `chain.process(ctx)`** every frame in update()
5. **Keep `update()` fast** - It runs every frame
6. **State persists** - Animation phases survive hot-reload

## See Also

- [CREATING-OPERATORS.md](CREATING-OPERATORS.md) - Creating custom operators
- [SHADER-CONVENTIONS.md](SHADER-CONVENTIONS.md) - Writing shaders
- `projects/2d-effects/chain-basics/` - Full working example
- `projects/2d-effects/particles/` - 2D particle system
- `projects/3d-rendering/3d-basics/` - 3D mesh rendering
- `projects/3d-rendering/instancing/` - GPU instanced rendering
