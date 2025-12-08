# Vivid Roadmap

A **minimal-core, addon-first creative coding framework** built on WebGPU. Plain C++ that language models can read and write—combining TouchDesigner's inspect-anywhere philosophy with the portability of text-based code.

See [docs/PHILOSOPHY.md](docs/PHILOSOPHY.md) for design goals and lessons learned.

---

## Previous Attempts: V1 and V2

Vivid V3 is the third attempt at this framework. Understanding why V1 and V2 failed is essential context for the design decisions in V3.

### Vivid V1 (2023-2024)

**Architecture:**
- ~47,000 lines of C++ code
- WebGPU via wgpu-native (Mozilla's Rust implementation)
- Custom build system with CMake + FetchContent
- VS Code extension for live preview and editing
- Plugin-based operator system with registration macros

**What Worked:**
- Hot-reload system for chain.cpp files
- 29 operators implemented: Blur, Chromatic, Composite, Displace, EdgeDetect, Feedback, Kaleidoscope, Mirror, Noise, Pixelate, Posterize, Ripple, Rotate, Scale, Threshold, Tunnel, Twirl, Vignette, Warp, Wave, Zoom, and more
- Fluent API pattern (`ctx.noise().blur().feedback()`) was intuitive
- VS Code integration provided good developer experience
- WGSL shader system was clean

**Why It Failed:**
1. **Scope creep.** Started adding 3D capabilities, skeletal animation, physics—none finished.
2. **Skeletal animation was fundamentally broken.** Spent months on bone hierarchies and skinning that never worked correctly. The code is still there but produces garbage output.
3. **Platform fragmentation.** macOS worked, Windows had driver issues, Linux was untested. Each platform needed special handling that was never completed.
4. **Plugin system became a maintenance burden.** The registration macros, parameter binding, and state management added complexity without clear benefit.
5. **WebGPU initialization was fragile.** The async adapter/device request pattern led to race conditions and hard-to-debug startup failures.

**Key Files (for reference):**
- `vivid_v1/src/operators/` - 29 operator implementations
- `vivid_v1/src/core/context.cpp` - Hot-reload and frame timing
- `vivid_v1/vscode-extension/` - VS Code integration (could be resurrected)

### Vivid V2 (2024)

**Architecture:**
- ~9,500 lines of custom C++ code
- Diligent Engine as graphics backend (abstraction over Vulkan/D3D12/Metal/OpenGL)
- CMake with FetchContent for dependencies
- PBR rendering pipeline
- GLTF model loader

**What Worked:**
- Simpler operator count (18 operators, more focused)
- GLTF loading worked well
- Basic PBR materials rendered correctly
- State preservation during hot-reload was more robust

**Why It Failed:**
1. **Shadow mapping never worked.** Spent weeks debugging shadow maps—everything was either in shadow or fully lit. The shadow cascade system was too complex for the simple use case.
2. **Diligent Engine was over-engineered for our needs.** The abstraction layer added complexity without benefit since we only targeted Vulkan. Debugging required understanding both our code AND Diligent's internals.
3. **Performance overhead.** Diligent's abstraction layer added overhead that was noticeable with feedback buffers.
4. **Dependency hell.** Diligent brought in many transitive dependencies that complicated builds.

**Key Files (for reference):**
- `vivid_v2/src/operators/` - 18 operator implementations
- `vivid_v2/src/rendering/pbr_pipeline.cpp` - PBR system (worked, could be referenced)
- `vivid_v2/src/scene/gltf_loader.cpp` - GLTF loading (worked, could be referenced)

### Lessons for V3

1. **Stay minimal.** The core should be <1000 lines. Resist adding features until the basics are rock-solid.

2. **No skeletal animation.** If 3D models are added, they're static meshes only. Skeletal animation is a rabbit hole.

3. **No shadow maps in V3.** If shadows are ever added, use simple techniques like shadow volumes or baked shadows. Cascaded shadow maps are not worth the complexity.

4. **WebGPU via wgpu-native is correct.** V1's approach was right—we just added too much on top. V3 returns to wgpu-native with a cleaner abstraction.

5. **Operators are addons, not plugins.** No registration macros, no plugin discovery. Each addon is explicitly added to the chain. Simpler is better.

6. **Test on Windows from day one.** Platform issues compound. CI should run on all platforms from Phase 1.

7. **Fluent API is worth preserving.** The `ctx.operator().operator()` pattern from V1 was the right call. V3 keeps this.

8. **State preservation is non-negotiable.** Both V1 and V2 got this right. Hot-reload without state preservation is useless for creative work.

9. **Don't use complex external engines.** Diligent was too much. Own the graphics code or use something minimal like wgpu-native directly.

---

## Guiding Principles

1. **Minimal core.** The runtime is ~600 lines: window, timing, input, hot-reload, addon registry, and a simple texture display. Everything else is an addon.

2. **WebGPU is the graphics backend.** Core and all addons are written against the standard WebGPU C API (`webgpu.h`) using wgpu-native (Mozilla's Rust-based implementation)—battle-tested in Firefox, cross-platform, actively developed. Can switch to Dawn if needed.

3. **Get to the chain API fast.** The core value of Vivid is the `chain.cpp` programming model. All infrastructure work serves this goal.

4. **LLM-friendly.** Designed for AI-assisted development:
   - Everything is plain text (C++, WGSL shaders, JSON metadata)
   - Minimal core (~600 lines) fits in context windows
   - Self-contained operators with embedded shaders (no external dependencies)
   - Consistent patterns across all operators (init/process/cleanup)
   - Fluent API makes generated code readable
   - This ROADMAP serves as comprehensive architectural documentation
   - LLM-optimized docs: `docs/LLM-REFERENCE.md` (compact operator reference), `docs/RECIPES.md` (effect examples)
   - Project template with `CLAUDE.md` for per-project AI context (`examples/template/`)
   - Doxygen API documentation: run `doxygen Doxyfile` or `make docs` to generate `docs/api/html/`

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
- vivid-media: FFmpeg, AVFoundation, [Vidvox HAP](https://github.com/Vidvox/hap) — video/image **input**
- vivid-export: FFmpeg, VideoToolbox/NVENC — video/image **output**
- vivid-audio: miniaudio, Tonic, KissFFT
- vivid-imgui: Dear ImGui, imnodes
- vivid-midi: RtMidi
- vivid-osc: oscpack
- vivid-ml: ONNX Runtime (CoreML/DirectML/CUDA acceleration)
- vivid-webserver: cpp-httplib

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

**Minimal (local development):**
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

**Full (for registry publishing):**
```json
{
  "name": "vivid-effects-2d",
  "version": "0.1.0",
  "description": "2D texture effects for Vivid",
  "license": "MIT",
  "author": {
    "name": "Your Name",
    "email": "you@example.com",
    "url": "https://yoursite.com"
  },
  "repository": "https://github.com/username/vivid-effects-2d",
  "homepage": "https://github.com/username/vivid-effects-2d#readme",
  "keywords": ["effects", "2d", "blur", "noise", "composite"],
  "vivid": ">=3.0.0",
  "platforms": ["macos", "windows", "linux"],
  "detectHeaders": ["vivid/effects/effects.h"],
  "dependencies": {
    "vivid-core": "^3.0.0"
  },
  "optionalDependencies": {
    "vivid-imgui": "^1.0.0"
  },
  "includeDirs": ["include"],
  "libraries": {
    "macos": [],
    "windows": [],
    "linux": []
  }
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

### Parameter Wrappers (`Param<T>`)

To avoid redundant parameter declarations (member variable + ParamDecl in `params()`), use the `Param<T>` wrapper class from `vivid/param.h`. It combines the value with its metadata.

**Before (redundant):**
```cpp
class Noise : public TextureOperator {
    float m_scale = 4.0f;  // 1. Member

    Noise& scale(float s) { m_scale = s; return *this; }  // 2. Setter

    std::vector<ParamDecl> params() override {
        return {
            {"scale", ParamType::Float, 0.1f, 20.0f, {m_scale}}  // 3. ParamDecl
        };
    }
};
```

**After (unified):**
```cpp
#include <vivid/param.h>

class Noise : public TextureOperator {
    Param<float> m_scale{"scale", 4.0f, 0.1f, 20.0f};  // Value + metadata together

    Noise& scale(float s) { m_scale = s; return *this; }  // Fluent setter

    std::vector<ParamDecl> params() override {
        return { m_scale.decl() };  // Trivial - just call .decl()
    }
};
```

**Available param types:**
- `Param<float>` - scalar float with min/max range
- `Param<int>` - scalar int with min/max range
- `Param<bool>` - boolean toggle
- `Vec2Param` - 2D vector (x, y) with range
- `ColorParam` - RGBA color (r, g, b, a)

**Key features:**
- Implicit conversion: `m_scale` works as a float in shader uniforms
- Self-documenting: member declaration includes name, default, and range
- Type-safe: `Param<float>` → `ParamType::Float` mapping is automatic

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

### Phase 1: Core Setup ✓

**Goal:** Clean slate with WebGPU foundation

- [x] Set up WebGPU via CMake FetchContent (wgpu-native)
- [x] GLFW window creation
- [x] WebGPU init via webgpu.h
- [x] Surface creation
- [x] Main loop
- [x] Context with time/input
- [x] Hot-reload system (clang++, dlopen)
- [x] Texture blit pipeline
- [x] Bitmap font for errors

**Milestone:** Window opens, shows error message when no chain loaded ✓

**Validation:**
- [ ] Builds on macOS, Windows, Linux (CI)
- [ ] Window opens at 1280x720
- [ ] Error text displays correctly
- [ ] Hot-reload detects file changes

**Troubleshooting Notes (wgpu-native on macOS):**

We encountered a blank/dark gray window despite all WebGPU operations appearing successful. The fixes (learned from vivid_v1):

1. **Query surface capabilities for format** - Don't hardcode `BGRA8Unorm`. Use `wgpuSurfaceGetCapabilities()` to get the preferred format (Metal on macOS wants `BGRA8UnormSrgb`):
   ```cpp
   WGPUSurfaceCapabilities capabilities = {};
   wgpuSurfaceGetCapabilities(surface, adapter, &capabilities);
   WGPUTextureFormat surfaceFormat = capabilities.formats[0];
   ```

2. **Present before releasing texture view** - Call `wgpuSurfacePresent()` BEFORE `wgpuTextureViewRelease()`:
   ```cpp
   wgpuQueueSubmit(queue, 1, &cmdBuffer);
   wgpuSurfacePresent(surface);        // Present first
   wgpuTextureViewRelease(view);       // Then release view
   ```

3. **Don't release surface texture** - The texture from `wgpuSurfaceGetCurrentTexture()` is owned by the surface. Don't call `wgpuTextureRelease()` on it.

### Phase 2: First Addon (vivid-effects-2d) ✓

**Goal:** Basic 2D operators working

- [x] TextureOperator base class
- [x] Noise operator
- [x] SolidColor operator
- [x] Composite operator
- [x] Output operator (registers with core)

**Milestone:** `Noise → Output` chain displays animated noise ✓

**Validation:**
- [ ] Noise output matches reference image (visual diff)
- [x] Noise animates smoothly over 60 frames
- [ ] Composite blends two textures correctly
- [x] examples/hello-noise runs on macOS (other platforms pending)

### Phase 3: Chain API ✓

**Goal:** Fluent API working

- [x] Chain class
- [x] Topological sort (Kahn's algorithm)
- [x] Named operator retrieval
- [x] Auto-init/process

**Milestone:** Fluent chain syntax works ✓

**Validation:**
- [x] Operators execute in correct dependency order
- [x] Circular dependency detection works
- [x] `chain.get<T>("name")` retrieves correct operator
- [x] examples/chain-demo runs on macOS (other platforms pending)

### Phase 4: State Preservation ✓

**Goal:** Feedback survives hot-reload

- [x] OperatorState struct
- [x] saveState/loadState
- [x] Feedback operator

**Milestone:** Feedback buffer survives code edit

**Validation:**
- [x] Feedback trails persist across hot-reload
- [x] Animation phase (e.g., LFO) continues after reload
- [x] No visual discontinuity during reload
- [x] examples/feedback runs on all platforms

### Phase 5: 2D Effects Library

**Goal:** Port essential operators from v1/v2

**Generators (no input):**
- [x] Noise - Fractal Perlin noise with octaves, lacunarity, persistence
- [x] Gradient - Linear, radial, angular, diamond modes
- [x] Shape - SDF shapes (circle, rect, star, polygon, ring)
- [x] SolidColor - Constant color or value

**Effects (texture input):**
- [x] Blur - Separable Gaussian with radius and passes
- [x] HSV - Hue shift, saturation, value adjustment
- [x] Brightness - Brightness and contrast
- [x] Transform - Scale, rotate, translate with pivot
- [x] Mirror - Axis mirroring and kaleidoscope
- [x] Displacement - Texture-based UV distortion
- [x] Edge - Sobel edge detection with threshold
- [x] ChromaticAberration - RGB channel separation
- [x] Pixelate - Mosaic/pixelation effect
- [x] Feedback - Double-buffered with decay, zoom, rotate
- [x] Bloom - Threshold, blur, and add-back glow effect
- [x] Tile - Texture tiling/repetition with offset and scale

**Particle Systems:**
- [x] Particles2D - 2D GPU particle system with emitters, forces, and lifetime
- [x] PointSprites - Point-based rendering with size, color, and texture

**Implementation Notes (Phase 5 Particles):**
- `ParticleRenderer` provides GPU-instanced circle and sprite rendering (ported from v1)
- Single draw call per operator regardless of particle count (up to 10,000)
- SDF antialiasing for smooth circle edges
- Dynamic instance buffer resizing for efficient memory use
- Emitter shapes: Point, Line, Ring, Disc, Rectangle
- Color modes: Solid, Gradient, Rainbow, Random
- Physics: gravity, drag, turbulence, attractors
- Texture sprite support for custom particle images

**Compositing:**
- [x] Composite - Two-input or multi-input (up to 8) blending
- [x] Switch - Select between inputs by index

**Modulation (generate numeric values):**
- [x] LFO - Sine, saw, square, triangle oscillator
- [x] Math - Add, subtract, multiply, divide, clamp, remap
- [x] Logic - Greater than, less than, in range, toggle

**Media (vivid-video addon):**
- [x] ImageFile - Load PNG/JPG with hot-reload
- [x] VideoPlayer - Video playback (HAP codec support)
- [x] Webcam - Camera capture (AVFoundation on macOS, Media Foundation on Windows)

**Canvas (Procedural Texture Generation):**
- [x] Canvas - Imperative 2D drawing with shapes and text

Draw 2D primitives to a texture for procedural graphics, liveries, UI elements:

```cpp
class Canvas : public Operator {
public:
    Canvas& size(int width, int height);
    Canvas& clear(glm::vec4 color);

    // Drawing primitives (call in update())
    void rect(float x, float y, float w, float h, glm::vec4 color);
    void rectFilled(float x, float y, float w, float h, glm::vec4 color);
    void circle(float x, float y, float radius, glm::vec4 color);
    void circleFilled(float x, float y, float radius, glm::vec4 color);
    void line(glm::vec2 a, glm::vec2 b, float width, glm::vec4 color);
    void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 color);

    // Text rendering
    void text(const std::string& str, float x, float y, Font& font, glm::vec4 color);

    // Texture operations
    void blit(WGPUTextureView src, float x, float y);
    void blit(WGPUTextureView src, float x, float y, float w, float h);
};

class Font {
public:
    static Font load(const std::string& ttfPath, float size);
    float width(const std::string& str) const;
    float height() const;
};
```

**Usage Example (Wipeout Livery Generation):**
```cpp
// Generate procedural racing livery
chain.add<Canvas>("livery").size(256, 512);
Font teamFont = Font::load("assets/fonts/racing.ttf", 48);

void setup(Context& ctx) {
    auto& canvas = chain.get<Canvas>("livery");
    canvas.clear({0.1f, 0.2f, 0.8f, 1.0f});  // FEISAR blue

    // Racing stripes
    canvas.rectFilled(0, 100, 256, 30, {1, 1, 1, 1});
    canvas.rectFilled(0, 140, 256, 10, {1, 0, 0, 1});

    // Team number
    canvas.text("07", 80, 200, teamFont, {1, 1, 1, 1});

    // Sponsor decals, panel lines, etc.
}
```

**Retro Post-Processing:**

Effects for achieving stylized/retro aesthetics:

- [x] Dither - Ordered dithering (Bayer 2x2, 4x4, 8x8)
- [x] Quantize - Reduce color palette
- [x] Scanlines - CRT-style horizontal lines
- [x] CRTEffect - Curvature, vignette, phosphor glow
- [x] Downsample - Low-res with nearest-neighbor upscale

```cpp
class Dither : public Operator {
public:
    Dither& input(const std::string& src);
    Dither& pattern(DitherPattern p);  // Bayer2x2, Bayer4x4, Bayer8x8, BlueNoise
    Dither& levels(int n);              // Color levels per channel (2-256)
    Dither& strength(float s);          // 0-1, blend with original
};

class Scanlines : public Operator {
public:
    Scanlines& input(const std::string& src);
    Scanlines& spacing(int pixels);     // Lines every N pixels
    Scanlines& thickness(float t);      // 0-1, line thickness
    Scanlines& intensity(float i);      // 0-1, darkening amount
    Scanlines& vertical(bool v);        // Vertical instead of horizontal
};

class CRTEffect : public Operator {
public:
    CRTEffect& input(const std::string& src);
    CRTEffect& curvature(float c);      // 0-1, barrel distortion
    CRTEffect& vignette(float v);       // 0-1, edge darkening
    CRTEffect& scanlines(float s);      // 0-1, scanline intensity
    CRTEffect& bloom(float b);          // 0-1, phosphor glow
    CRTEffect& chromatic(float c);      // 0-1, RGB separation
};

class Downsample : public Operator {
public:
    Downsample& input(const std::string& src);
    Downsample& resolution(int w, int h);  // Target resolution (e.g., 320x240)
    Downsample& filter(FilterMode f);       // Nearest (pixelated) or Linear
};
```

**Usage Example (PS1-style rendering):**
```cpp
// Wipeout-style retro output
chain.add<Render3D>("scene").shadingMode(ShadingMode::Flat);
chain.add<Downsample>("lowres").input("scene").resolution(480, 270).filter(FilterMode::Nearest);
chain.add<Dither>("dither").input("lowres").pattern(DitherPattern::Bayer4x4).levels(32);
chain.add<Scanlines>("scanlines").input("dither").spacing(2).intensity(0.3f);
chain.add<Output>("out").input("scanlines");
```

**Validation:**
- [ ] Each operator has reference image test
- [ ] Extreme parameter values don't crash
- [ ] All operators work on macOS, Windows, Linux
- [ ] Canvas renders text correctly with TTF fonts
- [ ] Canvas primitives are pixel-accurate
- [ ] Dither produces correct Bayer patterns
- [ ] CRTEffect curvature doesn't clip edges
- [ ] examples/retro-demo runs on all platforms
- [ ] Update README.md with new operators and usage examples

### Phase 6: ImGui Addon

**Goal:** Chain visualizer with ImNode

- [x] ImGui integration (vivid-imgui addon with WebGPU backend)
- [x] ImNode integration (demo node editor)
- [x] Operator preview thumbnails (requires chain introspection)
- [x] Parameter display (requires operator params() implementation)

**Implementation Notes:**
- Uses ImGui master branch for wgpu-native compatibility
- ImNodes for node graph visualization
- Tab key toggles visualizer overlay
- Performance overlay shows FPS, DT, resolution
- Demo node editor shows example Noise→Blur/Feedback→Output graph

**Files Created:**
- `addons/vivid-imgui/CMakeLists.txt` - Fetches ImGui/ImNodes, builds addon
- `addons/vivid-imgui/include/vivid/imgui/imgui_integration.h` - Public API
- `addons/vivid-imgui/include/vivid/imgui/chain_visualizer.h` - Visualizer class
- `addons/vivid-imgui/src/imgui_integration.cpp` - ImGui lifecycle
- `addons/vivid-imgui/src/chain_visualizer.cpp` - ImNodes rendering

**Validation:**
- [x] Chain visualizer toggles with keyboard (Tab key)
- [x] Nodes display correct connections
- [x] Thumbnails update in real-time
- [x] UI doesn't impact rendering performance (<1ms overhead)
- [x] Update README.md with UI/editor features

### Phase 6b: Operator Registry & Chain Introspection

**Goal:** Enable real chain visualization by adding operator registration and introspection

**Context API Additions:**
```cpp
// In context.h
struct OperatorInfo {
    std::string name;
    Operator* op = nullptr;
};

// Context methods:
void registerOperator(const std::string& name, Operator* op);
const std::vector<OperatorInfo>& registeredOperators() const;
void clearRegisteredOperators();  // Called on hot-reload
```

**Operator API Additions:**
```cpp
// In operator.h
virtual WGPUTextureView outputView() const { return nullptr; }  // For thumbnails
```

**Tasks:**
- [x] Add OperatorInfo struct to context.h
- [x] Add operator registry vector to Context
- [x] Implement registerOperator() / registeredOperators() / clearRegisteredOperators()
- [x] Add outputView() to Operator base class
- [x] Implement outputView() in TextureOperator
- [x] Update ChainVisualizer to use real operator data
- [x] Auto-layout nodes based on dependency depth
- [x] Draw connections by querying getInput()
- [x] Render thumbnails from outputView()
- [x] Clear registry on hot-reload (before chain reload)

**Usage Pattern (in chain.cpp):**
```cpp
void setup(Context& ctx) {
    static auto noise = std::make_unique<Noise>();
    static auto blur = std::make_unique<Blur>();
    static auto output = std::make_unique<Output>();

    blur->setInput(noise.get());
    output->setInput(blur.get());

    // Register for visualization
    ctx.registerOperator("noise", noise.get());
    ctx.registerOperator("blur", blur.get());
    ctx.registerOperator("output", output.get());

    ctx.setOutput(output.get());
}
```

**Validation:**
- [x] Registered operators appear as nodes
- [x] Connections match setInput() relationships
- [x] Thumbnails show operator output textures
- [x] Hot-reload clears and rebuilds visualization
- [x] Performance impact <1ms per frame

### Phase 6c: Core-Managed Chain Lifecycle ✓

**Goal:** Move Chain ownership from user code to core, eliminating boilerplate and enabling automatic state preservation.

**Motivation:**
- Users had to write awkward `delete chain; chain = new Chain();` in setup() for hot-reload
- Users had to remember to call `chain->init(ctx)` and `chain->process(ctx)`
- State preservation required manual `ctx.preserveStates()` / `ctx.restoreStates()` calls
- Output operator was required boilerplate

**Solution:**
Context now owns the Chain. Core automatically handles lifecycle:

```cpp
// New pattern (simple!)
void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.add<Noise>("noise").scale(4.0f);
    chain.output("noise");  // Specify output - no Output operator needed
}

void update(Context& ctx) {
    // Parameter tweaks only - process() is automatic
}

VIVID_CHAIN(setup, update)
```

**API Additions:**

```cpp
// context.h
Chain& chain();                    // Get/create the chain
void resetChain();                 // Create fresh chain (called on hot-reload)
bool hasChain() const;             // Check if chain exists

// chain.h
void output(const std::string& name);  // Specify display output
Operator* getOutput() const;           // Get output operator
```

**Core Lifecycle (main.cpp):**
1. On hot-reload: save operator states, reset chain
2. Call `setup(ctx)` - user builds chain
3. Call `chain.init(ctx)` - auto-init operators, restore states
4. Each frame: call `update(ctx)`, then `chain.process(ctx)` - auto-process

**Tasks:**
- [x] Add Chain to Context (`chain()`, `resetChain()`, `hasChain()`)
- [x] Add `chain.output()` method to Chain class
- [x] Update `Chain::process()` to set output texture via `ctx.setOutputTexture()`
- [x] Update main.cpp hot-reload loop (auto-init, auto-process, state preservation)
- [x] Migrate all examples to new pattern
- [x] Update documentation (README, LLM-REFERENCE, RECIPES)

**Backward Compatibility:**
- Output operator still works for complex use cases
- Dynamic output switching works via `chain.output()` in update()

**Validation:**
- [x] All examples work with new pattern
- [x] Hot-reload preserves Feedback buffer state
- [x] Dynamic output switching works (video-demo, webcam-retro)
- [x] Documentation updated

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
- [x] VideoPlayer - Playback with play/pause/seek/loop/speed
- [x] Webcam - Camera capture (AVFoundation on macOS, Media Foundation on Windows)
- [ ] ImageSequence - Frame sequence playback

**Validation:**
- [x] HAP video plays without frame drops at 60fps
- [x] Seek works correctly (no artifacts)
- [x] Webcam works on macOS (AVFoundation)
- [ ] Webcam works on Windows (Media Foundation) - implemented, needs testing
- [x] examples/video-demo runs on macOS
- [x] examples/webcam-retro demo created
- [ ] Update README.md with media playback documentation

### Phase 8: 3D Addon (vivid-render3d)

**Goal:** 3D rendering with procedural geometry, CSG, and multiple shading modes

#### Terminology: Geometry vs Mesh

**Geometry** is an abstract output type (`OutputKind::Geometry`) for operators that produce 3D data. **Mesh** is the concrete data structure containing vertices, normals, UVs, and indices. The distinction matters because:

- `MeshOperator` is the base class for operators that output a single `Mesh` (primitives, CSG results)
- `SceneComposer` outputs a `Scene` (collection of meshes with transforms) but still has `OutputKind::Geometry`
- The chain visualizer renders geometry previews for both, using different strategies

#### Node-Based Architecture

All 3D components are operators that appear in the chain visualizer:

```cpp
// Mesh operators (blue nodes with 3D preview)
auto& box = chain.add<Box>("box").size(1.0f);
auto& sphere = chain.add<Sphere>("sphere").radius(0.5f);
auto& csg = chain.add<Boolean>("csg").inputA(&box).inputB(&sphere).operation(BooleanOp::Subtract);

// Scene composition (blue node with full scene preview)
auto& scene = SceneComposer::create(chain, "scene");
scene.add<Box>("box1", transform1, color1);
scene.add(&csg, transform2, color2);

// Camera operator (green node with camera icon)
auto& camera = chain.add<CameraOperator>("camera")
    .orbitCenter(0, 0, 0)
    .distance(10.0f)
    .azimuthInput(&timeLfo);  // Animated orbit

// Light operators (yellow nodes with light bulb icon)
auto& sun = chain.add<DirectionalLight>("sun")
    .direction(1, 2, 1)
    .color(1.0f, 0.95f, 0.9f)
    .intensity(1.5f);

// Render3D connects everything (shows links in visualizer)
auto& render = chain.add<Render3D>("render")
    .input(&scene)         // Slot 0: scene
    .cameraInput(&camera)  // Slot 1: camera
    .lightInput(&sun);     // Slot 2: light
```

#### Step 1: Geometry & Flat Rendering (Complete)

**Status:** ✅ Complete

**Core Data Structures:**
```cpp
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color;
};

