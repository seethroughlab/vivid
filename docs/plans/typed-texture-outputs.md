# Plan — First-class typed texture outputs

*(Formerly "runtime aux-output-texture allocation." Full implementation plan.)*

## Context

A GPU operator gets exactly one runtime-allocated output texture, so Motion's **Flow** mode channel-packs the optical-flow field into one texture (`operators/gpu/motion/motion.cpp:75` — `RG` = flow encoded `flow*0.5+0.5`, `B` = magnitude), and Particles2D's `emit_flow` re-decodes it. This is lossy and conflates an image and a vector field on one wire, forcing every consumer to know the packing convention.

The correct outcome: **operators declare N texture output ports, each with its own format, all runtime-allocated and routed.** Motion emits a dedicated `flow_vector` output (signed `RG16Float`) while its primary stays a clean magnitude texture; Particles2D reads the flow directly. We are free to break ABI and drop backwards-compat.

### What's already built (verified)
The auxiliary-output path is scaffolded but **never exercised** (no existing operator declares two texture outputs):
- Context fields `aux_output_texture_views` / `aux_output_texture_count` (`src/operator_api/gpu_operator.h:43–45`).
- Compiler catalogs 2nd+ texture output ports → `GpuNodeState::aux_texture_output_port_indices / aux_gpu_textures / aux_gpu_texture_views` (`src/runtime/graph/graph_compiler_init.cpp:212–246`; storage `src/runtime/graph/compiled_graph.h:336–338`).
- Frame executor passes aux to operators (`frame_executor.cpp:492–495`) and routes downstream texture inputs / GPU-sink reads that connect to an aux port (`frame_executor.cpp:452–460`, `986–1004`).

### What's missing
1. `VividPortDescriptor` has **no format field** (`src/operator_api/types.h:126–151`); all node textures use the single offscreen format (`kOffscreenFormat = WGPUTextureFormat_RGBA16Float`, `src/runtime/core/main.cpp:1225`, threaded through `RuntimeCore::allocate_gpu_textures` → `FrameExecutor`).
2. `FrameExecutor::allocate_gpu_textures()` creates only the primary and **nulls** the aux arrays (`frame_executor.cpp:865–866`).
3. `create_pipeline` / `run_pass` are **single-color-target only** (`src/operator_api/gpu_common.h:104,140`).

### Key finding that bounds the work
`operator_codegen` emits each `collect_ports()` push_back expression **verbatim** (`tools/operator_codegen/descriptor_builder.cpp:1331–1333` → `1525–1530`); `normalize_port_expr` (`667–673`) only prepends `VividPortDescriptor` to a brace literal. **So codegen needs no changes** — a new descriptor field flows through automatically. The only constraint: C++ forbids mixing positional and designated initializers, so any port that sets the format must use a fully-designated initializer.

## Design decisions
- **Vivid-owned format enum**, not raw `WGPUTextureFormat` (which isn't in the C ABI / not included by `types.h`). Runtime maps it to `WGPUTextureFormat`.
- **Keep primary + typed-aux** (port 0 = the displayable output that drives `video_out`, thumbnails, bypass-override, sink). A real semantic distinction, not a flat output array.
- **MRT, not a second pass** — Motion computes magnitude and flow in one fragment shader; a second pass would recompute gradients.
- **No back-compat**: bump ABI to v4, rebuild all operators, delete the packed paths in Motion and Particles2D.

---

## Step 1 — Operator API: format enum + descriptor field + ABI bump
File: `src/operator_api/types.h`
- Add a format enum (near the port enums ~62–84):
  ```c
  typedef uint32_t VividTextureFormat;
  #define VIVID_TEXFMT_DEFAULT     0u  // inherit the node's primary/offscreen format
  #define VIVID_TEXFMT_RGBA8_UNORM 1u
  #define VIVID_TEXFMT_RGBA16F     2u
  #define VIVID_TEXFMT_RG16F       3u
  #define VIVID_TEXFMT_RG32F       4u
  #define VIVID_TEXFMT_R16F        5u
  #define VIVID_TEXFMT_R32F        6u
  ```
- Append to `VividPortDescriptor` (after `repeat_group_idx`, line 150), with a default so existing short-form ports are unaffected:
  ```c
  VividTextureFormat gpu_texture_format = 0; // VIVID_TEXFMT_DEFAULT; only meaningful for TEXTURE outputs
  ```
- Bump `VIVID_OPERATOR_ABI_VERSION` 3u → 4u (`types.h:11`), updating the comment. The loader then rejects all stale v3 dylibs → full operator rebuild (expected).

**Codegen:** no change. Confirm by inspection that `descriptor_builder.cpp` still emits Motion's new fully-designated port literal verbatim. (Existing operators' generated port arrays are unchanged — they never mention the new field.)

