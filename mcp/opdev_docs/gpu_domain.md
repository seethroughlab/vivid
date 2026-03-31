# GPU Domain

GPU operators run on the main thread at frame rate (~60 Hz), rendering via WebGPU (Dawn backend).

## Two Approaches

### 1. Full GPU Operator (OperatorBase + GpuProcessable)

Complete control over pipeline, shaders, and bind groups. Use `gpu_common.h` helpers.

```cpp
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

struct MyGpuOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "MyGpuOp";
    static constexpr bool kTimeDependent = true;

    void process_gpu(const VividGpuContext* ctx) override;
};
```

### 2. WGSL Filter (WgslFilterBase) — Recommended for Most Filters

Write only the `@fragment fn fs_main(...)` in a `.wgsl` file. The base class auto-generates:
- Vertex shader (fullscreen triangle)
- Uniform buffer with `resolution`, `time`, `frame`, plus all your params by name
- Texture bindings for all input ports
- Pipeline, bind groups, hot-reload

```cpp
#include "operator_api/wgsl_filter.h"

struct MyFilter : vivid::WgslFilterBase {
    static constexpr const char* kName = "MyFilter";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> intensity{"intensity", 0.5f, 0.0f, 1.0f};

    MyFilter() : WgslFilterBase("my_filter.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&intensity);
    }
    // collect_ports() inherited: 1 input texture + 1 output texture
    // Override collect_ports() to add more texture inputs
};

VIVID_REGISTER(MyFilter)
```

The `.wgsl` file only needs the fragment shader:
```wgsl
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    return mix(color, vec4f(1.0), u.intensity);
}
```

## Auto-Generated WGSL Preamble (WgslFilterBase)

The preamble provides these bindings and types automatically:

```wgsl
// Math constants: PI, TAU, E, PHI, SQRT2

struct Uniforms {
    resolution: vec2f,
    time: f32,
    frame: u32,
    // ... all your Param<T> members by name ...
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;   // 1 input
// or: inputTex0, inputTex1, ... for multiple inputs

// VertexOutput with .position and .uv
// fullscreenTriangle() helper function
```

## WGSL Include System

WGSL files support `// @include "filename.wgsl"` directives for sharing code:
```wgsl
// @include "noise_functions.wgsl"

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let n = perlinNoise(input.uv * u.scale);
    return vec4f(n, n, n, 1.0);
}
```

Includes are resolved relative to the shader file's directory (with `lib/` fallback). Cycle detection prevents infinite loops.

## Hot Reload

WgslFilterBase checks shader file modification time every 30 frames. When a change is detected, it recompiles the shader and rebuilds the pipeline. Failed recompilation keeps the old pipeline active.

## VividGpuContext Fields

| Field | Type | Description |
|-------|------|-------------|
| `time` | `double` | Elapsed time |
| `delta_time` | `double` | Frame delta |
| `frame` | `uint64_t` | Frame counter |
| `param_values` | `float*` | Auto-synced param values |
| `input_values` / `output_values` | `float*` | Float ports |
| `device` | `WGPUDevice` | WebGPU device |
| `queue` | `WGPUQueue` | WebGPU queue |
| `command_encoder` | `WGPUCommandEncoder` | Current frame's encoder |
| `output_texture` | `WGPUTexture` | Output texture |
| `output_texture_view` | `WGPUTextureView` | Output texture view |
| `output_width` / `output_height` | `uint32_t` | Output dimensions |
| `output_format` | `WGPUTextureFormat` | Output pixel format |
| `input_texture_views` | `WGPUTextureView*` | Input textures (per port) |
| `input_texture_count` | `uint32_t` | Number of input textures |
| `input_textures` | `WGPUTexture*` | Raw texture handles |
| `input_texture_widths` / `input_texture_heights` | `uint32_t*` | Input dimensions |
| `operators_src_dir` | `const char*` | Path to operators/ source tree |
| `input_handles` / `output_handles` | `void**` | Handle ports |
| `input_lanes` / `output_lanes` | `VividLanePort*` | Spread ports |
| `input_string_values` / `output_string_values` | `const char**` | String ports |
| `input` | `VividInputState*` | Mouse/keyboard events |

## gpu_common.h Helpers

```cpp
namespace vivid::gpu {
    // Create shader from fragment source (prepends fullscreen vertex shader)
    WGPUShaderModule create_shader(device, frag_src, label);

    // Create fullscreen render pipeline
    WGPURenderPipeline create_pipeline(device, shader, layout, format, label);

    // Run a fullscreen render pass (clear + draw 3 vertices)
    void run_pass(encoder, pipeline, bind_group, target, label, clear_color);

    // Create a Uniform|CopyDst buffer
    WGPUBuffer create_uniform_buffer(device, size, label);

    // Create a ClampToEdge/Linear sampler
    WGPUSampler create_linear_sampler(device, label);

    // RAII release helpers
    void release(WGPURenderPipeline&);  // also for Buffer, Shader, BindGroup, etc.

    // RAII handle types
    using PipelineHandle   = GpuHandle<WGPURenderPipeline, ...>;
    using ShaderHandle     = GpuHandle<WGPUShaderModule, ...>;
    using BufferHandle     = GpuHandle<WGPUBuffer, ...>;
    using BindGroupHandle  = GpuHandle<WGPUBindGroup, ...>;
    using BindLayoutHandle = GpuHandle<WGPUBindGroupLayout, ...>;
    using SamplerHandle    = GpuHandle<WGPUSampler, ...>;
    using TextureHandle    = GpuHandle<WGPUTexture, ...>;
    using TexViewHandle    = GpuHandle<WGPUTextureView, ...>;
}
```

## Requesting Output Size

```cpp
vivid_request_output_size(ctx, desired_width, desired_height);
```

This sets write-back fields; the runtime reallocates the texture on the next frame.