struct SceneObject {
    Mesh* mesh;
    glm::mat4 transform = glm::mat4(1.0f);
    glm::vec4 color = {1, 1, 1, 1};
};

class Scene {
public:
    Scene& add(Mesh& mesh);
    Scene& add(Mesh& mesh, const glm::mat4& transform);
    Scene& add(Mesh& mesh, const glm::mat4& transform, const glm::vec4& color);
    void clear();
    const std::vector<SceneObject>& objects() const;
};
```

**Step 1 Tasks:**
- [x] Addon scaffolding (CMakeLists.txt, addon.json, directory structure)
- [x] Mesh class with GPU upload
- [x] MeshBuilder primitives (box, sphere, cylinder, cone, torus, plane)
- [x] MeshBuilder modifiers (transform, computeNormals, computeFlatNormals)
- [x] CSG operations via Manifold (add, subtract, intersect)
- [x] Camera3D (perspective, lookAt, orbit)
- [x] CameraOperator (camera as a chainable node with animatable inputs)
- [x] LightOperators (DirectionalLight, PointLight, SpotLight as nodes)
- [x] Scene class (multiple objects with transforms)
- [x] SceneComposer operator (node-based scene building)
- [x] Render3D operator with flat.wgsl shader
- [x] Render3D.cameraInput() and lightInput() for node connections
- [x] Shading modes: Unlit, Flat, Gouraud
- [x] Chain visualizer: geometry previews, camera/light icons, connection links
- [x] examples/render3d-demo
- [x] examples/geometry-showcase
- [x] examples/geometry-pipeline

**Step 2: PBR & Materials**

Deferred to Step 2: PBR materials, IBL, glTF loading, GPU instancing, toon shading.

---

#### PBR Implementation Strategy (Step 2)

**Reference Implementation:** Port from [webgpu-native-examples](https://github.com/samdauwe/webgpu-native-examples) (Apache-2.0)
- Already C/Dawn-based
- Includes `pbr_basic.c`, `pbr_texture.c`, `pbr_ibl.c`
- Well-structured, production-tested

**Alternative References:**
- [Learn WebGPU PBR tutorial](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/lighting-and-material/pbr.html)
- [Nadrin/PBR](https://github.com/Nadrin/PBR) - Heavily commented shaders
- [Bevy WGSL PBR shaders](https://github.com/bevyengine/bevy) (MIT/Apache-2.0)

#### IBL Pipeline

**Offline Preprocessing** (ship pre-baked assets):
1. **BRDF LUT** - Generate once with [BRDFGenerator](https://github.com/HectorMF/BRDFGenerator)
   - 512x512 16-bit float texture
   - Ship as `assets/brdf_lut.ktx`
2. **Environment Maps** - Bake with [IBLBaker](https://github.com/derkreature/IBLBaker)
   - Diffuse irradiance cubemap
   - Specular prefilter cubemap (multiple mip levels for roughness)
   - Ship default HDRI with addon

**Split-Sum Approximation** (Epic/UE4 approach):
```wgsl
// PBR shader sampling
let prefiltered = textureSampleLevel(envMap, sampler, R, roughness * maxMipLevel);
let irradiance = textureSample(irradianceMap, sampler, N);
let brdf = textureSample(brdfLUT, sampler, vec2(NdotV, roughness));
let specular = prefiltered * (F * brdf.x + brdf.y);
let diffuse = irradiance * albedo * (1.0 - metallic);
```

#### Tasks

**Core PBR:**
- [x] PBR WGSL shaders (Cook-Torrance BRDF, GGX distribution, Schlick-GGX geometry, Fresnel)
- [x] PBRMaterial struct (baseColor, metallic, roughness, normal, ao, emissive)
- [x] TexturedMaterial class with texture loading for all material maps
- [x] Scalar and textured PBR pipeline support
- [x] Tone mapping (Reinhard) and gamma correction
- [x] BRDF LUT generation (runtime compute shader in IBLEnvironment)

**Lighting:**
- [x] Directional light (sun) with operator
- [ ] Point lights (up to 4) - class defined, shader implementation pending
- [ ] Spot lights - class defined, shader implementation pending
- [x] Environment map sampling (IBL) - compute shaders for irradiance/radiance, IBLEnvironment class
- [x] Ambient occlusion (sampled from AO texture map)

**Geometry (completed in Step 1):**
- [x] Camera3D (perspective, orbit controls)
- [x] MeshUtils (cube, sphere, plane, cylinder, torus)
- [x] Vertex format (position, normal, tangent, uv, color)
- [x] Tangent generation for normal mapping
- [x] MeshBuilder - Procedural mesh construction

**Procedural Mesh Generation:**

Build geometry from code for procedural content:

```cpp
class MeshBuilder {
public:
    // Vertex data
    MeshBuilder& addVertex(glm::vec3 position);
    MeshBuilder& addVertex(glm::vec3 position, glm::vec3 normal);
    MeshBuilder& addVertex(glm::vec3 position, glm::vec3 normal, glm::vec2 uv);
    MeshBuilder& addVertex(glm::vec3 position, glm::vec3 normal, glm::vec2 uv, glm::vec4 color);

