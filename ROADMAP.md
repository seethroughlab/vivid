# Vivid Rebuild Roadmap

A complete reimplementation of Vivid with **Diligent Engine** at its core from the ground up.

## Why Start Fresh

The existing codebase was built around wgpu-native/WebGPU patterns. Retrofitting Diligent Engine on top created architectural friction:

- WebGPU assumptions embedded in context management, shader handling, and pipeline state
- Awkward seams where two paradigms clashed
- "Basic" rendering tasks becoming complex due to legacy abstractions
- ~5000 lines of custom pipeline code that Diligent can replace

**Starting fresh allows:**
- Designing around Diligent's idioms (RenderDevice → SwapChain → Pipeline → Draw)
- Using DiligentFX for PBR, shadows, IBL out of the box
- HLSL as universal shader language (cross-compiles to all backends)
- Future flexibility: can switch to Vulkan/Metal by changing one line

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
    ├── DiligentFX (PBR, Shadows, IBL, Post-FX)
    ├── Custom shaders (HLSL) for effects
    └── Texture/Mesh management
    │
    ▼
Diligent Core (WebGPU backend initially, Vulkan/Metal later)
    │
    ▼
Native GPU API
```

---

## Phase 1: Foundation

**Goal:** Window creation, Diligent initialization, clear screen to color

### Tasks
- [ ] Create minimal `main.cpp` with GLFW window
- [ ] Initialize Diligent Engine with WebGPU backend
- [ ] Create swap chain
- [ ] Implement frame loop: beginFrame → clear → endFrame → present
- [ ] Handle window resize
- [ ] Verify on macOS (primary target)

### Files to Create
```
runtime/
├── CMakeLists.txt
├── src/
│   ├── main.cpp              # Entry point, GLFW window
│   ├── diligent_renderer.h   # Core renderer class
│   └── diligent_renderer.cpp
```

### Dependencies
```cmake
# Diligent Engine (submodule)
add_subdirectory(external/DiligentEngine)

# GLFW for windowing
FetchContent_Declare(glfw ...)

# GLM for math
FetchContent_Declare(glm ...)
```

---

## Phase 2: Shader System

**Goal:** Load and compile HLSL shaders, create pipelines

### Tasks
- [ ] Create shader loading utility (from file and embedded)
- [ ] Implement fullscreen triangle vertex shader
- [ ] Create basic fragment shader (solid color)
- [ ] Build pipeline state object (PSO) creation helpers
- [ ] Add runtime shader compilation with error reporting
- [ ] Hot-reload shader files in dev mode

### Core Shaders (HLSL)
```
shaders/
├── common/
│   ├── fullscreen.hlsl       # Fullscreen triangle vertex shader
│   └── uniforms.hlsl         # Common uniform structures
├── effects/
│   ├── passthrough.hlsl
│   ├── solid_color.hlsl
│   └── ... (more later)
```

### Shader Structure
```hlsl
// fullscreen.hlsl - Base for all 2D effects
struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

VSOutput VS_Main(uint vertexId : SV_VertexID) {
    VSOutput output;
    // Fullscreen triangle from vertex ID
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv.y = 1.0 - output.uv.y;  // Flip Y for texture coords
    return output;
}
```

---

## Phase 3: Texture System

**Goal:** Create, sample, and render to textures

### Tasks
- [ ] Implement `Texture` struct with Diligent handles
- [ ] Create texture from dimensions (empty render target)
- [ ] Create texture from image file (stb_image)
- [ ] Create texture from pixel data
- [ ] Implement render-to-texture (framebuffer)
- [ ] Sampler creation (linear, nearest, wrap modes)
- [ ] Texture format handling (RGBA8, RGBA16F, etc.)

### Texture Structure
```cpp
struct Texture {
    Diligent::RefCntAutoPtr<ITexture> texture;
    Diligent::RefCntAutoPtr<ITextureView> rtv;   // Render target view
    Diligent::RefCntAutoPtr<ITextureView> srv;   // Shader resource view
    int width = 0;
    int height = 0;
    TextureFormat format = TextureFormat::RGBA8;
};
```

---

## Phase 4: 2D Effects Pipeline

**Goal:** Implement all 2D texture processing operators

### Core Effects (Priority Order)
1. [ ] **Passthrough** - Identity transform, base for all effects
2. [ ] **Solid Color** - Fill with constant color
3. [ ] **Noise** - Perlin/Simplex noise generation
4. [ ] **Blur** - Gaussian blur (separable, two-pass)
5. [ ] **Brightness/Contrast** - Basic color correction
6. [ ] **HSV** - Hue/Saturation/Value adjustment
7. [ ] **Composite** - Blend two textures (add, multiply, screen, etc.)
8. [ ] **Transform** - Translate, rotate, scale
9. [ ] **Edge Detection** - Sobel/Canny edge filter
10. [ ] **Feedback** - Recursive buffer with decay

### Additional Effects
- [ ] Gradient (linear, radial, angular)
- [ ] Shape (circle, rectangle, polygon)
- [ ] Displacement mapping
- [ ] Chromatic aberration
- [ ] Pixelate
- [ ] Scanlines
- [ ] Mirror/Kaleidoscope
- [ ] Tile/Repeat

### Post-Processing Effects
- [ ] **Bloom** - Multi-pass glow (threshold → blur → composite)
- [ ] **Vignette** - Darkened edges
- [ ] **Film Grain** - Noise overlay for cinematic look
- [ ] **Tonemap** - HDR to LDR conversion (ACES, Reinhard, etc.)
- [ ] **Color Grade** - Lift/gamma/gain, color LUT
- [ ] **Radial Blur** - Zoom/spin blur from center point
- [ ] **Lens Distortion** - Barrel/pincushion distortion
- [ ] **Lens Flare** - Anamorphic streaks, ghosts
- [ ] **Glow** - Soft glow around bright areas
- [ ] **Motion Blur** - Velocity-based blur

### Text Rendering
- [ ] **Text** operator - Render text to texture
- [ ] Font loading (TTF via stb_truetype)
- [ ] Font atlas generation and caching
- [ ] Text alignment (left, center, right)
- [ ] `measureText()` for layout calculations

```cpp
// Text operator usage
auto title = chain.op<Text>("title")
    .text("Hello World")
    .font("fonts/Roboto.ttf", 48.0f)
    .color({1, 1, 1, 1})
    .align(TextAlign::Center)
    .position(width/2, 100);
