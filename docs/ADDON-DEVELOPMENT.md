# Addon Development Guide

This guide walks through creating custom operators for Vivid. Whether you're building a one-off effect for your project or creating a reusable addon, the process is the same.

## Quick Start: Minimal Operator

Here's the simplest possible operator - a color fill:

```cpp
// my_fill.h
#pragma once
#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

class MyFill : public TextureOperator {
public:
    // Public parameters (editable in chain visualizer)
    Param<float> red{"red", 1.0f, 0.0f, 1.0f};
    Param<float> green{"green", 0.0f, 0.0f, 1.0f};
    Param<float> blue{"blue", 0.0f, 0.0f, 1.0f};

    MyFill() {
        registerParam(red);
        registerParam(green);
        registerParam(blue);
    }

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "MyFill"; }
};

} // namespace vivid::effects
```

## Operator Lifecycle

Every operator follows this lifecycle:

1. **Construction** - Set default parameter values
2. **init(Context&)** - Create GPU resources (pipelines, buffers, textures)
3. **process(Context&)** - Called every frame, render your output
4. **cleanup()** - Release GPU resources

```cpp
void MyFill::init(Context& ctx) {
    // Create GPU resources once
    createPipeline(ctx);
    createTextures(ctx);
}

void MyFill::process(Context& ctx) {
    // Called every frame
    // 1. Check if we need to recalculate
    if (!needsCook()) return;

    // 2. Do your rendering
    renderToTexture(ctx);

    // 3. Mark output as updated
    didCook();
}

void MyFill::cleanup() {
    // Release GPU resources
    if (m_pipeline) wgpuRenderPipelineRelease(m_pipeline);
    if (m_texture) wgpuTextureRelease(m_texture);
}
```

## Operator Types

### TextureOperator (Most Common)

For operators that output a texture:

```cpp
#include <vivid/effects/texture_operator.h>

class MyEffect : public TextureOperator {
    // Inherits:
    // - outputView() returns your texture
    // - checkResize() handles window resize
    // - createOutputTexture() creates render target
};
```

### Value Operators

For operators that output a single value (like LFO, audio level):

```cpp
class MyLFO : public Operator {
public:
    OutputKind outputKind() const override { return OutputKind::Value; }
    float outputValue() const override { return m_currentValue; }

private:
    float m_currentValue = 0.0f;
};
```

### Geometry Operators

For 3D geometry (meshes, primitives):

```cpp
class MyMesh : public Operator {
public:
    OutputKind outputKind() const override { return OutputKind::Geometry; }
    // Return mesh data for Render3D to consume
};
```

### Output Types

| Type | Description | Preview |
|------|-------------|---------|
| `Texture` | 2D image | Thumbnail in preview panel |
| `Value` | Single float | Numeric display with sparkline |
| `ValueArray` | Float array | Waveform display |
| `Geometry` | 3D mesh | Wireframe preview |

## Context API

The `Context` object provides access to the runtime.

### Time and Frame Info

```cpp
float t = ctx.time();    // Seconds since start
float dt = ctx.dt();     // Delta time (seconds since last frame)
int f = ctx.frame();     // Frame number
```

### Resolution

```cpp
int w = ctx.width();     // Output width in pixels
int h = ctx.height();    // Output height in pixels
```

### Debug Values

```cpp
ctx.debug("myValue", someFloat);  // Show in debug panel (D key)
```

## 2D Instanced Rendering

For efficient rendering of many 2D shapes (particles, data visualizations), use GPU instancing:

```cpp
void update(Chain& chain, Context& ctx) {
    std::vector<Circle2D> circles;

    for (int i = 0; i < 1000; i++) {
        circles.emplace_back(
            glm::vec2(x, y),           // position (0-1 normalized)
            0.02f,                      // radius (normalized)
            glm::vec4(1, 0, 0, 1)      // color (RGBA)
        );
    }

    // Render all circles in ONE draw call
    Texture output = ctx.createTexture();
    ctx.drawCircles(circles, output, glm::vec4(0, 0, 0, 1));  // clearColor
}
```

**Performance:** All shapes are rendered in a single GPU draw call using instancing.

