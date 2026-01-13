# Volumetric Lighting Implementation Plan

This document provides a detailed phased implementation plan for adding volumetric lighting (god rays, light shafts, fog with light scattering) to Vivid's render3d module.

---

## Implementation Progress

### Phase 1 Status: COMPLETE

**Completed:**
- [x] Created `volumetric_lighting.h` header file
- [x] Created `volumetric_lighting.cpp` implementation with WGSL ray marching shader
- [x] Added to CMakeLists.txt
- [x] Registered operator in registry
- [x] Added `#include` to `render3d.h` umbrella header
- [x] Created `streetlight-fog` example chain demonstrating the effect
- [x] Implemented Henyey-Greenstein phase function (artistic version without 4π normalization)
- [x] Implemented world-space reconstruction from depth
- [x] Added debug modes for visualization (0=off, 1=depth, 2=worldPos, 3=distance, 4=lightContrib, 5=passthrough, 6+=extended)
- [x] Added `cameraInput()` method for world-space reconstruction
- [x] Ray marching through empty air (sky pixels) for visible light shafts

**Bug Fix (Jan 2026):**
The original implementation had a uniform buffer alignment bug that caused all texture sampling to fail:

**Root Cause:** In `VolumetricUniforms` struct, `_pad1[3]` was incorrect:
```cpp
// WRONG: 3 floats = 12 bytes (offsets 88, 92, 96)
float _pad1[3];  // Pushed lightPos to offset 100 instead of 96

// CORRECT: 2 floats = 8 bytes (offsets 88, 92)
float _pad1[2];  // lightPos correctly at offset 96
```

This misalignment caused all subsequent uniform fields (lightPos, lightColor, etc.) to be read from wrong offsets, resulting in garbage values.

**Additional Changes:**
1. Removed 4π normalization from phase function for more visible artistic effect
2. Modified ray marching to process sky pixels (not early-exit) for light scattering in empty air
3. Set default parameters for visible volumetric effect

**Files:**
- `modules/vivid-render3d/include/vivid/render3d/volumetric_lighting.h`
- `modules/vivid-render3d/src/volumetric_lighting.cpp`
- `modules/vivid-render3d/examples/streetlight-fog/chain.cpp`
- `modules/vivid-render3d/examples/streetlight-fog/CLAUDE.md`

### Phase 2 Status: COMPLETE

**Completed:**
- [x] Added shadow accessor methods to `Render3D` (getShadowMapView, getShadowSampler, getLightViewProjection, etc.)
- [x] Added `useShadows`, `shadowBias`, `shadowStrength` parameters to VolumetricLighting
- [x] Extended uniform buffer with `lightViewProj` matrix and shadow parameters
- [x] Added shadow map texture and comparison sampler bindings to bind group layout
- [x] Implemented `sampleShadow()` function in WGSL shader
- [x] Added dummy shadow texture for fallback when shadows disabled
- [x] Updated `streetlight-fog` example with shadow controls
- [x] Tested shadow-occluded light shafts with spot light

**Implementation Details:**
- Shadow map is sampled at each ray march step to determine occlusion
- World-space position is transformed to light clip space using `lightViewProj` matrix
- Comparison sampler with `LessEqual` provides hardware-accelerated shadow testing
- Shadow strength parameter allows artistic control over occlusion intensity
- Graceful fallback to 1x1 dummy depth texture when shadows unavailable

**New Parameters:**
- `useShadows` (bool, default false): Enable shadow map sampling
- `shadowBias` (float, 0.0-0.02, default 0.002): Depth bias to prevent shadow acne
- `shadowStrength` (float, 0.0-1.0, default 1.0): Shadow intensity multiplier

**Files Modified:**
- `modules/vivid-render3d/include/vivid/render3d/renderer.h` - Added shadow accessor methods
- `modules/vivid-render3d/src/renderer.cpp` - Implemented shadow accessors
- `modules/vivid-render3d/include/vivid/render3d/volumetric_lighting.h` - Added shadow parameters
- `modules/vivid-render3d/src/volumetric_lighting.cpp` - Shadow sampling implementation
- `modules/vivid-render3d/examples/streetlight-fog/chain.cpp` - Shadow demo