```

### Effect Pipeline Architecture
```cpp
class Effect {
public:
    virtual void init(DiligentRenderer& renderer) = 0;
    virtual void apply(const Texture& input, Texture& output,
                       const EffectParams& params) = 0;
};

// Usage in chain
noise.process(ctx);
blur.input(noise).radius(5.0f).process(ctx);
```

---

## Phase 5: 3D Rendering Foundation

**Goal:** Basic 3D mesh rendering with transforms and camera

### Tasks
- [ ] Implement `Mesh` struct (vertex buffer, index buffer)
- [ ] Vertex format: position, normal, UV, tangent
- [ ] Primitive generators: cube, sphere, plane, cylinder, torus, cone, elliptic torus
- [ ] Model loading via DiligentTools GLTF loader (handles OBJ, glTF, FBX)
- [ ] Camera3D class (perspective projection, view matrix)
- [ ] Camera helpers: orbit(), zoom() for interactive control
- [ ] Model transform uniform buffer
- [ ] Depth buffer management
- [ ] Basic unlit 3D rendering
- [ ] GPU instancing support (Instance3D, drawMeshInstanced)

### Mesh Structure
```cpp
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct Mesh {
    Diligent::RefCntAutoPtr<IBuffer> vertexBuffer;
    Diligent::RefCntAutoPtr<IBuffer> indexBuffer;
    uint32_t vertexCount;
    uint32_t indexCount;
    BoundingBox bounds;
};
```

### Camera3D Helpers
```cpp
class Camera3D {
    glm::vec3 position, target, up;
    float fov, nearPlane, farPlane;

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspectRatio) const;

    // Interactive helpers
    void orbit(float yawDelta, float pitchDelta);  // Orbit around target
    void zoom(float delta);                         // Move toward/away from target
};
```

### GPU Instancing
```cpp
struct Instance3D {
    glm::mat4 model;   // Per-instance transform
    glm::vec4 color;   // Per-instance color
};

// Render thousands of instances in one draw call
ctx.drawMeshInstanced(mesh, instances, camera, output);
```

---

## Phase 6: PBR & Advanced Lighting

**Goal:** Physically-based rendering using DiligentFX

### Tasks
- [ ] Integrate DiligentFX PBR_Renderer
- [ ] Material system (albedo, metallic, roughness, AO, emissive)
- [ ] Normal mapping
- [ ] Light types: directional, point, spot
- [ ] Multiple lights in single pass
- [ ] Shadow mapping (using DiligentFX ShadowMapManager)
- [ ] Image-based lighting (IBL)
- [ ] Environment map loading (HDR → cubemap)
- [ ] Irradiance and radiance map generation

### Material Structure
```cpp
struct PBRMaterial {
    Texture albedoMap;
    Texture normalMap;
    Texture metallicRoughnessMap;  // G = roughness, B = metallic
    Texture aoMap;
    Texture emissiveMap;

    glm::vec3 albedo = {1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    glm::vec3 emissive = {0, 0, 0};
};
```

### Lighting Structure
```cpp
struct Light {
    enum Type { Directional, Point, Spot };
    Type type;
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 color;
    float intensity;
    float radius;        // Point/spot falloff
    float innerAngle;    // Spot cone
    float outerAngle;
    bool castShadows;
};

struct SceneLighting {
    std::vector<Light> lights;
    glm::vec3 ambientColor;
    float ambientIntensity;
    Texture* environmentMap;  // Optional IBL

    // Convenience presets
    static SceneLighting outdoor();    // Sun + sky ambient
    static SceneLighting indoor();     // Warm point light + cool ambient
    static SceneLighting threePoint(); // Key, fill, rim lights
};
```

### Decals
Project textures onto 3D geometry (bullet holes, grime, logos):
```cpp
auto decal = chain.op<Decal>("logo")
    .texture("logo.png")
    .position({0, 1, 0})
    .rotation({-90, 0, 0})  // Project downward
    .size({2, 2, 1})
    .blendMode(DecalBlend::Multiply);
```

---

## Phase 7: Media Pipeline

**Goal:** Video playback, HAP codec, camera input

### Video Playback
- [ ] VideoLoader interface (platform-agnostic)
- [ ] macOS implementation (AVFoundation)
- [ ] HAP codec support (FFmpeg demux + DXT upload)
- [ ] Playback controls: play, pause, seek, loop, speed
- [ ] Frame-accurate seeking
- [ ] Audio track extraction (future)

### Camera Input
- [ ] Webcam capture interface
- [ ] macOS implementation (AVFoundation)
- [ ] Device enumeration
- [ ] Resolution/framerate selection

### Platform Support
| Feature | macOS | Windows | Linux |
|---------|-------|---------|-------|
| H.264/H.265 | AVFoundation | Media Foundation | FFmpeg |
| HAP | FFmpeg | FFmpeg | FFmpeg |
| ProRes | AVFoundation | FFmpeg | FFmpeg |
| Webcam | AVFoundation | Media Foundation | V4L2 |

---

## Phase 8: Hot Reload System

**Goal:** Live code recompilation and reload

### Tasks
- [ ] File watcher for chain.cpp changes
- [ ] Invoke compiler (clang/MSVC) to build shared library
- [ ] Dynamic library loading/unloading
- [ ] Symbol resolution (setup, update functions)
- [ ] Error capture and display
- [ ] State preservation across reloads
- [ ] CMakeLists.txt generation for user code

### Compiler Interface
```cpp
class Compiler {
public:
    bool compile(const std::filesystem::path& projectPath);
    bool load();
    void unload();

