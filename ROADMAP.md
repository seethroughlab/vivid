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

## ⚠️ CRITICAL: Use DiligentFX for 3D/PBR Rendering

**DO NOT write custom PBR shaders.** DiligentFX provides production-quality, tested implementations:

| Feature | DiligentFX Class | DO NOT Write Custom |
|---------|------------------|---------------------|
| PBR Rendering | `PBR_Renderer` | No custom Cook-Torrance shaders |
| IBL Precomputation | `PBR_Renderer::PrecomputeCubemaps()` | No custom irradiance/prefilter code |
| GLTF Loading | `GLTF_PBR_Renderer` | No custom model loaders |
| Shadow Mapping | `ShadowMapManager` | No custom shadow code |
| Post-Processing | `PostFXContext` | No custom bloom/tonemap shaders |

**Why this matters:**
- DiligentFX shaders are tested across all platforms and backends
- IBL precomputation is complex (spherical harmonics, GGX filtering)
- Material system handles all PBR texture permutations
- Shadow cascades, PCF filtering already implemented
- Code is maintained by Diligent team, not us

**The only custom shaders should be for:**
- 2D image effects (blur, color correction, etc.)
- Novel effects not covered by DiligentFX
- Chain-specific compositing operations

**Before writing ANY shader code, verify:**
1. Is this a 3D/PBR feature? → Use DiligentFX
2. Is this a standard effect (shadows, SSAO, bloom)? → Check DiligentFX first
3. Is this truly a novel 2D effect? → Then custom HLSL is appropriate

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

## Phase 3: Additional 2D Operators ✓

**Goal:** Expand the operator library with more effects

**Status:** Complete (macOS verified)

### Effects
- [x] **Passthrough** - Identity transform
- [x] **Brightness/Contrast** - Basic color correction
- [x] **HSV** - Hue/Saturation/Value adjustment
- [x] **Transform** - Translate, rotate, scale
- [x] **Feedback** - Recursive buffer with decay
- [x] **Gradient** - Linear, radial, angular gradients
- [x] **Edge Detection** - Sobel filter
- [x] **Displacement** - UV displacement mapping
- [x] **Chromatic Aberration** - RGB channel separation
- [x] **Pixelate** - Pixelation effect
- [x] **Mirror** - Mirror/Kaleidoscope effect

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

## Phase 4: 3D Rendering ✓

**Goal:** Basic 3D with camera, meshes, and PBR materials

**Status:** Complete (macOS verified)

### Tasks
- [x] Vertex format: position, normal, UV, tangent (vec3)
- [x] MeshUtils: sphere (UV sphere with proper tangents)
- [x] Camera3D: perspective, view matrix, orbit helpers
- [x] Depth buffer management
- [x] DiligentFX PBR_Renderer integration
- [x] IBL cubemap precomputation from HDR environment
- [x] PBR material textures (albedo, normal, metallic, roughness, AO)
- [ ] MeshUtils: cube, plane, cylinder, torus, cone (future)
- [ ] Multiple light support (future)
- [ ] Shadow mapping via ShadowMapManager (future)
- [ ] GLTF model loading via GLTF_PBR_Renderer (future)

---

### Procedural Geometry

#### Vertex Structure (44 bytes, must match exactly)
```cpp
struct Vertex3D {
    glm::vec3 position;   // 12 bytes - ATTRIB0
    glm::vec3 normal;     // 12 bytes - ATTRIB1
    glm::vec2 uv;         // 8 bytes  - ATTRIB2
    glm::vec3 tangent;    // 12 bytes - ATTRIB7 (DiligentFX expects float3, not float4)
};
// Total: 44 bytes stride
```

**Critical:** DiligentFX shaders expect tangent as `float3` at ATTRIB7, not `float4`. Using a 4-component tangent causes vertex stride mismatch and rendering corruption.

#### UV Sphere Generation
```cpp
void generateUVSphere(std::vector<Vertex3D>& vertices,
                      std::vector<uint32_t>& indices,
                      float radius, int segments, int rings) {
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = glm::pi<float>() * ring / rings;  // 0 to PI
        float y = cos(phi);
        float ringRadius = sin(phi);

        for (int seg = 0; seg <= segments; ++seg) {
            float theta = 2.0f * glm::pi<float>() * seg / segments;  // 0 to 2PI

            Vertex3D v;
            v.position = glm::vec3(
                ringRadius * cos(theta),
                y,
                ringRadius * sin(theta)
            ) * radius;
            v.normal = glm::normalize(v.position);
            v.uv = glm::vec2(
                static_cast<float>(seg) / segments,
                static_cast<float>(ring) / rings
            );

            // Tangent: derivative of position w.r.t. theta
            v.tangent = glm::normalize(glm::vec3(
                -sin(theta),
                0.0f,
                cos(theta)
            ));

            vertices.push_back(v);
        }
    }

    // Generate indices (triangle strips converted to triangles)
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            int curr = ring * (segments + 1) + seg;
            int next = curr + segments + 1;

            indices.push_back(curr);
            indices.push_back(next);
            indices.push_back(curr + 1);

            indices.push_back(curr + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
}
```

