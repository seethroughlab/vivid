# Tier 3: Moderate Operator Ports

More complex shaders, multiple uniforms, or shader-side data structures. Most use `WgslFilterBase` but shaders are longer.

## Operators

### 1. Ramp (generator)
- **Params:** type (enum: linear/radial, default 0), angle (0-360, default 0), offset_x (-1 to 1, default 0), offset_y (-1 to 1, default 0), scale (0.1-10, default 1), repeat (0.1-10, default 1), hue_offset (0-1, default 0), hue_range (0-1, default 1), saturation (0-1, default 1), brightness (0-1, default 1)
- **Ports:** output only (generator)
- **Shader:** Generate gradient value, map through HSV color ramp. The gradient position determines hue (offset + range), with configurable saturation/brightness.
- **Files:** `operators/gpu/ramp/ramp.cpp`, `operators/gpu/ramp/ramp.wgsl`

### 2. Dither
- **Params:** pattern (enum: bayer2x2/bayer4x4/bayer8x8, default 1), levels (2-256, default 4), strength (0-1, default 1)
- **Ports:** default filter
- **Shader:** Define Bayer matrices as const arrays. Look up threshold from matrix using `pixel_pos % matrix_size`. Quantize: `floor((color + threshold * strength) * levels) / levels`.
- **Note:** Bayer 8x8 matrix is 64 floats — keep as const array in WGSL.
- **Files:** `operators/gpu/dither/dither.cpp`, `operators/gpu/dither/dither.wgsl`

### 3. Transform
- **Params:** scale_x (0.01-10, default 1), scale_y (0.01-10, default 1), rotation (0-360, default 0), translate_x (-1 to 1, default 0), translate_y (-1 to 1, default 0), pivot_x (0-1, default 0.5), pivot_y (0-1, default 0.5)
- **Ports:** default filter
- **Shader:** Translate UV to pivot origin, apply inverse rotation and scale, translate back, add translation offset. Sample input at transformed UV.
- **Note:** Must apply inverse transform to UV (we're mapping output pixels to input locations).
- **Files:** `operators/gpu/transform/transform.cpp`, `operators/gpu/transform/transform.wgsl`

### 4. CRTEffect
- **Params:** curvature (0-1, default 0.3), vignette (0-1, default 0.3), scanline_intensity (0-1, default 0.4), bloom (0-1, default 0.2), chromatic (0-0.05, default 0.005)
- **Ports:** default filter
- **Shader:** Combined effect chain in single pass:
  1. Barrel distortion (curvature)
  2. Chromatic aberration (RGB channel split)
  3. Scanline darkening (periodic)
  4. Fake bloom (biased sample blend)
  5. Vignette (radial darken)
  6. Black outside distorted region
- **Files:** `operators/gpu/crt_effect/crt_effect.cpp`, `operators/gpu/crt_effect/crt_effect.wgsl`

## CMake Additions
```cmake
add_vivid_operator(ramp        operators/gpu/ramp/ramp.cpp             EXTRA_LIBS webgpu)
add_vivid_operator(dither      operators/gpu/dither/dither.cpp         EXTRA_LIBS webgpu)
add_vivid_operator(transform   operators/gpu/transform/transform.cpp   EXTRA_LIBS webgpu)
add_vivid_operator(crt_effect  operators/gpu/crt_effect/crt_effect.cpp EXTRA_LIBS webgpu)
```

## Verification
Build and launch vivid. For each operator:
- Ramp: verify color rainbow output, hue range sweep, repeat tiling
- Dither: chain after Gradient, compare Bayer matrix sizes, adjust levels
- Transform: chain after Shape, verify rotation around pivot, scale
- CRTEffect: chain after any source, verify retro CRT look, edge curvature blackout