    std::string lastError() const;

    // User-defined entry points
    using SetupFn = void(*)(Chain&);
    using UpdateFn = void(*)(Chain&, Context&);

    SetupFn getSetup();
    UpdateFn getUpdate();
};
```

---

## Phase 9: Chain API & Operators

**Goal:** High-level visual programming interface

### Core Concepts
```cpp
// User's chain.cpp
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

    // Metadata for UI/serialization
    virtual std::string name() const = 0;
    virtual std::vector<ParamDecl> params() = 0;
    virtual OutputKind outputKind() = 0;  // Texture, Value, Geometry

    // Line tracking for VS Code decorations
    int sourceLine = 0;
};
```

### Operator Categories

#### Generators (no input)
| Operator | Output | Description |
|----------|--------|-------------|
| Noise | Texture | Perlin/Simplex/Worley noise |
| Gradient | Texture | Linear/radial/angular gradient |
| Shape | Texture | Geometric shapes |
| Constant | Value | Constant float value |
| LFO | Value | Low-frequency oscillator |
| ImageFile | Texture | Load image from disk |
| VideoFile | Texture | Video playback |
| Webcam | Texture | Camera capture |

#### Filters (texture → texture)
| Operator | Description |
|----------|-------------|
| Blur | Gaussian blur |
| Brightness | Brightness/contrast |
| HSV | Hue/saturation/value |
| Edge | Edge detection |
| Transform | 2D transform |
| Displacement | UV displacement |
| ChromaticAberration | RGB channel offset |
| Pixelate | Pixelation |
| Scanlines | CRT scanline effect |
| Mirror | Mirror/kaleidoscope |
| Tile | Tiling/repeat |

#### Compositors (multiple inputs)
| Operator | Description |
|----------|-------------|
| Composite | Blend two textures |
| Switch | Select between inputs |
| Feedback | Recursive buffer |

#### 3D Operators
| Operator | Description |
|----------|-------------|
| Render3D | 3D scene to texture |
| Camera3D | 3D perspective camera |
| Mesh | 3D geometry |
| PointSprites | GPU particle rendering |
| Particles | Particle system |

#### Utility
| Operator | Description |
|----------|-------------|
| Math | Arithmetic operations |
| Logic | Boolean operations |
| Reference | Reference another node |
| Passthrough | Identity (debugging) |

---

## Phase 10: VS Code Extension

**Goal:** Live previews and editor integration

### Features
- [ ] WebSocket connection to runtime
- [ ] Inline preview decorations (thumbnail next to code)
- [ ] Hover for full preview
- [ ] Preview panel (all operators)
- [ ] Compile error diagnostics
- [ ] Status bar (connection state, FPS)
- [ ] Commands: Start/Stop Runtime, Reload, Show Preview

### Architecture
```
┌─────────────────┐         WebSocket          ┌─────────────────┐
│   VS Code       │◄───────────────────────────│    Runtime      │
│   Extension     │                            │                 │
│                 │    node_update (previews)  │                 │
│  - Decorations  │◄───────────────────────────│  - Renderer     │
│  - Preview Panel│    compile_status          │  - Compiler     │
│  - Status Bar   │◄───────────────────────────│  - PreviewServer│
└─────────────────┘                            └─────────────────┘
```

### Protocol
```typescript
// Runtime → Extension
interface NodeUpdate {
    id: string;
    line: number;
    kind: 'texture' | 'value' | 'geometry';
    preview?: string;  // base64 image (fallback)
    value?: number;
}

interface CompileStatus {
    success: boolean;
    message: string;
}

// Extension → Runtime
interface ReloadCommand { type: 'reload' }
interface PauseCommand { type: 'pause', paused: boolean }
```

### Shared Memory Preview System (High Performance)

WebSocket + base64 works for prototyping but doesn't scale (GPU readback blocks, base64 inflates data 33%). For production, use shared memory:

```
┌─────────────────────────────────────────────────────────────────┐
│                        MAIN RENDER THREAD                        │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐                      │
│  │ Op1     │───▶│ Op2     │───▶│ Op3     │───▶ Display          │
│  └────┬────┘    └────┬────┘    └────┬────┘                      │
│       │              │              │                            │
│       ▼              ▼              ▼                            │
│  Queue async    Queue async    Queue async   ◀── non-blocking   │
│  readback       readback       readback                         │
└─────────────────────────────────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│                      PREVIEW THREAD                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │ Wait for GPU │───▶│ Downsample   │───▶│ Write to     │       │
│  │ fence        │    │ to thumbnail │    │ shared mem   │       │
│  └──────────────┘    └──────────────┘    └──────────────┘       │
└─────────────────────────────────────────────────────────────────┘
                       │
        ───────────────┴───────────────
       │                               │
       ▼                               ▼
┌─────────────────┐           ┌─────────────────┐
│  SHARED MEMORY  │           │    WEBSOCKET    │
│  ┌───────────┐  │           │  (metadata only)│
│  │ Op1: RGB  │  │           │  {              │
│  │ 128x128   │  │           │    "ready": 1,  │
│  ├───────────┤  │           │    "frame": 42  │
│  │ Op2: RGB  │  │           │  }              │
│  └───────────┘  │           └─────────────────┘
└─────────────────┘                   │
        │                             │
        └──────────────┬──────────────┘
                       ▼
              ┌─────────────────┐
              │  VS CODE EXT    │
              │  (native module │
              │   reads mmap)   │
              └─────────────────┘