## Step 2 — Compiled graph: store the per-aux format hint
File: `src/runtime/graph/compiled_graph.h` (`GpuNodeState`, ~336–338)
- Add, parallel to the aux vectors:
  ```cpp
  std::vector<VividTextureFormat> aux_texture_format_hints; // raw enum; resolved at allocation
  ```
  Store the enum (not a resolved `WGPUTextureFormat`) because `VIVID_TEXFMT_DEFAULT` resolves against the offscreen format known only at allocate time.

File: `src/runtime/graph/graph_compiler_init.cpp` (~212–246)
- In the aux-port cataloging loop, also push `desc->ports[i].gpu_texture_format` into `aux_texture_format_hints` (kept index-aligned with `aux_texture_output_port_indices`).

## Step 3 — Frame executor: resolve format + allocate aux + retire correctly
File: `src/runtime/graph/frame_executor.cpp`
- Add a file-local mapper: `WGPUTextureFormat resolve_texfmt(VividTextureFormat f, WGPUTextureFormat default_fmt)` → `DEFAULT`→`default_fmt`, else the matching WGPU format.
- In `allocate_gpu_textures()` (845–947):
  - Replace the bare null-out of aux arrays (865–866) with **deferred-release retirement** into the existing `DeferredGpuRelease batch` (mirror the primary at 862–864): push each non-null aux view/texture into `batch.views/textures`, then null. (`DeferredGpuRelease` + `drain_deferred_gpu_releases`, 3-frame grace, already exist.)
  - After the primary texture+view are created (after 939), size the aux vectors to `aux_texture_output_port_indices.size()` and loop, creating one texture+view per aux port using `resolve_texfmt(aux_texture_format_hints[i], format)`, the same `w/h`, and the same usage flags as the primary (`RenderAttachment | TextureBinding | CopySrc | CopyDst | extra_usage`). `CopySrc` is required for Motion's history copy. Reuse the descriptor pattern at 907–939; label `"Node Aux Texture [<id>#<n>]"`. On failure, null that slot and continue (mirror primary error handling).
- No change to context population (492–495) — it already forwards the now-populated aux views. (Operators know their own declared format, so we do **not** add aux formats to `VividGpuContext`.)

## Step 4 — GPU helpers: MRT variants
File: `src/operator_api/gpu_common.h`
- Add:
  ```cpp
  inline WGPURenderPipeline create_pipeline_mrt(WGPUDevice, WGPUShaderModule, WGPUPipelineLayout,
      const WGPUTextureFormat* formats, uint32_t count, const char* label);
  inline void run_pass_mrt(WGPUCommandEncoder, WGPURenderPipeline, WGPUBindGroup,
      const WGPUTextureView* targets, uint32_t count, const WGPUColor* clears, const char* label);
  ```
  `create_pipeline_mrt` builds `count` `WGPUColorTargetState` (one per format); `run_pass_mrt` builds `count` `WGPURenderPassColorAttachment` (each `LoadOp_Clear`/`StoreOp_Store`, per-target clear). All attachments share size and `sampleCount=1`; differing formats per attachment is valid.
- Refactor the existing `create_pipeline`/`run_pass` (93–149) to delegate to the MRT variants with `count=1`, preserving their signatures so the ~40 other GPU operators are untouched.

## Step 5 — Motion: real dual output via MRT
File: `operators/gpu/motion/motion.cpp`
- **Ports** (`collect_ports`, 134–137): keep `{"input", TEXTURE, INPUT}` and `{"texture", TEXTURE, OUTPUT}` (primary, default format = magnitude); add the flow output with a **fully-designated initializer** (required to set the format):
  ```cpp
  out.push_back({.name="flow_vector", .type=VIVID_PORT_TEXTURE,
                 .direction=VIVID_PORT_OUTPUT, .gpu_texture_format=VIVID_TEXFMT_RG16F});
  ```
- **Shader**: `fs_main` returns a struct with two attachments:
  ```wgsl
  struct FragOut { @location(0) magnitude: vec4f, @location(1) flow: vec2f }
  ```
  Attachment 0 = `vec4(m,m,m,1)`; attachment 1 = raw signed flow (`vec2`, no `*0.5+0.5`). In Magnitude mode write `flow=vec2(0)`. Single MRT pipeline for both modes (flow target just gets zeros in Magnitude mode).