**See:** `examples/2d-effects/particles/` for a complete particle system example.

## Parameters

### Basic Parameter Types

```cpp
// Scalar types
Param<float> intensity{"intensity", 1.0f, 0.0f, 2.0f};  // name, default, min, max
Param<int> iterations{"iterations", 4, 1, 10};
Param<bool> enabled{"enabled", true, false, true};

// Vectors
Vec2Param offset{"offset", 0.0f, 0.0f, -1.0f, 1.0f};    // name, x, y, min, max
Vec3Param position{"position", 0.0f, 0.0f, 0.0f, -10.0f, 10.0f};

// Color
ColorParam tint{"tint", 1.0f, 1.0f, 1.0f, 1.0f};  // name, r, g, b, a

// File paths
FilePathParam texture{"texture", "", "*.png;*.jpg", "image"};  // name, default, filter, category
```

### Registering Parameters

Call `registerParam()` in constructor to expose parameters to the chain visualizer:

```cpp
MyEffect() {
    registerParam(intensity);
    registerParam(iterations);
    registerParam(tint);
}
```

### Reading Parameters

Parameters implicitly convert to their value type:

```cpp
void process(Context& ctx) {
    float i = intensity;  // Implicit conversion
    int n = iterations.get();  // Explicit

    // For std:: functions, use explicit cast
    float clamped = std::max(0.0f, static_cast<float>(intensity));
}
```

## Inputs and Outputs

### Accepting Input

```cpp
class MyBlur : public TextureOperator {
public:
    void input(TextureOperator* op) {
        setInput(0, op);  // Slot 0
    }

    void process(Context& ctx) override {
        Operator* in = getInput(0);
        if (!in) return;

        WGPUTextureView inputView = in->outputView();
        // ... use inputView in your shader
    }
};
```

### Multiple Inputs

```cpp
class MyComposite : public TextureOperator {
public:
    void inputA(TextureOperator* op) { setInput(0, op); }
    void inputB(TextureOperator* op) { setInput(1, op); }

    void process(Context& ctx) override {
        auto* a = getInput(0);
        auto* b = getInput(1);
        // Blend a and b
    }
};
```

## Cooking System

Vivid uses demand-based cooking to avoid unnecessary work.

### Check Before Processing

```cpp
void process(Context& ctx) {
    // Skip if nothing changed
    if (!needsCook()) return;

    // Do expensive work...

    // Mark output as updated
    didCook();
}
```

### Mark Dirty When Parameters Change

If you have custom setters:

```cpp
void setIntensity(float v) {
    if (intensity.get() != v) {
        intensity = v;
        markDirty();  // Force recook
    }
}
```

## Shaders (WGSL)

Vivid uses WebGPU with WGSL shaders. Place your shader in `shaders/`:

```wgsl
// shaders/my_effect.wgsl

struct Uniforms {
    intensity: f32,
    time: f32,
    resolution: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var inputSampler: sampler;

@fragment
fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
    let color = textureSample(inputTex, inputSampler, uv);
    return color * u.intensity;
}
```

## State Preservation (Hot-Reload)

For operators that maintain state (like Feedback), override save/load:

```cpp
std::unique_ptr<OperatorState> saveState() override {
    auto state = std::make_unique<TextureState>();
    // Copy texture pixels to state->pixels
    return state;
}

void loadState(std::unique_ptr<OperatorState> state) override {
    if (auto* ts = dynamic_cast<TextureState*>(state.get())) {
        // Restore texture from ts->pixels
    }
}
```

## Complete Example: Invert Effect

```cpp
// invert.h
#pragma once
#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

class Invert : public TextureOperator {
public:
    Param<float> amount{"amount", 1.0f, 0.0f, 1.0f};

    Invert() {
        registerParam(amount);
    }

    void input(TextureOperator* op) { setInput(0, op); }

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Invert"; }

private:
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::effects
```

