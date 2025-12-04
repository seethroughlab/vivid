# Vivid Roadmap

A **LLM-first creative coding framework** built on Diligent Engine. Plain C++ that language models can read, write, and reason about—combining TouchDesigner's inspect-anywhere philosophy with the portability of text-based code.

See [docs/PHILOSOPHY.md](docs/PHILOSOPHY.md) for the full vision.

---

## Guiding Principles

1. **Don't reinvent the wheel.** Before implementing any feature, check if Diligent Engine already provides it. Search DiligentCore (low-level utilities), DiligentTools (texture loading, asset management), DiligentFX (PBR, shadows, post-processing), and DiligentSamples (reference implementations). Only write custom code when Diligent doesn't have a solution.

2. **Get to the chain API fast.** The core value of Vivid is the `chain.cpp` programming model. All infrastructure work serves this goal.

3. **Cross-platform from day one.** Test on macOS, Windows, and Linux continuously. Catch platform-specific issues early, not after the API is locked in.

4. **HLSL as universal shader language.** Cross-compiles to all backends via Diligent's shader tools.

---

## Project Structure

A minimal vivid project requires only:

```
my-project/
├── chain.cpp       # Required: Your visual program
├── ROADMAP.md      # Recommended: For LLM-assisted development
└── assets/         # Optional: Project-specific assets
    ├── textures/
    ├── hdris/
    └── models/
```

**Asset loading:** The runtime searches for assets in this order:
1. Project's `assets/` folder
2. Vivid runtime's `assets/` folder (fallback for shared resources)

---

## Dependencies

| Library | Purpose | Notes |
|---------|---------|-------|
| Diligent Engine | Graphics abstraction | Core, Tools, FX, Samples |
| GLFW | Window/input management | Cross-platform |
| glm | Math library | Vectors, matrices, transforms |
| stb_image | Image loading | Via DiligentTools |

---

## Testing Strategy

Testing is continuous, not a phase. Every operator and example must be testable from day one.

### Visual Regression Testing
- Reference images for each operator (golden master approach)
- Pixel-diff comparison with tolerance for floating-point variance
- Automated screenshot capture during CI

### Example Project Testing
- All examples in `examples/` must run without errors
- Each example renders for N frames and captures output
- CI runs examples on all three platforms

### Operator Testing
- Each operator has a test chain that exercises its parameters
- Test edge cases: zero, negative, extreme values
- Output comparison against reference images

### CI Pipeline (GitHub Actions)
- Triggered on every PR and push to main
- Matrix: macOS, Windows, Linux
- Steps: Build → Run unit tests → Run examples → Visual diff report
- Block merge if tests fail or visual regressions detected

---

## Diligent Engine Components