    // Face construction
    MeshBuilder& addTriangle(uint32_t a, uint32_t b, uint32_t c);
    MeshBuilder& addQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d);

    // Modifiers
    MeshBuilder& computeNormals();                  // Calculate from faces
    MeshBuilder& computeFlatNormals();              // Per-face normals (faceted look)
    MeshBuilder& computeTangents();                 // For normal mapping
    MeshBuilder& mirror(Axis axis);                 // X/Y/Z symmetry
    MeshBuilder& transform(const glm::mat4& matrix);

    // Build
    Mesh build();
    void clear();

    // Helpers for common shapes
    static MeshBuilder box(float w, float h, float d);
    static MeshBuilder cylinder(float radius, float height, int segments);
    static MeshBuilder cone(float radius, float height, int segments);
    static MeshBuilder sphere(float radius, int segments);
    static MeshBuilder torus(float outerRadius, float innerRadius, int segments);

    // CSG Boolean Operations (via Manifold library)
    MeshBuilder& add(const MeshBuilder& other);       // Union
    MeshBuilder& subtract(const MeshBuilder& other);  // Difference
    MeshBuilder& intersect(const MeshBuilder& other); // Intersection
};
```

**CSG Boolean Operations:**

For complex procedural geometry, use [Manifold](https://github.com/elalish/manifold) for robust boolean mesh operations:

| Library | Purpose | License |
|---------|---------|---------|
| [Manifold](https://github.com/elalish/manifold) | Boolean mesh operations | Apache-2.0 |

```cpp
// Create complex shapes via CSG
MeshBuilder fuselage = MeshBuilder::box(0.5f, 0.3f, 3.0f);
MeshBuilder cockpit = MeshBuilder::sphere(0.25f, 12);
cockpit.transform(glm::translate(glm::mat4(1), {0, 0.1f, 0.8f}));

MeshBuilder engineCutout = MeshBuilder::cylinder(0.15f, 1.0f, 8);
engineCutout.transform(glm::translate(glm::mat4(1), {0, 0, -1.2f}));

// Boolean operations
MeshBuilder craft = fuselage;
craft.add(cockpit);           // Union: add cockpit dome
craft.subtract(engineCutout); // Difference: hollow out engine bay

// Side pods with intake scoops
MeshBuilder pod = MeshBuilder::cylinder(0.12f, 0.8f, 6);
MeshBuilder scoop = MeshBuilder::box(0.1f, 0.08f, 0.3f);
scoop.transform(glm::translate(glm::mat4(1), {0.08f, 0, 0.2f}));
pod.subtract(scoop);  // Carve intake

pod.transform(glm::translate(glm::mat4(1), {0.4f, 0, 0}));
craft.add(pod);
craft.mirror(Axis::X);  // Symmetric pods

