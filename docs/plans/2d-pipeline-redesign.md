# 2D Pipeline Redesign (Instancing Phase E)

**Status:** Planned (not yet started). Approved 2026-04-19.
**Predecessor work:** Phases A–B of the instancing initiative complete; Phase C subsumed into E.5 here.

## Why this plan exists

Vivid's current 2D GPU pipeline is texture-chain: every operator inputs and outputs `VIVID_PORT_TEXTURE`, and each operator renders to its own texture via a fullscreen-triangle fragment shader (`run_pass()`). This model can't scale to TD-grade sprite instancing (10K+ sprites), doesn't support cross-operator batching, and has no depth-sort affordance. `Composite` (max 16 manually-wired layers, no batching) is the only multi-input compositor.

Meanwhile, 3D already ships a split pipeline: `VividSceneFragment` (a tagged-union drawable record) flows through emitter → modifier → `Render3D` (terminal rasterizer). Phase E ports that architecture to 2D.

**Why now.** Vivid is pre-alpha. Migration cost of a 2D pipeline change is as low as it will ever be. Waiting until there's a user base amplifies cost. The texture-chain model fundamentally can't scale to TD-grade sprite instancing or cross-operator batching without this split. Half-measures (compute-particles alone) would deliver one capability but not unlock the broader architectural win.

**Scope discipline.** Existing texture-chain operators keep working. Five ops (Bloom, Feedback, TimeMachine, MovieFile, LutApply) can't naturally migrate — their state *is* a texture; they remain post-processors. The drawable pipeline terminates at `Render2D` which outputs a standard texture; texture-chain continues from there. Users can mix both worlds in one graph.

## Where this sits in the broader instancing initiative

| Phase | Status | What it delivered |
|-------|--------|-------------------|
| **A** — stop the bleeding | Complete 2026-04-19 | Restored `SpreadNoise` in vivid core; fixed `Instancer3D` lane_behavior (`VIVID_LANE_KERNEL`); retuned and regression-guarded `3d_instancer_demo.json`. Verification surfaced that the regression guard is inert at the ctest level and only fires via GPU-enabled CI. |
| **B** — unify 3D instancing API | Complete 2026-04-19. Shim retired 2026-04-19. | Introduced `InstanceArray3D` custom-ref port type in vivid-3d. Added `InstanceGrid`, `InstanceNoise`, `InstancesFromLanes` operators. `Instancer3D` gained a unified `instances` input. Renamed `InstancedRender` → `MeshDraw`. Canonical recipe is `Shape3D → Instancer3D ← InstanceGrid`. **Clean-break follow-up:** deleted the 7 deprecated lane-array ports + `count`/`layout`/`spacing`/`palette` params from `Instancer3D` (~180 LOC). Dropped `"Instanced Shapes" → "ShapeField"` runtime alias. |
| **C** — 2D parity (original plan) | **Subsumed into E.5 — Complete 2026-04-19** | Delivered via E.5 on the new drawable pipeline: `InstanceNoise2D`, `InstancesFromLanes2D`, shared `instance_algorithms.h`, `InstancedShapes → ShapeField` rename + JSON alias. See `docs/plans/2d-pipeline-e5-parity.md`. |
| **D** — UX polish | Deferred | "Make many…" node shortcut, lane-count wire badges, operator browser "Instancing" category. Ships after E. |
| **E** — 2D pipeline redesign | **This document. E.1–E.6 Complete 2026-04-19.** | Full split-pipeline redesign for 2D. All sub-phases shipped. |

Full context and completion memory lives at `~/.claude/projects/-Users-jeff-Developer-vivid/memory/project_instancing_ux.md`.

## Architecture review corrections

This plan reflects corrections from a critical architecture review. Notable ones:

