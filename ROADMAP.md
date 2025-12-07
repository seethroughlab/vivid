# Vivid Roadmap

A **minimal-core, addon-first creative coding framework** built on WebGPU. Plain C++ that language models can read and write—combining TouchDesigner's inspect-anywhere philosophy with the portability of text-based code.

See [docs/PHILOSOPHY.md](docs/PHILOSOPHY.md) for design goals and lessons learned.

---

## Guiding Principles

1. **Minimal core.** The runtime is ~600 lines: window, timing, input, hot-reload, addon registry, and a simple texture display. Everything else is an addon.

2. **Dawn is the graphics backend.** Core and all addons are written against the standard WebGPU C API (`webgpu.h`) using Google's Dawn implementation—battle-tested in Chrome, well-documented, actively developed.

3. **Get to the chain API fast.** The core value of Vivid is the `chain.cpp` programming model. All infrastructure work serves this goal.

4. **LLM-friendly.** Everything is plain text (C++, WGSL shaders, JSON metadata). Models can read, write, and reason about the entire codebase.

5. **Hot reload everything.** Edit C++ or WGSL, save, see changes. No restart, no lost state.

6. **State preservation is sacred.** Feedback buffers, animation phases, and operator state survive hot-reload. Creative iteration never loses accumulated state.

7. **Test continuously.** Every phase includes validation. Visual regression testing, example project testing, and CI on all three platforms. Testing is not a phase—it's part of every step.

---

## Testing Strategy

Testing is continuous, not a final phase. Every operator and example must be testable from day one.

### Visual Regression Testing
- Reference images for each operator (golden master approach)
- Pixel-diff comparison with tolerance for floating-point variance
- Automated screenshot capture during CI

### Example Project Testing
- All examples in `examples/` must run without errors
- Each example renders for N frames and captures output
- CI runs examples on macOS, Windows, and Linux

### Operator Testing
- Each operator has a test chain that exercises its parameters
- Test edge cases: zero, negative, extreme values
- Output comparison against reference images

### CI Pipeline (GitHub Actions)
- Triggered on every PR and push to main
- Matrix: macOS, Windows, Linux
- Steps: Build → Run examples → Visual diff report
- Block merge if visual regressions detected

