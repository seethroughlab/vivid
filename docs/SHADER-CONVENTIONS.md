# Vivid Shader Conventions

Vivid uses WGSL (WebGPU Shading Language) for all shaders. The runtime automatically prepends a wrapper that provides uniforms, vertex shader, and input texture bindings.

## Shader Structure

You only need to write the fragment shader. The runtime provides everything else:

```wgsl
// Your shader file only needs the fragment function
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let uv = in.uv;  // UV coordinates [0,1]
    return vec4f(uv.x, uv.y, 0.0, 1.0);
}
```

## Provided Uniforms

The runtime provides a uniform buffer `u` with these fields:

| Uniform | Type | Description |
|---------|------|-------------|
| `u.time` | `f32` | Seconds since start |
| `u.deltaTime` | `f32` | Seconds since last frame |
| `u.resolution` | `vec2f` | Output resolution in pixels |
| `u.frame` | `i32` | Frame number |
| `u.mode` | `i32` | Mode selector (for multi-mode shaders) |
| `u.param0` - `u.param7` | `f32` | Generic float parameters |
| `u.vec0` | `vec2f` | Generic 2D vector parameter |
| `u.vec1` | `vec2f` | Generic 2D vector parameter |

## Provided Bindings

| Binding | Name | Type | Description |
|---------|------|------|-------------|
| `@group(0) @binding(0)` | `u` | `Uniforms` | Uniform buffer |
| `@group(0) @binding(1)` | `inputSampler` | `sampler` | Linear sampler |
| `@group(0) @binding(2)` | `inputTexture` | `texture_2d<f32>` | Input texture (if provided) |

## Provided Types

The wrapper defines these types you can use:

```wgsl
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}
```

## Example: Animated Noise

```wgsl
// shaders/my_noise.wgsl
// Uses: u.param0 = scale, u.param1 = speed

fn hash(p: vec2f) -> f32 {
    return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453);
}

fn noise(p: vec2f) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i), hash(i + vec2f(1.0, 0.0)), u.x),
        mix(hash(i + vec2f(0.0, 1.0)), hash(i + vec2f(1.0, 1.0)), u.x),
        u.y
    );
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let scale = u.param0;
    let speed = u.param1;

    let uv = in.uv * scale;
    let t = u.time * speed;

    let n = noise(uv + vec2f(t, t * 0.5));
    return vec4f(n, n, n, 1.0);
}
```

## Example: Image Processing

```wgsl
// shaders/brightness.wgsl
// Uses: u.param0 = brightness, u.param1 = contrast

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let brightness = u.param0;
    let contrast = u.param1;

    // Sample input texture
    let color = textureSample(inputTexture, inputSampler, in.uv);

    // Apply brightness and contrast
    var result = (color.rgb - 0.5) * contrast + 0.5 + brightness;
    result = clamp(result, vec3f(0.0), vec3f(1.0));

    return vec4f(result, color.a);
}
```

## Example: Two-Input Composite

```wgsl
// shaders/composite.wgsl
// For two-input shaders, the second texture uses binding 3
// Uses: u.param0 = mix amount, u.mode = blend mode

@group(0) @binding(3) var inputTexture2: texture_2d<f32>;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let mix_amount = u.param0;
    let mode = u.mode;

    let a = textureSample(inputTexture, inputSampler, in.uv);
    let b = textureSample(inputTexture2, inputSampler, in.uv);

    var result: vec3f;

    // Blend modes
    if (mode == 0) {
        // Normal (over)
        result = mix(a.rgb, b.rgb, b.a * mix_amount);
    } else if (mode == 1) {
        // Add
        result = a.rgb + b.rgb * mix_amount;
    } else if (mode == 2) {
        // Multiply
        result = mix(a.rgb, a.rgb * b.rgb, mix_amount);
    } else if (mode == 3) {
        // Screen
        result = mix(a.rgb, 1.0 - (1.0 - a.rgb) * (1.0 - b.rgb), mix_amount);
    } else {
        // Difference
        result = mix(a.rgb, abs(a.rgb - b.rgb), mix_amount);
    }

    return vec4f(clamp(result, vec3f(0.0), vec3f(1.0)), a.a);
}
```

## Parameter Conventions

Common parameter mappings used by built-in operators:

| Parameter | Common Usage |
|-----------|--------------|
| `param0` | Primary effect strength (scale, amount, intensity) |
| `param1` | Secondary parameter (speed, contrast, radius) |
| `param2` | Tertiary parameter (octaves, passes, threshold) |
| `param3` - `param7` | Additional parameters as needed |
| `vec0` | 2D offset, translation, center point |
| `vec1` | 2D scale, secondary offset |
| `mode` | Effect variant selector |

## Coordinate System

- UV coordinates range from `(0, 0)` at top-left to `(1, 1)` at bottom-right
- Aspect ratio can be corrected: `uv.x * u.resolution.x / u.resolution.y`
- Pixel coordinates: `in.uv * u.resolution`

## Hot Reload

Shaders hot-reload automatically when you save the `.wgsl` file. The runtime:
1. Detects the file change
2. Recompiles the shader
3. Updates the render pipeline
4. Continues rendering with the new shader

If there's a compilation error, it appears in the VS Code editor and the previous shader continues running.

## Debugging Tips

1. **Visualize UVs**: Return `vec4f(in.uv, 0.0, 1.0)` to see coordinate mapping
2. **Visualize parameters**: Return `vec4f(u.param0, u.param1, u.param2, 1.0)`
3. **Check resolution**: `let aspect = u.resolution.x / u.resolution.y`
4. **Time-based debugging**: Use `fract(u.time)` to create repeating patterns

## WGSL Tips

```wgsl
// Smooth interpolation
let t = smoothstep(0.0, 1.0, value);

// Remap value from one range to another
fn remap(v: f32, in_min: f32, in_max: f32, out_min: f32, out_max: f32) -> f32 {
    return out_min + (v - in_min) * (out_max - out_min) / (in_max - in_min);
}

// Rotate UV coordinates
fn rotate(uv: vec2f, angle: f32) -> vec2f {
    let c = cos(angle);
    let s = sin(angle);
    return vec2f(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
}

// Distance to center (for radial effects)
let d = length(in.uv - 0.5);
```