**Additional Examples Created:**

1. `modules/vivid-render3d/examples/tropical-godrays/` - God rays through palm fronds
   - SpotLight pointing down through swaying palm fronds
   - Dramatic shadow-occluded light shafts
   - ISLANDS: Non-Places inspired teal/cyan monochrome aesthetic

2. `modules/vivid-render3d/examples/window-light/` - Sunlight through window
   - DirectionalLight with shadow casting
   - Interior room with window blinds and furniture
   - Demonstrates atmospheric volumetric fog (subtle effect vs dramatic shafts)

---

### Uniform Buffer Alignment (Corrected)

The C++ struct and WGSL struct alignment must match exactly:
```
WGSL vec3f = align 16, size 12
WGSL mat4x4f = align 16, size 64
```

Key offsets in VolumetricUniforms struct:
- `invViewProj`: offset 0-63 (mat4x4f)
- `cameraPos`: offset 64-75 (vec3f components as 3 floats)
- `nearPlane`: offset 76
- `farPlane`: offset 80
- `lightType`: offset 84
- `_pad1[2]`: offset 88-95 (padding to align vec3f)
- `lightPos`: offset 96 (aligned to 16)
- etc.

---

## Overview

Volumetric lighting simulates light scattering through a participating medium (dust, fog, smoke). When light passes through the medium, some is absorbed and some is scattered toward the camera, creating visible "light shafts" or "god rays."

### Visual Effects Enabled
- **God rays** - Visible light beams from directional lights
- **Light shafts** - Beams occluded by geometry creating shaft patterns
- **Volumetric fog** - Fog that interacts with light sources
- **Atmospheric haze** - Distance-based light scattering

## Current Infrastructure

These systems are already implemented and ready to use:

| Component | Location | API |
|-----------|----------|-----|
| Depth output | `Render3D` | `setDepthOutput(true)`, `depthOutputView()` |
| Shadow maps (dir/spot) | `ShadowManager` | `getShadowMapView()`, `getLightViewProj()` |
| Shadow maps (point) | `ShadowManager` | `getPointShadowAtlasView()` |
| Light system | `light_operators.h` | `DirectionalLight`, `PointLight`, `SpotLight` |
| Camera data | `Render3D` | `getNearPlane()`, `getFarPlane()` |
| Post-process pattern | `Fog`, `Bloom` | Template for new operators |

## Architecture Decision

**Recommended: Standalone Post-Effect Operator**

Follow the established Fog/DepthOfField pattern:
```
Render3D → VolumetricLighting → Bloom → Output
```

Benefits:
- Doesn't modify Render3D internals
- Can be added/removed from chain
- Easier to iterate and debug
- Reuses proven patterns from Fog and Bloom

---

## Phase 1: Basic God Rays (No Shadows)

**Goal:** Validate ray marching infrastructure with simple depth-based light scattering.

### 1.1 Create Operator Skeleton

**File:** `modules/vivid-render3d/include/vivid/render3d/volumetric_lighting.h`