### Headless Mode (for CI)
```bash
vivid my-project --headless --frames 10 --output screenshot.png
```
- Render to offscreen framebuffer without window
- Capture specific frame for comparison
- Exit after N frames

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│ User Code (chain.cpp)                                    │
│                                                          │
│  void setup(Context& ctx) { ... }                        │
│  void update(Context& ctx) { ... }                       │
│  VIVID_CHAIN(setup, update)                              │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│ Addons (vivid-effects-2d, vivid-render3d, etc.)          │
│                                                          │
│  All written against webgpu.h                            │
│  Operators provide fluent API for chain composition      │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│ Vivid Core (~600 lines)                                  │
│                                                          │
│  - GLFW window + input                                   │
│  - WebGPU device/surface (via webgpu.h)                 │
│  - Texture blit (display addon output)                   │
│  - Bitmap font (error overlay)                           │
│  - Hot-reload (file watch, compile, dlopen)              │
│  - Addon registry (auto-detect via #include)             │
│  - Context (time, input, device access)                  │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│ Dawn (Google's WebGPU)                                   │
│                                                          │
│  Vulkan/Metal/D3D12 abstraction, Chrome-tested           │
└─────────────────────────────────────────────────────────┘
```

### Why This Approach

**Rendering engines have opinions.** Full-featured engines provide powerful abstractions, but they impose constraints. When an engine's PBR renderer and GLTF loader use incompatible internal structures, you end up fighting the engine.

**The solution:** Use a low-level graphics API (WebGPU) that gives control without imposing a rendering paradigm. Let addons provide higher-level abstractions. The core has no rendering opinions—all rendering is done by addons, and compatibility between addons is the user's choice.

---

## Project Structure

A minimal vivid project:

```
my-project/
├── chain.cpp       # Required: Your visual program
├── ROADMAP.md      # Recommended: For LLM-assisted development
├── shaders/        # Optional: Custom WGSL shaders
└── assets/         # Optional: Project-specific assets
    ├── textures/
    ├── hdris/
    └── models/
```

**Asset Path Resolution:**

When operators load assets (images, models, etc.), Vivid searches:
1. `<project>/assets/` (project-specific)
2. `<vivid>/assets/` (shared defaults)

```cpp
// In operator code
std::string path = ctx.resolvePath("textures/wood.png");
// Returns: "/path/to/my-project/assets/textures/wood.png"
// Falls back to: "/path/to/vivid/assets/textures/wood.png"
```

---

## Dependencies

| Library | Purpose | Notes |
|---------|---------|-------|
| Dawn | WebGPU implementation | Google's production-tested C++ WebGPU |
| GLFW | Window/input management | Cross-platform |
| GLM | Math library | Vectors, matrices, column-major convention |

**Addons may have additional dependencies:**
- vivid-media: FFmpeg, AVFoundation
- vivid-audio: miniaudio
- vivid-imgui: Dear ImGui, imnodes
- vivid-ml: ONNX Runtime

---

## Core Components (~600 lines total)

### 1. Window & Main Loop (~50 lines)

```cpp
// core/src/main.cpp
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include "context.h"
#include "display.h"
#include "hot_reload.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: vivid <project-path>" << std::endl;
        return 1;
    }
    fs::path projectPath = argv[1];

    // GLFW window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vivid", nullptr, nullptr);

    // WebGPU init
    WGPUInstance instance = wgpuCreateInstance(nullptr);
    WGPUSurface surface = glfwGetWGPUSurface(instance, window);
    WGPUAdapter adapter = requestAdapter(instance, surface);
    WGPUDevice device = requestDevice(adapter);

    // Core systems
    Context ctx(device, window);
    Display display(device, surface);
    HotReload hotReload;

    hotReload.init(projectPath);

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ctx.beginFrame();

        if (hotReload.poll()) {
            if (hotReload.hasError()) {
                display.setError(hotReload.error());
            } else {
                display.clearError();
                hotReload.setup()(ctx);
            }
        }

        if (hotReload.isReady()) {
            hotReload.update()(ctx);
        }

        display.render(ctx.getOutputTexture());
        ctx.endFrame();
    }

    // Cleanup
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuSurfaceRelease(surface);
    wgpuInstanceRelease(instance);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
```

### 2. Context (~100 lines)

Provides frame state and WebGPU device to user code and addons.

```cpp
// core/include/vivid/context.h
#pragma once
#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace vivid {

class Context {
public:
    Context(WGPUDevice device, GLFWwindow* window);

    // Frame lifecycle
    void beginFrame();
    void endFrame();

    // Timing
    float time() const { return time_; }
    float dt() const { return dt_; }
    int frame() const { return frame_; }

    // Window
    int width() const { return width_; }
    int height() const { return height_; }

    // Input
    glm::vec2 mousePosition() const { return mousePos_; }
    float mouseNormX() const { return mousePos_.x / width_; }
    float mouseNormY() const { return mousePos_.y / height_; }
    bool wasMousePressed(int button = 0) const;
    bool isMouseDown(int button = 0) const;
    bool wasKeyPressed(int key) const;
    bool isKeyDown(int key) const;

    // WebGPU access (for addons)
    WGPUDevice device() const { return device_; }
    WGPUQueue queue() const { return queue_; }

    // Output registration (addon → core)
    void registerOutput(WGPUTextureView texture);
    WGPUTextureView getOutputTexture() const { return outputTexture_; }

private:
    WGPUDevice device_;
    WGPUQueue queue_;
    GLFWwindow* window_;

    float time_ = 0.0f;
    float dt_ = 0.0f;
    int frame_ = 0;
    int width_, height_;
    glm::vec2 mousePos_;

    WGPUTextureView outputTexture_ = nullptr;
};

} // namespace vivid
```

### 3. Display (~150 lines)

Displays addon output texture and error messages.

```cpp
// core/include/vivid/display.h
#pragma once
#include <webgpu/webgpu.h>
#include <string>