Mesh finalMesh = craft.build();
```

**Usage Example (Wipeout Craft Generation):**
```cpp
// Procedurally generate anti-gravity racing craft
MeshBuilder craft;

// Fuselage - elongated hexagonal prism
for (int i = 0; i < 10; i++) {
    float z = -2.0f + i * 0.4f;
    float scale = 1.0f - abs(i - 5) * 0.1f;  // Taper at ends
    for (int j = 0; j < 6; j++) {
        float angle = j * glm::pi<float>() / 3.0f;
        craft.addVertex({cos(angle) * scale * 0.3f, sin(angle) * scale * 0.2f, z});
    }
}
// Add faces connecting segments...

// Side pods
MeshBuilder pod = MeshBuilder::cylinder(0.15f, 0.8f, 6);
pod.transform(glm::translate(glm::mat4(1), {0.5f, 0, 0}));
// Merge pods into craft...

craft.mirror(Axis::X);           // Symmetry
craft.computeFlatNormals();      // Faceted PS1 look

Mesh vehicleMesh = craft.build();
```

**Integration:**
- [x] Render3D operator with PBR, Flat, Gouraud, Unlit shading modes
- [ ] InstancedRender3D - GPU instancing for thousands of objects
- [ ] GLTFLoader (cgltf + stb_image)
- [ ] Default HDRI environment
- [ ] Particles3D - 3D GPU particle system with world-space physics
- [ ] PointSprites3D - 3D point-based rendering with billboarding

**Non-PBR Shading Modes:**

For stylized/retro rendering that doesn't use physically-based materials:

```cpp
enum class ShadingMode {
    PBR,           // Full PBR with IBL (default)
    Flat,          // Flat shading, per-face normals, no smoothing
    VertexLit,     // Simple N·L diffuse, vertex colors
    Unlit,         // Texture/color only, no lighting
    Toon,          // Quantized lighting ramps (cel-shading)
    Gouraud        // Per-vertex lighting (PS1-style)
};

class Render3D : public Operator {
public:
    // Existing...
    Render3D& shadingMode(ShadingMode mode);

    // For Toon shading
    Render3D& toonLevels(int levels);            // 2-8 shading bands
    Render3D& toonEdge(bool enabled);            // Outline effect
    Render3D& toonEdgeColor(glm::vec4 color);
    Render3D& toonEdgeWidth(float width);

    // Debug visualization
    Render3D& wireframe(bool enabled);
    Render3D& wireframeColor(glm::vec4 color);
    Render3D& wireframeWidth(float width);
    Render3D& showNormals(bool enabled);
    Render3D& normalsLength(float length);
};
```

**Flat/Gouraud Shader Implementation:**
```wgsl
// shaders/flat.wgsl - PS1-style vertex lighting
@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = uniforms.mvp * vec4f(input.position, 1.0);

    // Per-vertex lighting (Gouraud)
    let worldNormal = normalize((uniforms.model * vec4f(input.normal, 0.0)).xyz);
    let NdotL = max(dot(worldNormal, uniforms.lightDir), 0.0);
    output.lighting = uniforms.ambient + NdotL * uniforms.lightColor;

    output.uv = input.uv;
    output.color = input.color;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let albedo = textureSample(diffuseTexture, texSampler, input.uv);
    return vec4f(albedo.rgb * input.lighting * input.color.rgb, albedo.a);
}
```

**Usage Example (Wipeout-style Rendering):**
```cpp
chain.add<Render3D>("craft")
    .mesh(vehicleMesh)
    .texture(liveryTexture)              // From Canvas operator
    .shadingMode(ShadingMode::Flat)      // Faceted PS1 look
    .wireframe(false);

// Debug mode
if (showWireframe) {
    chain.get<Render3D>("craft")
        .wireframe(true)
        .wireframeColor({0, 1, 0, 1});
}
```

#### Shader Structure

```
addons/vivid-render3d/shaders/
├── pbr.wgsl           # Main PBR fragment shader
├── pbr_vertex.wgsl    # Vertex transformation
├── ibl.wgsl           # IBL sampling functions
├── brdf.wgsl          # GGX, Fresnel, geometry functions
├── tonemap.wgsl       # ACES/Reinhard tonemapping
└── common.wgsl        # Shared math utilities
```

#### Pre-baked Assets

```
addons/vivid-render3d/assets/
├── brdf_lut.ktx           # 512x512 BRDF lookup table
└── env/
    ├── default_irradiance.ktx   # Diffuse IBL
    └── default_prefilter.ktx    # Specular IBL (with mips)
```

#### GPU Instancing

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
- [x] PBR materials render correctly (Cook-Torrance BRDF working)
- [x] IBL produces correct reflections (examples/ibl-demo)
- [ ] GLTF models load with correct materials
- [x] MeshBuilder produces valid geometry
- [x] CSG union/subtract/intersect produce watertight meshes
- [x] Flat shading shows faceted surfaces
- [x] Smooth shading with proper normals (cylinder caps stay flat, sides smooth)
- [x] Wireframe renders correctly
- [ ] Toon shading produces clean bands
- [ ] examples/3d-instancing runs on all platforms
- [ ] examples/pbr-spheres runs on all platforms
- [ ] examples/procedural-mesh runs on all platforms
- [ ] examples/csg-demo runs on all platforms
- [ ] examples/wipeout-craft runs on all platforms
- [ ] Update README.md with 3D rendering documentation

### Phase 9: Editor Integration

**Goal:** Chain visualizer and VS Code extension

#### ImNodes Chain Visualizer (Complete)

**Status:** ✅ Core functionality complete

The chain visualizer is an ImNodes-based overlay that shows the operator graph with live previews.

**Node Colors by OutputKind:**
| OutputKind | Title Bar Color | Description |
|------------|-----------------|-------------|
| Texture | Default gray | 2D texture operators (Noise, Blur, etc.) |
| Geometry | Blue | 3D mesh/scene operators (Box, Boolean, SceneComposer) |
| Value | Orange | Numeric value operators (LFO, Math, Logic) |
| Camera | Green | Camera operators (CameraOperator) |
| Light | Yellow | Light operators (DirectionalLight, PointLight, SpotLight) |

**Thumbnails by OutputKind:**
- **Texture**: Live texture preview (100x56 pixels)
- **Geometry**: Rotating 3D preview (single mesh or full scene)
- **Value**: "Value" label in colored box
- **Camera**: Camera icon (body + lens + viewfinder)
- **Light**: Light bulb icon with rays

**Features:**
- [x] In-window overlay showing operator graph
- [x] Node thumbnails showing operator output
- [x] Connection visualization between operators
- [x] Toggle with Tab key
- [x] Automatic graph layout by depth
- [x] Parameter display within nodes
- [x] Performance overlay (FPS, DT, operator count)

**WebSocket Server (Future):**
- [ ] JSON protocol for compile status and errors
- [ ] Compile error broadcast with line numbers
- [ ] Frame metadata (fps, time, operator count)

**VS Code Extension (Future):**
- [ ] Error highlighting with compile errors
- [ ] Basic status display (connected, fps, errors)

**Validation:**
- [x] Chain visualizer displays all operators
- [x] Geometry previews render correctly
- [x] Camera/Light nodes show icons
- [x] Connections drawn between linked operators

### Phase 10: Audio Addon

**Goal:** Audio analysis and synthesis

**Libraries:**
| Library | Purpose | License |
|---------|---------|---------|
| [miniaudio](https://miniaud.io/) | Audio I/O (playback, capture) | Public Domain |
| [Tonic](https://github.com/TonicAudio/Tonic) | Synthesis (oscillators, filters) | Unlicense |
| [KissFFT](https://github.com/mborgerding/kissfft) | FFT analysis | BSD |

**Audio Analysis Operators:**
- [ ] AudioIn - Capture from microphone/line-in
- [ ] FFT - Frequency spectrum analysis
- [ ] BandSplit - Bass/mids/highs extraction
- [ ] BeatDetect - Onset and tempo detection
- [ ] AudioFile - Load and analyze audio files

**Audio Synthesis Operators:**
- [ ] Oscillator - Sine, saw, square, triangle waveforms
- [ ] Envelope - ADSR envelope generator
- [ ] Filter - Lowpass, highpass, bandpass
- [ ] Synth - Combined oscillator + envelope + filter
- [ ] AudioOut - Send audio to speakers

**Usage Example:**
```cpp
// Audio-reactive visuals
chain.add<AudioIn>("mic");
chain.add<FFT>("fft").input("mic");
chain.add<BandSplit>("bands").input("fft");

// Use bass level to drive visual effect
float bass = chain.get<BandSplit>("bands").bass();
chain.get<Noise>("noise").scale(4.0f + bass * 10.0f);
```

**Sample Playback Operators:**
- [ ] SampleBank - Load folder of WAV/MP3 files
- [ ] SamplePlayer - Trigger samples with polyphony

```cpp
class SampleBank : public Operator {
public:
    SampleBank& folder(const std::string& path);  // Load all audio files
    SampleBank& polyphony(int voices);             // Max simultaneous plays (default: 16)

    int count() const;                             // Number of loaded samples
    std::string name(int index) const;             // Sample filename
};

class SamplePlayer : public Operator {
public:
    SamplePlayer& bank(const std::string& bankName);

    void trigger(int index);                       // Play sample once
    void trigger(int index, float volume);         // With volume (0-1)
    void trigger(int index, float volume, float pan); // With pan (-1 to 1)
    void triggerLoop(int index);                   // Loop until stopped
    void stop(int index);                          // Stop specific sample
    void stopAll();                                // Stop all playing samples

    bool isPlaying(int index) const;
};
```

**Audio Effect Operators:**
- [ ] AudioFilter - Real-time filter on audio stream
- [ ] AudioMixer - Mix multiple audio sources

```cpp
class AudioFilter : public Operator {
public:
    AudioFilter& input(const std::string& source);
    AudioFilter& lowpass(float cutoffHz);          // 20-20000 Hz
    AudioFilter& highpass(float cutoffHz);
    AudioFilter& bandpass(float centerHz, float bandwidth);
    AudioFilter& resonance(float q);               // 0.1-10
};

class AudioMixer : public Operator {
public:
    AudioMixer& input(const std::string& source, float volume = 1.0f);
    AudioMixer& master(float volume);
};
```

**Usage Example (Installation with triggered samples):**
```cpp
// Musical Bodies - zone-triggered audio
chain.add<SampleBank>("sounds").folder("assets/audio/zones/");
chain.add<SamplePlayer>("player").bank("sounds");
chain.add<AudioFilter>("filter").input("player");
chain.add<AudioOut>("out").input("filter");

void update(Context& ctx) {
    // When person enters zone 5
    if (zoneEntered[5]) {
        chain.get<SamplePlayer>("player").trigger(5);
    }

    // Zone slice controls filter cutoff (0-1 mapped to 200-8000 Hz)
    float cutoff = 200.0f + zoneSlice * 7800.0f;
    chain.get<AudioFilter>("filter").lowpass(cutoff);
}
```

**Validation:**
- [ ] AudioIn captures from default device
- [ ] FFT produces correct frequency bins
- [ ] BeatDetect triggers on transients
- [ ] Oscillator produces clean audio at 44.1kHz
- [ ] SampleBank loads WAV/MP3 files from folder
- [ ] SamplePlayer triggers with <5ms latency
- [ ] AudioFilter applies HPF/LPF in real-time
- [ ] examples/audio-reactive runs on all platforms
- [ ] examples/sample-trigger runs on all platforms
- [ ] Update README.md with audio features documentation

### Phase 11: MIDI & OSC Addon

**Goal:** Hardware controller and network protocol support

**Libraries:**
| Library | Purpose | License |
|---------|---------|---------|
| [RtMidi](https://github.com/thestk/rtmidi) | MIDI I/O | MIT |
| [oscpack](https://code.google.com/archive/p/oscpack/) | OSC protocol | Public Domain |

**MIDI Operators:**
```cpp
class MidiIn : public Operator {
public:
    MidiIn& port(const std::string& name);  // Device name
    MidiIn& channel(int ch);                 // 1-16, or 0 for all