```cpp
#pragma once

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <glm/glm.hpp>

namespace vivid::render3d {

class Render3D;
class LightOperator;

/**
 * @brief Volumetric lighting post-process effect
 *
 * Adds god rays and light shafts to a 3D scene using ray marching.
 * Requires Render3D with depth output enabled.
 *
 * @par Example
 * @code
 * auto& render = chain.add<Render3D>("render");
 * render.setDepthOutput(true);  // Required!
 *
 * auto& volumetric = chain.add<VolumetricLighting>("volumetric");
 * volumetric.input(&render);
 * volumetric.lightInput(&sun);
 * volumetric.raySteps = 32;
 * volumetric.density = 0.02f;
 * @endcode
 */
class VolumetricLighting : public effects::TextureOperator {
public:
    VolumetricLighting();
    ~VolumetricLighting();

    // -------------------------------------------------------------------------
    /// @name Input Configuration
    /// @{

    /// Set the Render3D input (must have depth output enabled)
    void input(Render3D* render);

    /// Set the light source for volumetrics
    void lightInput(LightOperator* light);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    /// Number of ray march steps (8-128, default 32)
    /// Higher = better quality, lower performance
    Param<int> raySteps{"raySteps", 32, 8, 128};

    /// Maximum ray march distance in world units
    Param<float> maxDistance{"maxDistance", 100.0f, 1.0f, 500.0f};

    /// Fog/medium density (0-0.2, default 0.02)
    Param<float> density{"density", 0.02f, 0.0f, 0.2f};

    /// Scattering intensity multiplier (0-5, default 1.0)
    Param<float> intensity{"intensity", 1.0f, 0.0f, 5.0f};

    /// Scattering anisotropy for Henyey-Greenstein phase function
    /// -1 = back scatter, 0 = isotropic, 1 = forward scatter
    Param<float> anisotropy{"anisotropy", 0.5f, -1.0f, 1.0f};

    /// Enable shadow map sampling (Phase 2+)
    Param<bool> useShadows{"useShadows", false};

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "VolumetricLighting"; }

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    void createPipeline(Context& ctx);

    Render3D* m_render3d = nullptr;
    LightOperator* m_lightOp = nullptr;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUTextureView m_lastColorView = nullptr;
    WGPUTextureView m_lastDepthView = nullptr;
};

} // namespace vivid::render3d
```

### 1.2 Uniform Buffer Structure

**File:** `modules/vivid-render3d/src/volumetric_lighting.cpp`

```cpp
namespace {

// Must match WGSL layout (aligned to 16 bytes)
struct VolumetricUniforms {
    // Camera data (vec4 aligned)
    float cameraPos[4];          // offset 0:  xyz = position, w = unused
    float cameraDir[4];          // offset 16: xyz = forward direction, w = unused

    // Inverse matrices for world reconstruction
    float invViewProj[16];       // offset 32: 4x4 matrix

    // Light data
    float lightDir[4];           // offset 96:  xyz = direction (normalized), w = unused
    float lightColor[4];         // offset 112: rgb = color * intensity, w = unused

    // Depth reconstruction
    float nearPlane;             // offset 128
    float farPlane;              // offset 132

    // Ray march parameters
    int raySteps;                // offset 136
    float maxDistance;           // offset 140
    float density;               // offset 144
    float intensity;             // offset 148
    float anisotropy;            // offset 152
    float _pad;                  // offset 156 (align to 16)
};

} // namespace
```

### 1.3 WGSL Ray Marching Shader (Basic)

