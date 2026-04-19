# Phase E.2 — Modifiers + Depth Sort + Blend Modes + Aspect Correction

**Status: COMPLETE 2026-04-19.** See end of doc for what shipped.

Detail plan for the second sub-phase of the 2D pipeline redesign. Master plan at `docs/plans/2d-pipeline-redesign.md`. E.1 foundation is complete (4-variant Render2D, ShapeEmitter, SpriteEmitter, DrawableMerge, InstanceGrid2D, Instancer2D).

## Context

E.1 got us to "drawables render." E.2 makes them *composable* and *correct*:
- **Transform2D** — the first drawable modifier. The whole drawable-pipeline story is "emitters produce → modifiers transform → renderer rasterises." Without at least one modifier, the pipeline shape isn't proven.
- **z_layer sort** — makes painter's-algorithm ordering explicit. Without it, overlap is whatever traversal order gives you.
- **Blend modes** — ALPHA alone covers maybe 70% of visual cases. Additive + multiply + screen + overlay cover the rest and are what `Composite` offers today.
- **Aspect correction** — NDC `[-1, 1]` stretches horizontally on non-square textures (currently visible: circles render as ovals). Blocks making real demos.

Also handles a cosmetic fix surfaced in E.1:
- Drop unused `pad_viewport` alias and rewire the vertex shaders to use the viewport uniform for aspect correction (it's already in the struct, just unused).

## Sub-phase scope

Ordered by dependency + user-visible payoff. Each item stands alone; later items don't require earlier ones.

### E.2.1 — Aspect-ratio correction (half day)

**File:** `operators/gpu/render_2d/render_2d.cpp`

`Uniforms.viewport` already carries width/height; it's not currently used. Add a helper to the common WGSL:

```wgsl
fn apply_aspect(p: vec2f) -> vec2f {
    let aspect = u.viewport.x / max(u.viewport.y, 1.0);
    return vec2f(p.x / aspect, p.y);
}
```

Call `apply_aspect(world)` at the tail of every `vs_main`. Effect: NDC `[-1, 1]` becomes uniform in world space; circles stay circles on 16:9 textures.

**Verification:** re-render the 400-circle grid; circles should be round, not oval.

### E.2.2 — `z_layer` stable sort (half day)

**File:** `render_2d.cpp`, in `process_gpu` after `collect_recursive`.

Current `collected_` is in traversal order. Add:
1. For each collected drawable, assign implicit index = traversal order.
2. Stable-sort `collected_` by `(isnan(z_layer) ? implicit_index : z_layer)`. NaN items keep traversal order relative to each other.
3. Use `std::stable_sort` with a lambda comparator.

Depth test stays OFF. Only painter's algorithm ordering.

**Verification:** compose two overlapping sprites with different z_layer values; verify the higher-z one draws on top regardless of wiring order.

### E.2.3 — Additional blend modes (1 day)

**File:** `render_2d.cpp`.

WebGPU pipelines bake blend state — we need a pipeline per blend mode. Current code has 4 pipelines (shape/sprite × single/instanced) all with ALPHA. Target: 4 × 5 = 20 pipelines.

Approach — **lazy pipeline cache**:
- Replace the four fixed `pipeline_*` fields with a single cache:
  ```cpp
  struct PipelineKey { bool is_sprite; bool is_instanced; VividBlendMode blend; };
  struct PipelineEntry { PipelineKey key; WGPURenderPipeline pipeline; };
  std::vector<PipelineEntry> pipeline_cache_;
  ```
- Pre-create the 4 ALPHA variants in `lazy_init` so first-frame perf matches current.
- On dispatch, call `get_or_create_pipeline(key)`. Cache miss → compile + store.
- Blend modes map to WGPU blend state in `build_pipeline`:
  - ALPHA: src=One, dst=OneMinusSrcAlpha (current)
  - ADDITIVE: src=One, dst=One
  - MULTIPLY: src=Dst, dst=Zero
  - SCREEN: src=One, dst=OneMinusSrc
  - OVERLAY: fragment-shader blend against dest — not a pipeline state. Either require a separate pass with readback (expensive), or approximate with SCREEN for MVP and document the limitation.

For E.2, ship ALPHA / ADDITIVE / MULTIPLY / SCREEN as real pipeline variants. OVERLAY lands in a later sub-phase when we wire destination-read sampling.

**Verification:** render 3 overlapping circles with different blend modes (e.g. red additive, blue multiply, green alpha); visually confirm each pixel blends correctly.

### E.2.4 — `Transform2D` modifier (2 days)

**New file:** `operators/gpu/transform_2d/transform_2d.cpp`.

Takes a drawable input and a TRS (translate_x/y, rotation, scale_x/y) and emits a drawable with those additional transforms applied.

Handling the two drawable shapes:
- **Single-instance drawable**: compose the new TRS into the drawable's own `transform` field. One matrix multiply in `process_gpu`. Output is a shallow-copy of input with updated transform.
- **Instanced drawable**: two options —
  - (a) CPU pre-multiply each `InstanceData2D` record by the new TRS, upload to a new owned storage buffer. Cost: O(n) CPU work per frame.
  - (b) Compose at the vertex-shader level by carrying an "outer" transform in the uniforms and multiplying `outer * inner * vertex` in `vs_main`. Cost: one extra matrix multiply per vertex per frame. No CPU work.
  
  Pick **(b)** — cleaner, no extra GPU buffers, no per-frame upload. Requires adding `mat3x2 outer_transform` to the Render2D uniforms and defaulting it to identity for non-Transform2D callers.

Actually — cleaner still: Transform2D doesn't need to cooperate with Render2D. It just composes into the drawable's own `transform`, and for instanced drawables it composes into each instance's transform via a CPU pre-pass. Keep Render2D agnostic.

Final approach: **CPU pre-multiply for instanced case, transform composition for single case**. If profiling shows the CPU pre-multiply dominates for large N later, revisit with the vertex-shader-compose path.

Params:
- `translate_x / translate_y` (default 0)
- `rotation` (default 0 radians)
- `scale_x / scale_y` (default 1)

Port shape:
- Input: `drawable` (VividDrawable2D)
- Output: `drawable` (VividDrawable2D)

**Verification:** compose `ShapeEmitter → Transform2D → Render2D`; change Transform2D's rotation param, confirm the single shape rotates. Then `ShapeEmitter → Instancer2D ← InstanceGrid2D → Transform2D → Render2D`; confirm all instances transform together.

### E.2.5 — `DrawableFilter` (optional, defer if time-short)

**New file:** `operators/gpu/drawable_filter/drawable_filter.cpp`.

Pass-through modifier with a predicate on children. Filter by `z_layer` range, or by drawable type. Low priority; useful for later compositions but not required for the "E.2 complete" milestone.

Defer unless everything else finishes quickly.

## Critical files

**New:**
- `operators/gpu/transform_2d/transform_2d.cpp`
- `operators/gpu/drawable_filter/drawable_filter.cpp` (optional)

**Modified:**
- `operators/gpu/render_2d/render_2d.cpp` — aspect correction, z_layer sort, lazy pipeline cache with blend-mode dimension.
- `cmake/operators.cmake` — register new ops.

**Reuse:**
- Existing common WGSL (`sd_shape`, `shape_color`, `sprite_color_tinted`, `unit_quad`, `InstanceData2D`).
- Existing `drawable_transform_compose` helper in `gpu_2d.h` — Transform2D uses this directly for CPU matrix composition.
- `InstanceArray2D` bundle type — Transform2D emits a new bundle when the input is instanced.

## Verification

Sequential sanity checks via MCP. Each sub-item above has its own checkpoint. Cumulative end-of-E.2 verification:

1. **Build:** full `ctest` (background, per user memory); confirm no new failures vs. Phase A baseline.
2. **Aspect regression:** re-run E.1's 400-circle demo. Circles should now be round on the default 16:9 viewport.
3. **z_layer**: hand-compose a graph with two overlapping ShapeEmitters at different z_layers; verify the expected one paints on top.
4. **Blend-mode regression:** ALPHA renders identically to E.1 baseline (frame_hash match). ADDITIVE produces visibly brighter overlaps.
5. **Transform2D** single-instance: rotate a single sprite via Transform2D; confirm it rotates.
6. **Transform2D** instanced: apply Transform2D after an Instancer2D; confirm all 400 instances transform together as a group.
7. **Pipeline cache**: compose a graph that exercises 3+ blend modes in one frame; confirm no shader-compile warnings and all pipelines render.
8. **Coexistence**: existing `bloom_demo.json` still renders identically.

## Scope bound

- ~1 week of focused work (half-day + half-day + 1 day + 2 days + buffer).
- DrawableFilter optional; drop if scope slips.
- **Not in scope:** Particles2D / Flocking2D migration (E.3), text rendering (E.4), InstanceNoise2D / InstancesFromLanes2D (E.5), demo graphs + docs (E.6).

## Open questions to resolve at start of work

1. **OVERLAY blend mode**: fake it as SCREEN (simple) or defer entirely and document the gap? Recommend defer to a later sub-phase; expose only 4 modes in E.2 (Alpha/Additive/Multiply/Screen). Add OVERLAY when we do destination-read sampling in a follow-up pass.
2. **Transform2D instanced path**: confirm CPU pre-multiply is fast enough at 4096 instances (current InstanceGrid2D cap). If profiling shows >0.5ms/frame, revisit with a compute-shader pre-pass in a follow-up.
3. **Blend mode as a drawable field vs. a Render2D param**: keep as drawable field (`VividDrawable2D.blend_mode`, already in the struct). This way each emitter can set its own blend mode; a DrawableMerge subtree can mix blend modes naturally. Render2D picks the pipeline variant per-drawable.

## Completion summary (2026-04-19)

**Shipped:**

| Item | Resolution |
|---|---|
| E.2.1 Aspect-ratio correction | `apply_aspect()` helper in common WGSL divides NDC x by viewport aspect. Applied in all 4 vs_main variants. Circles now render round on 16:9 textures. |
| E.2.2 z_layer stable sort | `std::stable_sort` after `collect_recursive` in `process_gpu`. NaN treated as 0 (stable sort preserves traversal order within the z=0 bucket). |
| E.2.3 Blend modes | Lazy pipeline cache keyed on (is_sprite, is_instanced, blend). 4 ALPHA variants pre-created in `lazy_init`; others (ADDITIVE / MULTIPLY / SCREEN) compiled on first use. OVERLAY falls through to ALPHA with a comment explaining why (needs destination-read, deferred). |
| E.2.4 Transform2D | New operator, composes TRS onto `drawable.transform`. Single-instance → group-level transform. Instanced → per-instance-local transform (rotation / uniform scale still look correct for symmetric shapes; translation has per-instance-local semantics, documented as a known limitation). Verified visibly: 64-star grid tilted by 0.7 rad via Transform2D. |
| E.2.5 DrawableFilter | Deferred per plan ("Defer unless everything else finishes quickly"). |

**Files:**

- **Modified:** `operators/gpu/render_2d/render_2d.cpp` (aspect correction, z_layer sort, lazy pipeline cache, blend state mapping).
- **New:** `operators/gpu/transform_2d/transform_2d.cpp` + `cmake/operators.cmake` entry.

**Validated:**

- `ShapeEmitter → Instancer2D ← InstanceGrid2D → Transform2D → Render2D → video_out` — 64 gold 5-point stars in a grid, each tilted by Transform2D's 0.7 rad rotation.
- Same graph with `sides=0` (circle) — round circles, aspect correction confirmed.
- All E.1 smoke graphs continue to work (no regression).

**Deferred to follow-up sub-phases:**

- `outer_transform` field on VividDrawable2D for true group-level transforms on instanced drawables.
- OVERLAY blend mode (needs destination-read pass).
- DrawableFilter operator.