    // Outputs: "noteOn", "noteOff", "velocity", "cc:1", "pitchBend"
};

class MidiOut : public Operator {
public:
    MidiOut& port(const std::string& name);
    void noteOn(int note, int velocity);
    void controlChange(int cc, int value);
};
```

**OSC Operators:**
```cpp
class OscIn : public Operator {
public:
    OscIn& port(int port);                     // UDP port
    OscIn& address(const std::string& pattern); // e.g., "/audio/*"
};

class OscOut : public Operator {
public:
    OscOut& host(const std::string& hostname);
    OscOut& port(int port);
    void send(const std::string& address, float value);
};
```

**Tasks:**
- [ ] MidiIn - Receive MIDI notes, CC, pitch bend
- [ ] MidiOut - Send MIDI messages
- [ ] MidiLearn - Map CC to parameters automatically
- [ ] OscIn - Receive OSC messages (UDP)
- [ ] OscOut - Send OSC messages
- [ ] Device enumeration for MIDI ports

**Validation:**
- [ ] MidiIn receives from hardware controller
- [ ] CC values update operator parameters
- [ ] OscIn receives from TouchOSC/similar
- [ ] examples/midi-control runs correctly

### Phase 11b: Network Addon (vivid-network)

**Goal:** Raw UDP/TCP networking for custom protocols and hardware integration

**Library:** Platform sockets (no external dependency) or [asio](https://think-async.com/Asio/) (header-only)

**Use Cases:**
- Quanergy Q-Dome / Qortex (LIDAR zone tracking)
- Custom hardware protocols
- Inter-application communication
- Artnet/sACN DMX (lighting control)

**Network Operators:**

```cpp
class UdpIn : public Operator {
public:
    UdpIn& port(int port);                         // Listen port
    UdpIn& bufferSize(int bytes);                  // Max packet size (default: 65535)

    bool hasData() const;                          // New data available this frame
    const std::vector<uint8_t>& data() const;      // Raw packet bytes
    size_t size() const;                           // Packet size

    // Convenience for common formats
    std::string asString() const;
    std::vector<float> asFloats() const;           // Interpret as float array
};

class UdpOut : public Operator {
public:
    UdpOut& host(const std::string& hostname);
    UdpOut& port(int port);

    void send(const void* data, size_t size);
    void send(const std::string& message);
    void send(const std::vector<float>& values);
};

class TcpClient : public Operator {
public:
    TcpClient& host(const std::string& hostname);
    TcpClient& port(int port);
    TcpClient& reconnect(bool enabled);            // Auto-reconnect on disconnect

    bool connected() const;
    void send(const void* data, size_t size);
    bool hasData() const;
    const std::vector<uint8_t>& data() const;
};
```

**Artnet/DMX Support:**
```cpp
class ArtnetIn : public Operator {
public:
    ArtnetIn& universe(int universe);              // 0-32767

    uint8_t channel(int ch) const;                 // Get DMX channel (1-512)
    float channelNorm(int ch) const;               // Normalized 0-1
};

class ArtnetOut : public Operator {
public:
    ArtnetOut& host(const std::string& hostname);
    ArtnetOut& universe(int universe);

    void channel(int ch, uint8_t value);
    void channels(int start, const std::vector<uint8_t>& values);
};
```

**Usage Example (Quanergy Q-Dome Integration):**
```cpp
// Musical Bodies - receive zone occupancy from Qortex
chain.add<UdpIn>("qortex").port(5000);

void update(Context& ctx) {
    auto& udp = chain.get<UdpIn>("qortex");

    if (udp.hasData()) {
        // Parse Qortex zone data (60 zones, each with occupancy count)
        auto data = udp.data();
        for (int zone = 0; zone < 60; zone++) {
            int occupancy = data[zone];  // People count in zone
            if (occupancy > 0 && !zoneWasOccupied[zone]) {
                // Person entered zone - trigger sample
                chain.get<SamplePlayer>("player").trigger(zone % sampleCount);
                zoneGlow[zone] = 1.0f;
            }
            zoneWasOccupied[zone] = occupancy > 0;
        }
    }
}
```

**Validation:**
- [ ] UdpIn receives packets on specified port
- [ ] UdpOut sends to remote host
- [ ] TcpClient connects and reconnects
- [ ] ArtnetIn receives DMX universes
- [ ] <1ms latency from packet receive to data available
- [ ] examples/udp-receiver runs correctly
- [ ] examples/artnet-control runs correctly

### Phase 12: Machine Learning Addon (ONNX)

**Goal:** ML inference for creative applications

**Library:** [ONNX Runtime](https://onnxruntime.ai/) with platform-specific acceleration

| Platform | Accelerator |
|----------|-------------|
| macOS | CoreML |
| Windows | DirectML |
| Linux | CUDA (optional) |

**Core Operator:**
```cpp
class ONNXModel : public Operator {
public:
    ONNXModel& model(const std::string& path);  // .onnx file
    ONNXModel& input(const std::string& node);  // Texture input

    // Output: inference result as texture or values
};
```

**Specialized Operators:**
- [ ] PoseDetector - MoveNet skeleton tracking
- [ ] SegmentMask - Background/person segmentation
- [ ] StyleTransfer - Neural style transfer
- [ ] DepthEstimate - Monocular depth estimation

**MoveNet Body Tracking Example:**

MoveNet Lightning detects 17 body keypoints in real-time. Download the ONNX model from TensorFlow Hub.

```cpp
// examples/movenet-tracking/chain.cpp
#include <vivid/vivid.h>
#include <vivid/media/webcam.h>
#include <vivid/ml/pose_detector.h>
#include <vivid/effects/operators.h>

using namespace vivid;

static Chain chain;

void setup(Context& ctx) {
    // Webcam input
    chain.add<Webcam>("cam").resolution(640, 480);

    // MoveNet pose detection (Lightning = fast, Thunder = accurate)
    chain.add<PoseDetector>("pose")
        .input("cam")
        .model("assets/models/movenet_lightning.onnx");

    // Visualize skeleton on top of camera feed
    chain.add<Composite>("out")
        .a("cam")
        .b("pose");  // PoseDetector outputs skeleton overlay

    chain.setOutput("out");
    chain.init(ctx);
}

void update(Context& ctx) {
    chain.process(ctx);

    // Access individual keypoints (normalized 0-1 coordinates)
    auto& pose = chain.get<PoseDetector>("pose");

    if (pose.detected()) {
        // 17 keypoints: nose, eyes, ears, shoulders, elbows, wrists,
        //               hips, knees, ankles
        glm::vec2 nose = pose.keypoint(PoseDetector::Nose);
        glm::vec2 leftWrist = pose.keypoint(PoseDetector::LeftWrist);
        glm::vec2 rightWrist = pose.keypoint(PoseDetector::RightWrist);

        float confidence = pose.confidence(PoseDetector::Nose);

        // Use keypoints to drive effects
        if (confidence > 0.5f) {
            float handDistance = glm::distance(leftWrist, rightWrist);
            chain.get<Noise>("effect").scale(handDistance * 20.0f);
        }
    }
}

VIVID_CHAIN(setup, update)
```

**PoseDetector Keypoint Enum:**
```cpp
enum Keypoint {
    Nose = 0,
    LeftEye, RightEye,
    LeftEar, RightEar,
    LeftShoulder, RightShoulder,
    LeftElbow, RightElbow,
    LeftWrist, RightWrist,
    LeftHip, RightHip,
    LeftKnee, RightKnee,
    LeftAnkle, RightAnkle
};
```

**Tasks:**
- [ ] ONNX Runtime integration
- [ ] GPU acceleration per platform (CoreML/DirectML/CUDA)
- [ ] Texture→tensor conversion (NHWC format, 192x192 or 256x256)
- [ ] Tensor→keypoint parsing (17 points × 3 values: x, y, confidence)
- [ ] Skeleton overlay rendering
- [ ] Model hot-reload
- [ ] Bundle MoveNet Lightning ONNX model

**Validation:**
- [ ] MoveNet detects 17 keypoints from webcam
- [ ] Inference runs at >30fps on integrated GPU
- [ ] Keypoint coordinates are correctly normalized (0-1)
- [ ] Skeleton overlay draws correctly
- [ ] examples/movenet-tracking runs on macOS and Windows

### Phase 13: Web Server Addon

**Goal:** Remote control and web-based UI

**Library:** [cpp-httplib](https://github.com/yhirose/cpp-httplib) (header-only HTTP/HTTPS)

**Features:**
- [ ] Serve static files (HTML/CSS/JS)
- [ ] REST API for parameter control
- [ ] WebSocket for real-time updates
- [ ] Thumbnail streaming (JPEG)

**WebServer Operator:**
```cpp
class WebServer : public Operator {
public:
    WebServer& port(int port);              // Default: 8080
    WebServer& staticDir(const std::string& path);

    // Automatically exposes:
    // GET  /api/operators     - List all operators
    // GET  /api/operator/:id  - Get operator params
    // POST /api/operator/:id  - Set operator params
    // WS   /ws                - Real-time updates
};
```

**Usage:**
```cpp
void setup(Chain& chain, Context& ctx) {
    chain.add<WebServer>("web").port(8080).staticDir("web/");
    // Access at http://localhost:8080
}
```

**Web Interface:**
- Parameter sliders for all operators
- Live preview thumbnails
- Chain graph visualization
- Mobile-friendly for TouchOSC alternative

**Validation:**
- [ ] Static files served correctly
- [ ] Parameter changes via REST work
- [ ] WebSocket broadcasts frame updates
- [ ] examples/web-control runs and serves UI

### Phase 14: Advanced Window & Input

**Goal:** Professional output control for installations and performances

**Window Management:**
- [ ] Fullscreen toggle (F11 or programmatic)
- [ ] Borderless window mode (frameless)
- [ ] Multi-monitor support (select display by index)
- [ ] Span displays (single window across multiple monitors)
- [ ] Multi-window support (secondary output windows)
- [ ] Window positioning and sizing API
- [ ] Always-on-top mode
- [ ] Cursor visibility control

**Input Handling:**
- [ ] Mouse: position, buttons, drag, scroll, delta
- [ ] Keyboard: key states, text input
- [ ] Gamepad: axes, buttons, triggers (via GLFW)
- [ ] Touch input (where supported)

**Context API:**
```cpp
void update(Context& ctx) {
    // Window control
    if (ctx.wasKeyPressed(Key::F)) {
        ctx.toggleFullscreen();
    }
    if (ctx.wasKeyPressed(Key::B)) {
        ctx.setBorderless(!ctx.isBorderless());
    }
    if (ctx.wasKeyPressed(Key::M)) {
        ctx.moveToMonitor(1);  // Move to second display
    }

    // Multi-window (for projection mapping, LED walls, etc.)
    if (!ctx.hasWindow("output2")) {
        ctx.createWindow("output2", 1920, 1080)
            .monitor(1)
            .fullscreen(true);
    }
    ctx.setWindowTexture("output2", chain.get<Output>("led").texture());

    // Input
    glm::vec2 mouse = ctx.mousePosition();
    float scroll = ctx.scrollDelta().y;

    if (ctx.isGamepadConnected(0)) {
        float leftX = ctx.gamepadAxis(0, GamepadAxis::LeftX);
        bool aButton = ctx.gamepadButton(0, GamepadButton::A);
    }
}
```

**Multi-Window Architecture:**
```
┌─────────────────┐     ┌─────────────────┐
│  Main Window    │     │  Output Window  │
│  (Editor view)  │     │  (Fullscreen)   │
│                 │     │                 │
│  Chain + ImGui  │     │  Clean output   │
│  visualizer     │     │  for projection │
└─────────────────┘     └─────────────────┘
        │                       │
        └───────────┬───────────┘
                    │
            ┌───────┴───────┐
            │   Vivid Core  │
            │   (shared)    │
            └───────────────┘