**Key insight:** Tangent is the partial derivative of position with respect to the UV.x direction (theta for a sphere). This ensures normal mapping works correctly.

---

### PBR Material System

#### Texture Array Requirement (CRITICAL)
DiligentFX PBR shaders declare all texture maps as `Texture2DArray`, not `Texture2D`:
```hlsl
// In DiligentFX shaders:
Texture2DArray g_BaseColorMap;
Texture2DArray g_NormalMap;
Texture2DArray g_MetallicMap;
// etc.
```

**All material textures must be created as `RESOURCE_DIM_TEX_2D_ARRAY` with ArraySize=1**, even for single textures. Using `RESOURCE_DIM_TEX_2D` will fail silently - the shader will sample but get undefined values.

#### Loading Textures as Arrays
```cpp
bool loadTextureAsArray(Context& ctx, const std::string& path,
                        ITexture*& tex, ITextureView*& srv, bool srgb) {
    TextureLoadInfo loadInfo;
    loadInfo.IsSRGB = srgb;
    loadInfo.GenerateMips = true;
    loadInfo.Name = path.c_str();

    RefCntAutoPtr<ITextureLoader> pLoader;
    CreateTextureLoaderFromFile(path.c_str(), IMAGE_FILE_FORMAT_UNKNOWN,
                                loadInfo, &pLoader);
    if (!pLoader) return false;

    const TextureDesc& srcDesc = pLoader->GetTextureDesc();

    // Create as texture array with 1 slice
    TextureDesc arrayDesc;
    arrayDesc.Name = path.c_str();
    arrayDesc.Type = RESOURCE_DIM_TEX_2D_ARRAY;  // CRITICAL!
    arrayDesc.Width = srcDesc.Width;
    arrayDesc.Height = srcDesc.Height;
    arrayDesc.ArraySize = 1;
    arrayDesc.MipLevels = srcDesc.MipLevels;
    arrayDesc.Format = srcDesc.Format;
    arrayDesc.BindFlags = BIND_SHADER_RESOURCE;
    arrayDesc.Usage = USAGE_IMMUTABLE;

    // Copy subresource data for all mip levels
    std::vector<TextureSubResData> subResData(srcDesc.MipLevels);
    for (Uint32 mip = 0; mip < srcDesc.MipLevels; ++mip) {
        subResData[mip] = pLoader->GetSubresourceData(mip, 0);
    }

    TextureData initData;
    initData.pSubResources = subResData.data();
    initData.NumSubresources = srcDesc.MipLevels;

    RefCntAutoPtr<ITexture> texture;
    ctx.device()->CreateTexture(arrayDesc, &initData, &texture);
    if (!texture) return false;

    tex = texture.Detach();
    srv = tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    return true;
}
```

#### PBRMaterial Class
```cpp
class PBRMaterial {
public:
    // Texture SRVs (all Texture2DArray)
    ITexture* albedoTex_ = nullptr;      // sRGB
    ITextureView* albedoSRV_ = nullptr;

    ITexture* normalTex_ = nullptr;      // Linear
    ITextureView* normalSRV_ = nullptr;

    ITexture* metallicTex_ = nullptr;    // Linear, single channel
    ITextureView* metallicSRV_ = nullptr;

    ITexture* roughnessTex_ = nullptr;   // Linear, single channel
    ITextureView* roughnessSRV_ = nullptr;

    ITexture* aoTex_ = nullptr;          // Linear, single channel
    ITextureView* aoSRV_ = nullptr;

    // Default fallback textures (also Texture2DArray)
    ITexture* defaultWhiteTex_ = nullptr;
    ITextureView* defaultWhiteSRV_ = nullptr;

    ITexture* defaultNormalTex_ = nullptr;  // (0.5, 0.5, 1.0) flat normal
    ITextureView* defaultNormalSRV_ = nullptr;

    ISampler* sampler_ = nullptr;

    bool loadFromDirectory(Context& ctx, const std::string& dirPath);
    ITextureView* getAlbedoSRV() const { return albedoSRV_ ? albedoSRV_ : defaultWhiteSRV_; }
    // ... similar getters for other textures
};
```

---

### DiligentFX PBR_Renderer Integration

#### Constant Buffer Structures
The PBR_Renderer requires specific constant buffer layouts:

```cpp
// Frame-level constants (1552 bytes)
// Use PBRFrameAttribs from PBR_Structures.fxh

// Per-primitive constants (128 bytes)
struct alignas(16) PrimitiveCB {
    glm::mat4 model;      // 64 bytes - row-major for Diligent
    glm::mat4 normalMat;  // 64 bytes - inverse transpose of model
};

// Material constants - basic (96 bytes)
// Use PBRMaterialBasicAttribs from RenderPBR_Structures.fxh

// Material constants - textured (336 bytes for 5 textures)
// PBRMaterialBasicAttribs (96) + 5 * PBRMaterialTextureAttribs (48 each)
```

#### Matrix Conventions (GLM vs Diligent)
**Critical:** GLM uses column-major matrices, Diligent expects row-major.

```cpp
// WRONG - will cause inverted/corrupted rendering
glm::mat4 view = glm::lookAt(eye, target, up);
glm::mat4 proj = glm::perspective(fov, aspect, near, far);

// CORRECT - transpose before uploading to GPU
glm::mat4 viewT = glm::transpose(view);
glm::mat4 projT = glm::transpose(proj);
// Upload viewT and projT to constant buffer
```

#### PBRMaterialTextureAttribs Structure
```cpp
// Each texture needs these attributes (48 bytes)
struct PBRMaterialTextureAttribs {
    uint32_t PackedProps;           // Bits [0-2]: UVSelector (1 = UV0)
    float TextureSlice;             // Array slice (0.0 for single textures)
    float UBias;                    // UV offset
    float VBias;
    float4 UVScaleAndRotation;      // (scaleX, rotateXY, rotateYX, scaleY)
    float4 AtlasUVScaleAndBias;     // For texture atlases
};
```

**PackedProps UV selector:** The low 3 bits select which UV set to use. Value `1` means use UV0 (the primary UV coordinates). Value `0` means don't sample (uses default).

#### Setting Up Texture Attributes
```cpp
void setupTextureAttribs(PBRMaterialTextureAttribs* texAttrib) {
    texAttrib->PackedProps = 1;  // Use UV0
    texAttrib->TextureSlice = 0.0f;
    texAttrib->UBias = 0.0f;
    texAttrib->VBias = 0.0f;
    texAttrib->UVScaleAndRotation = float4(1.0f, 0.0f, 0.0f, 1.0f);  // Identity
    texAttrib->AtlasUVScaleAndBias = float4(1.0f, 1.0f, 0.0f, 0.0f);
}
```

#### PSO Flags for Textured Materials
```cpp
PBR_Renderer::PSO_FLAGS texturedPsoFlags =
    PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
    PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
    PBR_Renderer::PSO_FLAG_USE_METALLIC_MAP |
    PBR_Renderer::PSO_FLAG_USE_ROUGHNESS_MAP |
    PBR_Renderer::PSO_FLAG_USE_AO_MAP |
    PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM;  // Required for UV attribs
```

#### Binding Textures to SRB
```cpp
void bindMaterialTextures(IShaderResourceBinding* srb, PBRMaterial& mat) {
    auto setTex = [&](const char* name, ITextureView* srv) {
        if (auto* var = srb->GetVariableByName(SHADER_TYPE_PIXEL, name)) {
            var->Set(srv);
        }
    };

    setTex("g_BaseColorMap", mat.getAlbedoSRV());
    setTex("g_NormalMap", mat.getNormalSRV());
    setTex("g_MetallicMap", mat.getMetallicSRV());
    setTex("g_RoughnessMap", mat.getRoughnessSRV());
    setTex("g_OcclusionMap", mat.getAoSRV());
}
```

---

### Common Pitfalls & Solutions

| Problem | Symptom | Solution |
|---------|---------|----------|
| Texture2D instead of Texture2DArray | Textures don't appear, default color shows | Load all textures with `RESOURCE_DIM_TEX_2D_ARRAY` |
| Wrong vertex stride | Mesh appears corrupted/exploded | Ensure 44-byte stride, tangent as float3 not float4 |
| Matrix not transposed | Scene rendered inverted/wrong orientation | Transpose GLM matrices before uploading |
| BaseColorFactor gray | Textures appear darker than expected | Set to white (1,1,1,1) for textured materials |
| UV selector not set | Textures mapped but show default color | Set `PackedProps = 1` to enable UV0 |
| Missing ENABLE_TEXCOORD_TRANSFORM | UV transforms ignored | Add PSO flag when using texture attribs |
| Wrong constant buffer size | Validation errors or corruption | Basic=96 bytes, +48 per texture with attribs |

---