```

#### Shared Memory Layout
```cpp
constexpr int THUMB_SIZE = 128 * 128 * 3;  // RGB thumbnails
constexpr int MAX_OPERATORS = 64;

struct SharedPreviewHeader {
    uint32_t magic;              // 'VIVD'
    uint32_t version;
    uint32_t operatorCount;
    uint32_t frameNumber;
};

struct SharedPreviewSlot {
    char operatorId[64];
    int32_t sourceLine;
    uint32_t frameNumber;
    uint8_t kind;                // Texture, Value, Geometry
    uint8_t ready;
    uint8_t pixels[THUMB_SIZE];  // RGB thumbnail
};
```

#### Implementation Tasks
- [ ] Async GPU readback (non-blocking texture copy)
- [ ] Preview thread (separate from render loop)
- [ ] Shared memory segment (POSIX shm_open / Windows CreateFileMapping)
- [ ] VS Code native module (node-gyp) to read shared memory
- [ ] Fallback to WebSocket+base64 when native module unavailable

#### Performance Target
| Metric | WebSocket | Shared Memory |
|--------|-----------|---------------|
| Latency | 10-50ms | 1-5ms |
| Max operators at 60fps | ~10-20 | 64+ |
| CPU overhead | JSON encode/decode | Direct memory access |

---

## Phase 11: Addon System

**Goal:** Modular community extensions

### Architecture
- Pre-built static libraries (fast hot-reload)
- Auto-detection via `#include` directives
- Zero configuration for users
- Platform-aware (Windows/macOS/Linux)

### Addon Structure
```
addons/
├── vivid-spout/           # Windows texture sharing
│   ├── include/vivid/spout/
│   ├── src/
│   ├── addon.json
│   └── CMakeLists.txt
├── vivid-syphon/          # macOS texture sharing
├── vivid-ndi/             # Network video (NDI)
├── vivid-models/          # 3D model loading (Assimp)
├── vivid-storage/         # JSON persistence
├── vivid-nuklear/         # Nuklear GUI
└── vivid-csg/             # CSG boolean operations
```

### addon.json Schema
```json
{
    "name": "spout",
    "version": "1.0.0",
    "platforms": ["windows"],
    "detect_headers": ["vivid/spout/spout.h"],
    "libraries": {
        "windows": {
            "static": ["lib/vivid-spout.lib"],
            "system": ["opengl32.lib"]
        }
    }
}
```

### Usage
```cpp
// Just include and use - addon auto-detected
#include <vivid/spout/spout.h>

void update(Chain& chain, Context& ctx) {
    spout::send("OutputName", chain.get("final"));
}
```

### ImGui Addon (vivid-imgui)

Previously blocked due to WebGPU API mismatch. With Diligent Engine, ImGui integration is straightforward using DiligentTools:

```cpp
// addons/vivid-imgui/include/vivid/imgui/imgui.h
#pragma once
#include <imgui.h>

namespace vivid {
namespace imgui {

// Initialize ImGui with Diligent backend
void init(DiligentRenderer& renderer);

// Begin/End frame (call in update loop)
void beginFrame();
void endFrame();

// Render ImGui draw data
void render();

// Cleanup
void shutdown();

} // namespace imgui
} // namespace vivid
```

**Implementation:**
```cpp
// Uses DiligentTools ImGui integration
#include <DiligentTools/Imgui/interface/ImGuiImplDiligent.hpp>

class ImGuiImpl {
    std::unique_ptr<Diligent::ImGuiImplDiligent> impl_;

public:
    void init(IRenderDevice* device, IDeviceContext* ctx,
              TEXTURE_FORMAT rtFormat, TEXTURE_FORMAT dsFormat) {
        impl_ = std::make_unique<Diligent::ImGuiImplDiligent>(
            device, rtFormat, dsFormat);
    }

    void render(IDeviceContext* ctx) {
        impl_->Render(ctx);
    }
};
```

**Usage in chain.cpp:**
```cpp
#include <vivid/imgui/imgui.h>

void update(Chain& chain, Context& ctx) {
    imgui::beginFrame();

    ImGui::Begin("Controls");
    static float speed = 1.0f;
    ImGui::SliderFloat("Speed", &speed, 0.0f, 10.0f);
    ImGui::End();

    imgui::endFrame();

    auto noise = chain.op<Noise>("noise").speed(speed);
    // ...

    imgui::render();  // Render UI on top
}
```

### Addon Registry & Auto-CMake Generation

The compiler scans user code for addon `#include` directives and auto-generates CMakeLists.txt with proper linkage:

```
┌─────────────────────────────────────────────────────────────────┐
│                    AUTO-CMAKE GENERATION                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. User writes chain.cpp with addon includes:                  │
│     #include <vivid/spout/spout.h>                              │
│     #include <vivid/imgui/imgui.h>                              │
│                                                                 │
│  2. AddonRegistry scans source for #include patterns:           │
│     → Matches against addon detect_headers in addon.json        │
│     → Filters by current platform                               │
│                                                                 │
│  3. Compiler generates CMakeLists.txt:                          │
│     → Adds addon include directories                            │
│     → Links addon static libraries                              │
│     → Links system dependencies                                 │
│                                                                 │
│  4. User code compiles with zero configuration                  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### AddonRegistry Class
```cpp
class AddonRegistry {
public:
    // Load all addon.json files from addons directory
    void loadFromDirectory(const std::filesystem::path& addonsDir);

    // Scan source file for #include directives, return required addons
    std::vector<std::string> scanSourceForAddons(
        const std::filesystem::path& sourcePath) const;

