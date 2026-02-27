# Tier 1: Trivial Operator Ports

Single-pass shaders, minimal parameters, straightforward math. All use `WgslFilterBase`.

## Operators

### 1. SolidColor (generator)
- **Params:** r (0-1, default 1), g (0-1, default 1), b (0-1, default 1), a (0-1, default 1)
- **Ports:** output texture only (no input — override `collect_ports`)
- **Shader:** Returns `vec4f(u.r, u.g, u.b, u.a)`
- **Note:** Generator, not a filter — needs custom `collect_ports` with no input port
- **Files:** `operators/gpu/solid_color/solid_color.cpp`, `operators/gpu/solid_color/solid_color.wgsl`

### 2. Brightness
- **Params:** brightness (-1 to 1, default 0), contrast (-1 to 1, default 0), gamma (0.1 to 10, default 1)
- **Ports:** input texture, output texture (default WgslFilterBase)
- **Shader:** Apply brightness as add, contrast as multiply around 0.5, gamma as pow
- **Files:** `operators/gpu/brightness/brightness.cpp`, `operators/gpu/brightness/brightness.wgsl`

### 3. Pixelate
- **Params:** size_x (1-256, default 8), size_y (1-256, default 8)
- **Ports:** default filter
- **Shader:** Quantize UV with `floor(uv * size) / size`, sample input
- **Files:** `operators/gpu/pixelate/pixelate.cpp`, `operators/gpu/pixelate/pixelate.wgsl`

### 4. Tile
- **Params:** repeat_x (0.1-32, default 2), repeat_y (0.1-32, default 2), offset_x (0-1, default 0), offset_y (0-1, default 0), mirror (bool, default false)
- **Ports:** default filter
- **Shader:** Multiply UV by repeat, add offset, apply fract() (or mirror with ping-pong)
- **Files:** `operators/gpu/tile/tile.cpp`, `operators/gpu/tile/tile.wgsl`

### 5. ChromaticAberration
- **Params:** amount (0-0.1, default 0.01), angle (0-360, default 0), radial (bool, default false)
- **Ports:** default filter
- **Shader:** Sample R/G/B channels at offset UVs along angle direction; if radial, offset scales with distance from center
- **Files:** `operators/gpu/chromatic_aberration/chromatic_aberration.cpp`, `operators/gpu/chromatic_aberration/chromatic_aberration.wgsl`

## CMake Additions
```cmake
add_vivid_operator(solid_color          operators/gpu/solid_color/solid_color.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(brightness           operators/gpu/brightness/brightness.cpp             EXTRA_LIBS webgpu)
add_vivid_operator(pixelate             operators/gpu/pixelate/pixelate.cpp                 EXTRA_LIBS webgpu)
add_vivid_operator(tile                 operators/gpu/tile/tile.cpp                         EXTRA_LIBS webgpu)
add_vivid_operator(chromatic_aberration operators/gpu/chromatic_aberration/chromatic_aberration.cpp EXTRA_LIBS webgpu)
```

## Verification
Build and launch vivid. For each operator:
- Add it to a graph, connect to output
- SolidColor: verify color picker works, alpha transparency
- Brightness: chain after Noise, sweep each param
- Pixelate: chain after Noise, verify mosaic at various sizes
- Tile: chain after Shape, verify repeat/mirror modes
- ChromaticAberration: chain after any source, verify RGB fringing
