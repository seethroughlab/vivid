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

Export it by adding `VIVID_THUMBNAIL` after your struct definition:

```cpp
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
- topology:
  - `connected_input_mask` — bit `i` set when input port ordinal `i` has an upstream
    wire (resolved by the host from the compiled graph's edge list; covers the first
    32 input ports). This is the authoritative connectivity signal for thumbnails —
    the audio/frame contexts deliberately do not carry it, and a disconnected input
    still receives a zero-filled buffer, so buffer presence does **not** indicate a
    connection. Variable-input operators (e.g. `Mixer`) use this to draw exactly
    their connected channels.

Rules:

- render only into `thumbnail_texture_view`
- do not mutate the main graph output texture
- keep the pass deterministic and lightweight
- treat `draw_thumbnail()` as a fast draw path, not a first-use setup hook
- move heavyweight one-time CPU preparation into `prepare_instance_assets()` so async
  add/load compile work absorbs it before the node becomes visible
- use `main_thread_update()` for work that truly requires the live main thread; do not
  use it as the default place for initial heavy asset prep when `prepare_instance_assets()`
  can prewarm from synced graph values instead
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
struct MyOp : vivid::OperatorBase, vivid::FrameProcessable {
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

VIVID_THUMBNAIL(MyOp)
```

See [clock.h](/Users/jeff/Developer/vivid/operators/control/clock/clock.h) for a seed-operator example.

## Performance Guidance

- treat thumbnail rendering like UI-adjacent work, not a full effect pass
- assume slow thumbnail draws will be surfaced by runtime warnings; keep main-thread
  thumbnail work comfortably under a frame budget
- prefer one tiny fullscreen pass over complex multi-pass pipelines
- reuse thumbnail pipelines and buffers across frames
- rebuild only when the thumbnail format changes
- avoid per-frame resource allocation inside the hot path
- if a thumbnail needs expensive CPU-side asset generation first, do it in
  `prepare_instance_assets()` and let the UI keep showing the default thumbnail until
  the custom path is ready