1. **Simulators stay in one operator, not split.** `Particles3D` does compute-sim + fragment-emission in the same `process_gpu`. `Particles2D` mirrors that. No "sim feeds emitter" split.
2. **`VividDrawable2D` designed with ABI-stability reserved fields up front.** `VividSceneFragment` grew by accretion over 6 phases; 2D avoids that by reserving padding slots per sub-block now.
3. **Per-drawable `DrawIndexed` in Render2D, not batched mega-draw.** Sort by pipeline+bindgroup to minimize state switches. Do not re-introduce an uber-shader.
4. **Per-draw data via storage buffer indexed by `@builtin(instance_index)` or a per-draw u32, not dynamic UBO offsets.** `Render3D` uses UBO offsets because of shadow-pass alignment constraints; 2D has no such constraint and should use the cleaner pattern.
5. **`mat3x2` for 2D transforms**, not separate `position[2]/rotation/scale[2]` fields. `Transform2D` composition demands proper affine.
6. **`pipeline_flags` inferred by Render2D from drawable state**, not a field on `VividDrawable2D`. Decouples renderer pipeline-cache keys from the data type.
7. **`z_layer` as NaN-default stable-sort key on the drawable** (not per-instance). Depth test OFF. Alpha blending only. Traversal order is the default; z_layer overrides when set.
8. **Only Particles/Flocking migrate to drawable-emitters.** Fluid / ReactionDiffusion / CellularAutomata stay texture-chain (their state *is* a texture).
9. **TextEmitter with glyph atlas is a 2–3 week sub-project on its own**, not a bullet in a larger phase.
10. **Honest timeline is 14–17 weeks**, not 8–12. `Render2D` alone is ~1500–2000 LOC mirroring `Render3D`'s complexity.

## Design — `VividDrawable2D`