    // Get addon info by name
    const AddonInfo* getAddon(const std::string& name) const;

    // Check if addon is available on current platform
    bool isAvailable(const std::string& name) const;

private:
    std::unordered_map<std::string, AddonInfo> addons_;
    std::unordered_map<std::string, std::string> headerToAddon_;
};
```

#### Generated CMakeLists.txt Template
```cmake
# Auto-generated CMakeLists.txt for Vivid project
cmake_minimum_required(VERSION 3.20)
project(vivid_operators)

set(CMAKE_CXX_STANDARD 20)

# Vivid paths (passed by runtime)
set(VIVID_INCLUDE_DIR "${VIVID_ROOT}/include")
set(VIVID_ADDONS_DIR "${VIVID_ROOT}/addons")

# Build operators shared library
add_library(operators SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/chain.cpp"
)

target_include_directories(operators PRIVATE
    ${VIVID_INCLUDE_DIR}
    ${VIVID_ADDONS_DIR}/include
)

# === AUTO-DETECTED ADDONS ===
# Generated based on #include directives in chain.cpp

# Addon: imgui
target_link_libraries(operators PRIVATE
    ${VIVID_ADDONS_DIR}/lib/libvivid-imgui.a
)

# Addon: spout (Windows only)
if(WIN32)
    target_link_libraries(operators PRIVATE
        ${VIVID_ADDONS_DIR}/lib/vivid-spout.lib
        opengl32.lib
    )
endif()

# === END ADDONS ===
```

#### Error Messages
```
[Addon] Error: 'spout' is not available on this platform (macOS)
        Spout is Windows-only. Use 'syphon' on macOS instead.

        Replace:
          #include <vivid/spout/spout.h>
        With:
          #include <vivid/syphon/syphon.h>
```

```
[Addon] Error: 'imgui' addon library not found
        Expected: build/addons/lib/libvivid-imgui.a

        Rebuild Vivid with addons:
          cmake --build build --target vivid-imgui
```

---

## Phase 12: CLI & Project Templates

**Goal:** User-friendly command-line interface

### Commands
```bash
# Create new project
vivid new my-project

# Run a project
vivid my-project
vivid run my-project

# Run in specific mode
vivid my-project --headless    # No window (render to file)
vivid my-project --record      # Record to video

# Development
vivid --version
vivid --help
```

### Project Template Structure
```
my-project/
├── chain.cpp       # Main code (setup/update)
├── SPEC.md         # Project specification
├── CLAUDE.md       # AI context
└── assets/         # Images, videos, models
```

### Template chain.cpp
```cpp
#include <vivid/vivid.h>
using namespace vivid;

void setup(Chain& chain) {
    // GOAL: Configure output resolution and format
    chain.setResolution(1920, 1080);
    chain.setOutput("output");
}

void update(Chain& chain, Context& ctx) {
    // GOAL: Create visual output
    auto noise = chain.op<Noise>("noise")
        .scale(4.0f)
        .speed(0.5f);

    chain.op<Output>("output")
        .input(noise);
}

VIVID_CHAIN(setup, update)
```

---

## Phase 13: Audio System

**Goal:** Audio input, analysis, synthesis, and external protocols

### Audio Input & Analysis
```cpp
class AudioIn : public Operator {
    AudioIn& source(AudioSource src);  // System, Microphone, File
    AudioIn& fftSize(int size);        // 512, 1024, 2048, 4096
    AudioIn& smoothing(float s);       // 0.0-1.0

    // Outputs: spectrum, bass, mids, highs, volume, waveform
};
```

### Beat Detection
```cpp
struct BeatInfo {
    bool isBeat;           // True on beat onset
    float confidence;      // 0-1
    float bpm;             // Estimated tempo
    float phase;           // 0-1, position in beat cycle
};

class BeatDetector {
    void process(const float* samples, int count);
    BeatInfo getInfo() const;
    void setSensitivity(float s);  // 0-1
};
```

### Audio Synthesis
- Oscillators: sine, square, sawtooth, triangle, noise
- ADSR envelope generator
- Filters: lowpass, highpass, bandpass, notch

### MIDI Support
```cpp
class MidiIn : public Operator {
    MidiIn& port(const std::string& name);
    MidiIn& channel(int ch);  // 1-16, or 0 for all
    // Outputs: noteOn, noteOff, velocity, cc:{n}, pitchBend
};

class MidiOut : public Operator {
    void noteOn(int note, int velocity);
    void controlChange(int cc, int value);
};
```

### OSC Support
```cpp
class OscIn : public Operator {
    OscIn& port(int port);
    OscIn& address(const std::string& pattern);
};

