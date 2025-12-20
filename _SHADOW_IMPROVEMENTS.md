# Shadow Improvements: Research & Implementation Plan

Research into three.js's shadow implementation compared with Vivid's current approach.

**Key finding:** Three.js uses essentially the same 2D texture atlas workaround for point shadows that Vivid uses (due to similar WebGL/wgpu limitations), but has more sophisticated filtering techniques.

---

## Current Architecture

Shadow code has been extracted from `renderer.cpp` to a dedicated `ShadowManager` class:

| File | Purpose |
|------|---------|
| `shadow_manager.h` | ShadowManager class definition, all shadow member variables |
| `shadow_manager.cpp` | Shadow resource creation, render passes, bind group management |
| `renderer.cpp` | Calls ShadowManager; contains shadow sampling shaders in FLAT_SHADER_SOURCE |

**Key classes:**
- `ShadowManager` - Owns all shadow GPU resources, handles shadow pass rendering
- `Render3D` - Uses `std::unique_ptr<ShadowManager> m_shadowManager`

---

## Current State: Vivid vs Three.js

| Feature | Three.js | Vivid |
|---------|----------|-------|
| Point shadow storage | Single 2D atlas with viewports | 6 separate 2D textures |
| Directional/Spot | 2D depth texture | 2D depth texture |
| PCF filtering | 5-sample Vogel disk + IGN | None (single sample) |
| VSM support | Yes | No |
| Soft point shadows | Yes (tangent-space offsetting) | No |
| Shadow update control | autoUpdate/needsUpdate flags | Always update |
| Material caching | Extensive | Partial (bind groups only) |

---

## Key Learnings from Three.js

### 1. PCF Filtering with Vogel Disk Sampling

Three.js uses sophisticated PCF rather than simple grid sampling:

```glsl
// Interleaved Gradient Noise for per-pixel rotation
float interleavedGradientNoise(vec2 position) {
    return fract(52.9829189 * fract(dot(position, vec2(0.06711056, 0.00583715))));
}

// Vogel disk distribution (golden angle spiral)
vec2 vogelDiskSample(int sampleIndex, int samplesCount, float phi) {
    float r = sqrt(float(sampleIndex) + 0.5) / sqrt(float(samplesCount));
    float theta = float(sampleIndex) * 2.4 + phi;  // golden angle
    return r * vec2(cos(theta), sin(theta));
}
```

**Benefits:**
- Only 5 samples needed for good results (vs typical 9-25 for grid)
- Per-pixel rotation via IGN breaks up banding artifacts
- Better distribution than random or grid patterns
- Combined with hardware linear filtering = "20 effective taps"

### 2. Point Light Shadow: 2D Atlas Approach

Three.js faced the same WebGL limitation Vivid faces with wgpu:
- Renders 6 faces to viewports within a single 2D texture
- Uses a `cube2uv` function to remap 3D direction to atlas UV coordinates
- Closed as "not planned" when asked to switch to cubemaps (April 2024)

**From three.js maintainers:**
> "Using 2D samplers instead solved portability issues... the perceived advantages of cubemaps rely on assumptions about OpenGL implementation specifics not mandated by the specification."

**Vivid's current approach:** 6 separate 2D textures (workaround for wgpu-native #1690)

**Potential improvement:** Pack all 6 faces into ONE texture with viewports:
- Reduces texture bindings (1 vs 6)
- Simpler bind group management
- May help with memory leak (fewer Metal shader recompilations)

### 3. Variance Shadow Maps (VSM)

Three.js supports VSM as an alternative to PCF:

```glsl
float variance = texel.y - texel.x * texel.x;
float d = depth - texel.x;
float p_max = variance / (variance + d * d);  // Chebyshev bound
p_max = clamp((p_max - 0.3) / 0.65, 0.0, 1.0);  // Light bleeding reduction
```

**Pros:** Very soft shadows, can be blurred (separable filter)
**Cons:** Light bleeding artifacts, requires RG16F texture

### 4. Shadow Update Control

```javascript
shadow.autoUpdate = false;     // Disable automatic updates
shadow.needsUpdate = true;     // Manual trigger when needed
```