namespace vivid {

class Display {
public:
    Display(WGPUDevice device, WGPUSurface surface);
    ~Display();

    void render(WGPUTextureView texture);
    void setError(const std::string& message);
    void clearError();

private:
    void initBlitPipeline();
    void initTextPipeline();
    void drawText(WGPURenderPassEncoder pass, const std::string& text,
                  float x, float y, float r, float g, float b);

    WGPUDevice device_;
    WGPUSurface surface_;
    WGPURenderPipeline blitPipeline_;
    WGPURenderPipeline textPipeline_;
    WGPUBindGroup blitBindGroup_;
    WGPUSampler sampler_;
    WGPUTexture fontTexture_;
    WGPUBuffer textVertexBuffer_;

    std::string errorMessage_;
    bool hasError_ = false;
};

} // namespace vivid
```

### 4. Hot Reload (~300 lines)

Watches chain.cpp, compiles to shared library, loads via dlopen.

Key components:
- **FileWatcher**: Polls file modification time
- **Compiler**: Invokes clang++/g++/cl.exe with correct flags
- **DynamicLibrary**: Cross-platform dlopen wrapper
- **Symbol resolution**: Finds `vivid_setup` and `vivid_update`

**Build number tracking**: Each compilation produces a unique library name (e.g., `chain_0042.dylib`) to prevent OS library caching issues.

**Error preservation**: If compilation fails, the previous working library continues running. Errors are displayed via:
1. Console output (stderr)
2. Bitmap font overlay in window
3. WebSocket broadcast to VS Code extension

**State preservation protocol**:
```cpp
// Before unload
auto states = chain.saveAllStates();  // Map<name, OperatorState>

// Load new library
hotReload.reload();
hotReload.setup()(ctx);

// After reload
chain.restoreAllStates(states);
```

### 5. Addon Registry (~100 lines)

Detects addons by scanning `#include` directives, loads addon.json metadata.

---

## Shaders (WGSL)

### Shader Parameter Convention

All effect shaders use a standardized uniform structure for consistency:

```wgsl
struct Uniforms {
    time: f32,
    deltaTime: f32,
    resolution: vec2f,
    frame: i32,
    mode: i32,              // Integer mode selector
    param0: f32,            // Primary effect parameter
    param1: f32,            // Secondary parameter
    param2: f32,
    param3: f32,
    param4: f32,
    param5: f32,
    param6: f32,
    param7: f32,            // 8 generic float params
    vec0: vec2f,            // 2D vector parameter
    vec1: vec2f,            // 2D vector parameter
}
@group(0) @binding(0) var<uniform> u: Uniforms;
```

C++ side mapping:
```cpp
Context::ShaderParams params;
params.param0 = scale_;
params.param1 = speed_;
params.mode = blendMode_;
ctx.runShader("shaders/effect.wgsl", input, output, params);
```

### Core Shaders

Core has two shaders for display:

### Blit Shader

```wgsl
// core/shaders/blit.wgsl

struct Uniforms {
    resolution: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
@group(0) @binding(2) var inputSampler: sampler;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f(3.0, -1.0),
        vec2f(-1.0, 3.0)
    );
    var uvs = array<vec2f, 3>(
        vec2f(0.0, 1.0),
        vec2f(2.0, 1.0),
        vec2f(0.0, -1.0)
    );

    var output: VertexOutput;
    output.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    output.uv = uvs[vertexIndex];
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(inputTexture, inputSampler, input.uv);
}
```

### Text Shader (Bitmap Font)

```wgsl
// core/shaders/text.wgsl

struct Uniforms {
    resolution: vec2f,
    color: vec4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var fontTexture: texture_2d<f32>;
@group(0) @binding(2) var fontSampler: sampler;

struct VertexInput {
    @location(0) position: vec2f,
    @location(1) uv: vec2f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let ndc = (input.position / uniforms.resolution) * 2.0 - 1.0;
    output.position = vec4f(ndc.x, -ndc.y, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let alpha = textureSample(fontTexture, fontSampler, input.uv).r;
    return vec4f(uniforms.color.rgb, uniforms.color.a * alpha);
}
```