class OscOut : public Operator {
    OscOut& host(const std::string& hostname);
    OscOut& port(int port);
    void send(const std::string& address, float value);
};
```

### Dependencies
| Library | Purpose | License |
|---------|---------|---------|
| KissFFT | FFT analysis | BSD |
| RtMidi | MIDI I/O | MIT |
| oscpack | OSC protocol | Public Domain |
| miniaudio | Audio I/O | Public Domain |

---

## Phase 14: Export & Distribution

**Goal:** Video export, image sequences, WASM, standalone apps

### Video Recording
```cpp
class Record : public Operator {
    Record& output(const std::string& path);
    Record& codec(VideoCodec codec);  // H264, ProRes, HAP
    Record& fps(float framerate);
    Record& quality(float q);
    void start();
    void stop();
};
```

### Supported Codecs
| Codec | Format | Platform |
|-------|--------|----------|
| H.264 | MP4 | All |
| H.265/HEVC | MP4 | All |
| ProRes 422/4444 | MOV | macOS |
| HAP / HAP Alpha | MOV | All (VJ-friendly) |

### Image Sequence Export
```cpp
class ImageSequence : public Operator {
    ImageSequence& output(const std::string& pattern);  // "frame_%04d.png"
    ImageSequence& format(ImageFormat fmt);  // PNG, EXR, TIFF
    void saveFrame();
};
```

### WASM Export
```bash
vivid export --wasm my-project --output dist/
```
- Compile to WebAssembly for browser playback
- WebGPU rendering in browser
- Asset bundling
- HTML template generation

### Standalone Export
```bash
vivid export --standalone my-project --output MyProject.app
```
- macOS .app bundle
- Windows .exe with dependencies
- Linux AppImage
- Minimal runtime (no hot-reload)
- Embedded assets

### Offline Rendering
```cpp
class OfflineRenderer {
    void setFrameRate(float fps);
    void setDuration(float seconds);
    void render();  // Fixed timestep, deterministic output
};
```

---

## Phase 15: Input & Interaction

**Goal:** Mouse, keyboard, gamepad, touch input, and window management

### Window Management
```cpp
// Display modes
ctx.setFullscreen(true);           // Enter fullscreen
ctx.toggleFullscreen();            // Toggle fullscreen/windowed
ctx.setBorderless(true);           // Borderless window mode

// Window properties
ctx.setWindowPosition(100, 100);
ctx.setWindowSize(1920, 1080);
ctx.moveToMonitor(1);              // Move to specific monitor
ctx.setAlwaysOnTop(true);          // Keep window above others

// Cursor
ctx.setCursorVisible(false);       // Hide cursor (for installations)

// VSync
ctx.setVSync(true);                // Cap to monitor refresh rate
```

### Mouse Input
```cpp
void update(Chain& chain, Context& ctx) {
    glm::vec2 pos = ctx.mousePosition();  // Normalized 0-1
    bool leftDown = ctx.isMouseButtonDown(MouseButton::Left);
    bool rightPressed = ctx.wasMouseButtonPressed(MouseButton::Right);
    glm::vec2 scroll = ctx.mouseScroll();

    if (ctx.isMouseDragging(MouseButton::Left)) {
        glm::vec2 dragDelta = ctx.mouseDragDelta();
    }
}
```

### Keyboard Input
```cpp
bool spaceDown = ctx.isKeyDown(Key::Space);
bool enterPressed = ctx.wasKeyPressed(Key::Enter);
std::string text = ctx.textInput();  // Characters typed this frame
```

### Gamepad Input
```cpp
if (ctx.isGamepadConnected(0)) {
    glm::vec2 leftStick = ctx.gamepadAxis(0, GamepadAxis::LeftStick);
    float leftTrigger = ctx.gamepadTrigger(0, GamepadTrigger::Left);
    bool aPressed = ctx.wasGamepadButtonPressed(0, GamepadButton::A);
}
```

### Touch Input (Mobile/Tablet)
```cpp
int touchCount = ctx.touchCount();
for (int i = 0; i < touchCount; i++) {
    Touch touch = ctx.getTouch(i);
    // touch.position, touch.id, touch.phase
}

if (ctx.wasPinchGesture()) {
    float scale = ctx.pinchScale();
}
```

---

## Phase 16: ML Integration

**Goal:** Machine learning via ONNX Runtime

### ONNX Runtime Infrastructure
```cpp
class ONNXSession {
    bool load(const std::string& modelPath);
    bool run(const Tensor& input, Tensor& output);
};
```

Execution Providers: CPU, CUDA, DirectML, CoreML, Metal

### Pose Detection
```cpp
class PoseDetector : public Operator {
    PoseDetector& input(const std::string& node);
    PoseDetector& model(PoseModel model);  // MoveNet, BlazePose
    // Outputs: keypoints, connections, mask
};
```

Models: MoveNet Lightning/Thunder, BlazePose Lite/Full/Heavy

### Background Segmentation
```cpp
class SegmentBackground : public Operator {
    SegmentBackground& input(const std::string& node);
    SegmentBackground& model(SegmentModel model);
    // Outputs: mask, foreground, background
};
```

Models: MediaPipe Selfie, BodyPix, MODNet, Robust Video Matting

### Style Transfer
```cpp
class StyleTransfer : public Operator {
    StyleTransfer& input(const std::string& node);
    StyleTransfer& style(const std::string& stylePath);  // Image or .onnx
    StyleTransfer& strength(float s);  // 0-1
};
```

Pre-trained: Mosaic, Candy, Udnie, Starry Night, etc.
Arbitrary: Any style image

### Object Detection
```cpp
class ObjectDetector : public Operator {
    ObjectDetector& input(const std::string& node);
    ObjectDetector& model(DetectionModel model);  // YOLO, SSD
    // Outputs: boxes, labels, overlay
};
```

### Face Detection & Landmarks
```cpp
class FaceDetector : public Operator {
    FaceDetector& input(const std::string& node);
    FaceDetector& landmarks(bool enable);  // 68/468 points
    FaceDetector& expressions(bool enable);
    // Outputs: faces, landmarks, expressions, mesh
};
```

### Model Registry
```
~/.vivid/models/
├── movenet_lightning.onnx
├── mediapipe_selfie.onnx
├── yolov8n.onnx
└── style_mosaic.onnx
```

Models downloaded on first use and cached locally.

---

## Phase 17: Community Operator Registry

**Goal:** Package management for sharing operators (like npm/cargo)

### Package Manifest (vivid.toml)
```toml
[package]
name = "glitch-fx"
version = "1.0.0"
description = "Glitch and datamosh effects"
authors = ["Jane Doe <jane@example.com>"]
license = "MIT"

[vivid]
version = ">=0.2.0"

[dependencies]
noise-utils = "^1.0"

[[operators]]
name = "Glitch"
file = "src/glitch.cpp"