```

**Projector Edge Blending:**

For seamless multi-projector installations, edge blending creates soft overlapping regions:

```cpp
struct EdgeBlend {
    float left = 0.0f;      // 0-0.5 (percentage of width)
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    float gamma = 2.2f;     // Blend curve gamma
    BlendCurve curve = BlendCurve::Linear;  // Linear, Cosine, Smoothstep
};

// Per-window edge blend configuration
ctx.createWindow("proj1", 1920, 1080)
    .monitor(0)
    .fullscreen(true)
    .edgeBlend({.right = 0.15f});  // 15% blend on right edge

ctx.createWindow("proj2", 1920, 1080)
    .monitor(1)
    .fullscreen(true)
    .edgeBlend({.left = 0.15f, .right = 0.15f});  // Both edges

ctx.createWindow("proj3", 1920, 1080)
    .monitor(2)
    .fullscreen(true)
    .edgeBlend({.left = 0.15f});  // 15% blend on left edge
```

**Edge Blend Implementation:**
```wgsl
// shaders/edge_blend.wgsl
fn edgeBlendAlpha(uv: vec2f, blend: EdgeBlendParams) -> f32 {
    var alpha = 1.0;

    // Left edge fade
    if (uv.x < blend.left && blend.left > 0.0) {
        let t = uv.x / blend.left;
        alpha *= pow(t, blend.gamma);
    }

    // Right edge fade
    if (uv.x > 1.0 - blend.right && blend.right > 0.0) {
        let t = (1.0 - uv.x) / blend.right;
        alpha *= pow(t, blend.gamma);
    }

    // Top/bottom similar...
    return alpha;
}
```

**Span Mode (Single Texture Across Displays):**
```cpp
// Render one large texture, split across projectors
ctx.setSpanMode({
    .displays = {0, 1, 2},           // Which monitors
    .totalWidth = 5760,               // Combined resolution
    .totalHeight = 1080,
    .overlap = 288,                   // Pixels of overlap (15% of 1920)
    .edgeBlend = {.gamma = 2.2f}
});

// Chain renders to 5760x1080, automatically split and blended
chain.setOutput("out");
```

**Use Cases:**
- **Projection mapping**: Different content per projector
- **LED walls**: Output to specific display
- **VJ performance**: Preview on laptop, output on projector
- **Installations**: Borderless fullscreen on dedicated display
- **Multi-projector blending**: Seamless panoramic displays

**Validation:**
- [ ] Fullscreen works on macOS, Windows, Linux
- [ ] Borderless mode removes window chrome
- [ ] Multi-monitor enumeration returns correct displays
- [ ] Secondary window renders independently
- [ ] Gamepad input works (tested with Xbox controller)
- [ ] Edge blend produces smooth gradients
- [ ] Span mode correctly splits texture across displays
- [ ] examples/triple-projector runs with edge blending

### Phase 15: Export Addon & CLI

**Goal:** Record video output, render offline, build standalone apps

**Addon:** `vivid-export` (separate from vivid-media)

| Addon | Purpose | Dependency |
|-------|---------|------------|
| vivid-media | Video/image **input** (playback, webcam) | FFmpeg, AVFoundation, HAP |
| vivid-export | Video/image **output** (recording, sequences) | FFmpeg, VideoToolbox/NVENC |

Both share FFmpeg but are independent—you can record without playback, or play without recording.

**CLI Commands:**
```bash
vivid new my-project              # Create new project (interactive)
vivid new my-project --minimal    # Minimal project (no prompts)
vivid new my-project --template audio-reactive  # Use specific template
vivid my-project                  # Run project
vivid my-project --record         # Record to video file
vivid my-project --record out.mov # Record to specific file
vivid my-project --headless       # Run without window (CI/batch)
vivid my-project --frames 300     # Render 300 frames then exit
vivid my-project --fps 60         # Lock to specific framerate
vivid export --standalone         # Build standalone app
```

**Interactive Project Creation (`vivid new`):**

```
$ vivid new my-project

  ╭─────────────────────────────────────────────────────╮
  │                                                     │
  │   🎨  Welcome to Vivid v3.0                         │
  │       Let's create your project!                    │
  │                                                     │
  ╰─────────────────────────────────────────────────────╯

? Project name: my-project
? Description: My awesome visual project

? What type of project are you creating?
  ❯ 2D Effects (noise, blur, feedback, compositing)
    3D Scene (PBR rendering, models, lighting)
    Audio-Reactive (FFT analysis, beat detection)
    Interactive Installation (sensors, projectors, networking)
    Video Processing (playback, effects, recording)
    Minimal (just the basics)

? Select addons to include:

  Core (always included)
  ────────────────────
  ✔ vivid-effects-2d    Basic 2D effects (noise, blur, composite)

  Recommended for your project type
  ──────────────────────────────────
  ◯ vivid-audio         Audio input, FFT, beat detection
  ◯ vivid-media         Video/image loading, webcam

  3D & Rendering
  ──────────────
  ◯ vivid-render3d      PBR rendering, GLTF, instancing
  ◯ vivid-imgui         In-app UI and chain visualizer

  Input & Control
  ───────────────
  ◯ vivid-midi          MIDI controllers
  ◯ vivid-osc           OSC protocol
  ◯ vivid-network       UDP/TCP, Artnet

  Advanced
  ────────
  ◯ vivid-ml            ONNX inference, pose detection
  ◯ vivid-export        Video recording, standalone builds
  ◯ vivid-webserver     Remote control via browser

  (Use arrow keys, <space> to toggle, <a> to select all, <enter> to confirm)

? Select a starter template:
  ❯ Blank - Empty setup/update functions
    Noise Demo - Animated noise with blur
    Feedback Loop - Classic video feedback effect
    Audio Visualizer - FFT-driven graphics
    3D Orbit - Rotating 3D model with PBR

Creating project...
  ✔ Created my-project/
  ✔ Created my-project/chain.cpp
  ✔ Created my-project/vivid.json
  ✔ Created my-project/shaders/
  ✔ Created my-project/assets/
  ✔ Installing addons...
    ✔ vivid-effects-2d@1.2.0
    ✔ vivid-audio@2.0.1
  ✔ Generated my-project/vivid.lock

  ╭─────────────────────────────────────────────────────╮
  │                                                     │
  │   ✨ Project created successfully!                  │
  │                                                     │
  │   Next steps:                                       │
  │     cd my-project                                   │
  │     vivid .                                         │
  │                                                     │
  │   Edit chain.cpp to start creating!                 │
  │                                                     │
  ╰─────────────────────────────────────────────────────╯
```

**Project Type Presets:**

| Type | Default Addons | Template |
|------|---------------|----------|
| 2D Effects | effects-2d | Noise Demo |
| 3D Scene | effects-2d, render3d, imgui | 3D Orbit |
| Audio-Reactive | effects-2d, audio | Audio Visualizer |
| Interactive Installation | effects-2d, network, audio, export | Blank |
| Video Processing | effects-2d, media, export | Blank |
| Minimal | (none, core only) | Blank |

**Starter Templates:**

Templates are stored in `~/.vivid/templates/` or fetched from registry:

```
templates/
├── blank/
│   ├── chain.cpp           # Empty setup/update
│   └── template.json       # Metadata
├── noise-demo/
│   ├── chain.cpp           # Noise → Blur → Output
│   └── template.json
├── feedback-loop/
│   ├── chain.cpp           # Feedback with decay/zoom
│   └── template.json
├── audio-visualizer/
│   ├── chain.cpp           # FFT bars + beat flash
│   ├── shaders/
│   │   └── spectrum.wgsl
│   └── template.json
└── 3d-orbit/
    ├── chain.cpp           # PBR model with orbit camera
    ├── assets/
    │   └── suzanne.glb
    └── template.json
```

**Non-Interactive Flags:**

```bash
# Skip all prompts with defaults
vivid new my-project --yes

# Specify everything via flags
vivid new my-project \
  --template audio-visualizer \
  --addons vivid-audio,vivid-effects-2d,vivid-export \
  --no-install  # Don't install addons yet
```

**Custom Templates:**

Users can create and share templates:

```bash
# Save current project as template
vivid template save my-cool-template

# List available templates
vivid template list

# Install template from GitHub
vivid template install github:user/vivid-template-glitch

# Use custom template
vivid new my-project --template my-cool-template
```

**Video Recording:**
```cpp
void setup(Context& ctx) {
    // Programmatic recording control
    ctx.startRecording("output.mov", {
        .codec = VideoCodec::ProRes422,
        .fps = 60,
        .width = 1920,
        .height = 1080
    });
}

void update(Context& ctx) {
    chain.process(ctx);

    // Stop after 10 seconds
    if (ctx.time() > 10.0f) {
        ctx.stopRecording();
        ctx.quit();
    }
}
```

**Export Formats:**

| Format | Codec | Use Case | Platform |
|--------|-------|----------|----------|
| H.264 (.mp4) | libx264 | Web, general sharing | All |
| ProRes (.mov) | prores_ks | Professional editing | All |
| HAP (.mov) | hap/hapq/hapalpha | Real-time playback | All |
| PNG sequence | - | Compositing, max quality | All |
| EXR sequence | - | HDR, VFX pipelines | All |

**Platform-Specific Encoding:**

| Platform | Hardware Encoder | Fallback |
|----------|------------------|----------|
| macOS | VideoToolbox (H.264/HEVC/ProRes) | FFmpeg |
| Windows | NVENC/AMF/QuickSync | FFmpeg |
| Linux | NVENC/VAAPI | FFmpeg |

**Headless Mode (for CI/batch rendering):**
```bash
# Render 300 frames at 60fps to PNG sequence
vivid my-project --headless --frames 300 --fps 60 --output frames/

# Render to video file
vivid my-project --headless --duration 10s --output render.mp4

# CI screenshot for visual regression
vivid my-project --headless --frames 1 --output screenshot.png
```

**Standalone Export:**
```bash
# Bundle project + runtime into distributable app
vivid export --standalone --output MyApp

# Platform-specific outputs:
# macOS:   MyApp.app (signed, notarized with --sign)
# Windows: MyApp.exe (+ DLLs)
# Linux:   MyApp.AppImage
```

**Standalone Bundle Contents:**
```
MyApp.app/
├── Contents/
│   ├── MacOS/
│   │   └── vivid              # Runtime binary
│   ├── Resources/
│   │   ├── chain.dylib        # Compiled project
│   │   ├── shaders/           # WGSL shaders
│   │   └── assets/            # Project assets
│   └── Info.plist
```

**Recording API:**
```cpp
struct RecordingOptions {
    VideoCodec codec = VideoCodec::H264;
    int fps = 60;
    int width = 0;   // 0 = window size
    int height = 0;
    int bitrate = 0; // 0 = auto
    bool includeAudio = false;
};

