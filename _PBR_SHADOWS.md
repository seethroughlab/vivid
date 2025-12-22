# PBR Shadow Support Implementation Plan

Add shadow mapping to PBR shaders (pbr.wgsl, pbr_textured.wgsl, pbr_ibl.wgsl). Currently only flat.wgsl has shadow support.

## Files to Modify

### Shaders (~100 lines each)
- `addons/vivid-render3d/shaders/pbr.wgsl`
- `addons/vivid-render3d/shaders/pbr_textured.wgsl`
- `addons/vivid-render3d/shaders/pbr_ibl.wgsl`

### C++
- `addons/vivid-render3d/include/vivid/render3d/gpu_structs.h`
- `addons/vivid-render3d/src/renderer.cpp`

---

## Part 1: Shader Changes

### 1.1 Add receiveShadow to Uniforms struct

**For pbr.wgsl** - replace `_pad0` with `receiveShadow`:
```wgsl
struct Uniforms {
    // ... existing fields ...
    lightCount: u32,
    receiveShadow: u32,  // was _pad0
    lights: array<Light, 4>,
}
```

**For pbr_textured.wgsl and pbr_ibl.wgsl** - add after `alphaMode`:
```wgsl
struct Uniforms {
    // ... existing fields ...
    alphaCutoff: f32,
    alphaMode: u32,
    receiveShadow: u32,
    _padShadow: vec3f,  // align lights to 16 bytes
    lights: array<Light, 4>,
}
```

### 1.2 Add ShadowUniforms struct (all three shaders)

Copy from flat.wgsl lines 36-43:
```wgsl
struct ShadowUniforms {
    lightViewProj: mat4x4f,
    shadowBias: f32,
    shadowMapSize: f32,
    shadowEnabled: u32,
    pointShadowEnabled: u32,
    pointLightPosAndRange: vec4f,
}
```

### 1.3 Add shadow bindings

**For pbr.wgsl and pbr_textured.wgsl** - use Group 1:
```wgsl
@group(1) @binding(0) var<uniform> shadow: ShadowUniforms;
@group(1) @binding(1) var shadowMap: texture_depth_2d;
@group(1) @binding(2) var shadowSampler: sampler_comparison;
@group(1) @binding(3) var pointShadowAtlas: texture_2d<f32>;
@group(1) @binding(4) var pointShadowSampler: sampler;
```

**For pbr_ibl.wgsl** - use Group 2 (Group 1 is IBL):
```wgsl
@group(2) @binding(0) var<uniform> shadow: ShadowUniforms;
@group(2) @binding(1) var shadowMap: texture_depth_2d;
@group(2) @binding(2) var shadowSampler: sampler_comparison;
@group(2) @binding(3) var pointShadowAtlas: texture_2d<f32>;
@group(2) @binding(4) var pointShadowSampler: sampler;
```

### 1.4 Add shadow sampling functions (all three shaders)

Copy from flat.wgsl lines 85-131:

