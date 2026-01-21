# Shader Standardization: vivid-core

## Goal
Apply the same shader standardization pattern established in vivid-render3d to vivid-core's 2D effects and particle systems.

## Problem
- Fullscreen triangle vertex shader duplicated 6+ times
- HSV↔RGB color conversion duplicated in ramp.wgsl and gpu_particles.cpp
- Simplex/Perlin noise implementations duplicated across particle effects
- Pixel-to-NDC coordinate conversion repeated in 5+ files
- C++ pipeline setup code (blend states, vertex attributes) repeated in 5+ files
- ~750 lines of duplicated code across 10+ patterns

## Solution

Use the same `// @include "path.wgsl"` preprocessing established in Phase 1.

---

## Implementation Plan

### Phase 1: Create WGSL Shader Libraries

**New directory:** `modules/vivid-core/shaders/lib/`

**Files to create:**

1. **`lib/fullscreen.wgsl`** (~30 lines)
   ```wgsl
   // Standard fullscreen triangle vertex shader
   // Usage: call vs_fullscreen() or use the provided vs_main

   struct FullscreenOutput {
       @builtin(position) position: vec4f,
       @location(0) uv: vec2f,
   }

   fn vs_fullscreen(vertexIndex: u32, flipY: bool) -> FullscreenOutput {
       var positions = array<vec2f, 3>(
           vec2f(-1.0, -1.0),
           vec2f( 3.0, -1.0),
           vec2f(-1.0,  3.0)
       );
       var out: FullscreenOutput;
       let pos = positions[vertexIndex];
       out.position = vec4f(pos, 0.0, 1.0);
       out.uv = pos * 0.5 + 0.5;
       if (flipY) { out.uv.y = 1.0 - out.uv.y; }
       return out;
   }
   ```

2. **`lib/color.wgsl`** (~50 lines)
   ```wgsl
   // Color space conversions

   fn hsv2rgb(hsv: vec3f) -> vec3f { ... }
   fn rgb2hsv(rgb: vec3f) -> vec3f { ... }
   fn srgbToLinear(c: vec3f) -> vec3f { ... }
   fn linearToSrgb(c: vec3f) -> vec3f { ... }
   ```

3. **`lib/noise.wgsl`** (~120 lines)
   ```wgsl
   // Noise functions

   // Helpers
   fn mod289_3(x: vec3f) -> vec3f { ... }
   fn mod289_4(x: vec4f) -> vec4f { ... }
   fn permute(x: vec4f) -> vec4f { ... }
   fn taylorInvSqrt(r: vec4f) -> vec4f { ... }

   // 2D Simplex noise
   fn snoise2(v: vec2f) -> f32 { ... }

   // 3D Simplex noise
   fn snoise3(v: vec3f) -> f32 { ... }

   // Curl noise (for particle advection)
   fn curlNoise(p: vec3f) -> vec3f { ... }
   ```

4. **`lib/coords.wgsl`** (~20 lines)
   ```wgsl
   // Coordinate transformation utilities

   // Pixel coordinates to NDC (-1 to 1)
   fn pixelToNdc(pixel: vec2f, resolution: vec2f) -> vec2f {
       return vec2f(
           (pixel.x / resolution.x) * 2.0 - 1.0,
           1.0 - (pixel.y / resolution.y) * 2.0
       );
   }

   // NDC to UV (0 to 1)
   fn ndcToUv(ndc: vec2f) -> vec2f {
       return ndc * 0.5 + 0.5;
   }

   // Aspect ratio correction
   fn correctAspect(uv: vec2f, resolution: vec2f) -> vec2f {
       let aspect = resolution.x / resolution.y;
       return vec2f((uv.x - 0.5) * aspect + 0.5, uv.y);
   }
   ```

5. **`lib/constants.wgsl`** (~15 lines)
   ```wgsl
   // Common constants for 2D effects

   const PI: f32 = 3.14159265359;
   const TWO_PI: f32 = 6.28318530718;
   const HALF_PI: f32 = 1.57079632679;
   const EPSILON: f32 = 0.0001;
   const GOLDEN_RATIO: f32 = 1.61803398875;
   ```

### Phase 2: Refactor External Shaders

**Files to modify:**

| File | Changes |
|------|---------|
| `blit.wgsl` | Use `// @include "lib/fullscreen.wgsl"` |
| `feedback.wgsl` | Use fullscreen include |
| `ramp.wgsl` | Use `// @include "lib/color.wgsl"` for HSV, fullscreen include |
| `text.wgsl` | Use `// @include "lib/coords.wgsl"` |