// Context methods
void startRecording(const std::string& path, RecordingOptions opts = {});
void stopRecording();
bool isRecording() const;
int recordedFrames() const;
```

**Validation:**
- [ ] H.264 recording works at 60fps without frame drops
- [ ] ProRes produces edit-ready files
- [ ] HAP produces playable files in VJ software
- [ ] PNG sequence matches pixel-perfect with live output
- [ ] Headless mode renders correct output
- [ ] Standalone app runs without vivid installed
- [ ] `vivid new` interactive flow works correctly
- [ ] `vivid new --template` creates valid project
- [ ] `vivid new --addons` installs specified addons
- [ ] Custom templates can be saved and reused
- [ ] examples/record-demo produces valid video file

### Phase 16: Addon Registry & Package Management

**Goal:** Enable community addon discovery, installation, and publishing

**Design Principles:**
1. **Git-first**: Addons are git repositories; registry stores metadata, not code
2. **Decentralized OK**: Users can install from any git URL, not just registry
3. **Prebuilt optional**: Source-first, but allow prebuilt binaries for complex deps
4. **Semantic versioning**: npm-style version constraints (`^1.0.0`, `~1.2.3`, `>=2.0.0`)

#### CLI Commands

```bash
# Discovery
vivid addon search noise              # Search registry
vivid addon info vivid-audio          # Show addon details
vivid addon list                      # List installed addons

# Installation
vivid addon install vivid-audio       # Install from registry
vivid addon install vivid-audio@2.1.0 # Specific version
vivid addon install github:user/repo  # Install from git URL
vivid addon install ./path/to/addon   # Install local addon

# Management
vivid addon update                    # Update all addons
vivid addon update vivid-audio        # Update specific addon
vivid addon remove vivid-audio        # Uninstall addon
vivid addon outdated                  # Show available updates

# Publishing
vivid addon init                      # Create addon.json template
vivid addon validate                  # Check addon.json and structure
vivid addon publish                   # Publish to registry (requires auth)
vivid addon unpublish vivid-foo@1.0.0 # Remove specific version
```

#### Project Addon Configuration

**vivid.json** (project root):
```json
{
  "name": "my-project",
  "version": "1.0.0",
  "addons": {
    "vivid-effects-2d": "^1.0.0",
    "vivid-audio": "^2.0.0",
    "vivid-my-custom": "github:myuser/vivid-my-custom#v1.2.0"
  }
}
```

**vivid.lock** (auto-generated):
```json
{
  "lockfileVersion": 1,
  "addons": {
    "vivid-effects-2d": {
      "version": "1.2.3",
      "resolved": "https://registry.vivid.dev/vivid-effects-2d/-/1.2.3.tar.gz",
      "integrity": "sha256-...",
      "dependencies": {}
    },
    "vivid-audio": {
      "version": "2.1.0",
      "resolved": "https://registry.vivid.dev/vivid-audio/-/2.1.0.tar.gz",
      "integrity": "sha256-...",
      "dependencies": {
        "vivid-core": "^3.0.0"
      }
    }
  }
}
```

#### Addon Installation Layout

```
~/.vivid/
├── cache/                    # Downloaded addon tarballs
│   ├── vivid-audio-2.1.0.tar.gz
│   └── ...
├── addons/                   # Installed addon sources
│   ├── vivid-audio@2.1.0/
│   │   ├── addon.json
│   │   ├── include/
│   │   ├── src/
│   │   └── shaders/
│   └── ...
└── builds/                   # Compiled addon libraries (per-platform)
    ├── macos-arm64/
    │   ├── vivid-audio@2.1.0.dylib
    │   └── ...
    └── windows-x64/
        └── ...
```

**Project-local addons** (when `--save-local` used):
```
my-project/
├── chain.cpp
├── vivid.json
├── vivid.lock
└── .vivid/
    └── addons/               # Project-local installations
        └── vivid-audio@2.1.0/
```

#### Registry Architecture

**Phase 1: GitHub-based (MVP)**
- Registry is a JSON index file in a GitHub repo
- Addons hosted on GitHub/GitLab/etc.
- CLI fetches index, clones repos
- No authentication for read, GitHub auth for publish

```
vivid-registry/
├── index.json                # All addon metadata
├── addons/
│   ├── vivid-effects-2d.json # Per-addon version history
│   ├── vivid-audio.json
│   └── ...
└── schema/
    └── addon.schema.json     # JSON Schema for validation
```

**Phase 2: Dedicated Registry (Future)**
- REST API at `registry.vivid.dev`
- User accounts and API tokens
- Download statistics and ratings
- Automated security scanning
- Prebuilt binary hosting (optional)

**Registry API (Phase 2):**
```
GET  /api/v1/addons                    # List all addons
GET  /api/v1/addons/:name              # Addon metadata + versions
GET  /api/v1/addons/:name/:version     # Specific version
GET  /api/v1/addons/:name/-/:version.tar.gz  # Download tarball
POST /api/v1/addons                    # Publish new addon (auth required)
DELETE /api/v1/addons/:name/:version   # Unpublish (auth required)
GET  /api/v1/search?q=:query           # Search addons
```

#### Version Resolution

Use npm-style semver with dependency resolution:

```cpp
// Simplified resolution algorithm
1. Parse vivid.json dependencies
2. For each addon:
   a. Fetch available versions from registry
   b. Find highest version matching constraint
   c. Recursively resolve addon's dependencies
3. Detect conflicts (same addon, incompatible versions)
4. Generate vivid.lock with exact versions
5. Download and cache missing addons
```

**Conflict Resolution:**
- If addon A needs `vivid-core@^3.0.0` and addon B needs `vivid-core@^3.2.0`
  → Resolve to `vivid-core@3.2.x` (satisfies both)
- If addon A needs `vivid-core@^2.0.0` and addon B needs `vivid-core@^3.0.0`
  → Error: incompatible versions, suggest user action

#### Publishing Workflow

```bash
# 1. Create addon structure
vivid addon init my-addon
cd my-addon

# 2. Develop addon...

# 3. Validate before publishing
vivid addon validate
# ✓ addon.json valid
# ✓ Required fields present (name, version, license, author)
# ✓ Operators compile successfully
# ✓ No security issues detected

# 4. Authenticate (one-time)
vivid auth login
# Opens browser for GitHub OAuth

# 5. Publish
vivid addon publish
# Publishing my-addon@1.0.0...
# ✓ Uploaded to registry
# ✓ Available at: https://registry.vivid.dev/addons/my-addon

# 6. Update version for next release
# Edit addon.json version, then:
vivid addon publish
```

#### Validation Rules

Before publishing, addons must pass:

1. **Metadata**: Required fields in addon.json (name, version, license, author, description)
2. **Naming**: Name matches `^[a-z0-9-]+$`, starts with `vivid-` for official or unique prefix for community
3. **Structure**: Has include/, src/, or shaders/ directories
4. **Compilation**: Builds successfully on at least one platform
5. **Security**: No obvious security issues (network calls, file system access outside project)
6. **Size**: Reasonable tarball size (<50MB source, <200MB with assets)

#### Tasks

**Phase 16a: Local Package Management**
- [ ] `vivid addon install <path>` - Install local addon
- [ ] `vivid addon list` - List installed addons
- [ ] `vivid addon remove` - Uninstall addon
- [ ] vivid.json parsing and validation
- [ ] vivid.lock generation
- [ ] Addon build caching
- [ ] **Update hot-reloader for dynamic addon discovery** - Scan `#include` directives in chain.cpp or read vivid.json to find addon dependencies (replaces current hard-coded addon paths in hot_reload.cpp)

**Phase 16b: Git-based Installation**
- [ ] `vivid addon install github:user/repo` - Clone and install
- [ ] `vivid addon install <url>` - Any git URL
- [ ] Tag/branch/commit specifiers (`#v1.0.0`, `#main`, `#abc123`)
- [ ] Shallow clone for faster downloads

**Phase 16c: Registry MVP (GitHub-based)**
- [ ] Registry index format and schema
- [ ] `vivid addon search` - Search index
- [ ] `vivid addon install <name>` - Install from registry
- [ ] `vivid addon publish` - Submit to registry via PR
- [ ] GitHub Actions for index validation
- [ ] Automatic addon discovery (see below)

#### Automatic Addon Discovery

Automatically discover vivid addons on GitHub without requiring manual registration:

**Discovery Methods:**

1. **GitHub Topic**: Repos tagged with `vivid-addon` topic
   ```bash
   # GitHub API query
   GET /search/repositories?q=topic:vivid-addon
   ```

2. **Naming Convention**: Repos named `vivid-*` with valid addon.json
   ```bash
   GET /search/repositories?q=vivid-+in:name+filename:addon.json
   ```

3. **Code Search**: Repos containing addon.json with `"vivid"` field
   ```bash
   GET /search/code?q=filename:addon.json+"vivid"
   ```

**Discovery Workflow:**

```
┌─────────────────────────────────────────────────────────────┐
│                  GitHub Actions (scheduled)                  │
│                      (runs daily)                           │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  1. Search GitHub for repos with topic:vivid-addon          │
│  2. Search for repos named vivid-* with addon.json          │
│  3. Fetch addon.json from each discovered repo              │
│  4. Validate addon.json schema                              │
│  5. Check if addon builds (optional, via CI)                │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  Update registry index with new/updated addons              │
│  - Auto-approve if validation passes                        │
│  - Flag for manual review if issues detected                │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  Commit updated index.json to vivid-registry repo           │
└─────────────────────────────────────────────────────────────┘
```

**For Addon Authors:**

To get your addon auto-discovered:

1. Name your repo `vivid-<name>` (e.g., `vivid-particles`, `vivid-shader-toys`)
2. Add the `vivid-addon` topic to your GitHub repo
3. Include a valid addon.json in the root
4. (Optional) Add `vivid` field to addon.json: `"vivid": ">=3.0.0"`

**Discovery CLI:**

```bash
# Search GitHub directly (bypasses registry)
vivid addon discover                    # Find all vivid addons on GitHub
vivid addon discover --topic particles  # Filter by topic
vivid addon discover --author username  # Filter by author

# Install discovered addon
vivid addon install github:someuser/vivid-cool-effect
```

**Registry Index Update Script:**

```python
# scripts/update-registry.py (runs in GitHub Actions)
import requests
import json

def discover_addons():
    # Search by topic
    topic_results = github_search("topic:vivid-addon")

    # Search by naming convention
    name_results = github_search("vivid- in:name filename:addon.json")

    # Merge and deduplicate
    all_repos = merge_unique(topic_results, name_results)

    addons = []
    for repo in all_repos:
        addon_json = fetch_file(repo, "addon.json")
        if validate_addon(addon_json):
            addons.append({
                "name": addon_json["name"],
                "version": addon_json["version"],
                "description": addon_json["description"],
                "repository": repo["html_url"],
                "author": addon_json.get("author", repo["owner"]["login"]),
                "stars": repo["stargazers_count"],
                "updated": repo["updated_at"]
            })

    return addons

def update_index(addons):
    with open("index.json", "w") as f:
        json.dump({"addons": addons, "updated": now()}, f, indent=2)
```

**Phase 16d: Dedicated Registry (Future)**
- [ ] Registry API server
- [ ] User authentication (GitHub OAuth)
- [ ] Web UI for browsing addons
- [ ] Download statistics
- [ ] Security scanning integration

**Validation:**
- [ ] `vivid addon install` works for local path
- [ ] `vivid addon install github:user/repo` clones and builds
- [ ] vivid.lock is reproducible (same deps → same lock)
- [ ] Version conflicts are detected and reported
- [ ] `vivid addon publish` creates valid registry entry

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
│   ├── vivid-media/       # Video/image INPUT (playback, webcam)
│   ├── vivid-export/      # Video/image OUTPUT (recording, sequences)
│   ├── vivid-audio/
│   ├── vivid-midi/
│   ├── vivid-osc/
│   ├── vivid-ml/
│   └── vivid-webserver/
│
├── external/
│   ├── dawn/
│   ├── glfw/
│   ├── glm/
│   ├── imgui/
│   ├── miniaudio/
│   ├── tonic/
│   ├── rtmidi/
│   └── onnxruntime/
│
└── examples/
    ├── hello/chain.cpp
    ├── noise/chain.cpp
    ├── feedback/chain.cpp
    ├── audio-reactive/chain.cpp
    ├── midi-control/chain.cpp
    ├── movenet-tracking/chain.cpp
    ├── web-control/chain.cpp
    ├── sample-trigger/chain.cpp
    ├── udp-receiver/chain.cpp
    ├── triple-projector/chain.cpp
    ├── retro-demo/chain.cpp
    ├── procedural-mesh/chain.cpp
    └── wipeout-craft/chain.cpp
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