```wgsl
struct Uniforms {
    cameraPos: vec4f,
    cameraDir: vec4f,
    invViewProj: mat4x4f,
    lightDir: vec4f,
    lightColor: vec4f,
    nearPlane: f32,
    farPlane: f32,
    raySteps: i32,
    maxDistance: f32,
    density: f32,
    intensity: f32,
    anisotropy: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var colorTexture: texture_2d<f32>;
@group(0) @binding(2) var depthTexture: texture_2d<f32>;
@group(0) @binding(3) var texSampler: sampler;

// Fullscreen triangle vertex shader
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;
    let x = f32(i32(vertexIndex & 1u) * 4 - 1);
    let y = f32(i32(vertexIndex >> 1u) * 4 - 1);
    output.position = vec4f(x, y, 0.0, 1.0);
    output.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return output;
}

// Henyey-Greenstein phase function
// Models anisotropic scattering (g > 0 = forward, g < 0 = back)
fn phaseHG(cosTheta: f32, g: f32) -> f32 {
    let g2 = g * g;
    let denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159 * pow(denom, 1.5));
}

// Reconstruct world position from UV and depth
fn reconstructWorldPos(uv: vec2f, depth: f32) -> vec3f {
    // Convert UV to NDC (-1 to 1)
    let ndc = vec4f(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);

    // Transform by inverse view-projection
    let worldPos = uniforms.invViewProj * ndc;
    return worldPos.xyz / worldPos.w;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(colorTexture, texSampler, input.uv);
    let normalizedDepth = textureSample(depthTexture, texSampler, input.uv).r;

    // Early out for sky (depth = 1.0)
    if (normalizedDepth >= 0.999) {
        return color;
    }

    // Reconstruct world position
    let worldPos = reconstructWorldPos(input.uv, normalizedDepth);
    let cameraPos = uniforms.cameraPos.xyz;
    let rayDir = normalize(worldPos - cameraPos);
    let rayLength = length(worldPos - cameraPos);

    // Limit ray march distance
    let marchDistance = min(rayLength, uniforms.maxDistance);
    let stepSize = marchDistance / f32(uniforms.raySteps);

    // Phase function: how much light scatters toward camera
    let lightDir = normalize(uniforms.lightDir.xyz);
    let cosTheta = dot(rayDir, -lightDir);
    let phase = phaseHG(cosTheta, uniforms.anisotropy);

    // Ray march accumulation
    var scatteredLight = vec3f(0.0);
    var transmittance = 1.0;

    for (var i = 0; i < uniforms.raySteps; i++) {
        let t = (f32(i) + 0.5) * stepSize;  // Sample at step center
        let samplePos = cameraPos + rayDir * t;

        // Basic scattering (no shadow check in Phase 1)
        let scatter = uniforms.density * phase;
        scatteredLight += transmittance * scatter * uniforms.lightColor.rgb;

        // Beer-Lambert absorption
        transmittance *= exp(-uniforms.density * stepSize);

        // Early termination if fully absorbed
        if (transmittance < 0.01) {
            break;
        }
    }

    // Apply intensity and add to scene
    let volumetric = scatteredLight * uniforms.intensity * stepSize;
    return vec4f(color.rgb + volumetric, color.a);
}
```

### 1.4 Process Function Pattern (from Fog)

```cpp
void VolumetricLighting::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    matchInputResolution(0);

    if (!m_render3d || !m_render3d->hasDepthOutput()) {
        // Depth required - pass through unchanged
        return;
    }

    if (!needsCook()) return;

    WGPUDevice device = ctx.device();

    // Get input textures
    WGPUTextureView colorView = m_render3d->outputView();
    WGPUTextureView depthView = m_render3d->depthOutputView();

    if (!colorView || !depthView) return;

    // Update bind group if inputs changed
    if (colorView != m_lastColorView || depthView != m_lastDepthView) {
        // Recreate bind group...
        m_lastColorView = colorView;
        m_lastDepthView = depthView;
    }

    // Get camera and light data from operators
    // ... populate uniforms ...

    // Write uniforms
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Render pass
    WGPUCommandEncoder encoder = ctx.gpuEncoder();
    // ... standard fullscreen pass ...

    didCook();
}
```

### 1.5 Tasks

- [ ] Create `volumetric_lighting.h` header
- [ ] Create `volumetric_lighting.cpp` implementation
- [ ] Add to CMakeLists.txt
- [ ] Register operator in registry
- [ ] Add `#include` to `render3d.h` umbrella header
- [ ] Write basic unit test
- [ ] Create example chain demonstrating the effect
- [ ] Document in module README

### 1.6 Testing Checklist

- [ ] Effect visible with directional light
- [ ] `density` parameter affects fog thickness
- [ ] `intensity` parameter affects brightness
- [ ] `anisotropy` changes based on viewing angle to light
- [ ] `raySteps` affects quality vs performance
- [ ] Works at different resolutions
- [ ] No artifacts at depth discontinuities

---

## Phase 2: Shadow-Occluded Volumetrics

**Goal:** Add shadow map sampling so light shafts are properly occluded by geometry.

### 2.1 Expose Shadow Data from Render3D

Add public methods to `Render3D`:

```cpp
// In renderer.h - add to Render3D class:

/// @name Shadow Access (for post-processing)
/// @{

/// Get directional/spot shadow map view (nullptr if shadows disabled)
[[nodiscard]] WGPUTextureView getShadowMapView() const;

/// Get point light shadow atlas view (nullptr if no point shadows)
[[nodiscard]] WGPUTextureView getPointShadowAtlasView() const;

/// Get shadow comparison sampler
[[nodiscard]] WGPUSampler getShadowSampler() const;

/// Get light view-projection matrix for shadow sampling
[[nodiscard]] const glm::mat4& getLightViewProjection() const;

/// Get point light position (for omnidirectional shadow lookup)
[[nodiscard]] glm::vec3 getPointLightPosition() const;

/// Get point light range
[[nodiscard]] float getPointLightRange() const;

/// @}
```

Implementation (delegates to ShadowManager):

```cpp
// In renderer.cpp:

WGPUTextureView Render3D::getShadowMapView() const {
    if (!m_shadowManager || !m_shadowManager->hasShadows()) return nullptr;
    return m_shadowManager->getShadowMapView();
}

WGPUTextureView Render3D::getPointShadowAtlasView() const {
    if (!m_shadowManager) return nullptr;
    return m_shadowManager->getPointShadowAtlasView();
}

WGPUSampler Render3D::getShadowSampler() const {
    if (!m_shadowManager) return nullptr;
    return m_shadowManager->getShadowSampler();
}

const glm::mat4& Render3D::getLightViewProjection() const {
    static glm::mat4 identity(1.0f);
    if (!m_shadowManager) return identity;
    return m_shadowManager->getLightViewProj();
}
```

### 2.2 Extended Uniform Buffer

```cpp
struct VolumetricUniformsV2 {
    // ... all Phase 1 uniforms ...

    // Shadow sampling (Phase 2)
    float lightViewProj[16];     // Light space matrix
    float shadowBias;            // Depth bias for shadow acne
    float shadowStrength;        // 0 = no shadows, 1 = full shadows
    int useShadows;              // Boolean flag
    float _pad2;
};
```

### 2.3 WGSL Shadow Sampling

Add to shader:

```wgsl
// Additional bindings for Phase 2
@group(0) @binding(4) var shadowMap: texture_depth_2d;
@group(0) @binding(5) var shadowSampler: sampler_comparison;

// Sample shadow map at world position
fn sampleShadow(worldPos: vec3f) -> f32 {
    // Transform to light space
    let lightSpacePos = uniforms.lightViewProj * vec4f(worldPos, 1.0);
    let projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Convert to UV coordinates
    let shadowUV = projCoords.xy * 0.5 + 0.5;
    let currentDepth = projCoords.z;

    // Out of shadow map bounds = lit
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        currentDepth > 1.0) {
        return 1.0;
    }

    // Sample with comparison sampler
    let shadow = textureSampleCompare(
        shadowMap, shadowSampler,
        shadowUV, currentDepth - uniforms.shadowBias
    );

    return mix(1.0, shadow, uniforms.shadowStrength);
}

// Modified ray march loop
@fragment
fn fs_main_v2(input: VertexOutput) -> @location(0) vec4f {
    // ... same setup as Phase 1 ...

    for (var i = 0; i < uniforms.raySteps; i++) {
        let t = (f32(i) + 0.5) * stepSize;
        let samplePos = cameraPos + rayDir * t;

        // Shadow occlusion (Phase 2)
        var shadowFactor = 1.0;
        if (uniforms.useShadows != 0) {
            shadowFactor = sampleShadow(samplePos);
        }

        let scatter = uniforms.density * phase * shadowFactor;
        scatteredLight += transmittance * scatter * uniforms.lightColor.rgb;
        transmittance *= exp(-uniforms.density * stepSize);
    }

    // ... same output as Phase 1 ...
}
```

### 2.4 Tasks