Static scenes don't need shadow map recomputation every frame.

### 5. Material Caching for Shadow Pass

Three.js caches depth materials to avoid shader recompilation:
- One depth material per unique vertex configuration
- Reused across all objects with same structure
- Directly addresses Vivid's memory leak (Metal shader recompilation)

---

## Implementation Phases

### Phase 1: Soft Shadow Filtering (PCF)
**Priority: High | Effort: Medium | Impact: High**

Add Vogel disk PCF to shadow sampling for soft shadow edges.

**Files to Modify:**
- `addons/vivid-render3d/src/renderer.cpp` (FLAT_SHADER_SOURCE - contains `sampleShadow()` and `samplePointShadow()`)
- `addons/vivid-render3d/src/shadow_manager.cpp` (shadow pass shaders: SHADOW_SHADER_SOURCE, POINT_SHADOW_SHADER_SOURCE)

**Steps:**
1. Add helper functions to shader:
   - `interleavedGradientNoise(position)` - per-pixel noise for rotation
   - `vogelDiskSample(index, count, phi)` - golden angle spiral sampling

2. Update `sampleShadow()` for directional/spot lights:
   - Replace single `textureSampleCompare` with 5-sample loop
   - Apply Vogel disk offsets scaled by texel size
   - Average results

3. Update `samplePointShadow()` for point lights:
   - Create tangent/bitangent basis from dominant axis
   - Apply Vogel disk offsets in tangent space
   - Average depth comparisons

4. Add quality control uniform:
   - `shadowSoftness: f32` to control filter radius

**WGSL Implementation:**
```wgsl
fn interleavedGradientNoise(pos: vec2<f32>) -> f32 {
    return fract(52.9829189 * fract(dot(pos, vec2(0.06711056, 0.00583715))));
}

fn vogelDiskSample(idx: i32, count: i32, phi: f32) -> vec2<f32> {
    let r = sqrt(f32(idx) + 0.5) / sqrt(f32(count));
    let theta = f32(idx) * 2.39996323 + phi;
    return r * vec2(cos(theta), sin(theta));
}

fn sampleShadowPCF(coord: vec3<f32>, texelSize: f32) -> f32 {
    let phi = interleavedGradientNoise(coord.xy * 1000.0) * 6.28318;
    var shadow = 0.0;
    for (var i = 0; i < 5; i++) {
        let offset = vogelDiskSample(i, 5, phi) * texelSize * 2.0;
        shadow += textureSampleCompare(shadowMap, shadowSampler,
                                       coord.xy + offset, coord.z);
    }
    return shadow * 0.2;
}
```

**Testing:**
- `testing-fixtures/shadow-directional/` - verify soft edges
- `testing-fixtures/shadow-point/` - verify point light soft shadows

---

### Phase 2: Point Shadow Atlas Consolidation
**Priority: Medium | Effort: Medium | Impact: Medium (may fix memory leak)**

Replace 6 separate point shadow textures with single 3x2 atlas.

**Files to Modify:**
- `addons/vivid-render3d/src/shadow_manager.cpp`
  - `createPointShadowResources()` - single texture creation
  - `renderPointShadowPass()` - viewport-based rendering
  - `POINT_SHADOW_SHADER_SOURCE` - may need adjustments
- `addons/vivid-render3d/src/renderer.cpp`
  - `samplePointShadow()` in FLAT_SHADER_SOURCE - cube2uv remapping
- `addons/vivid-render3d/include/vivid/render3d/shadow_manager.h`
  - Remove 6 texture members, add single atlas

**Steps:**

1. Create single atlas texture:
   ```cpp
   // 3 columns x 2 rows layout
   // Face order: +X, -X, +Y, -Y, +Z, -Z
   int atlasWidth = m_pointShadowResolution * 3;
   int atlasHeight = m_pointShadowResolution * 2;
   ```

