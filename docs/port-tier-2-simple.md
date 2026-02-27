# Tier 2: Simple Operator Ports

Slightly more shader math (color space conversion, edge detection, multi-mode). All use `WgslFilterBase`.

## Operators

### 1. Gradient (generator)
- **Params:** mode (enum: linear/radial, default 0), angle (0-360, default 0), center_x (0-1, default 0.5), center_y (0-1, default 0.5), scale (0.1-10, default 1), offset (-1 to 1, default 0)
- **Ports:** output texture only (generator — override `collect_ports`)
- **Shader:** Linear: project UV onto angle vector. Radial: distance from center. Apply scale/offset, output as grayscale.
- **Files:** `operators/gpu/gradient/gradient.cpp`, `operators/gpu/gradient/gradient.wgsl`

### 2. HSV
- **Params:** hue_shift (0-360, default 0), saturation (-1 to 1, default 0), value (-1 to 1, default 0)
- **Ports:** default filter
- **Shader:** RGB to HSV, apply shifts, HSV back to RGB. Standard conversion formulas.
- **Files:** `operators/gpu/hsv/hsv.cpp`, `operators/gpu/hsv/hsv.wgsl`

### 3. Mirror
- **Params:** mode (enum: horizontal/vertical/quad/kaleidoscope, default 0), segments (2-32, default 6), angle (0-360, default 0), center_x (0-1, default 0.5), center_y (0-1, default 0.5)
- **Ports:** default filter
- **Shader:** H: `abs(uv.x - 0.5) * 2`. V: same for y. Quad: both. Kaleidoscope: polar coords, modulo by segment angle, reflect.
- **Files:** `operators/gpu/mirror/mirror.cpp`, `operators/gpu/mirror/mirror.wgsl`

### 4. Edge
- **Params:** strength (0-10, default 1), threshold (0-1, default 0), invert (bool, default false)
- **Ports:** default filter
- **Shader:** Sobel operator — sample 8 neighbors, compute Gx/Gy gradients, magnitude = sqrt(Gx^2 + Gy^2). Threshold and scale.
- **Note:** Needs `u.resolution` for texel size (`1.0 / u.resolution`)
- **Files:** `operators/gpu/edge/edge.cpp`, `operators/gpu/edge/edge.wgsl`

### 5. Scanlines
- **Params:** spacing (2-100, default 4, int), thickness (0-1, default 0.5), intensity (0-1, default 0.5), vertical (bool, default false)
- **Ports:** default filter
- **Shader:** `line = fract(uv.y * resolution.y / spacing)`, darken when `line < thickness`, blend by intensity
- **Files:** `operators/gpu/scanlines/scanlines.cpp`, `operators/gpu/scanlines/scanlines.wgsl`

## CMake Additions
```cmake
add_vivid_operator(gradient   operators/gpu/gradient/gradient.cpp     EXTRA_LIBS webgpu)
add_vivid_operator(hsv        operators/gpu/hsv/hsv.cpp               EXTRA_LIBS webgpu)
add_vivid_operator(mirror     operators/gpu/mirror/mirror.cpp         EXTRA_LIBS webgpu)
add_vivid_operator(edge       operators/gpu/edge/edge.cpp             EXTRA_LIBS webgpu)
add_vivid_operator(scanlines  operators/gpu/scanlines/scanlines.cpp   EXTRA_LIBS webgpu)
```

## Verification
Build and launch vivid. For each operator:
- Gradient: verify linear vs radial modes, angle rotation, scale
- HSV: chain after Noise (color mode), sweep hue through full 360
- Mirror: test all 4 modes, kaleidoscope segment count
- Edge: chain after Shape, verify edge detection with threshold sweep
- Scanlines: chain after any source, toggle vertical mode, adjust spacing