```cpp
// invert.cpp
#include "invert.h"
#include <vivid/context.h>

namespace vivid::effects {

void Invert::init(Context& ctx) {
    // Load shader
    auto shaderCode = loadShader("invert.wgsl");

    // Create pipeline, bind group layout, uniform buffer...
    // (See existing effects for full GPU resource setup)
}

void Invert::process(Context& ctx) {
    if (!needsCook()) return;

    checkResize(ctx);  // Handle window resize

    auto* input = getInput(0);
    if (!input) return;

    // Update uniforms
    struct Uniforms { float amount; float pad[3]; };
    Uniforms u{amount, {0,0,0}};
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &u, sizeof(u));

    // Render pass...
    // (See existing effects for render pass setup)

    didCook();
}

void Invert::cleanup() {
    if (m_pipeline) wgpuRenderPipelineRelease(m_pipeline);
    // Release other resources...
}

} // namespace vivid::effects
```

```wgsl
// shaders/invert.wgsl
struct Uniforms {
    amount: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;

@fragment
fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
    let c = textureSample(tex, samp, uv);
    let inverted = vec4f(1.0 - c.rgb, c.a);
    return mix(c, inverted, u.amount);
}
```

## Using Your Operator

```cpp
// chain.cpp
#include <vivid/vivid.h>
#include "invert.h"  // Your operator

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    chain.add<Noise>("noise").scale = 4.0f;
    chain.add<Invert>("invert").input(&chain.get<Noise>("noise"));
    chain.get<Invert>("invert").amount = 0.5f;

    chain.output("invert");
}

void update(Context& ctx) {
    ctx.chain().process();
}

VIVID_CHAIN(setup, update)
```

## Creating an Addon

For reusable operators, create an addon structure:

```
my-addon/
├── include/
│   └── my-addon/
│       └── my_effect.h
├── src/
│   └── my_effect.cpp
├── shaders/
│   └── my_effect.wgsl
├── tests/
│   └── test_my_effect.cpp
├── CMakeLists.txt
├── addon.json
└── README.md
```

### addon.json

```json
{
    "name": "my-addon",
    "version": "1.0.0",
    "description": "My custom Vivid addon",
    "operators": ["MyEffect"],
    "dependencies": []
}
```

### CMakeLists.txt

```cmake
add_library(my-addon STATIC
    src/my_effect.cpp
)

target_include_directories(my-addon PUBLIC include)
target_link_libraries(my-addon PRIVATE vivid-core)

# Copy shaders to build directory
file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})
```

## Tips

1. **Look at existing operators** - `core/src/effects/` has many examples
2. **Start simple** - Get basic rendering working before adding features
3. **Use Param<T>** - It handles UI integration automatically
4. **Call didCook()** - Forgetting this causes downstream operators to skip updates
5. **Check needsCook()** - Improves performance when nothing changed
6. **Test with snapshot mode** - `--snapshot test.png` for automated testing

## Debugging

Enable debug output to trace cooking:

```bash
VIVID_DEBUG_CHAIN=1 ./build/bin/vivid my-project
```

Or in code:

```cpp
ctx.chain().setDebug(true);
```

This shows which operators are processing each frame and helps identify cooking issues.

## Custom Visualization (drawVisualization)

Operators can provide custom visualizations for the chain visualizer. This is useful for showing real-time state like levels, envelopes, or keyboard activity.

### Basic Setup

Override `drawVisualization()` in your operator:

```cpp
#include <vivid/viz_helpers.h>

class MyAnalyzer : public Operator {
public:
    bool drawVisualization(VizDrawList* dl, float minX, float minY,
                           float maxX, float maxY) override;
private:
    float m_level = 0.0f;
};
```

### Using VizHelpers (Recommended)

The `VizHelpers` class provides high-level drawing functions that handle layout, colors, and styling:

```cpp
bool MyAnalyzer::drawVisualization(VizDrawList* dl, float minX, float minY,
                                    float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    // Draw background
    viz.drawBackground(bounds);

    // Draw a level meter with gradient (green→yellow→red)
    viz.drawMeter(bounds.inset(4), m_level);

    return true;  // Return true if you drew something
}
```

### VizBounds Layout Helper

`VizBounds` simplifies layout calculations:

```cpp
VizBounds bounds{x, y, width, height};

bounds.cx();        // Center X
bounds.cy();        // Center Y
bounds.right();     // Right edge (x + w)
bounds.bottom();    // Bottom edge (y + h)

bounds.inset(4);              // Shrink by 4px on all sides
bounds.inset(4, 8);           // Shrink by 4px horizontal, 8px vertical
bounds.splitLeft(0.5f);       // Left 50%
bounds.splitRight(0.5f);      // Right 50%
bounds.splitTop(0.3f);        // Top 30%
bounds.splitBottom(0.7f);     // Bottom 70%
bounds.sub(10, 20, 50, 30);   // Sub-region at (x+10, y+20) size 50x30
```

### VizHelpers Functions

| Function | Description |
|----------|-------------|
| `drawBackground(bounds, color)` | Dark background fill |
| `drawMeter(bounds, value, horizontal)` | Level meter with gradient |
| `drawDualMeter(bounds, rms, peak)` | RMS + Peak side-by-side |
| `drawSpectrum(bounds, bins, count, numBars)` | FFT spectrum bars |
| `drawWaveform(bounds, samples, count, color)` | Audio waveform |
| `drawEnvelopeADSR(bounds, a, d, s, r, current)` | ADSR shape |
| `drawEnvelopeBar(bounds, value, color)` | Simple vertical bar |
| `drawDualEnvelope(bounds, v1, v2, c1, c2)` | Two envelopes stacked |
| `drawKeyboard(bounds, lo, hi, active, available)` | Piano keyboard |
| `drawGate(bounds, isOpen, openAmount)` | Gate open/closed |
| `drawActivityDot(cx, cy, intensity, color)` | Activity indicator |
| `drawLabel(bounds, text, color)` | Centered text |
| `drawValue(bounds, value, suffix, precision)` | Formatted number |

### VizColors Standard Palette

```cpp
VizColors::Background       // Dark purple (40, 30, 50)
VizColors::BackgroundDark   // Darker variant
VizColors::MeterGreen       // Level meter low
VizColors::MeterYellow      // Level meter mid
VizColors::MeterRed         // Level meter high
VizColors::Highlight        // Warm gold accent
VizColors::Active           // Bright blue
VizColors::StatusOpen       // Gate open (green)
VizColors::StatusClosed     // Gate closed (red)
VizColors::EnvelopeWarm     // Orange for envelopes
VizColors::TextPrimary      // White text
VizColors::TextSecondary    // Dim text

// Helpers
VizColors::meterGradient(t);      // Get color for 0-1 position
VizColors::lerp(a, b, t);         // Blend two colors
```

### Low-Level Drawing (VizDrawList)

For custom shapes, use `VizDrawList` directly:

```cpp
bool MyOp::drawVisualization(VizDrawList* dl, float minX, float minY,
                              float maxX, float maxY) {
    // Filled shapes
    dl->AddRectFilled({x, y}, {x+w, y+h}, VIZ_COL32(255, 0, 0, 255));
    dl->AddCircleFilled({cx, cy}, radius, color);
    dl->AddTriangleFilled({p1x, p1y}, {p2x, p2y}, {p3x, p3y}, color);

    // Outlines
    dl->AddRect({x, y}, {x+w, y+h}, color, rounding, flags, thickness);
    dl->AddCircle({cx, cy}, radius, color, segments, thickness);
    dl->AddLine({x1, y1}, {x2, y2}, color, thickness);

    // Text
    dl->AddText({x, y}, color, "Hello");
    VizTextSize size = dl->CalcTextSize("Hello");

    return true;
}
```

### Example: Level Meter

Before VizHelpers (50 lines):
```cpp
// Manual gradient loop, layout calculations, color picking...
for (int i = 0; i < height; i++) {
    float t = static_cast<float>(i) / height;
    uint32_t col;
    if (t < 0.5f) col = VIZ_COL32(80, 180, 80, 255);
    else if (t < 0.8f) col = VIZ_COL32(200, 180, 60, 255);
    else col = VIZ_COL32(200, 80, 80, 255);
    // ...
}
```

After VizHelpers (5 lines):
```cpp
bool Levels::drawVisualization(VizDrawList* dl, float minX, float minY,
                                float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};
    viz.drawBackground(bounds);
    viz.drawDualMeter(bounds.inset(4), m_rms, m_peak);
    return true;
}
```