**File:** new `src/operator_api/gpu_2d.h` (core-owned; mirrors `gpu_3d.h`'s convention in vivid-3d).

```cpp
struct InstanceData2D {
    float transform[6];   // 2D affine: [a, b, c, d, tx, ty] (row-major mat3x2)
    float _pad_xform[2];  // align to 16 bytes
    float color[4];       // RGBA multiplier
};
static_assert(sizeof(InstanceData2D) == 48, "InstanceData2D is 48 bytes");

enum VividDrawable2DType : uint32_t {
    VIVID_DRAWABLE2D_SPRITE = 0,  // textured quad
    VIVID_DRAWABLE2D_SHAPE  = 1,  // SDF primitive
    VIVID_DRAWABLE2D_TEXT   = 2,  // glyph run
    VIVID_DRAWABLE2D_MESH   = 3,  // arbitrary 2D geometry (future)
    VIVID_DRAWABLE2D_CUSTOM = 4,  // opaque custom shader path
};

enum VividBlendMode : uint32_t {
    VIVID_BLEND_ALPHA    = 0,  // src-over (default)
    VIVID_BLEND_ADDITIVE = 1,
    VIVID_BLEND_MULTIPLY = 2,
    VIVID_BLEND_SCREEN   = 3,
    VIVID_BLEND_OVERLAY  = 4,
};

struct VividDrawable2D {
    // === Type dispatch ===
    VividDrawable2DType type;
    VividBlendMode      blend_mode;
    float               z_layer;       // NaN = use traversal order (default)
    float               _pad_header;
    uint32_t            _reserved_header[4];  // ABI padding

    // === Single-instance fallback transform ===
    // When instance_count is 0 or 1 and instance_buffer is null, this is the
    // transform. When instances are present, this is ignored.
    float    transform[6];      // mat3x2 affine
    float    color[4];          // RGBA multiplier
    uint32_t _reserved_xform[4];

    // === Sprite payload ===
    WGPUTextureView texture_view;
    WGPUSampler     texture_sampler;
    float           uv_rect[4];          // u0, v0, u1, v1 (atlas sub-region)
    uint32_t        _reserved_sprite[4];

    // === Shape payload (SDF primitives) ===
    uint32_t shape_sides;                // 0=circle, N=polygon/star
    float    shape_star_factor;          // 0=polygon, >0=star
    float    shape_softness;
    float    _pad_shape;
    uint32_t _reserved_shape[4];

    // === Text payload (filled in by TextEmitter) ===
    WGPUTextureView text_atlas_view;
    WGPUBuffer      text_glyph_buffer;   // per-glyph: atlas uv + local offset
    uint32_t        text_glyph_count;
    uint32_t        _reserved_text[5];

    // === Custom pipeline override ===
    // If non-null, Render2D uses this pipeline instead of its cached variant.
    WGPURenderPipeline custom_pipeline;
    WGPUBindGroup      custom_material_binds;
    uint32_t           _reserved_custom[6];

    // === Instancing ===
    // If instance_buffer is non-null, Render2D issues Draw*(..., instance_count).
    // Instance data is an array of InstanceData2D records indexed by
    // @builtin(instance_index) in the vertex shader.
    WGPUBuffer instance_buffer;
    uint32_t   instance_count;
    uint32_t   _reserved_inst[6];

    // === Composition ===
    // DrawableMerge and emitters that produce subtrees populate these.
    VividDrawable2D** children;
    uint32_t          child_count;
    uint32_t          _reserved_composition[3];
};

VIVID_DECLARE_CUSTOM_REF_TYPE(VividDrawable2D,
                              "seethroughlab.vivid.drawable_2d_v1",
                              "VividDrawable2D",
                              false);
```

**Pad discipline.** Every sub-block ends with a `_reserved_*[N]` array sized to keep the total block 16-byte-aligned and leave 2–6 `uint32_t` slots of growth room. Pin the total size with a `static_assert` at the bottom. When a future phase needs a new field in a block, it replaces reserved slots — no ABI break.

**What's deliberately absent.** No `pipeline_flags` bitfield (Render2D infers from state). No `cpu_vertices` / `cpu_indices` (CPU-side access, if ever needed, through a separate side-channel buffer pointed at by a payload field, not first-class). No lights / environment / shadow baggage. No custom camera (2D has one implicit orthographic viewport; if that changes, add via reserved slots).

## Sub-phases

### E.1 — Foundation (4–5 weeks)

Deliverable: a minimal drawable pipeline end-to-end, rendering alongside texture-chain.

**Files:**
- `src/operator_api/gpu_2d.h` (new) — type definitions above.
- `src/operator_api/gpu_common.h` — add `begin_2d_pass(encoder, view, clear)` helper (mirrors `begin_3d_pass`).
- `operators/gpu/render_2d/render_2d.cpp` (new) — terminal rasterizer.
- `operators/gpu/sprite_emitter/sprite_emitter.cpp` (new) — texture input → drawable.
- `operators/gpu/shape_emitter/shape_emitter.cpp` (new) — SDF primitive params → drawable.
- `operators/gpu/drawable_merge/drawable_merge.cpp` (new) — N drawables → merged drawable via children array (mirrors `SceneMerge`, ~20 LOC).
- `operators/gpu/instancer_2d/instancer_2d.cpp` (new) — takes a drawable template + `InstanceArray2D` bundle → drawable with `instance_buffer` set.
- `cmake/operators.cmake` — register the six new ops.

**`InstanceArray2D` custom-ref port type** (also added to `gpu_2d.h`):
```cpp
struct InstanceArray2D {
    const InstanceData2D* data;
    uint32_t              count;
    uint32_t              _pad0;
};
VIVID_DECLARE_CUSTOM_REF_TYPE(InstanceArray2D, "seethroughlab.vivid.instance_array_2d_v1", ...);
```

**Render2D scope for E.1:**
- Pipeline variant cache keyed on `(type, has_texture, has_instances, blend_mode)`. Compile on demand, cache in operator state. Expect ~16 variants by end of E.1, ~80 by end of Phase E — size the cache accordingly.
- Per-drawable `DrawIndexed(6, instance_count, ...)` for quad-based drawables (sprites/shapes). Shape-SDF uses a fragment shader that evaluates the SDF within a bounded quad.
- Sort collected drawables by pipeline-variant first, then by bind group, to minimize state changes.
- Bind group 0: frame-level uniforms (viewport size, time). Bind group 1: per-drawable data (texture, sampler, instance buffer).
- Uses raw `wgpuCommandEncoderBeginRenderPass` — NOT `run_pass()`. One render pass, multiple `DrawIndexed` calls.
- Output to `ctx->output_texture_view` at the runtime-provided resolution.

**Verification for E.1:**
- Build: new operators compile, dylibs produced, 6 new operators registered.
- Canonical demo graph: `SpriteEmitter ← TextureLoader → Instancer2D ← InstanceGrid2D → Render2D → video_out`. Renders N textured sprites in a grid.
- Shape demo: `ShapeEmitter → Instancer2D ← InstanceGrid2D → Render2D → video_out`. Renders N SDF circles.
- Coexistence: load an existing texture-chain demo (`bloom_demo.json`), confirm it still runs unchanged.
- Interleaved demo: `SpriteEmitter → Render2D → Bloom → video_out`. Verify drawable output feeds Bloom correctly.

### E.2 — Core modifiers + depth sort + blend modes (2 weeks)

**New operators:**
- `operators/gpu/transform_2d/transform_2d.cpp` — mat3x2 affine applied to drawable (single or instanced). For non-instanced drawables, composes into the drawable's own transform. For instanced, wraps the drawable's instance buffer with a compute pass (or CPU pre-multiply, depending on size).
- `operators/gpu/drawable_filter/drawable_filter.cpp` — pass-through modifier with filter predicate on children (e.g., z_layer range, tag). Lower priority; optional in E.2.

**Render2D additions:**
- Stable-sort drawables by `z_layer` after collection; NaN keeps traversal order.
- Blend mode state switching between draws (pipeline variant axis added). Alpha / Additive / Multiply / Screen / Overlay.
- Depth test remains OFF throughout. No depth buffer allocated.

**Verification:** compose `SpriteEmitter → Transform2D → Render2D`; visually verify transform is applied. Load a graph with two SpriteEmitters at different z_layers, verify draw order overrides traversal.

### E.3 — Simulator migration: Particles2D, Flocking2D (1.5–2 weeks)

Each is a single operator that does compute sim + drawable emission, exactly mirroring `Particles3D`.

**Files:**
- `operators/gpu/particles_2d/particles_2d.cpp` (new) — compute shader maintains per-particle state (position, velocity, age, lifetime) via ping-pong storage buffers; writes `InstanceData2D` records to an internal instance buffer; emits one `VividDrawable2D` (SPRITE or SHAPE, based on param) with that buffer attached.
- `operators/gpu/flocking_2d/flocking_2d.cpp` (new) — Reynolds boids compute shader, same emission pattern.

**Critical non-migrations:**
- `Fluid`, `ReactionDiffusion`, `CellularAutomata` stay as-is. Their state is a texture. No drawable version exists or needs to.
- `Trails` stays as-is. Its state is a ring-buffer texture.
- Existing 2D `Particles` and `Flocking` remain for backward compat. Optional deprecation later.

**Verification:** `Particles2D → Render2D → Bloom → video_out` renders 10K+ animated particles at 60fps.

### E.4 — Text rendering (3 weeks, own sub-project)

This is a standalone sub-project per the architecture review.

**Files:**
- `operators/gpu/text_emitter/text_emitter.cpp` (new)
- `operators/shared/glyph_atlas/` (new, maybe — if atlas management is reusable)

**Design:**
- Maintains a glyph atlas texture per (font, size) combination. Rebuilds on font/size change.
- Emits one `VividDrawable2D` per string of type `VIVID_DRAWABLE2D_TEXT`, with `text_atlas_view` + `text_glyph_buffer` populated. The glyph buffer is per-glyph records (atlas UV + local offset).
- Render2D's text pipeline variant: vertex shader reads glyph index from `@builtin(vertex_index) / 6`, looks up atlas UV + offset from storage buffer, emits a quad; fragment shader samples atlas.
- Existing `Text` operator (507 LOC, SDF-baked per-character rendering) stays for backward compat.

**Verification:** render long text strings with instancing; confirm atlas packing works across fonts/sizes.

### E.5 — Phase C subsumed (1 week)

The generator/modifier/adapter family that was planned as Phase C, now natively on the new pipeline.

**Files:**
- `operators/gpu/instance_grid_2d/instance_grid_2d.cpp` (new)
- `operators/gpu/instance_noise_2d/instance_noise_2d.cpp` (new)
- `operators/gpu/instances_from_lanes_2d/instances_from_lanes_2d.cpp` (new)

All three emit `InstanceArray2D` bundles (same pattern as Phase B's 3D versions, minus the Y axis — 2D uses mat3x2 transforms internally). They feed directly into `Instancer2D`'s `instances` input.

Also in E.5:
- Rename `InstancedShapes` → `ShapeField` (decouples the preset SDF-field effect from the "generic 2D instancer" name). Update `tests/ops/test_instanced_shapes_lanes.cpp` stale assertion (5 ports vs actual 7). Migrate `graphs/gpu/instanced_shapes_simple.json`.
- **Extract `src/operator_api/instance_algorithms.h`** — header-only shared math (PCG hash, value-noise, grid/circle/line layout formulas) consumed by both the new 2D generators (this phase) and Phase B's 3D generators (opportunistic retrofit). See the "Relationship to 3D instancing" section above for rationale.

### E.6 — Integration, docs, discoverability (2 weeks)

**Demo graphs:**
- `graphs/gpu/render_2d_sprites_demo.json` — TextureLoader → SpriteEmitter → Instancer2D ← InstanceGrid2D → Render2D → video_out
- `graphs/gpu/render_2d_particles_demo.json` — Particles2D at 10K count → Render2D → Bloom → video_out
- `graphs/gpu/render_2d_from_lanes_demo.json` — SpreadNoise × N → InstancesFromLanes2D → Instancer2D → Render2D → video_out
- `graphs/gpu/render_2d_text_demo.json` — TextEmitter → Instancer2D ← InstanceGrid2D → Render2D → video_out
- Plus: update intro / showcase graphs to use the new pipeline where appropriate.

**Docs:**
- `docs/runtime/2d_pipeline.md` (new) — drawable model, emitter/modifier/renderer roles, texture-chain coexistence.
- Operator browser category — new "2D Drawables" group next to the existing "GPU" group.
- User-facing "Render2D vs Composite" decision doc — which to use when.

**UI discoverability (can defer to Phase D if time-boxed):**
- "Make many…" affordance on sprite-producing nodes auto-wires `SpriteEmitter → Instancer2D ← InstanceGrid2D`.

### E.7 — Buffer (2 weeks)

Reserved for unknowns: ABI surprises, WebGPU bind-group-layout edge cases, render-pass management bugs, texture-format mismatches with downstream ops, performance tuning on the pipeline variant cache, hot-reload fragility.

## Total timeline

**14–17 weeks of focused work.** Each sub-phase ships independently — if a real-world constraint forces an early stop, E.1 + E.2 alone deliver a coherent alpha-quality drawable pipeline that users can start exploring.

## Critical files

**New (vivid core):**
- `src/operator_api/gpu_2d.h` — type definitions (`VividDrawable2D`, `InstanceData2D`, `InstanceArray2D`, enums).
- Ten new operators (6 in E.1, 1 in E.2, 2 in E.3, 1 in E.4, 3 in E.5).
- Four new demo graphs (E.6).

**Modified (vivid core):**
- `src/operator_api/gpu_common.h` — add `begin_2d_pass()` helper.
- `cmake/operators.cmake` — register each new operator.
- `graphs/gpu/instanced_shapes_simple.json` — rename + type update (E.5).
- `tests/ops/test_instanced_shapes_lanes.cpp` — rename + fix stale 5-port assertion (E.5).
- `docs/runtime/` — new 2D pipeline doc.

**Reuse:**
- `VividSceneFragment` pattern at `vivid-3d/include/operator_api/gpu_3d.h:351–421` — direct template for `VividDrawable2D`'s flat-struct-with-enum design.
- `Render3D` dispatch loop at `vivid-3d/operators/gpu/render_3d/render_3d.cpp:1632–1712` — direct template for `Render2D`'s collect-sort-draw loop.
- `SceneMerge` at `vivid-3d/operators/gpu/scene_merge/scene_merge.cpp:29–54` — ~20-LOC template for `DrawableMerge`.
- `Particles3D` at `vivid-3d/operators/gpu/particles3d/particles3d.cpp` — direct template for `Particles2D`'s compute-sim + emit-drawable pattern in one operator.
- `VIVID_DECLARE_CUSTOM_REF_TYPE` / `VIVID_DESCRIBE_REF_TYPE` macros at `src/operator_api/type_id.h` + `port_type_registry.h` — custom port registration.
- Phase B's `InstanceGrid` / `InstanceNoise` / `InstancesFromLanes` (3D) — structural templates for their 2D siblings in E.5.

**Unchanged:**
- Texture-chain post-processors: `Bloom`, `Feedback`, `TimeMachine`, `MovieFile`, `LutApply`. These keep working. Render2D outputs a texture they can consume.
- Texture-state simulators: `Fluid`, `ReactionDiffusion`, `CellularAutomata`, `Trails`. Their state is a texture; drawable model doesn't fit.
- All of vivid-3d. Phase E is core-only.

## Verification

Each sub-phase has its own verification bullets above. Cumulative end-of-Phase-E verification:

1. **Build:** full `cmake --build build` produces all ten new operator dylibs.
2. **Coexistence regression:** every existing `graphs/gpu/*.json` loads and renders identically to pre-Phase-E baseline (compare `frame_hash` values via `inspect_node` on each terminal op).
3. **New pipeline end-to-end:** each of the four Phase-E demo graphs renders correctly, captured via MCP interface mode.
4. **Scale:** `render_2d_particles_demo.json` sustains 60fps at 10K+ particles (`runtime_status` check).
5. **ctest:** full suite green modulo the pre-existing failures documented in Phase A verification. `test_shape_field_lanes` passes (stale assertion fixed).
6. **ABI stability check:** write a tiny sanity test that constructs `VividDrawable2D`, verifies `sizeof` matches the pinned `static_assert`, round-trips through a shared-handle register/resolve cycle.
7. **Custom port discovery:** `operator_docs` resolves for each new operator; `inspect_graph` on a drawable-pipeline graph shows `custom_ref` inputs/outputs correctly typed.
8. **Pipeline variant cache correctness:** compose a graph that exercises all 16+ pipeline variants in one frame; confirm Render2D's cache handles them without leaks or miscompile.

## Scope bound (what Phase E is NOT)

- **Not a UI overhaul.** "Make many…" shortcut, lane-count wire badges, operator-browser "Instancing" category — those are Phase D. Phase E ships the plumbing; Phase D makes it discoverable.
- **Not a deprecation of texture-chain.** Existing ops stay. No sunset timelines.
- **Not the final pipeline state.** Future phases may add: a `Group2D` scene-graph wrapper for hierarchical transforms, a `Picking2D` operator for per-drawable hit-testing, a GPU-side sprite atlas manager for cross-operator sharing, compute-driven drawable emitters (e.g. `Flocking2D` variants). All deferred.
- **Not cross-package.** vivid-3d and other packages are untouched. They can, in future phases, choose to emit drawables — but Phase E doesn't require it.

## Relationship to 3D instancing (Phase B)

Phase B shipped a parallel generator/modifier/adapter family for 3D (`InstanceGrid`, `InstanceNoise`, `InstancesFromLanes`, `Instancer3D`) backed by `InstanceArray3D`. Phase E's 2D family is structurally analogous but uses different record types (`mat3x2` affine vs. 3D `position + rotations + scale`) and feeds a different consumer pipeline (`VividDrawable2D` vs. `VividSceneFragment`).

**Decision: keep the stacks separate; extract only the shared math.**

Unifying the types would force either a bloated superset record (wastes bytes, confuses readers) or runtime polymorphism (doesn't survive `dlopen` cleanly). The record layouts are genuinely different because 2D and 3D per-instance data is different. The consumer pipelines are different worlds.

What *is* worth sharing is algorithm code. During E.5, extract a `src/operator_api/instance_algorithms.h` header-only module containing:
- `hash_float(seed)` — PCG hash
- `value_noise_1d(phase, seed)` — smoothstep-interpolated value noise
- `circle_position(i, n, radius, out_x, out_y)` — ring layout
- `line_position(i, n, spacing, out_t)` — 1D line
- `grid_position(i, n_cols, spacing, out_col, out_row)` — 2D grid
- `grid3d_position(i, n_dim, spacing, out_xyz)` — cubic lattice

Both 2D and 3D families include this header. Phase B operators (vivid-3d) can be retrofitted opportunistically when the duplication becomes visible; forcing the retrofit now would be a premature abstraction.

## Current status

**E.1 COMPLETE 2026-04-19.** Foundation working end-to-end for both shape and sprite drawables, with and without instancing. Ready to proceed to E.2 (core modifiers + depth sort + blend modes).

### E.1 progress

| Item | Status | Notes |
|---|---|---|
| E.1.1 `gpu_2d.h` + `VividDrawable2D` | ✅ | 312-byte struct, ABI-padded sub-blocks, pinned via static_assert. `InstanceData2D` 48-byte record with mat3x2 column-major transform + color. Port helpers (`drawable_port`, `instance_array_port`, `drawable_input`, `instance_array_input`, `drawable_identity`, `drawable_transform_compose`, `drawable_transform_trs`). |
| E.1.2 `DrawableMerge` | ✅ | 4 inputs → 1 composite with `children[]`. ~60 LOC, mirrors `SceneMerge`. |
| E.1.3 `ShapeEmitter` | ✅ | Params: sides (circle=0 .. 32-gon), star_factor, softness, position/rotation/scale, color. |
| E.1.4 `SpriteEmitter` | ✅ | Texture input → drawable with `texture_view`/`texture_sampler` set. Propagates through Instancer2D unchanged, enabling 1..N-sprite rendering. Uses default linear sampler; flips Y to match upstream top-down texture convention. |
| E.1.5 `Render2D` | ✅ | **Four** pipeline variants: `shape-single`, `shape-instanced`, `sprite-single`, `sprite-instanced`. Three bind group layouts (uniforms, storage, texture) reused across variants. Dynamic UBO offsets for per-drawable uniforms, cached per-drawable storage + texture bind groups, premultiplied alpha blend, recursive tree walk for collection. |
| E.1.6 `Instancer2D` | ✅ | Drawable template + InstanceArray2D → drawable with `instance_buffer` set. Shallow-copies the template so sprite texture handles flow through automatically. Fall-through when bundle unconnected. |
| E.1.7 `InstanceGrid2D` | ✅ | Grid / Circle / Line layouts. Emits InstanceArray2D with per-instance mat3x2 transforms (scale + translation). |
| E.1.8 End-to-end smoke | ✅ | Three proofs-of-life: (a) `ShapeEmitter → Render2D` (single hexagon), (b) `ShapeEmitter → DrawableMerge ← ShapeEmitter → Render2D` (hexagon + star composition), (c) `ShapeEmitter → Instancer2D ← InstanceGrid2D → Render2D` (400 circles via `DrawIndexed(6, 400)`), (d) `NoiseTexture → SpriteEmitter → Instancer2D ← InstanceGrid2D → Render2D` (textured sprite grid with per-instance transforms). |

### Validated architectural choices

- **Flat tagged-union record with reserved padding**: working as intended across 6 operators. `VIVID_DECLARE_CUSTOM_REF_TYPE` registers the type, custom-ref port transport carries the pointer. Plugin-boundary ABI stable via pinned `sizeof`.
- **Per-drawable `DrawIndexed`, not batched mega-draw**: 400 instances in one call via `@builtin(instance_index)`, pulling per-instance transforms from a storage buffer. No uber-shader.
- **Storage buffer indexed by `instance_index`**: cleaner than dynamic UBO offsets for instance data. Dynamic UBO offsets used only for per-drawable uniforms (color / shape params / global transform).
- **Separate pipelines per variant**: 4 cached pipelines with 3 reusable bind group layouts. Scales to more variants (text, custom) without architectural change.
- **Coexistence with texture-chain**: Render2D outputs a standard `VIVID_PORT_TEXTURE`; existing texture-chain demos (Bloom, Composite, Feedback) untouched.

### Files shipped (vivid core)

- `src/operator_api/gpu_2d.h`
- `operators/gpu/drawable_merge/drawable_merge.cpp`
- `operators/gpu/shape_emitter/shape_emitter.cpp`
- `operators/gpu/sprite_emitter/sprite_emitter.cpp`
- `operators/gpu/render_2d/render_2d.cpp`
- `operators/gpu/instancer_2d/instancer_2d.cpp`
- `operators/gpu/instance_grid_2d/instance_grid_2d.cpp`
- 7 `cmake/operators.cmake` entries under `# --- 2D drawable-pipeline operators (Phase E) ---`

### Known issues to address in later sub-phases

- **Aspect ratio**: drawables placed at NDC `[-1,1]` appear stretched on non-square output textures. Fix in E.2 via viewport-corrected projection in the vertex shader (add aspect ratio to the Uniforms struct).
- **`z_layer` stable-sort** not wired yet (E.2).
- **Only ALPHA blend mode**; other four (additive, multiply, screen, overlay) in E.2.
- **Runtime sometimes loses nodes on capture/reload** — cosmetic MCP issue, not a Phase E bug; the rendered output in the thumbnails is correct. Inspect via `capture_image` directly.

Phase A and B work lives on master; Phase E is on master too for now — feature-branch migration deferred.

## E.2 + E.3 status (2026-04-19)

- **E.2 complete** — aspect correction, z_layer sort, lazy pipeline cache (4 blend modes), Transform2D modifier. Detail: `docs/plans/2d-pipeline-e2-modifiers.md`.
- **E.3 Particles2D complete** — compute-shader particle simulator (573 LOC) emitting `VividDrawable2D`. Verified at 50K particles in a single `DrawIndexed` call. Detail: `docs/plans/2d-pipeline-e3-particles.md`.
- **E.3b Flocking2D complete** — Reynolds boids (separation/alignment/cohesion) via GPU compute. Verified at 800 boids with visible clustering. Detail in same e3-particles doc.
- **E.4 Text rendering** — next sub-phase; largest sub-project (3 weeks).
- **E.5 Phase C subsumption** — InstanceNoise2D, InstancesFromLanes2D, rename InstancedShapes → ShapeField.
- **E.6 Demo graphs + docs**.