Vivid uses specific parts of Diligent Engine. Understanding what we use (and don't use) is critical.

### DiligentCore (Graphics Abstraction)

The low-level graphics API abstraction. We use:

| Component | Purpose |
|-----------|---------|
| `IRenderDevice` | GPU resource creation |
| `IDeviceContext` | Command submission, state management |
| `ISwapChain` | Window surface presentation |
| `IPipelineState` | Shader pipeline configuration |
| `IBuffer` | Vertex, index, and uniform buffers |
| `ITexture` / `ITextureView` | 2D textures and render targets |
| `IShader` | Compiled shader programs |
| `IShaderResourceBinding` | Binding textures/buffers to shaders |

**Backend:** Vulkan (via MoltenVK on macOS). Metal backend requires proprietary DiligentCorePro.

### DiligentFX (High-Level Rendering)

Battle-tested rendering components. We use selectively:

| Component | What We Use | Notes |
|-----------|-------------|-------|
| `PBR_Renderer` | IBL cubemap precomputation, PBR shader infrastructure | `PrecomputeCubemaps()`, `GetIrradianceCubeSRV()`, `GetPrefilteredEnvMapSRV()` |
| `PBR_Renderer::CreateInfo` | Configuration for PBR pipeline | `EnableIBL`, `UseSeparateMetallicRoughnessTextures`, `TextureAttribIndices` |
| `GLTF_PBR_Renderer` | (Future) GLTF model rendering | Not yet integrated |
| `ShadowMapManager` | (Future) Cascaded shadow maps | Not yet integrated |

**Key insight:** DiligentFX expects material textures as `Texture2DArray` (even with single slices), not `Texture2D`. Our `TextureUtils::loadFromFileAsArray()` handles this.

**Shader structures we include:**
```cpp
namespace Diligent { namespace HLSL {
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
}}
```

### DiligentTools (Utilities)

Helper utilities. We use:

| Component | Purpose |
|-----------|---------|
| `TextureLoader` | Loading PNG, JPG, HDR images |
| `Image` | Raw image data access for custom texture creation |
| `CreateTextureFromFile()` | Quick texture loading |

**Not using (yet):**
- `ImGuiImplDiligent` - ImGui integration (future addon)
- `AssetLoader` - Asset management
- `RenderStateNotationLoader` - PSO from JSON

### DiligentSamples (Reference)

We study these for implementation patterns:
- `Tutorials/Tutorial03_Texturing` - Texture binding
- `Tutorials/Tutorial19_RenderPasses` - Render target management
- `SampleBase` - Application structure patterns

---

## Architecture Overview

```
User Code (chain.cpp)
    │
    ▼
Chain API (operators, connections)
    │
    ▼
Context (frame lifecycle, resource management)
    │
    ▼
DiligentRenderer
    ├── DiligentFX (PBR, IBL)
    ├── Custom shaders (HLSL) for 2D effects
    ├── MeshUtils (primitives, buffers)
    └── TextureUtils (loading, render targets)
    │
    ▼
Diligent Core (Vulkan backend)
    │
    ▼
MoltenVK (macOS) / Native Vulkan (Windows/Linux)
```

---

## Phase 1: Core Rendering Infrastructure ✓

**Goal:** Window, Vulkan initialization, texture management

**Status:** Complete (macOS verified)

### Tasks
- [x] GLFW window creation and event handling
- [x] Diligent Engine initialization with Vulkan backend
- [x] Swap chain management and resize handling
- [x] Frame loop: beginFrame → render → endFrame → present
- [x] TextureUtils: load from file, create render targets
- [x] TextureUtils: `loadFromFileAsArray()` for DiligentFX compatibility
- [ ] ShaderUtils: HLSL loading and compilation (deferred to Phase 2)
- [ ] Basic pipeline state object (PSO) creation (deferred to Phase 2)

### Files
```
runtime/
├── src/
│   ├── main.cpp                # Entry point
│   ├── context.cpp             # Vulkan/GLFW init, frame loop
│   ├── chain.cpp               # Operator chain management
│   ├── texture_utils.cpp       # Texture loading
│   └── macos_helpers.mm        # CAMetalLayer setup
├── include/vivid/
│   ├── vivid.h                 # Main include
│   ├── context.h               # Frame context
│   ├── chain.h                 # Chain API
│   ├── operator.h              # Base operator class
│   └── texture_utils.h         # Texture utilities
```

### Cross-Platform Setup

**macOS (MoltenVK):**
```bash
VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
DYLD_LIBRARY_PATH=/opt/homebrew/lib
```

**Windows:**
- Native Vulkan via GPU drivers
- MSVC or Clang for compilation

**Linux:**
- Native Vulkan via Mesa or proprietary drivers
- GCC or Clang for compilation

### Cross-Platform Validation
- [x] Verify build on macOS (Apple M4 Max, MoltenVK)
- [ ] Verify build on Windows
- [ ] Verify build on Linux
- [ ] Document any platform-specific workarounds

---

## Phase 2: Chain API + Core Operators ✓

**Goal:** Establish the `chain.cpp` programming model with essential operators

**Status:** Complete (macOS verified)

The Chain API requires working operators to be useful. This phase delivers both together.

### Chain API Tasks
- [x] Define `Operator` base class with `init()`, `process()`, `cleanup()`
- [x] Create `Context` class for frame state and resource access
- [x] Implement operator connection system (input/output via `setInput()`)
- [x] Add parameter system with fluent API
- [x] ShaderUtils for HLSL loading/compilation
- [x] FullscreenQuad utility for 2D effects rendering
- [x] TextureOperator base class for 2D texture effects
- [ ] Implement `Chain` class for operator graph management (deferred)
- [ ] Create `VIVID_CHAIN(setup, update)` macro (deferred)

### Core Operators (Required for Chain API)
- [x] **Output** - Display results to screen
- [x] **SolidColor** - Fill with constant color
- [x] **Noise** - Simplex noise with fBm generator
- [x] **Blur** - Box blur with 9-tap Gaussian weights
- [x] **Composite** - Blend two textures (5 blend modes)

### Target Usage
```cpp
#include <vivid/vivid.h>
using namespace vivid;

void setup(Chain& chain) {
    chain.setResolution(1920, 1080);
    chain.setOutput("final");
}

void update(Chain& chain, Context& ctx) {
    auto noise = chain.op<Noise>("noise")
        .scale(4.0f)
        .speed(0.5f);

    auto blur = chain.op<Blur>("blur")
        .input(noise)
        .radius(10.0f);

    chain.op<Output>("final")
        .input(blur);
}

VIVID_CHAIN(setup, update)
```

### Operator Base Class
```cpp
class Operator {
public:
    virtual ~Operator() = default;
    virtual void init(Context& ctx) {}
    virtual void process(Context& ctx) = 0;
    virtual void cleanup() {}

    virtual std::string name() const = 0;
    virtual std::vector<ParamDecl> params() = 0;
    virtual OutputKind outputKind() = 0;  // Texture, Value, Geometry

    int sourceLine = 0;  // For VS Code decorations
};
```

---

## Phase 3: Additional 2D Operators

**Goal:** Expand the operator library with more effects

### Effects
- [ ] **Passthrough** - Identity transform
- [ ] **Brightness/Contrast** - Basic color correction
- [ ] **HSV** - Hue/Saturation/Value adjustment
- [ ] **Transform** - Translate, rotate, scale
- [ ] **Feedback** - Recursive buffer with decay
- [ ] **Gradient** - Linear, radial, angular gradients
- [ ] **Edge Detection** - Sobel filter
- [ ] **Displacement** - UV displacement mapping
- [ ] **Chromatic Aberration** - RGB channel separation
- [ ] Pixelate
- [ ] Mirror/Kaleidoscope

### Shaders
```
shaders/
├── common/
│   ├── fullscreen.hlsl    # Fullscreen triangle vertex shader
│   └── uniforms.hlsl      # Common structures
└── effects/
    ├── passthrough.hlsl
    ├── noise.hlsl
    ├── blur.hlsl
    └── ...
```

---

## Phase 4: 3D Rendering

**Goal:** Basic 3D with camera, meshes, and PBR materials

### Tasks
- [ ] Vertex format: position, normal, UV, tangent (vec3)
- [ ] MeshUtils: cube, sphere, plane, cylinder, torus, cone
- [ ] Camera3D: perspective, view matrix, orbit/zoom helpers
- [ ] Depth buffer management
- [ ] DiligentFX PBR_Renderer integration
- [ ] IBL cubemap precomputation from HDR environment
- [ ] PBR material textures (albedo, normal, metallic, roughness, AO)
- [ ] Multiple light support
- [ ] Shadow mapping via ShadowMapManager
- [ ] GLTF model loading via GLTF_PBR_Renderer

### Vertex Structure
```cpp
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;  // DiligentFX expects float3 at ATTRIB7
};
```

### PBR Material
```cpp
struct PBRMaterial {
    ManagedTexture albedoMap;      // sRGB, Texture2DArray
    ManagedTexture normalMap;      // Linear, Texture2DArray
    ManagedTexture metallicMap;    // Linear, Texture2DArray
    ManagedTexture roughnessMap;   // Linear, Texture2DArray
    ManagedTexture aoMap;          // Linear, Texture2DArray
};
```

---

## Phase 5: Hot Reload System

**Goal:** Live code recompilation without restart

### Zero-Config Build
Vivid projects don't require CMakeLists.txt. The runtime automatically infers build configuration:
- [ ] Scan chain.cpp for `#include` directives
- [ ] Auto-detect required libraries and addons
- [ ] Generate compiler flags dynamically
- [ ] Addons auto-linked when their headers are included

### Tasks
- [ ] File watcher for chain.cpp changes
- [ ] Invoke compiler (clang/MSVC) to build shared library
- [ ] Dynamic library loading/unloading
- [ ] Symbol resolution (setup, update functions)
- [ ] State preservation across reloads

### Error Handling
Errors must be shown to the user **before** replacing running code:
- [ ] Compile errors: Display in window overlay, keep old code running
- [ ] Shader errors: Show shader name, line number, error message
- [ ] Runtime errors: Catch and display, don't crash
- [ ] Error overlay: Non-intrusive display that doesn't block the visualization

### Compiler Interface
```cpp
class Compiler {
public:
    bool compile(const std::filesystem::path& projectPath);
    bool load();
    void unload();
    std::string lastError() const;

    using SetupFn = void(*)(Chain&);
    using UpdateFn = void(*)(Chain&, Context&);
    SetupFn getSetup();
    UpdateFn getUpdate();
};
```

---

## Phase 6: Addon System

**Goal:** Modular extensions with zero-config detection

Establishing the addon system early allows future features (media, audio, ML) to be implemented as optional addons rather than core dependencies.

### Built-in Addons
| Addon | Platform | Description |
|-------|----------|-------------|
| vivid-imgui | All | ImGui via DiligentTools (includes FPS display, parameter tweaking) |
| vivid-spout | Windows | Texture sharing |
| vivid-syphon | macOS | Texture sharing |
| vivid-models | All | 3D model loading |

### Usage
```cpp
// Just include - addon auto-detected from #include
#include <vivid/imgui/imgui.h>

void update(Chain& chain, Context& ctx) {
    imgui::beginFrame();
    ImGui::SliderFloat("Speed", &speed, 0.0f, 10.0f);
    imgui::endFrame();
}
```

---

## Phase 7: VS Code Extension

**Goal:** Live previews and editor integration

### Features
- [ ] WebSocket connection to runtime
- [ ] Inline preview decorations
- [ ] Hover for full preview
- [ ] Preview panel (all operators)
- [ ] Compile error diagnostics
- [ ] Status bar (connection state, FPS)

### Protocol
```typescript
interface NodeUpdate {
    id: string;
    line: number;
    kind: 'texture' | 'value' | 'geometry';
    preview?: string;  // base64 thumbnail
}
```

---

## Phase 8: Media Pipeline

**Goal:** Video playback and camera input

### Tasks
- [ ] VideoLoader interface (platform-agnostic)
- [ ] macOS: AVFoundation implementation
- [ ] HAP codec support (FFmpeg demux + DXT upload)
- [ ] Webcam capture
- [ ] Playback controls: play, pause, seek, loop, speed

### Platform Support
| Feature | macOS | Windows | Linux |
|---------|-------|---------|-------|
| H.264/H.265 | AVFoundation | Media Foundation | FFmpeg |
| HAP | FFmpeg | FFmpeg | FFmpeg |
| Webcam | AVFoundation | Media Foundation | V4L2 |

---

## Phase 9: Input & Window Management

**Goal:** Mouse, keyboard, gamepad, advanced window control

### Input Tasks
- [ ] Mouse: position, buttons, drag, scroll
- [ ] Keyboard: key state, text input
- [ ] Gamepad: axes, buttons, triggers
- [ ] Cursor visibility control

### Window Management
- [ ] Fullscreen toggle
- [ ] Borderless window mode
- [ ] Multi-monitor support (select display, span displays)
- [ ] Multi-window support (secondary output windows)
- [ ] Window positioning and sizing API

```cpp
void update(Chain& chain, Context& ctx) {
    glm::vec2 mouse = ctx.mousePosition();
    bool clicked = ctx.wasMouseButtonPressed(MouseButton::Left);

    if (ctx.wasKeyPressed(Key::F)) {
        ctx.toggleFullscreen();
    }
}
```

---

## Phase 10: Audio System

**Goal:** Audio input, FFT analysis, MIDI, OSC

### Tasks
- [ ] Audio capture (system, microphone)
- [ ] FFT spectrum analysis
- [ ] Beat detection
- [ ] MIDI input/output
- [ ] OSC input/output

### Dependencies
| Library | Purpose |
|---------|---------|
| miniaudio | Audio I/O |
| KissFFT | FFT analysis |
| RtMidi | MIDI |
| oscpack | OSC protocol |

---

## Phase 11: Export & CLI

**Goal:** Video recording, standalone apps, CLI

### CLI Commands
```bash
vivid new my-project           # Create project
vivid my-project               # Run project
vivid my-project --record      # Record to video
vivid my-project --headless    # Run without window (for CI/rendering)
vivid export --standalone      # Build standalone app
```

### Headless Mode
- [ ] Run without creating a window
- [ ] Render to offscreen framebuffer
- [ ] Useful for CI pipelines, batch rendering, server-side generation

### Export Formats
- Video: H.264, ProRes, HAP
- Image sequence: PNG, EXR
- Standalone: macOS .app, Windows .exe, Linux AppImage

---

## Phase 12: ML Integration

**Goal:** ONNX Runtime for ML inference

### Operators
- PoseDetector (MoveNet, BlazePose)
- SegmentBackground (MediaPipe Selfie, MODNet)
- StyleTransfer (arbitrary style)
- ObjectDetector (YOLO)
- FaceDetector (landmarks, expressions)

---

## Implementation Order

### Sprint 1: Foundation (Current)
1. Phase 1 (Rendering) - Window, Vulkan, textures
2. Phase 2 (Chain API + Core Operators) - Operator system + Output, Noise, Blur, Composite

### Sprint 2: More Effects
3. Phase 3 (Additional 2D Operators) - Expand operator library

### Sprint 3: 3D & PBR
4. Phase 4 (3D Rendering) - Using DiligentFX PBR_Renderer

### Sprint 4: Live Development + Extensibility
5. Phase 5 (Hot Reload) - Live code recompilation
6. Phase 6 (Addon System) - Enable extensibility early so future features can be addons

### Sprint 5: Editor Integration
7. Phase 7 (VS Code) - Editor integration

### Sprint 6: Media & Input (can be addons)
8. Phase 8 (Media) - Video, webcam
9. Phase 9 (Input) - Mouse, keyboard, gamepad

### Sprint 7: Audio (can be addon)
10. Phase 10 (Audio) - FFT, MIDI, OSC

### Sprint 8: Polish
11. Phase 11 (Export) - Recording, CLI
12. Phase 12 (ML) - ONNX integration (addon)

---

## Operator Categories

### Generators (no input)
| Operator | Output | Description |
|----------|--------|-------------|
| Noise | Texture | Perlin/Simplex/Worley |
| Gradient | Texture | Linear/radial/angular |
| Shape | Texture | Geometric shapes |
| Constant | Value | Constant float |
| LFO | Value | Oscillator |
| ImageFile | Texture | Load from disk |
| VideoFile | Texture | Video playback |
| Webcam | Texture | Camera capture |

### Filters (texture → texture)
| Operator | Description |
|----------|-------------|
| Blur | Gaussian blur |
| Brightness | Brightness/contrast |
| HSV | Hue/saturation/value |
| Edge | Edge detection |
| Transform | 2D transform |
| Displacement | UV displacement |

### Compositors
| Operator | Description |
|----------|-------------|
| Composite | Blend two textures |
| Switch | Select between inputs |
| Feedback | Recursive buffer |

### 3D Operators
| Operator | Description |
|----------|-------------|
| Render3D | 3D scene to texture |
| Camera3D | Perspective camera |
| Mesh | 3D geometry |
| Particles | Particle system |

---

## Directory Structure

```
vivid/
├── CMakeLists.txt
├── runtime/
│   ├── CMakeLists.txt
│   ├── include/vivid/
│   │   ├── vivid.h           # Main include
│   │   ├── chain.h           # Chain API
│   │   ├── context.h         # Frame context
│   │   ├── operator.h        # Base class
│   │   ├── diligent_renderer.h
│   │   ├── texture_utils.h
│   │   ├── mesh.h
│   │   └── camera.h
│   └── src/
│       ├── main.cpp
│       ├── chain.cpp
│       ├── context.cpp
│       ├── diligent_renderer.cpp
│       ├── texture_utils.cpp
│       ├── mesh.cpp
│       └── camera.cpp
├── operators/
│   └── *.cpp                 # Built-in operators
├── shaders/
│   ├── common/
│   └── effects/
├── addons/
│   └── vivid-*/
├── assets/
│   ├── materials/
│   └── hdris/
└── examples/
```

---

## Success Criteria

Each phase is complete when:
- All features work as documented
- Examples render correctly
- Performance: 60fps at 1080p
- No memory leaks

Final success:
- `chain.cpp` workflow is intuitive
- Hot reload enables rapid iteration
- VS Code extension shows live previews
- Built-in operators cover common use cases
