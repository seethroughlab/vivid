# Data-Driven WGSL Operators

GPU operators that are authored entirely in WGSL — no C++ boilerplate needed. The runtime reads a JSON metadata header from the `.wgsl` file and generates a full operator wrapper automatically.

## How It Works

1. Write a `.wgsl` file with a JSON metadata header in a block comment
2. The WGSL header parser (`parse_wgsl_header()`) extracts the metadata
3. `WgslOperator` wraps the shader as a fully functional operator with params, ports, hot-reload

## WGSL Header Format

The JSON metadata block must be the first thing in the file (a `/*{...}*/` block comment):

```wgsl
/*{
  "name": "my_filter",
  "time_dependent": false,
  "inputs": [
    {"name": "source"},
    {"name": "mask"}
  ],
  "params": [
    {"name": "amount", "type": "float", "default": 0.5, "min": 0.0, "max": 1.0},
    {"name": "mode",   "type": "int",   "default": 0, "choices": ["Add", "Multiply", "Screen"]},
    {"name": "color_r", "type": "float", "default": 1.0, "min": 0.0, "max": 1.0,
     "display": "color", "group": "Color", "columns": 3, "column": 0},
    {"name": "color_g", "type": "float", "default": 1.0, "min": 0.0, "max": 1.0,
     "display": "color", "group": "Color", "columns": 3, "column": 1},
    {"name": "color_b", "type": "float", "default": 1.0, "min": 0.0, "max": 1.0,
     "display": "color", "group": "Color", "columns": 3, "column": 2}
  ]
}*/

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let src = textureSample(inputTex0, texSampler, input.uv);
    let mask = textureSample(inputTex1, texSampler, input.uv);
    return mix(src, src * vec4f(u.color_r, u.color_g, u.color_b, 1.0), u.amount * mask.r);
}
```

## Header Fields

### Top-Level

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Operator type name |
| `description` | string | No | Human-readable description |
| `time_dependent` | bool | No | `true` if shader reads `u.time` (default `false`) |
| `inputs` | array | No | Input texture port definitions. Omit for default 1-in/1-out. |

### Param Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | string | (required) | Param name (appears as `u.<name>` in WGSL) |
| `type` | string | `"float"` | `"float"`, `"int"`, or `"bool"` |
| `default` | number | `0.0` | Default value |
| `min` | number | `0.0` | Minimum value |
| `max` | number | `1.0` | Maximum value |
| `label` | string | (name) | Display label in inspector |
| `choices` | array | — | String labels for int enum params |
| `display` | string | — | Display hint: `"knob"`, `"xy_pad"`, `"color"`, `"hidden"` |
| `group` | string | — | Collapsible group name |
| `columns` | int | `0` | Multi-column layout: total columns |
| `column` | int | `0` | Multi-column layout: this param's column index |
| `asset_kind` | string | — | Asset library kind (e.g. `"wavetable"`) |

## Auto-Generated Preamble

Data-driven operators get the same auto-generated WGSL preamble as `WgslFilterBase` operators:

```wgsl
struct Uniforms {
    resolution: vec2f,
    time: f32,
    frame: u32,
    // ... all params by name ...
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;   // 1 input
// or: inputTex0, inputTex1, ... for multiple inputs
```

## Default Port Layout

- If `inputs` is omitted: 1 input texture (`input`) + 1 output texture (`texture`)
- If `inputs` is specified: N named input textures + 1 output texture (`texture`)

## Hot Reload

Like all `WgslFilterBase` operators, data-driven operators check for shader file changes every 30 frames and automatically recompile. Failed recompilation keeps the previous pipeline active.

## When to Use

- **Data-driven WGSL** — when you want a pure-shader GPU filter with no C++ at all. Best for visual effects, color grading, generative patterns, and post-processing.
- **WgslFilterBase (C++)** — when you need C++ logic alongside the shader (custom state, file loading, compute buffers).
- **Full GpuProcessable (C++)** — when you need complete control over pipelines, bind groups, and multi-pass rendering.

## Source Reference

- Header parser: `src/runtime/gpu/wgsl_header_parser.h`
- Runtime wrapper: `src/operator_api/data_driven_filter.h`