---

## Addons

### Addon Structure

```
addons/vivid-effects-2d/
├── addon.json              # Metadata
├── CMakeLists.txt
├── include/vivid/effects/
│   ├── effects.h           # Main include
│   ├── texture_operator.h  # Base class
│   └── operators/
│       ├── noise.h
│       ├── blur.h
│       └── ...
├── src/
│   ├── texture_operator.cpp
│   └── operators/
│       ├── noise.cpp
│       └── ...
└── shaders/
    ├── noise.wgsl
    ├── blur.wgsl
    └── ...
```

### Addon Metadata (addon.json)

```json
{
  "name": "vivid-effects-2d",
  "version": "0.1.0",
  "description": "2D texture effects for Vivid",
  "platforms": ["macos", "windows", "linux"],
  "detectHeaders": ["vivid/effects/effects.h"],
  "dependencies": [],
  "includeDirs": ["include"],
  "libraries": {}
}
```

### Writing an Addon

1. Create directory in `addons/`
2. Add `addon.json` with metadata
3. Write operators using webgpu.h
4. Register textures via `ctx.registerOutput()`

Example operator:

```cpp
// addons/vivid-effects-2d/include/vivid/effects/operators/noise.h
#pragma once
#include <vivid/operator.h>
#include <webgpu/webgpu.h>

namespace vivid::effects {

class Noise : public Operator {
public:
    // Fluent API
    Noise& scale(float s) { scale_ = s; return *this; }
    Noise& speed(float s) { speed_ = s; return *this; }
    Noise& octaves(int o) { octaves_ = o; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    std::string name() const override { return "Noise"; }
    OutputKind outputKind() const override { return OutputKind::Texture; }

    WGPUTextureView outputView() const { return outputView_; }

private:
    float scale_ = 4.0f;
    float speed_ = 0.5f;
    int octaves_ = 4;

    WGPUTexture output_ = nullptr;
    WGPUTextureView outputView_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
};

} // namespace vivid::effects
```

---

## Operator Base Class

```cpp
// core/include/vivid/operator.h
#pragma once
#include <string>
#include <vector>
#include <memory>

namespace vivid {

class Context;

enum class OutputKind {
    Texture,
    Value,
    ValueArray,
    Geometry
};

enum class ParamType {
    Float, Int, Bool, Vec2, Vec3, Vec4, Color, String
};

struct ParamDecl {
    std::string name;
    ParamType type;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float defaultVal[4] = {0, 0, 0, 0};
};

struct OperatorState {
    virtual ~OperatorState() = default;
};

class Operator {
public:
    virtual ~Operator() = default;

    // Lifecycle
    virtual void init(Context& ctx) {}
    virtual void process(Context& ctx) = 0;
    virtual void cleanup() {}

    // Metadata
    virtual std::string name() const = 0;
    virtual OutputKind outputKind() const { return OutputKind::Texture; }
    virtual std::vector<ParamDecl> params() { return {}; }

    // State preservation (for hot reload)
    virtual std::unique_ptr<OperatorState> saveState() { return nullptr; }
    virtual void loadState(std::unique_ptr<OperatorState> state) {}

    // Input connections
    void setInput(Operator* op) { inputs_.push_back(op); }
    void setInput(int index, Operator* op);
    Operator* getInput(int index = 0) const;
    size_t inputCount() const { return inputs_.size(); }

    // Source location (for editor)
    int sourceLine = 0;

protected:
    std::vector<Operator*> inputs_;
};

} // namespace vivid
```

---

## Chain API

The fluent Chain API enables declarative operator composition:

```cpp
// core/include/vivid/chain.h
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

class Operator;
class Context;

class Chain {
public:
    template<typename T, typename... Args>
    T& add(const std::string& name, Args&&... args) {
        auto op = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *op;
        operators_[name] = std::move(op);
        orderedNames_.push_back(name);
        return ref;
    }

    template<typename T>
    T& get(const std::string& name) {
        return dynamic_cast<T&>(*operators_.at(name));
    }

    Operator* getByName(const std::string& name);
    void setOutput(const std::string& name) { outputName_ = name; }

    void init(Context& ctx);
    void process(Context& ctx);

    // State preservation
    std::map<std::string, std::unique_ptr<OperatorState>> saveAllStates();
    void restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states);

private:
    void computeExecutionOrder();  // Topological sort via Kahn's algorithm

    std::unordered_map<std::string, std::unique_ptr<Operator>> operators_;
    std::unordered_map<std::string, std::vector<std::string>> dependencies_;
    std::vector<std::string> orderedNames_;
    std::vector<Operator*> executionOrder_;
    std::string outputName_;
};

} // namespace vivid
```

### Fluent Input Patterns

Operators declare their inputs via fluent `.input()` method:

```cpp
// Single input (most effects)
chain.add<Blur>("blur").input("noise").radius(5.0f);

// Two named inputs (compositing)
chain.add<Composite>("comp").a("background").b("foreground").mode(BlendMode::Over);

// Multi-input (up to 8 layers)
chain.add<Composite>("layers").inputs({"layer1", "layer2", "layer3", "layer4"});
```

### Constant vs Node Input

Math operators accept either a node reference OR a constant value:

```cpp
chain.add<Math>("scaled")
    .a("lfo")           // Input from another operator
    .b(0.5f)            // Constant value
    .multiply();
```

### Usage Example

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/operators.h>

using namespace vivid;
using namespace vivid::effects;

static Chain chain;

void setup(Context& ctx) {
    chain.add<Noise>("noise").scale(4.0f).speed(0.5f);
    chain.add<Blur>("blur").input("noise").radius(5.0f);
    chain.add<Output>("out").input("blur");

    chain.setOutput("out");
    chain.init(ctx);
}