```wgsl
fn sampleShadow(worldPos: vec3f) -> f32 {
    if (shadow.shadowEnabled == 0u) { return 1.0; }
    let lightSpacePos = shadow.lightViewProj * vec4f(worldPos, 1.0);
    var projCoords = lightSpacePos.xyz / lightSpacePos.w;
    let texCoordX = projCoords.x * 0.5 + 0.5;
    let texCoordY = 1.0 - (projCoords.y * 0.5 + 0.5);
    let texCoordZ = projCoords.z;
    if (texCoordX < 0.0 || texCoordX > 1.0 || texCoordY < 0.0 || texCoordY > 1.0 || texCoordZ < 0.0 || texCoordZ > 1.0) { return 1.0; }
    let currentDepth = texCoordZ - shadow.shadowBias;
    return textureSampleCompare(shadowMap, shadowSampler, vec2f(texCoordX, texCoordY), currentDepth);
}

fn samplePointShadow(worldPos: vec3f) -> f32 {
    if (shadow.pointShadowEnabled == 0u) { return 1.0; }
    let lightToFrag = worldPos - shadow.pointLightPosAndRange.xyz;
    let fragDist = length(lightToFrag);
    let absDir = abs(lightToFrag);
    var faceIndex: i32; var u: f32; var v: f32; var ma: f32;
    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        ma = absDir.x;
        if (lightToFrag.x > 0.0) { faceIndex = 0; u = -lightToFrag.z; v = -lightToFrag.y; }
        else { faceIndex = 1; u = lightToFrag.z; v = -lightToFrag.y; }
    } else if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
        ma = absDir.y;
        if (lightToFrag.y > 0.0) { faceIndex = 2; u = lightToFrag.x; v = lightToFrag.z; }
        else { faceIndex = 3; u = lightToFrag.x; v = -lightToFrag.z; }
    } else {
        ma = absDir.z;
        if (lightToFrag.z > 0.0) { faceIndex = 4; u = lightToFrag.x; v = -lightToFrag.y; }
        else { faceIndex = 5; u = -lightToFrag.x; v = -lightToFrag.y; }
    }
    let texU = (u / ma) * 0.5 + 0.5;
    let texV = 0.5 - (v / ma) * 0.5;
    let faceUV = vec2f(texU, texV);
    let col = f32(faceIndex % 3);
    let row = f32(faceIndex / 3);
    let atlasUV = (faceUV + vec2f(col, row)) / vec2f(3.0, 2.0);
    let sampledDepth = textureSample(pointShadowAtlas, pointShadowSampler, atlasUV).r;
    let normalizedFragDist = fragDist / shadow.pointLightPosAndRange.w;
    if (normalizedFragDist - shadow.shadowBias > sampledDepth) { return 0.0; }
    return 1.0;
}
```

### 1.5 Modify calculateLightContribution()

Add `lightIndex: u32` and `worldPos: vec3f` parameters. Apply shadow factor:

```wgsl
fn calculateLightContribution(
    light: Light,
    lightIndex: u32,
    worldPos: vec3f,
    N: vec3f,
    V: vec3f,
    albedo: vec3f,
    metallic: f32,
    roughness: f32,
    F0: vec3f
) -> vec3f {
    // ... existing radiance calculation ...

    // Shadow factor (only for first light, only if receiving shadows)
    var shadowFactor: f32 = 1.0;
    if (lightIndex == 0u && uniforms.receiveShadow != 0u) {
        if (light.lightType == LIGHT_POINT) {
            shadowFactor = samplePointShadow(worldPos);
        } else {
            shadowFactor = sampleShadow(worldPos);
        }
    }

    // ... existing BRDF calculation ...

    return (diffuse + specular) * radiance * NdotL * shadowFactor;
}
```

### 1.6 Update fragment shader light loop

Pass light index and worldPos to calculateLightContribution:
```wgsl
for (var i = 0u; i < lightCount; i++) {
    Lo += calculateLightContribution(
        uniforms.lights[i], i, in.worldPos, N, V, albedo, metallic, roughness, F0
    );
}
```

---

## Part 2: C++ Changes

### 2.1 gpu_structs.h - Update PBRUniforms

Replace `_pad0` with `receiveShadow` (no size change):
```cpp
struct PBRUniforms {
    // ... existing fields through roughness ...
    float roughness;            // f32: 4 bytes, offset 228
    uint32_t lightCount;        // u32: 4 bytes, offset 232
    uint32_t receiveShadow;     // u32: 4 bytes, offset 236 (was _pad0)
    GPULight lights[MAX_LIGHTS]; // 64 * 4 = 256 bytes, offset 240
};                               // Total: 496 bytes (unchanged)
```

### 2.2 gpu_structs.h - Update PBRTexturedUniforms