- [x] Add shadow accessor methods to `Render3D`
- [x] Extend uniform buffer with shadow data
- [x] Add shadow map bindings to bind group layout
- [x] Implement `sampleShadow()` in WGSL
- [x] Add `useShadows` parameter toggle
- [x] Add `shadowBias` and `shadowStrength` parameters
- [x] Test with directional light shadows
- [x] Test with spot light shadows
- [x] Handle case when shadows are disabled in Render3D

### 2.5 Testing Checklist

- [x] Light shafts blocked by shadow-casting geometry (SpotLight)
- [x] No shadow acne artifacts (adjustable via shadowBias)
- [x] Smooth transitions at shadow edges
- [x] Works with SpotLight (dramatic god ray shafts)
- [x] Works with DirectionalLight (subtle atmospheric effect)
- [x] Graceful fallback when shadows disabled

### 2.6 DirectionalLight vs SpotLight Findings

**SpotLight** (streetlight-fog, tropical-godrays):
- Creates dramatic, visible god ray shafts
- Cone shape defines a clear light volume
- Shadow occlusion creates distinct gaps/bands in the beam
- Ideal for: streetlights, flashlights, stage lighting

**DirectionalLight** (window-light):
- Creates subtle atmospheric haze effect
- No distinct beam shape (light fills space uniformly)
- Shadow occlusion affects overall fog brightness, not distinct bands
- Ideal for: general atmosphere, outdoor scenes, fill lighting

**Recommendation**: Use SpotLight for dramatic god ray effects. DirectionalLight is better
for overall atmospheric haze without distinct light shafts.

---

## Phase 3 Status: COMPLETE

**Completed:**
- [x] Added `resolutionScale` parameter (1=full, 2=half, 4=quarter)
- [x] Implemented low-res intermediate texture creation
- [x] Modified main shader to support volumetric-only output mode
- [x] Implemented bilinear upsample shader with composite
- [x] Two-pass rendering pipeline (renderLowRes → renderUpsample)
- [x] Added cleanup for all new GPU resources
- [x] Added ImGui control in streetlight-fog example
- [x] Tested visual quality at scale 1, 2, and 4

**Implementation Details:**
- When `resolutionScale > 1`, volumetric lighting uses two-pass rendering
- Pass 1: Render volumetric contribution to low-res texture (outputMode=1)
- Pass 2: Bilinear upsample and composite with full-res scene color
- Bilinear filtering via hardware sampler provides smooth upsampling
- Visual quality remains good even at quarter resolution (scale=4)

**Technical Notes:**
- R32Float depth textures require `UnfilterableFloat` sample type in WebGPU
- Current implementation uses bilinear upsampling (bilateral with depth weighting reserved for future)
- Full-res scene color and depth are read but only color is used for composite

**Files Modified:**
- `modules/vivid-render3d/include/vivid/render3d/volumetric_lighting.h` - Added parameters and members
- `modules/vivid-render3d/src/volumetric_lighting.cpp` - Two-pass rendering implementation
- `modules/vivid-render3d/examples/streetlight-fog/chain.cpp` - ImGui control

---

## Phase 3: Performance Optimization (Reference)

**Goal:** Achieve real-time performance with half-resolution rendering and temporal filtering.

### 3.1 Half-Resolution Rendering

Render volumetrics at 1/2 or 1/4 resolution, then upsample:

```cpp
// New parameters
Param<int> resolutionScale{"resolutionScale", 2, 1, 4};  // 1 = full, 2 = half, 4 = quarter

// In createPipeline():
void VolumetricLighting::createIntermediateTextures(Context& ctx) {
    int scaledWidth = m_width / static_cast<int>(resolutionScale);
    int scaledHeight = m_height / static_cast<int>(resolutionScale);

    // Create low-res volumetric texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = scaledWidth;
    texDesc.size.height = scaledHeight;
    // ... etc
    m_lowResTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);
}
```

### 3.2 Bilateral Upsampling

Preserve edges when upsampling using depth-aware filtering:

```wgsl
// Bilateral upsample shader
@fragment
fn fs_upsample(input: VertexOutput) -> @location(0) vec4f {
    let fullResDepth = textureSample(depthTexture, texSampler, input.uv).r;

    // Sample 4 nearest low-res neighbors
    let texelSize = vec2f(1.0) / vec2f(textureDimensions(lowResTexture));
    var totalWeight = 0.0;
    var result = vec3f(0.0);

    for (var dy = -1; dy <= 1; dy += 2) {
        for (var dx = -1; dx <= 1; dx += 2) {
            let offset = vec2f(f32(dx), f32(dy)) * texelSize * 0.5;
            let sampleUV = input.uv + offset;

            let lowResColor = textureSample(lowResTexture, texSampler, sampleUV);
            let lowResDepth = textureSample(lowResDepthTexture, texSampler, sampleUV).r;

            // Depth-based weight (closer depth = higher weight)
            let depthDiff = abs(fullResDepth - lowResDepth);
            let weight = exp(-depthDiff * 100.0);

            result += lowResColor.rgb * weight;
            totalWeight += weight;
        }
    }

    return vec4f(result / totalWeight, 1.0);
}
```

### 3.3 Temporal Filtering (Optional)

Spread ray marching cost across frames:

```cpp
// Additional resources for temporal
WGPUTexture m_historyTexture = nullptr;
WGPUTextureView m_historyView = nullptr;
glm::mat4 m_prevViewProj;

Param<float> temporalBlend{"temporalBlend", 0.9f, 0.0f, 0.99f};
```

```wgsl
// Temporal accumulation
@fragment
fn fs_temporal(input: VertexOutput) -> @location(0) vec4f {
    let current = textureSample(currentFrame, texSampler, input.uv);

    // Reproject to previous frame
    let worldPos = reconstructWorldPos(input.uv, depth);
    let prevClip = uniforms.prevViewProj * vec4f(worldPos, 1.0);
    let prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // Check if reprojection is valid
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        return current;  // No history available
    }

    let history = textureSample(historyFrame, texSampler, prevUV);

    // Blend with clamped history
    let blended = mix(current.rgb, history.rgb, uniforms.temporalBlend);
    return vec4f(blended, current.a);
}
```

### 3.4 Tasks

- [x] Add `resolutionScale` parameter
- [x] Create low-res intermediate texture
- [x] Implement volumetric-only output mode in shader
- [x] Implement bilinear upsample shader
- [x] Add upsampling render pass
- [x] Test visual quality at different scales
- [ ] (Optional) Add bilateral depth-aware upsampling
- [ ] (Optional) Add temporal filtering
- [ ] (Optional) Add motion vector support

### 3.5 Performance Targets

| Resolution | Ray Steps | Target FPS (1080p) |
|------------|-----------|-------------------|
| Full (1x) | 64 | 30 fps |
| Half (2x) | 64 | 60 fps |
| Quarter (4x) | 64 | 120 fps |
| Half (2x) + Temporal | 32 | 60 fps |

---

## Phase 4: Multi-Light and Advanced Features

**Goal:** Support multiple lights and heterogeneous fog density.

### 4.1 Multiple Light Support

```cpp
// Extended parameters
static constexpr int MAX_VOLUMETRIC_LIGHTS = 4;

// Uniform buffer for multiple lights
struct LightVolumetricData {
    float direction[4];    // xyz = dir or position, w = type (0=dir, 1=point, 2=spot)
    float color[4];        // rgb = color, a = intensity
    float params[4];       // x = range, y = spotAngle, z = spotSoftness, w = unused
};

struct VolumetricUniformsV4 {
    // ... base uniforms ...
    LightVolumetricData lights[MAX_VOLUMETRIC_LIGHTS];
    int numLights;
    float _pad[3];
};
```

### 4.2 Fog Density Map Input

