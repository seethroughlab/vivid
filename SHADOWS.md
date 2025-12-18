# 3D Shadow Mapping Implementation Plan

## Overview

Add shadow mapping to vivid-render3d with support for all three light types (Directional, Spot, Point), per-light shadow control, and a phased approach starting with hard shadows.

**Key Decisions (Learning from V2):**
- NO cascaded shadow maps (V2 spent weeks debugging, never worked)
- Simple single-shadow-map per light type
- Phased implementation: Directional → Spot → Point → Soft
- Conservative defaults (1024 resolution, small bias)

---

## Phase 1: Directional Light Shadows

**Goal:** Single directional light with orthographic shadow map

### 1.1 Add Shadow Properties to Light System

**File:** `addons/vivid-render3d/include/vivid/render3d/light_operators.h`

Add to `LightData` struct:
```cpp
bool castShadow = false;
float shadowBias = 0.001f;
```

Add to `DirectionalLight` class:
```cpp
DirectionalLight& castShadow(bool enabled);
DirectionalLight& shadowBias(float bias);
```

### 1.2 New Shadow GPU Structures

**File:** `addons/vivid-render3d/src/renderer.cpp`

```cpp
struct ShadowData {
    float lightViewProj[16];  // 64 bytes
    float shadowBias;         // 4 bytes
    float normalBias;         // 4 bytes
    float shadowMapSize;      // 4 bytes
    uint32_t enabled;         // 4 bytes
};  // Total: 80 bytes

// New member variables:
WGPUTexture m_shadowMapTexture = nullptr;
WGPUTextureView m_shadowMapView = nullptr;
WGPURenderPipeline m_shadowPassPipeline = nullptr;
WGPUSampler m_shadowSampler = nullptr;  // Comparison sampler
WGPUBuffer m_shadowUniformBuffer = nullptr;
int m_shadowMapResolution = 1024;
bool m_shadowsEnabled = false;
```

### 1.3 Shadow Pass Shader (Depth-Only)

**File:** `addons/vivid-render3d/shaders/shadow_pass.wgsl`

```wgsl
struct ShadowUniforms {
    lightViewProj: mat4x4f,
    model: mat4x4f,
}

@group(0) @binding(0) var<uniform> uniforms: ShadowUniforms;

@vertex
fn vs_main(@location(0) position: vec3f) -> @builtin(position) vec4f {
    return uniforms.lightViewProj * uniforms.model * vec4f(position, 1.0);
}
// No fragment shader - WebGPU writes depth automatically
```

### 1.4 Shadow Sampling in PBR Shader

Add to all PBR shader variants in `addons/vivid-render3d/shaders/`:

```wgsl
@group(2) @binding(0) var shadowMap: texture_depth_2d;
@group(2) @binding(1) var shadowSampler: sampler_comparison;
@group(2) @binding(2) var<uniform> shadowData: ShadowData;

fn sampleShadow(worldPos: vec3f) -> f32 {
    if (shadowData.enabled == 0u) { return 1.0; }

    let lightSpacePos = shadowData.lightViewProj * vec4f(worldPos, 1.0);
    var projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // NDC to texture coords (flip Y for WebGPU)
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * -0.5 + 0.5;

    // Outside frustum check
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }

    let currentDepth = projCoords.z - shadowData.shadowBias;
    return textureSampleCompare(shadowMap, shadowSampler, projCoords.xy, currentDepth);
}
```

Modify `calculateLightContribution()`:
```wgsl
// After computing NdotL, add:
let shadowFactor = sampleShadow(worldPos);
return (diffuse + specular) * radiance * NdotL * shadowFactor;
```

### 1.5 Render Pipeline Changes

In `Render3D::process()`:
```cpp
// Before main render pass:
if (m_shadowsEnabled && hasShadowCastingLight()) {
    renderShadowPass(ctx, encoder);
}
// Then main pass with shadow map bound to group 2
```

### 1.6 Public API

**File:** `addons/vivid-render3d/include/vivid/render3d/renderer.h`

```cpp
void setShadows(bool enabled);
void setShadowMapResolution(int size);  // 512, 1024, 2048
```

---

## Phase 2: Spot Light Shadows

**Changes from Phase 1:**
- Use **perspective projection** instead of orthographic
- Light space matrix uses spot angle for FOV