Add `receiveShadow` after `alphaMode`, add padding for alignment:
```cpp
struct PBRTexturedUniforms {
    // ... existing fields through alphaMode ...
    uint32_t alphaMode;         // u32: 4 bytes, offset 268
    uint32_t receiveShadow;     // u32: 4 bytes, offset 272 (NEW)
    float _padShadow[3];        // 12 bytes padding, offset 276 (align to 288)
    GPULight lights[MAX_LIGHTS]; // 64 * 4 = 256 bytes, offset 288
};                               // Total: 544 bytes (was 528)
```

### 2.3 renderer.cpp - Update pipeline layouts

Around line 2252, change PBR pipeline layout to include shadow bind group:
```cpp
WGPUBindGroupLayout pbrLayouts[] = { m_pbrBindGroupLayout, m_shadowManager->getShadowSampleBindGroupLayout() };
WGPUPipelineLayoutDescriptor pbrPipelineLayoutDesc = {};
pbrPipelineLayoutDesc.bindGroupLayoutCount = 2;  // Was 1
pbrPipelineLayoutDesc.bindGroupLayouts = pbrLayouts;
```

Same for textured PBR pipeline (~line 2380).

For IBL pipeline (~line 2520), need 3 bind groups:
```cpp
WGPUBindGroupLayout iblLayouts[] = {
    m_pbrTexturedBindGroupLayout,
    m_iblBindGroupLayout,
    m_shadowManager->getShadowSampleBindGroupLayout()  // Group 2
};
pipelineLayoutDesc.bindGroupLayoutCount = 3;  // Was 2
```

### 2.4 renderer.cpp - Bind shadow group during rendering

Around line 3303-3304, after setting PBR bind group:
```cpp
} else if (usePBR) {
    wgpuRenderPassEncoderSetBindGroup(pass, 0, scalarPbrBindGroup, 1, &dynamicOffset);
    // NEW: Bind shadow sample group (group 1) for PBR
    wgpuRenderPassEncoderSetBindGroup(pass, 1, m_shadowManager->getShadowSampleBindGroup(), 0, nullptr);
}
```

For textured PBR (around line 3293), bind shadow after material:
```cpp
wgpuRenderPassEncoderSetBindGroup(pass, 0, currentTexturedBindGroup, 1, &dynamicOffset);
if (objUseDisplacement && m_displacementBindGroup) {
    wgpuRenderPassEncoderSetBindGroup(pass, 1, m_displacementBindGroup, 0, nullptr);
} else if (objUseIBL && m_iblBindGroup) {
    wgpuRenderPassEncoderSetBindGroup(pass, 1, m_iblBindGroup, 0, nullptr);
    // NEW: Bind shadow at group 2 for IBL
    wgpuRenderPassEncoderSetBindGroup(pass, 2, m_shadowManager->getShadowSampleBindGroup(), 0, nullptr);
} else {
    // NEW: Bind shadow at group 1 for textured PBR (no IBL)
    wgpuRenderPassEncoderSetBindGroup(pass, 1, m_shadowManager->getShadowSampleBindGroup(), 0, nullptr);
}
```

### 2.5 renderer.cpp - Set receiveShadow in uniforms

Around line 3167 where uniforms are populated, add for PBR uniforms:
```cpp
pbrUniforms.receiveShadow = obj.receiveShadow ? 1 : 0;
```

---

## Implementation Order

1. **gpu_structs.h** - Add receiveShadow fields, update struct sizes
2. **pbr.wgsl** - Add all shadow code (simplest shader, good for testing)
3. **renderer.cpp** - Update PBR pipeline layout and binding
4. **Test** with simple PBR scene
5. **pbr_textured.wgsl** - Add shadow code
6. **renderer.cpp** - Update textured PBR binding
7. **Test** with textured PBR
8. **pbr_ibl.wgsl** - Add shadow code (Group 2)
9. **renderer.cpp** - Update IBL pipeline layout and binding
10. **Test** with IBL scene

---

## Testing

Use `testing-fixtures/shadow-comprehensive/` or create similar test with:
- DirectionalLight with castShadow=true
- Ground plane with receiveShadow=true
- Objects using PBR materials
- Verify shadows appear on PBR surfaces