[[shaders]]
path = "shaders/glitch.hlsl"
```

### CLI Commands
```bash
vivid init my-effects      # Create new package
vivid install glitch-fx    # Install from registry
vivid search glitch        # Search packages
vivid publish              # Publish to registry
vivid build                # Build package
```

### Registry Infrastructure
- REST API at registry.vivid.dev
- Package index with versioning
- Web interface for browsing/search
- User accounts and ownership

### Package Structure
```
my-package/
├── vivid.toml
├── README.md
├── LICENSE
├── src/
│   └── *.cpp
├── shaders/
│   └── *.hlsl
├── assets/
└── examples/
```

### Features
- Semantic versioning with constraints
- Dependency resolution
- Scoped packages (@user/package)
- Optional package signing
- Private registry support

---

## Phase 18: Testing Infrastructure

**Goal:** Systematic verification that everything works as expected

### Testing Layers

| Layer | What | How |
|-------|------|-----|
| Unit Tests | Core logic (math, parsing, data structures) | Catch2 |
| Operator Tests | Individual operator correctness | Golden image comparison |
| Integration Tests | Full chain execution | Example-based smoke tests |
| Visual Regression | Detect unintended rendering changes | Automated screenshot diff |

### Unit Testing (Non-Visual)
- [ ] Integrate Catch2 (header-only, simple setup)
- [ ] Test math utilities, parameter parsing, file loading
- [ ] Test Chain API graph construction
- [ ] Test addon detection and CMake generation
- [ ] CMake `add_test()` targets for `ctest` integration

```cpp
// tests/test_chain.cpp
TEST_CASE("Chain builds operator graph") {
    Chain chain;
    auto noise = chain.op<Noise>("n1");
    auto blur = chain.op<Blur>("b1").input(noise);

    REQUIRE(chain.getOperator("n1") != nullptr);
    REQUIRE(chain.getInputFor("b1") == "n1");
}
```

### Golden Image Testing (Visual)

Capture reference screenshots, compare new renders with tolerance:

```
tests/golden/
├── noise_perlin.png
├── blur_radius_10.png
├── composite_add.png
├── pbr_gold_sphere.png
└── ...
```

**Workflow:**
1. `--capture` mode: Render operator → save as golden image
2. `--compare` mode: Render operator → compare to golden (tolerance threshold)
3. `--update` mode: Compare, and update golden if different

```bash
# Capture new golden images
./vivid-test --capture --output tests/golden/

# Compare against golden (CI mode)
./vivid-test --compare --tolerance 0.01

# Update golden images after intentional changes
./vivid-test --update
```

**Tolerance:** Allow small pixel differences (anti-aliasing, floating point) - typically 1-2% threshold.

### Headless Rendering

For CI/CD without a display:
- [ ] Software rasterizer fallback (Mesa llvmpipe on Linux, WARP on Windows)
- [ ] Offscreen render targets (no swap chain required)
- [ ] Fixed resolution for reproducible results (e.g., 256x256 for speed)

### Integration Tests (Example-Based)

Extend existing `test-examples.sh` approach:
- [ ] Run each example for N frames
- [ ] Check for crashes, error messages, memory leaks
- [ ] Optional: capture final frame for visual regression

```bash
./scripts/test-examples.sh --frames 60 --capture-final
```

### Operator Test Matrix

Each operator should have tests covering:
- Default parameters
- Edge cases (zero radius blur, 100% feedback, etc.)
- Different input resolutions
- Chain combinations

| Operator | Unit Test | Golden Image | Edge Cases |
|----------|-----------|--------------|------------|
| Noise | ✓ params | ✓ perlin, simplex, worley | scale=0, speed=negative |
| Blur | ✓ params | ✓ radius 1,5,20 | radius=0, large radius |
| Composite | ✓ modes | ✓ add, multiply, screen | alpha=0, alpha=1 |
| ... | | | |

### CI/CD Integration (GitHub Actions)

```yaml
# .github/workflows/test.yml
name: Tests
on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake -B build && cmake --build build
      - run: ctest --test-dir build

  visual-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: apt-get install -y mesa-utils  # Software renderer
      - run: ./vivid-test --compare --tolerance 0.02
      - uses: actions/upload-artifact@v4
        if: failure()
        with:
          name: visual-diff
          path: tests/diff/
```

### Test Utilities

```cpp
namespace vivid::test {
    // Render operator to texture, return pixels
    std::vector<uint8_t> renderOperator(Operator& op, int width, int height);

    // Compare two images with tolerance (returns similarity 0-1)
    float compareImages(const uint8_t* a, const uint8_t* b, int w, int h);

    // Save/load golden images
    void saveGolden(const std::string& name, const uint8_t* pixels, int w, int h);
    bool compareToGolden(const std::string& name, const uint8_t* pixels, int w, int h, float tolerance);
}
```

### Test Directory Structure

```
vivid/
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/
│   │   ├── test_chain.cpp
│   │   ├── test_math.cpp
│   │   └── test_params.cpp
│   ├── visual/
│   │   ├── test_operators.cpp
│   │   └── test_3d.cpp
│   ├── golden/
│   │   └── *.png
│   └── diff/              # Generated on comparison failure
│       └── *.png
├── scripts/
│   └── test-examples.sh
└── .github/
    └── workflows/
        └── test.yml