2. Render to viewports:
   ```cpp
   // Face 0 (+X): viewport(0, 0, res, res)
   // Face 1 (-X): viewport(res, 0, res, res)
   // Face 2 (+Y): viewport(res*2, 0, res, res)
   // Face 3 (-Y): viewport(0, res, res, res)
   // Face 4 (+Z): viewport(res, res, res, res)
   // Face 5 (-Z): viewport(res*2, res, res, res)
   ```

3. Implement cube2uv shader function:
   ```wgsl
   fn cube2uv(dir: vec3<f32>) -> vec3<f32> {
       let absDir = abs(dir);
       var face: i32;
       var uv: vec2<f32>;

       if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
           face = select(1, 0, dir.x > 0.0);
           uv = vec2(-dir.z, -dir.y) / absDir.x * select(-1.0, 1.0, dir.x > 0.0);
       } else if (absDir.y >= absDir.z) {
           face = select(3, 2, dir.y > 0.0);
           uv = vec2(dir.x, dir.z) / absDir.y * select(-1.0, 1.0, dir.y > 0.0);
       } else {
           face = select(5, 4, dir.z > 0.0);
           uv = vec2(dir.x, -dir.y) / absDir.z * select(1.0, -1.0, dir.z > 0.0);
       }

       // Map to atlas UV
       let col = face % 3;
       let row = face / 3;
       let atlasUV = (uv * 0.5 + 0.5 + vec2(f32(col), f32(row))) / vec2(3.0, 2.0);

       return vec3(atlasUV, max(absDir.x, max(absDir.y, absDir.z)));
   }
   ```

4. Simplify bind groups (single texture binding instead of 6)

**Testing:**
- `testing-fixtures/shadow-point/` - verify shadows render correctly
- Monitor memory usage over time

---

### Phase 3: Shadow Update Control
**Priority: Low | Effort: Low | Impact: Low**

Add flags to skip shadow map rendering for static lights.

**Files to Modify:**
- `addons/vivid-render3d/include/vivid/render3d/light_operators.h`
- `addons/vivid-render3d/include/vivid/render3d/shadow_manager.h`
- `addons/vivid-render3d/src/shadow_manager.cpp`
- `addons/vivid-render3d/src/renderer.cpp` (process() - calls to ShadowManager)

**Steps:**

1. Add shadow update flags:
   ```cpp
   class DirectionalLight : public LightOperator {
       bool shadowAutoUpdate = true;
       bool shadowNeedsUpdate = true;
   };
   ```

2. Check flags in render loop:
   ```cpp
   void renderShadowPass() {
       for (auto& light : m_lights) {
           if (!light.shadowAutoUpdate && !light.shadowNeedsUpdate) continue;
           // ... render shadow map ...
           light.shadowNeedsUpdate = false;
       }
   }
   ```

3. Expose in chain API:
   ```cpp
   directional.shadowAutoUpdate = false;  // Static light
   directional.shadowNeedsUpdate = true;  // Trigger update when moved
   ```

---

## Future Considerations (Not Planned)

### Variance Shadow Maps (VSM)
- Requires RG16F texture format
- Separable blur pass
- Light bleeding artifacts require tuning

### Cascaded Shadow Maps (CSM)
- For large outdoor scenes
- Multiple shadow maps at different resolutions

### Contact Hardening (PCSS)
- Shadows softer further from occluder
- Requires blocker search pass
- Expensive but realistic

---

## Sources

- [Three.js WebGLShadowMap.js](https://github.com/mrdoob/three.js/blob/dev/src/renderers/webgl/WebGLShadowMap.js)
- [Should Point Shadows Use Cubemaps? (GitHub Issue #11189)](https://github.com/mrdoob/three.js/issues/11189)
- [Three.js Shadow Sampling Shaders](https://github.com/mrdoob/three.js/blob/dev/src/renderers/shaders/ShaderChunk/shadowmap_pars_fragment.glsl.js)
- [Cascaded Shadow Maps for Three.js](https://github.com/StrandedKitty/three-csm)
- [Three.js VSM Shadow Example](https://threejs.org/examples/webgl_shadowmap_vsm.html)
- [Three.js Shadows Journey Tutorial](https://threejs-journey.com/lessons/shadows)