```cpp
glm::mat4 computeSpotLightMatrix(glm::vec3 pos, glm::vec3 dir, float spotAngle, float range) {
    glm::mat4 view = glm::lookAt(pos, pos + dir, glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(spotAngle * 2.0f, 1.0f, 0.1f, range);
    return proj * view;
}
```

**Resource changes:**
```cpp
constexpr uint32_t MAX_SHADOW_LIGHTS = 2;
WGPUTexture m_shadowMapTextures[MAX_SHADOW_LIGHTS];
```

---

## Phase 3: Point Light Shadows (Cube Map)

**Cube map texture:**
```cpp
WGPUTextureDescriptor desc = {};
desc.size = {resolution, resolution, 6};  // 6 faces
desc.format = WGPUTextureFormat_Depth32Float;
desc.dimension = WGPUTextureDimension_2D;

WGPUTextureViewDescriptor viewDesc = {};
viewDesc.dimension = WGPUTextureViewDimension_Cube;
```

**Six render passes per point light:**
```cpp
const glm::vec3 directions[6] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
const glm::vec3 ups[6] = {{0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}};

for (int face = 0; face < 6; face++) {
    glm::mat4 view = glm::lookAt(lightPos, lightPos + directions[face], ups[face]);
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, range);
    // Render to cube face
}
```

**Cube sampling in shader:**
```wgsl
@group(2) @binding(3) var pointShadowMap: texture_depth_cube;

fn samplePointShadow(worldPos: vec3f, lightPos: vec3f, range: f32) -> f32 {
    let lightToFrag = worldPos - lightPos;
    let depth = length(lightToFrag) / range;
    return textureSampleCompare(pointShadowMap, shadowSampler,
                                 normalize(lightToFrag), depth - bias);
}
```

---

## Phase 4: Soft Shadows (PCF)

**3x3 PCF kernel:**
```wgsl
fn sampleShadowPCF(worldPos: vec3f) -> f32 {
    let texelSize = 1.0 / shadowData.shadowMapSize;
    var shadow = 0.0;

    for (var x = -1; x <= 1; x++) {
        for (var y = -1; y <= 1; y++) {
            let offset = vec2f(f32(x), f32(y)) * texelSize;
            shadow += textureSampleCompare(shadowMap, shadowSampler,
                                           projCoords.xy + offset, depth);
        }
    }
    return shadow / 9.0;
}
```

**API addition:**
```cpp
void setShadowSoftness(int kernelSize);  // 1=hard, 3=soft, 5=very soft
```

---

## Files to Modify

| File | Changes |
|------|---------|
| `addons/vivid-render3d/include/vivid/render3d/light_operators.h` | Add castShadow, shadowBias to LightData + setters |
| `addons/vivid-render3d/include/vivid/render3d/renderer.h` | Add shadow API methods, member variables |
| `addons/vivid-render3d/src/renderer.cpp` | Shadow pass, shader mods, resource creation (~500 lines) |
| `addons/vivid-render3d/shaders/shadow_pass.wgsl` | New depth-only shader |
| `addons/vivid-render3d/shaders/pbr_*.wgsl` | Add shadow sampling to all PBR variants |

---

## Risk Mitigation (V2 Lessons)

1. **Shadow acne**: Start with 0.001 bias, increase if needed
2. **Peter panning**: Don't over-bias, objects lift off ground
3. **Coordinate flip**: WebGPU Y is inverted from OpenGL
4. **Scope creep**: If Phase 1 takes >2 weeks, ship it and defer Phase 2-4

---

## Test Fixtures

Create after each phase:
- `testing-fixtures/shadow-directional/` - Basic sun shadow
- `testing-fixtures/shadow-spot/` - Spotlight shadow
- `testing-fixtures/shadow-point/` - Omnidirectional shadow
- `testing-fixtures/shadow-soft/` - PCF comparison

---

## Debug Tools

```cpp
void setDebugShadowMap(bool enabled);  // Render shadow map to screen corner
```

---

## Implementation Order

1. **Phase 1** (Directional): ~3-5 days
   - Light property additions
   - Shadow texture + sampler creation
   - Shadow pass pipeline
   - Shader modifications
   - API methods

2. **Phase 2** (Spot): ~2-3 days
   - Perspective matrix calculation
   - Multi-shadow-map support

3. **Phase 3** (Point): ~4-6 days
   - Cube map creation
   - 6-face rendering
   - Cube sampling shader

4. **Phase 4** (Soft): ~1-2 days
   - PCF kernel
   - Quality parameter

**Total estimate: 10-16 days**