```

### Success Criteria

- [ ] `ctest` runs all unit tests
- [ ] `vivid-test --compare` validates visual output
- [ ] CI passes on every PR
- [ ] Golden images updated intentionally (reviewed in PR)
- [ ] No flaky tests (deterministic rendering)

---

## Build System

### Directory Structure (Final)
```
vivid/
├── CMakeLists.txt
├── external/
│   └── DiligentEngine/     # Git submodule
├── runtime/
│   ├── CMakeLists.txt
│   ├── include/vivid/
│   │   ├── vivid.h         # Main include
│   │   ├── chain.h
│   │   ├── context.h
│   │   ├── operators.h
│   │   └── types.h
│   └── src/
│       ├── main.cpp
│       ├── diligent_renderer.cpp
│       ├── chain.cpp
│       ├── context.cpp
│       ├── compiler.cpp
│       ├── texture.cpp
│       ├── mesh.cpp
│       ├── video_loader.cpp
│       └── preview_server.cpp
├── operators/
│   ├── CMakeLists.txt
│   └── *.cpp               # Built-in operators
├── shaders/
│   └── hlsl/
│       ├── common/
│       └── effects/
├── vscode-extension/
│   ├── package.json
│   └── src/
├── addons/
│   └── vivid-*/
├── examples/
│   └── hello/
└── templates/
    └── default/
```

### CMake Configuration
```cmake
cmake_minimum_required(VERSION 3.20)
project(vivid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Diligent Engine
add_subdirectory(external/DiligentEngine)

# Dependencies
include(FetchContent)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.3.8)
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
FetchContent_MakeAvailable(glfw glm json)

# Runtime
add_subdirectory(runtime)

# Operators
add_subdirectory(operators)

# Addons (optional)
if(VIVID_BUILD_ADDONS)
    add_subdirectory(addons)
endif()
```

---

## Implementation Order

### Sprint 1: Core Loop
1. Phase 1 (Foundation) - Window, Diligent init, clear screen
2. Phase 2 (Shaders) - HLSL loading, basic PSO
3. Phase 3 (Textures) - Create, load, render-to-texture

### Sprint 2: 2D Effects & Chain
4. Phase 4 (Effects) - Essential 2D operators
5. Phase 8 (Hot Reload) - Basic compile/reload cycle
6. Phase 9 (Chain API) - Operator system, fluent API

### Sprint 3: 3D Rendering
7. Phase 5 (3D Foundation) - Mesh, camera, basic 3D
8. Phase 6 (PBR) - DiligentFX integration

### Sprint 4: Media & Input
9. Phase 7 (Media) - Video playback, HAP, webcam
10. Phase 15 (Input) - Mouse, keyboard, gamepad

### Sprint 5: Tooling
11. Phase 10 (VS Code) - Extension and previews
12. Phase 11 (Addons) - Addon system + ImGui
13. Phase 12 (CLI) - Command-line and templates

### Sprint 6: Audio
14. Phase 13 (Audio) - Audio input, FFT, beat detection
15. Phase 13b (Protocols) - MIDI, OSC support

### Sprint 7: Export
16. Phase 14 (Recording) - Video recording, image sequences
17. Phase 14b (Distribution) - WASM, standalone apps

### Sprint 8: Advanced
18. Phase 16 (ML) - ONNX, pose detection, segmentation
19. Phase 17 (Registry) - Community package registry

### Ongoing: Testing (Phase 18)
- Set up Catch2 after Sprint 1 (test core utilities)
- Add golden image tests as each operator is implemented (Sprint 2+)
- CI/CD integration after Sprint 5 (when tooling is ready)
- Expand operator test matrix throughout development

---

## Reference: Previous Operators

These operators existed in the original codebase and should be reimplemented:

### Texture Operators
- blur, brightness, chromatic_aberration, composite, displacement
- edge, feedback, gradient, hsv, imagefile, mirror
- noise, passthrough, pixelate, scanlines, shape, tile, transform

### Post-Processing Operators
- bloom, vignette, film_grain, tonemap, color_grade
- radial_blur, lens_distortion, lens_flare, glow, motion_blur

### Text Operators
- text

### Value Operators
- constant, lfo, math, logic, switch

### Media Operators
- videofile, webcam

### 3D Operators
- render3d, pointsprites, particles, decal

### Audio Operators
- audioin, beat

### Protocol Operators
- midiin, midiout, oscin, oscout

### ML Operators
- posedetector, segmentbackground, styletransfer, objectdetector, facedetector

### Export Operators
- record, imagesequence

### Utility
- reference

---

## Reference: VS Code Extension Features

From the original PLAN-04:

- Runtime connection via WebSocket (port 9876)
- Inline decorations showing operator output type
- Hover previews with base64 images
- Preview panel with all operators
- Status bar showing connection state
- Commands: Start/Stop Runtime, Reload, Toggle Decorations
- Auto-connect when opening Vivid project
- Compile error diagnostics in Problems panel

---

## Reference: Addon List

Addons that existed or were planned:

| Addon | Platform | Description | Status |
|-------|----------|-------------|--------|
| vivid-spout | Windows | Texture sharing (Spout2) | Reimplement |
| vivid-syphon | macOS | Texture sharing (Syphon) | Reimplement |
| vivid-models | All | 3D model loading (Assimp) | Reimplement |
| vivid-storage | All | JSON key/value persistence | Reimplement |
| vivid-nuklear | All | Nuklear GUI integration | Reimplement |
| vivid-csg | All | CSG boolean operations (Manifold) | Reimplement |
| vivid-imgui | All | ImGui integration (via DiligentTools) | **Now Possible** |
| vivid-ndi | All | Network video (NDI SDK) | Planned |

---

## Success Criteria

Each phase is complete when:

- [ ] All tests pass
- [ ] Examples render correctly
- [ ] Hot-reload works
- [ ] Performance is acceptable (60fps for 1080p)
- [ ] No memory leaks (validated with sanitizers)

Final success:
- [ ] All original examples work with new implementation
- [ ] VS Code extension connects and shows previews
- [ ] Addons can be used with zero configuration
- [ ] `vivid new` and `vivid run` work as expected