```cpp
/// Set optional density map (noise texture for heterogeneous fog)
/// Red channel = density multiplier (0-1)
void setDensityInput(effects::TextureOperator* densityOp);

// In shader:
fn sampleDensity(worldPos: vec3f) -> f32 {
    if (uniforms.useDensityMap == 0) {
        return uniforms.density;
    }

    // Project world position to UV (or use 3D noise coordinates)
    let uv = worldPos.xz * uniforms.densityScale;
    let densityMod = textureSample(densityMap, texSampler, uv).r;
    return uniforms.density * densityMod;
}
```

### 4.3 Point Light Volumetrics

```wgsl
fn samplePointLightShadow(worldPos: vec3f, lightPos: vec3f, lightRange: f32) -> f32 {
    let toLight = worldPos - lightPos;
    let dist = length(toLight);
    let dir = toLight / dist;

    // Determine which face of the cubemap
    let absDir = abs(dir);
    var face: i32;
    var uv: vec2f;

    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        face = select(1, 0, dir.x > 0.0);  // +X or -X
        uv = dir.zy / absDir.x;
    } else if (absDir.y >= absDir.z) {
        face = select(3, 2, dir.y > 0.0);  // +Y or -Y
        uv = dir.xz / absDir.y;
    } else {
        face = select(5, 4, dir.z > 0.0);  // +Z or -Z
        uv = dir.xy / absDir.z;
    }

    // Convert to atlas UV (3x2 layout)
    // ... atlas lookup logic ...

    let currentDepth = dist / lightRange;
    return textureSampleCompare(pointShadowAtlas, shadowSampler, atlasUV, currentDepth);
}
```

### 4.4 Tasks

- [ ] Add multi-light uniform structure
- [ ] Implement light loop in shader
- [ ] Add `setDensityInput()` for heterogeneous fog
- [ ] Implement point light shadow sampling
- [ ] Implement spot light volumetrics
- [ ] Add per-light enable/disable
- [ ] Performance test with 4 lights

---

## File Structure

```
modules/vivid-render3d/
├── include/vivid/render3d/
│   └── volumetric_lighting.h       # New header
├── src/
│   └── volumetric_lighting.cpp     # New implementation
└── examples/
    └── volumetric/                 # New example
        ├── chain.cpp
        ├── CLAUDE.md
        └── assets/
            └── models/
```

## API Summary

### Basic Usage (Phase 1)
```cpp
auto& render = chain.add<Render3D>("render");
render.setDepthOutput(true);  // Required!

auto& volumetric = chain.add<VolumetricLighting>("volumetric");
volumetric.input(&render);
volumetric.lightInput(&sun);
volumetric.density = 0.02f;
volumetric.intensity = 1.0f;
volumetric.raySteps = 32;

chain.output("volumetric");
```

### With Shadows (Phase 2)
```cpp
render.setShadows(true);

volumetric.useShadows = true;
volumetric.shadowBias = 0.001f;
volumetric.shadowStrength = 1.0f;
```

### Optimized (Phase 3)
```cpp
volumetric.resolutionScale = 2;  // Half resolution
volumetric.temporalBlend = 0.9f; // 90% history
```

### Multi-Light (Phase 4)
```cpp
volumetric.lightInput(&sun);
volumetric.addLight(&torch);
volumetric.addLight(&campfire);
volumetric.setDensityInput(&noise);  // Heterogeneous fog
```

## Reference Implementation

Study these existing files:
- `modules/vivid-render3d/src/fog.cpp` - Post-processing pattern
- `modules/vivid-core/src/effects/bloom.cpp` - Multi-pass rendering
- `modules/vivid-render3d/src/renderer.cpp` - Shadow/depth systems
- `modules/vivid-render3d/include/vivid/render3d/shadow_manager.h` - Shadow API

## Resources

- [GPU Gems 3: Volumetric Light Scattering](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process)
- [Physically Based Sky, Atmosphere and Cloud Rendering](https://www.alanzucconi.com/2017/10/10/atmospheric-scattering-1/)
- [Real-Time Volumetric Lighting in Frostbite](https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite)