## Target Project Reference Implementations

### Musical Bodies (Interactive Installation)

Complete example using: UDP input, sample triggering, audio filters, 2D instancing, multi-projector edge blending.

```cpp
// examples/musical-bodies/chain.cpp
#include <vivid/vivid.h>
#include <vivid/effects/operators.h>
#include <vivid/audio/operators.h>
#include <vivid/network/operators.h>

using namespace vivid;

static Chain chain;
static const int NUM_ZONES = 60;
static const int SLICES_PER_ZONE = 4;

static bool zoneWasOccupied[NUM_ZONES] = {false};
static float zoneGlow[NUM_ZONES] = {0.0f};
static float zoneCutoff[NUM_ZONES] = {0.5f};

void setup(Context& ctx) {
    // Network: Receive zone data from Quanergy Q-Dome via Qortex
    chain.add<UdpIn>("qortex").port(5000);

    // Audio: Load samples and set up filtered playback
    chain.add<SampleBank>("sounds").folder("assets/audio/zones/").polyphony(32);
    chain.add<SamplePlayer>("player").bank("sounds");
    chain.add<AudioFilter>("filter").input("player");
    chain.add<AudioOut>("audio").input("filter");

    // Visuals: 2D instanced rectangles for zone display
    chain.add<Instances2D>("zones").maxInstances(NUM_ZONES);
    chain.add<Blur>("blur").input("zones").radius(30.0f).passes(3);
    chain.add<Output>("out").input("blur");

    // Multi-projector setup (3 x 1920x1080, 15% overlap)
    ctx.createWindow("proj1", 1920, 1080).monitor(0).fullscreen(true)
        .edgeBlend({.right = 0.15f});
    ctx.createWindow("proj2", 1920, 1080).monitor(1).fullscreen(true)
        .edgeBlend({.left = 0.15f, .right = 0.15f});
    ctx.createWindow("proj3", 1920, 1080).monitor(2).fullscreen(true)
        .edgeBlend({.left = 0.15f});

    chain.setOutput("out");
    chain.init(ctx);
}

void update(Context& ctx) {
    auto& udp = chain.get<UdpIn>("qortex");
    auto& player = chain.get<SamplePlayer>("player");
    auto& filter = chain.get<AudioFilter>("filter");
    auto& instances = chain.get<Instances2D>("zones");

    // Parse zone occupancy from Qortex
    if (udp.hasData()) {
        auto data = udp.data();
        for (int zone = 0; zone < NUM_ZONES && zone < data.size(); zone++) {
            int occupancy = data[zone];  // People count in zone
            int slice = (data.size() > NUM_ZONES) ? data[NUM_ZONES + zone] : 0;

            // Person entered zone - trigger sample
            if (occupancy > 0 && !zoneWasOccupied[zone]) {
                player.trigger(zone % player.count());
                zoneGlow[zone] = 1.0f;  // Full brightness
            }

            // Update filter cutoff based on slice position
            if (occupancy > 0) {
                zoneCutoff[zone] = 200.0f + (slice / float(SLICES_PER_ZONE)) * 7800.0f;
            }

            zoneWasOccupied[zone] = occupancy > 0;
        }
    }

    // Apply audio filter (average of active zone cutoffs)
    float avgCutoff = 4000.0f;
    int activeCount = 0;
    for (int i = 0; i < NUM_ZONES; i++) {
        if (zoneWasOccupied[i]) {
            avgCutoff += zoneCutoff[i];
            activeCount++;
        }
    }
    if (activeCount > 0) avgCutoff /= activeCount;
    filter.lowpass(avgCutoff);

    // Update zone visuals
    instances.clear();
    float zoneWidth = ctx.width() / 10.0f;
    float zoneHeight = ctx.height() / 6.0f;

    for (int zone = 0; zone < NUM_ZONES; zone++) {
        int col = zone % 10;
        int row = zone / 10;
        float x = col * zoneWidth + zoneWidth * 0.5f;
        float y = row * zoneHeight + zoneHeight * 0.5f;

        // Decay glow over time
        zoneGlow[zone] *= 0.95f;
        float alpha = zoneGlow[zone];

        if (alpha > 0.01f) {
            instances.add({
                .position = {x, y},
                .size = {zoneWidth * 0.9f, zoneHeight * 0.9f},
                .color = {1.0f, 0.8f, 0.2f, alpha}
            });
        }
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

### Wipeout 2029 Craft Generator

Complete example using: Procedural mesh generation, Canvas for livery textures, flat shading, retro post-processing.

```cpp
// examples/wipeout-craft/chain.cpp
#include <vivid/vivid.h>
#include <vivid/render3d/operators.h>
#include <vivid/effects/operators.h>

using namespace vivid;

static Chain chain;
static Mesh craftMesh;
static Font teamFont;

// Team palettes
struct Team { std::string name; glm::vec4 primary; glm::vec4 secondary; };
static std::vector<Team> teams = {
    {"FEISAR",  {0.1f, 0.2f, 0.8f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {"AG-SYS",  {0.9f, 0.8f, 0.1f, 1.0f}, {0.1f, 0.2f, 0.8f, 1.0f}},
    {"AURICOM", {0.9f, 0.1f, 0.1f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {"QIREX",   {0.5f, 0.1f, 0.7f, 1.0f}, {0.1f, 0.9f, 0.9f, 1.0f}},
    {"PIRANHA", {0.1f, 0.1f, 0.1f, 1.0f}, {1.0f, 0.5f, 0.1f, 1.0f}},
};
static int currentTeam = 0;
static int craftNumber = 7;

Mesh generateCraft() {
    MeshBuilder craft;

    // Fuselage - elongated hexagonal prism (10 segments)
    const int SEGMENTS = 10;
    const int SIDES = 6;
    for (int i = 0; i < SEGMENTS; i++) {
        float z = -2.0f + i * 0.4f;
        float taper = 1.0f - pow(abs(i - SEGMENTS/2) / float(SEGMENTS/2), 2) * 0.4f;
        for (int j = 0; j < SIDES; j++) {
            float angle = j * glm::two_pi<float>() / SIDES;
            glm::vec3 pos = {cos(angle) * 0.3f * taper, sin(angle) * 0.15f * taper, z};
            glm::vec2 uv = {float(j) / SIDES, float(i) / SEGMENTS};
            craft.addVertex(pos, glm::vec3(0), uv);
        }
    }
    // Connect fuselage segments with quads
    for (int i = 0; i < SEGMENTS - 1; i++) {
        for (int j = 0; j < SIDES; j++) {
            int curr = i * SIDES + j;
            int next = i * SIDES + (j + 1) % SIDES;
            craft.addQuad(curr, next, next + SIDES, curr + SIDES);
        }
    }

    // Side pods (simplified cylinders)
    auto addPod = [&](float xOffset) {
        int baseIdx = craft.vertexCount();
        for (int i = 0; i < 6; i++) {
            float angle = i * glm::two_pi<float>() / 6;
            craft.addVertex({xOffset + cos(angle) * 0.1f, sin(angle) * 0.08f, -0.5f});
            craft.addVertex({xOffset + cos(angle) * 0.1f, sin(angle) * 0.08f, 0.3f});
        }
        for (int i = 0; i < 6; i++) {
            int j = (i + 1) % 6;
            craft.addQuad(baseIdx + i*2, baseIdx + j*2, baseIdx + j*2 + 1, baseIdx + i*2 + 1);
        }
    };
    addPod(0.5f);   // Right pod
    addPod(-0.5f);  // Left pod (will be mirrored, but explicit for UVs)

    craft.computeFlatNormals();  // Faceted PS1 look

    return craft.build();
}

void generateLivery(Context& ctx, int teamIdx, int number) {
    auto& canvas = chain.get<Canvas>("livery");
    Team& team = teams[teamIdx];

    // Base color
    canvas.clear(team.primary);

    // Racing stripes
    canvas.rectFilled(0, 180, 256, 40, team.secondary);
    canvas.rectFilled(0, 230, 256, 15, team.primary * 0.7f);

    // Team number
    char numStr[8];
    snprintf(numStr, sizeof(numStr), "%02d", number);
    float numWidth = teamFont.width(numStr);
    canvas.text(numStr, (256 - numWidth) / 2, 300, teamFont, team.secondary);

    // Panel lines (dark accent)
    glm::vec4 dark = team.primary * 0.3f; dark.a = 1.0f;
    canvas.line({0, 100}, {256, 100}, 2, dark);
    canvas.line({0, 400}, {256, 400}, 2, dark);
    canvas.line({128, 0}, {128, 180}, 1, dark);
}

void setup(Context& ctx) {
    // Generate procedural craft mesh
    craftMesh = generateCraft();

    // Load font for livery numbers
    teamFont = Font::load("assets/fonts/racing.ttf", 64);

    // Create livery texture via Canvas
    chain.add<Canvas>("livery").size(256, 512);

    // 3D rendering with flat shading
    chain.add<Render3D>("craft")
        .mesh(craftMesh)
        .texture("livery")
        .shadingMode(ShadingMode::Flat);

    chain.add<Camera3D>("camera")
        .target({0, 0, 0})
        .distance(5.0f)
        .fov(45.0f);

    // Retro post-processing pipeline
    chain.add<Downsample>("lowres")
        .input("craft")
        .resolution(480, 270)
        .filter(FilterMode::Nearest);

    chain.add<Dither>("dither")
        .input("lowres")
        .pattern(DitherPattern::Bayer4x4)
        .levels(32);

    chain.add<Scanlines>("crt")
        .input("dither")
        .spacing(3)
        .intensity(0.2f);

    chain.add<Output>("out").input("crt");

    // Generate initial livery
    generateLivery(ctx, currentTeam, craftNumber);

    chain.setOutput("out");
    chain.init(ctx);
}

void update(Context& ctx) {
    // Keyboard controls
    if (ctx.wasKeyPressed(Key::Space)) {
        currentTeam = (currentTeam + 1) % teams.size();
        generateLivery(ctx, currentTeam, craftNumber);
    }
    if (ctx.wasKeyPressed(Key::R)) {
        craftNumber = rand() % 100;
        generateLivery(ctx, currentTeam, craftNumber);
    }
    if (ctx.wasKeyPressed(Key::G)) {
        craftMesh = generateCraft();  // Regenerate with new random params
        chain.get<Render3D>("craft").mesh(craftMesh);
    }
    if (ctx.wasKeyPressed(Key::W)) {
        auto& r = chain.get<Render3D>("craft");
        r.wireframe(!r.isWireframe());
    }

    // Orbit camera with mouse
    auto& cam = chain.get<Camera3D>("camera");
    if (ctx.isMouseDown(0)) {
        cam.orbit(ctx.mouseDelta().x * 0.5f, ctx.mouseDelta().y * 0.5f);
    }
    cam.zoom(ctx.scrollDelta().y * 0.5f);

    // Slow auto-rotate
    cam.orbit(ctx.dt() * 10.0f, 0);

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
```

---

## Resources

- [WebGPU Specification](https://www.w3.org/TR/webgpu/)
- [WGSL Specification](https://www.w3.org/TR/WGSL/)
- [Dawn Repository](https://dawn.googlesource.com/dawn)
- [Learn WebGPU](https://eliemichel.github.io/LearnWebGPU/)
- [WebGPU Fundamentals](https://webgpufundamentals.org/)