- **History split** (was the packed `accum_tex_`): keep `prev_tex_` (previous input frame, RGBA16F). Replace `accum_tex_` with `accum_mag_` (RGBA16F, prev magnitude) and add `accum_flow_` (RG16F, prev flow). Shader reads `accum_mag_.b` (or `.r`) for magnitude decay and `accum_flow_.rg` (raw, no decode) for the flow smear. Add `accum_flow_` as bind-group binding 5; update `recreate_history`, `rebuild_bind_group`, and the bind-group-layout entry count.
- **Pipeline** (`lazy_init`, 323): `WGPUTextureFormat fmts[2] = { gpu->output_format, WGPUTextureFormat_RG16Float }; create_pipeline_mrt(..., fmts, 2, ...)`.
- **Pass** (175–176): `WGPUTextureView targets[2] = { ctx->output_texture_view, ctx->aux_output_texture_views[0] }; WGPUColor clears[2] = {black, black}; run_pass_mrt(ctx->command_encoder, pipeline_, bind_group_, targets, 2, clears, "Motion Pass")`. Guard: if `ctx->aux_output_texture_count < 1` log once and fall back to single-target (defensive; should never happen once allocation lands).
- **Copies** (178–193): after the pass, `primary → accum_mag_`, `aux[0] → accum_flow_`, `input → prev_tex_`.
- **Delete** the packed encoding (the `*0.5+0.5`, `a.rg`/`a.b` smear at shader 55/71–75).

## Step 6 — Particles2D: consume the dedicated flow port
File: `operators/gpu/particles_2d/particles_2d.cpp`
- **Input port**: add `{"flow_vector", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT}` alongside the existing `texture`/`emit_mask` inputs (~918).
- **Compute bindings**: add a binding for `flow_tex: texture_2d<f32>` + matching bind-group-layout entry (~121, ~1183–1211).
- **Decode**: in `emit_flow` (~307–352) and force-mode-3 steering (~395–405), read `textureLoad(flow_tex, …).rg` as the **raw signed** vector (remove the `*2-1` decode). Use `emit_mask` (wired from Motion's primary magnitude) for the spawn-probability gate as today; magnitude no longer comes from the flow texture's `.b`.
- **Delete** the packed-decode path.

## Step 7 — Demo graphs
Rewire the Motion→Particles2D demo graphs: `motion/texture` → `particles/emit_mask` (magnitude gate) and `motion/flow_vector` → `particles/flow_vector` (direction). Build the graphs as JSON and load via atomic `load_graph` (live topology edits on large running graphs are unstable — known issue).

---

## Downstream safety (verified, no change needed)
- **Thumbnails** render the node's *primary* texture (`thumbnail_cache` is RGBA16F); the `RG16F` aux is a separate port → thumbnails unaffected.
- **GPU sink / capture** sample `texture_2d<f32>`; `RG16F` samples fine (`.b/.a`→0/1). Wiring flow into `video_out` would just look red/green, not crash. Capture targets the primary/sink.
- `RG16Float` is a renderable, filterable color format in Dawn → valid as an MRT attachment and as a sampled input.

## Files
`src/operator_api/types.h` (+enum, +field, ABI v4), `src/operator_api/gpu_common.h` (MRT), `src/runtime/graph/{compiled_graph.h, graph_compiler_init.cpp, frame_executor.cpp}`, `operators/gpu/motion/motion.cpp`, `operators/gpu/particles_2d/particles_2d.cpp`, demo `graphs/*.json`. **No `operator_codegen` change.**

## Rollout
ABI bump v3→v4 invalidates all existing dylibs. Do a full operator rebuild (`vivid build` / clean operator dylibs) so the loader doesn't reject stale operators.

## Verification
1. Build core + all operators (`run_in_background: true`); confirm no loader ABI-mismatch rejections.
2. `inspect_node` Motion → three ports; `flow_vector` output present. `inspect_graph` on the demo → both wires resolve, no null-view warnings in stderr; aux-texture allocation log lines appear.
3. `capture_frame_strip` over a moving source:
   - Particle advection direction matches motion (no regression vs the packed version).
   - Motion's primary output reads as clean grayscale (no RG tint).
   - Finer directional fidelity than 8-bit packing — slow diagonal motion advects smoothly without stair-stepping.
4. Recompile the graph live (toggle a param forcing realloc) several times → no deferred-release asserts, no use-after-free (the aux retirement path).
5. Toggle Motion `mode` Flow→Magnitude→Flow → flow target zeros out and recovers; no crash.
6. Regression: load a few unrelated single-output GPU graphs (noise/blur/composite) → render identically (single-target `create_pipeline`/`run_pass` delegation intact).
