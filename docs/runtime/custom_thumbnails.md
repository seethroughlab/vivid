# Custom Thumbnails

Vivid node thumbnails are GPU-native by default:

- GPU nodes show their live output texture via the thumbnail cache
- non-GPU nodes fall back to the built-in sparkline / waveform body rendering

Use a **custom GPU thumbnail override** when an operator needs a more legible or more domain-specific
preview than the default node body provides.

## When To Use This

Use `draw_thumbnail(...)` when:

- the default preview is technically correct but not very informative
- the operator is control or audio domain and you want a richer visual summary
- a compact thumbnail can explain the operator state better than raw values

Do not use it when:

- the operator is already well-represented by its normal GPU output texture
- a custom thumbnail would just duplicate the main node output without adding clarity

## Authoring Contract

Implement the optional method on your operator:

```cpp
void draw_thumbnail(const VividThumbnailContext* ctx) override;
```

Export it alongside `VIVID_REGISTER(...)`:

```cpp
VIVID_REGISTER(MyOperator)
VIVID_THUMBNAIL(MyOperator)
```

If your operator uses the GPU thumbnail hook, it must have access to WebGPU headers and symbols.
In the main repo that means adding `EXTRA_LIBS webgpu` to the operator target in `CMakeLists.txt`.
Package operators should do the equivalent in their package build.

## `VividThumbnailContext`

The thumbnail hook renders into a runtime-owned thumbnail target. Important fields:

- timing/state:
  - `time`
  - `delta_time`
  - `frame`
- operator values:
  - `param_values`
  - `output_values`
  - `string_param_values`
  - `file_param_values`
- GPU resources:
  - `device`
  - `queue`
  - `command_encoder`
  - `thumbnail_texture`
  - `thumbnail_texture_view`
  - `thumbnail_width`
  - `thumbnail_height`
  - `thumbnail_format`
- optional source texture info:
  - `source_output_texture`
  - `source_output_texture_view`
  - `source_output_width`
  - `source_output_height`
  - `input_texture_views`

Rules:

- render only into `thumbnail_texture_view`
- do not mutate the main graph output texture
- keep the pass deterministic and lightweight
- report initialization/render errors with `vivid_report_thumbnail_error(ctx, "...")`
- fail closed: if shader/pipeline/bind resources are not valid, report the error and let the UI fall back to the default thumbnail instead of attempting to encode a partial pass
- thumbnail WGSL must follow current WGSL identifier rules; avoid reserved identifiers in struct fields and bindings
- the runtime may suppress custom thumbnail GPU work entirely when the current graph contains unresolved / missing-operator placeholders; that unresolved-graph path should degrade to default node bodies instead of continuing thumbnail rendering on partial graph state

## Helper API

`src/operator_api/thumbnail.h` provides a small helper layer:

- `vivid::thumbnail::create_shader(...)`
- `create_uniform_buffer(...)`
- `create_uniform_bind_layout(...)`
- `create_uniform_bind_group(...)`
- `create_pipeline_layout(...)`
- `create_pipeline(...)`
- `run_pass(...)`

These are intentionally small wrappers over the existing WebGPU helper style. The goal is to make
thumbnail-specific rendering easy without creating a second rendering framework.

## Example

Minimal pattern:

```cpp
struct MyOp : vivid::ControlOperatorBase {
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bind_group_ = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer uniform_buf_ = nullptr;
    WGPUShaderModule shader_ = nullptr;
    WGPUPipelineLayout pipe_layout_ = nullptr;
    WGPUTextureFormat pipeline_format_ = WGPUTextureFormat_Undefined;

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!pipeline_ || pipeline_format_ != ctx->thumbnail_format) {
            // (re)build pipeline for the current thumbnail format
        }

        if (!pipeline_ || !bind_group_ || !uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "my_op thumbnail pipeline init failed");
            return;
        }

        // update uniforms
        // submit one fullscreen pass into ctx->thumbnail_texture_view
        vivid::thumbnail::run_pass(ctx, pipeline_, bind_group_, "My Thumb Pass");
    }
};

VIVID_REGISTER(MyOp)
VIVID_THUMBNAIL(MyOp)
```

See [clock.h](/Users/jeff/Developer/vivid/operators/control/clock/clock.h) for a seed-operator example.

## Performance Guidance

- treat thumbnail rendering like UI-adjacent work, not a full effect pass
- prefer one tiny fullscreen pass over complex multi-pass pipelines
- reuse thumbnail pipelines and buffers across frames
- rebuild only when the thumbnail format changes
- avoid per-frame resource allocation inside the hot path