### Phase 3: Extract Embedded Shaders

**Priority extractions:**

| Source File | Shader Constant | New External File |
|-------------|-----------------|-------------------|
| `gpu_particles.cpp` | `PARTICLE_SHADER` | `particles_gpu.wgsl` |
| `gpu_particles.cpp` | `RENDER_SHADER` | `particles_gpu_render.wgsl` |
| `particle_renderer.cpp` | `CIRCLE_SHADER` | `particle_circle.wgsl` |
| `particle_renderer.cpp` | `SPRITE_SHADER` | `particle_sprite.wgsl` |
| `plexus.cpp` | `PLEXUS_POINT_SHADER` | `plexus_point.wgsl` |
| `plexus.cpp` | `PLEXUS_LINE_SHADER` | `plexus_line.wgsl` |
| `canvas_renderer.cpp` | `CANVAS_SHADER` | `canvas.wgsl` |
| `overlay_canvas.cpp` | `OVERLAY_SHADER` | `overlay.wgsl` |
| `fluid_sim.cpp` | Multiple compute shaders | `fluid_*.wgsl` (optional) |

**Keep embedded (compute-heavy, rarely changed):**
- `fluid_sim.cpp` compute shaders (complex simulation kernels)

### Phase 4: C++ Helper Utilities

**Enhance:** `modules/vivid-core/src/effects/gpu_common.cpp`

Add these utility functions:

```cpp
namespace vivid {

// Standard alpha blend state
WGPUBlendState createAlphaBlendState();

// Additive blend state
WGPUBlendState createAdditiveBlendState();

// Standard vertex layout for 2D rendering (pos, uv, color)
std::vector<WGPUVertexAttribute> create2DVertexAttributes();

// Create 1x1 white texture for solid color rendering
WGPUTexture createWhiteTexture(WGPUDevice device, WGPUQueue queue);

// Create circle mesh with triangle fan
CircleMesh createCircleMesh(int segments = 32);

// Create standard uniform+sampler+texture bind group layout
WGPUBindGroupLayout createStandardBindGroupLayout(WGPUDevice device);

} // namespace vivid
```

### Phase 5: Update Build System

**Modify:** `modules/vivid-core/CMakeLists.txt`
- Copy `shaders/lib/*.wgsl` to build output

---

## File Summary

**New WGSL files (5):**
- `lib/fullscreen.wgsl`
- `lib/color.wgsl`
- `lib/noise.wgsl`
- `lib/coords.wgsl`
- `lib/constants.wgsl`

**Modified external shaders (4):**
- `blit.wgsl`, `feedback.wgsl`, `ramp.wgsl`, `text.wgsl`

**New external shaders from extraction (8):**
- `particles_gpu.wgsl`, `particles_gpu_render.wgsl`
- `particle_circle.wgsl`, `particle_sprite.wgsl`
- `plexus_point.wgsl`, `plexus_line.wgsl`
- `canvas.wgsl`, `overlay.wgsl`

**Modified C++ files (6):**
- `gpu_common.cpp` - add utilities
- `gpu_particles.cpp` - load external shaders
- `particle_renderer.cpp` - load external shaders
- `plexus.cpp` - load external shaders
- `canvas_renderer.cpp` - load external shaders
- `overlay_canvas.cpp` - load external shaders

---

## Estimated Impact

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Duplicated lines | ~750 | ~300 | 60% reduction |
| Fullscreen shader copies | 6 | 1 | 83% reduction |
| Noise implementations | 2 | 1 | 50% reduction |
| HSV conversion copies | 2 | 1 | 50% reduction |
| Files with embedded shaders | 8 | 2 | 75% reduction |

---

## Verification

1. **Build test:** `cmake -B build && cmake --build build`
2. **Run 2D examples:**
   - `./build/bin/vivid projects/2d-effects/noise-layers`
   - `./build/bin/vivid projects/particles/gpu-particles`
3. **Visual regression:** Capture snapshots before/after
4. **Hot-reload test:** Edit a lib file, verify changes apply

---

## Implementation Order

1. Create shader libraries (Phase 1)
2. Update CMakeLists.txt (Phase 5)
3. Build and verify libraries copy correctly
4. Refactor external shaders one at a time (Phase 2)
5. Extract embedded shaders incrementally (Phase 3)
6. Add C++ utilities as needed (Phase 4)