### Files
```
runtime/
├── include/vivid/
│   ├── pbr_material.h      # PBR material class
│   └── mesh.h              # Procedural geometry
└── src/
    ├── pbr_material.cpp    # Texture loading as arrays
    ├── mesh.cpp            # UV sphere generation
    └── operators/
        └── render3d.cpp    # DiligentFX PBR_Renderer integration
```

---

## Phase 5: Hot Reload System ✓

**Goal:** Live code recompilation without restart

**Status:** Complete (macOS verified)

### Tasks
- [x] File watcher for chain.cpp changes (polling-based, cross-platform)
- [x] Invoke compiler (clang++) to build shared library (.dylib/.so/.dll)
- [x] Dynamic library loading/unloading (dlopen/dlclose on Unix, LoadLibrary on Windows)
- [x] Symbol resolution (vivid_setup, vivid_update functions)
- [x] VIVID_CHAIN macro for exporting entry points
- [x] Platform-specific defines (PLATFORM_MACOS=1) for Diligent headers
- [x] Build artifact management (unique library names, cleanup old builds)
- [ ] State preservation across reloads (future)
- [ ] Zero-config build with auto-detection of includes (future)

### Error Handling
- [x] Compile errors: Print to console, keep old code running
- [ ] Compile errors: Display in window overlay (future)
- [ ] Shader errors: Show shader name, line number, error message (future)
- [ ] Error overlay: Non-intrusive display (future)

---

### Hot Reload Architecture

#### File Structure
```
runtime/
├── include/vivid/
│   ├── vivid.h             # VIVID_CHAIN macro
│   └── hot_reload.h        # Hot reload system
└── src/
    └── hot_reload.cpp      # Implementation

examples/
└── hello-noise/
    └── chain.cpp           # User code (dynamically compiled)
```

#### Usage
```bash
# Run with a project path to enable hot reload
vivid /path/to/my-project

# The runtime will:
# 1. Find chain.cpp in the project directory
# 2. Compile it to a shared library
# 3. Load and execute setup() and update() functions
# 4. Watch for changes and recompile/reload on save
```

#### User Code Structure (chain.cpp)
```cpp
#include <vivid/vivid.h>

using namespace vivid;

void setup(Context& ctx) {
    // Called once when the chain is loaded (or reloaded)
}

void update(Context& ctx) {
    // Called every frame
}

VIVID_CHAIN(setup, update)
```

#### VIVID_CHAIN Macro
```cpp
// Exports entry points with extern "C" to prevent name mangling
#define VIVID_CHAIN(setup_fn, update_fn) \
    extern "C" { \
        void vivid_setup(vivid::Context& ctx) { setup_fn(ctx); } \
        void vivid_update(vivid::Context& ctx) { update_fn(ctx); } \
    }
```

---

### HotReload Class API

```cpp
class HotReload {
public:
    // Set up for a project directory (finds chain.cpp)
    bool init(const fs::path& projectPath);

    // Set vivid runtime path (for includes)
    void setRuntimePath(const fs::path& path);

    // Poll for file changes, recompile if needed
    // Returns true if a reload occurred
    bool poll();

    // Force a reload
    bool reload();

    // Check if code is loaded and ready
    bool isReady() const;

    // Get function pointers
    SetupFn setup() const;
    UpdateFn update() const;

    // Error info
    bool hasCompileError() const;
    const std::string& lastError() const;
    const std::string& compilerOutput() const;
};
```

---

### Compiler Configuration

The Compiler class automatically configures include paths:
- Vivid runtime headers
- Diligent Engine headers (Core, Tools, FX)
- GLM
- GLFW

Platform-specific flags:
```cpp
// macOS
clang++ -std=c++17 -O2 -shared -fPIC -dynamiclib -undefined dynamic_lookup
        -DPLATFORM_MACOS=1 ...

// Linux (future)
g++ -std=c++17 -O2 -shared -fPIC -DPLATFORM_LINUX=1 ...

// Windows (future)
cl.exe /nologo /EHsc /O2 /LD /DPLATFORM_WIN32=1 ...
```

---

### Build Artifact Management

Each compilation creates a unique library name to avoid caching issues:
```
project/.vivid-build/
├── chain_1.dylib   # Build 1
├── chain_2.dylib   # Build 2 (after code change)
└── chain_3.dylib   # Build 3 (current)
```

Old builds are automatically cleaned up (keeps last 3).

---

### Common Pitfalls & Solutions

| Problem | Symptom | Solution |
|---------|---------|----------|
| Missing PLATFORM_MACOS | "Unknown platform" compile error | Add `-DPLATFORM_MACOS=1` to compiler |
| Library caching | Changes don't take effect | Use unique library names per build |
| Symbol not found | dlsym returns null | Ensure `extern "C"` on exported functions |
| Context mismatch | Crash or undefined behavior | Context passed by reference, not copied |

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