void update(Context& ctx) {
    chain.get<Noise>("noise").speed(ctx.time() * 0.1f);
    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

### Execution Order

The Chain uses Kahn's algorithm for topological sort, automatically determining execution order from `.input()` dependencies. Circular dependencies are detected and reported as errors.

---

## Implementation Phases

### Phase 1: Core Setup

**Goal:** Clean slate with WebGPU foundation

- [ ] Set up Dawn via CMake FetchContent
- [ ] GLFW window creation
- [ ] WebGPU init via webgpu.h
- [ ] Surface creation
- [ ] Main loop
- [ ] Context with time/input
- [ ] Port hot-reload (remove previous engine deps)
- [ ] Texture blit pipeline
- [ ] Bitmap font for errors

**Milestone:** Window opens, shows error message when no chain loaded

**Validation:**
- [ ] Builds on macOS, Windows, Linux (CI)
- [ ] Window opens at 1280x720
- [ ] Error text displays correctly
- [ ] Hot-reload detects file changes

### Phase 2: First Addon (vivid-effects-2d)

**Goal:** Basic 2D operators working

- [ ] TextureOperator base class
- [ ] Noise operator
- [ ] SolidColor operator
- [ ] Composite operator
- [ ] Output operator (registers with core)

**Milestone:** `Noise → Output` chain displays animated noise

**Validation:**
- [ ] Noise output matches reference image (visual diff)
- [ ] Noise animates smoothly over 60 frames
- [ ] Composite blends two textures correctly
- [ ] examples/hello-noise runs on all platforms

### Phase 3: Chain API

**Goal:** Fluent API working

- [ ] Chain class
- [ ] Topological sort
- [ ] Named operator retrieval
- [ ] Auto-init/process

**Milestone:** Fluent chain syntax works

**Validation:**
- [ ] Operators execute in correct dependency order
- [ ] Circular dependency detection works
- [ ] `chain.get<T>("name")` retrieves correct operator
- [ ] examples/chain-demo runs on all platforms

### Phase 4: State Preservation

**Goal:** Feedback survives hot-reload

- [ ] OperatorState struct
- [ ] saveState/loadState
- [ ] Feedback operator

**Milestone:** Feedback buffer survives code edit

**Validation:**
- [ ] Feedback trails persist across hot-reload
- [ ] Animation phase (e.g., LFO) continues after reload
- [ ] No visual discontinuity during reload
- [ ] examples/feedback runs on all platforms

### Phase 5: 2D Effects Library

**Goal:** Port essential operators from v1/v2

**Generators (no input):**
- [ ] Noise - Fractal Perlin noise with octaves, lacunarity, persistence
- [ ] Gradient - Linear, radial, angular, diamond modes
- [ ] Shape - SDF shapes (circle, rect, star, polygon, ring)
- [ ] SolidColor - Constant color or value

**Effects (texture input):**
- [ ] Blur - Separable Gaussian with radius and passes
- [ ] HSV - Hue shift, saturation, value adjustment
- [ ] Brightness - Brightness and contrast
- [ ] Transform - Scale, rotate, translate with pivot
- [ ] Mirror - Axis mirroring and kaleidoscope
- [ ] Displacement - Texture-based UV distortion
- [ ] Edge - Sobel edge detection with threshold
- [ ] ChromaticAberration - RGB channel separation
- [ ] Pixelate - Mosaic/pixelation effect
- [ ] Feedback - Double-buffered with decay, zoom, rotate

**Compositing:**
- [ ] Composite - Two-input or multi-input (up to 8) blending
- [ ] Switch - Select between inputs by index

**Modulation (generate numeric values):**
- [ ] LFO - Sine, saw, square, triangle oscillator
- [ ] Math - Add, subtract, multiply, divide, clamp, remap
- [ ] Logic - Greater than, less than, in range, toggle

**Media (vivid-media addon):**
- [ ] ImageFile - Load PNG/JPG with hot-reload
- [ ] VideoPlayer - Video playback (HAP codec support)
- [ ] Webcam - Camera capture

**Validation:**
- [ ] Each operator has reference image test
- [ ] Extreme parameter values don't crash
- [ ] All operators work on macOS, Windows, Linux

### Phase 6: ImGui Addon

**Goal:** Chain visualizer with ImNode

- [ ] ImGui integration
- [ ] ImNode integration
- [ ] Operator preview thumbnails
- [ ] Parameter display

**Validation:**
- [ ] Chain visualizer toggles with keyboard
- [ ] Nodes display correct connections
- [ ] Thumbnails update in real-time
- [ ] UI doesn't impact rendering performance (<1ms overhead)

### Phase 7: Media Addon

**Goal:** Video playback with platform-specific backends

**Platform-Specific Approach:**
| Platform | Video Backend | Camera Backend |
|----------|---------------|----------------|
| macOS | AVFoundation | AVFoundation |
| Windows | Media Foundation | Media Foundation |
| Linux | FFmpeg | V4L2 |

**HAP Codec Support:**
Use the official HAP implementation from Vidvox: https://github.com/Vidvox/hap
- FFmpeg for container demuxing
- HAP library for GPU-accelerated DXT decompression
- Direct texture upload (no CPU decode)

**Operators:**
- [ ] VideoPlayer - Playback with play/pause/seek/loop/speed
- [ ] Webcam - Camera capture
- [ ] ImageSequence - Frame sequence playback

**Validation:**
- [ ] HAP video plays without frame drops at 60fps
- [ ] Seek works correctly (no artifacts)
- [ ] Webcam works on macOS (AVFoundation)
- [ ] Webcam works on Windows (Media Foundation)
- [ ] examples/video-demo runs on macOS and Windows

### Phase 8: 3D Addon

**Goal:** Basic 3D rendering

- [ ] Camera3D - Perspective, orbit controls
- [ ] MeshUtils - Cube, sphere, plane, cylinder, torus primitives
- [ ] PBR materials - Metallic/roughness workflow
- [ ] Lighting - Directional, point, spot lights
- [ ] Render3D operator - Procedural geometry rendering
- [ ] InstancedRender3D - GPU instancing for thousands of objects

**GPU Instancing Pattern:**
```cpp
std::vector<Instance3D> instances;
for (int i = 0; i < 1000; i++) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    instances.emplace_back(model, color, metallic, roughness);
}
ctx.drawMeshInstanced(cubeMesh, instances, camera, output);
```

**2D Instancing (vivid-effects-2d):**
```cpp
std::vector<Circle2D> circles;
for (int i = 0; i < 1000; i++) {
    circles.emplace_back(glm::vec2(x, y), radius, glm::vec4(r, g, b, a));
}
ctx.drawCircles(circles, output, clearColor);
```

**Validation:**
- [ ] 1000 instanced cubes render at 60fps
- [ ] PBR materials match reference images
- [ ] Lighting produces correct shadows/highlights
- [ ] examples/3d-instancing runs on all platforms

### Phase 9: Editor Integration

**Goal:** Chain visualizer and VS Code extension

**ImNodes Chain Visualizer (primary):**
- [ ] In-window overlay showing operator graph
- [ ] Node thumbnails showing operator output
- [ ] Connection visualization between operators
- [ ] Toggle with keyboard shortcut (e.g., Tab)

**WebSocket Server (port 9876):**
- [ ] JSON protocol for compile status and errors
- [ ] Compile error broadcast with line numbers
- [ ] Frame metadata (fps, time, operator count)

**VS Code Extension:**
- [ ] Error highlighting with compile errors
- [ ] Basic status display (connected, fps, errors)

**Validation:**
- [ ] WebSocket connects reliably
- [ ] Compile errors appear in VS Code within 1 second
- [ ] Extension reconnects after runtime restart

---

## Directory Structure

```
vivid/
├── CMakeLists.txt
├── ROADMAP.md
├── core/
│   ├── CMakeLists.txt
│   ├── include/vivid/
│   │   ├── vivid.h
│   │   ├── context.h
│   │   ├── operator.h
│   │   ├── chain.h
│   │   ├── addon.h
│   │   └── hot_reload.h
│   ├── src/
│   │   ├── main.cpp
│   │   ├── webgpu_init.cpp
│   │   ├── display.cpp
│   │   ├── bitmap_font.cpp
│   │   ├── context.cpp
│   │   ├── chain.cpp
│   │   ├── hot_reload.cpp
│   │   └── addon_registry.cpp
│   └── shaders/
│       ├── blit.wgsl
│       └── text.wgsl
│
├── addons/
│   ├── vivid-effects-2d/
│   ├── vivid-render3d/
│   ├── vivid-imgui/
│   ├── vivid-media/
│   └── vivid-audio/
│
├── external/
│   ├── dawn/
│   ├── glfw/
│   ├── glm/
│   └── imgui/
│
└── examples/
    ├── hello/chain.cpp
    ├── noise/chain.cpp
    └── feedback/chain.cpp
```

---

## Common Pitfalls

| Problem | Symptom | Solution |
|---------|---------|----------|
| Wrong vertex count | Nothing renders | Use 3 for fullscreen triangle |
| Texture format mismatch | Black or garbled output | Match surface format (BGRA8Unorm) |
| Missing uniform update | Shader has stale values | Update buffer before draw |
| No texture barrier | Flickering | Use proper resource transitions |
| Wrong coordinate space | Upside down | Flip Y in shader or vertex |

---

## Success Criteria

1. Core works standalone (window + error display)
2. Dawn backend builds and runs
3. Noise → Output chain displays
4. Hot-reload works (edit, save, see changes)
5. Feedback buffer survives reload
6. Error overlay works
7. ImGui + ImNode chain visualizer works
8. VS Code extension works

---

## Resources

- [WebGPU Specification](https://www.w3.org/TR/webgpu/)
- [WGSL Specification](https://www.w3.org/TR/WGSL/)
- [Dawn Repository](https://dawn.googlesource.com/dawn)
- [Learn WebGPU](https://eliemichel.github.io/LearnWebGPU/)
- [WebGPU Fundamentals](https://webgpufundamentals.org/)
