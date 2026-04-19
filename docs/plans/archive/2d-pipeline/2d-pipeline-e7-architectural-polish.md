# Phase E.7 — ShapeField drawable rewrite + RichText subsumed into Text2D

**Status: COMPLETE 2026-04-19.**

Final architectural consistency pass. Master plan at `docs/plans/2d-pipeline-redesign.md`; previous sub-phases at `docs/plans/2d-pipeline-{e2-modifiers,e3-particles,e4-text,e5-parity,e6-polish}.md`. After this, every 2D operator in `operators/gpu/` either emits/consumes a drawable or is a legitimate texture-chain post-processor (Bloom, Feedback, Fluid, CellularAutomata, MovieFile, TimeMachine, LutApply, …).

## What shipped

### E.7a — ShapeField now emits a `VividDrawable2D` tree

`operators/gpu/shape_field/shape_field.cpp` (765 → ~510 LOC, ~250 LOC deleted).

- **Output port:** `texture` → `drawable` (custom-ref `VividDrawable2D`).
- **Fragment shader + uniform path deleted.** No more `kShapeFieldFragment` WGSL, no `ShapeFieldUniforms` struct, no owned `pipeline_ / bind_group_ / uniform_buf_`. The operator is now a pure CPU compute + instance-buffer uploader.
- **Tree output.** The N computed instances are partitioned into six shape-kind buckets (circle / triangle / square / pentagon / hexagon / star). Each non-empty bucket gets its own `WGPUBuffer` (storage, grown with slack) holding `InstanceData2D[]`, and a child `VividDrawable2D` with the matching `shape_sides` + `shape_star_factor`. A no-op parent drawable with `child_count > 0` is the root; `collect_recursive()` in Render2D walks it naturally.
- **All unique capabilities preserved:** owned `ChildOp<LFO>` pools (scale / rotation / color_mod) with one LFO per instance; all 7 lane-array inputs (`pos_x / pos_y / size / hue / brightness / rotation / shape_idx`) still override cleanly; HSV→RGB via the existing CPU helper.
- **64-instance cap retained** — documented as intentional in the docstring (LFO pools don't scale past a handful; users should use Shape2D + Instancer2D for big instance counts).
- **NDC conversion:** UV [0,1] → NDC [-1,1] with Y flipped; size scales by 2 (unit quad is 2 units wide). Transform built column-major per `mat3x2 = T(pos) · R(rot) · S(size)`.
- **Tests updated:** `test_instanced_shapes_lanes.cpp` + `test_instanced_shapes_phase6.cpp` assert `drawable` output of `VIVID_PORT_TRANSPORT_CUSTOM_REF` instead of the old `texture` + `VIVID_PORT_TEXTURE`. Both pass.
- **Demo retuned:** `graphs/gpu/shape_field_simple.json` now wires `ShapeField/drawable → Render2D/drawable → video_out`. Verified visually — 12 hexagons in a circle layout render correctly.

### E.7b — Text2D subsumed the RichText animation story; RichText deleted

- **Three new params on Text2D** (grouped under "Animation"):
  - `anim_mode` (enum: None / Wave / Typewriter / Scatter / Fade)
  - `anim_speed` (0–10)
  - `anim_amount` (0–2)
- **`kTimeDependent` flipped `false → true`.** Cheap when `anim_mode=None` (the existing need_rebuild fast-path continues to suppress real work); when animation is active the glyph run is rebuilt every frame.
- **CPU-side modulation** injected into `rebuild_glyph_run`'s per-glyph loop:
  - *Wave:* sine-decorrelated y-offset per char.
  - *Typewriter:* progressive alpha reveal via smoothstep clamp.
  - *Scatter:* random xy offset decaying as progress climbs; alpha fades up with progress. Uses a deterministic 32-bit PCG hash keyed on char index × 2 (+1 for Y axis).
  - *Fade:* per-char staggered alpha ramp.
- **RichText deleted** — `operators/gpu/rich_text/` directory removed, `cmake/operators.cmake` entry removed. No tests existed for RichText (confirmed by grep); no graph references survive the migration below.
- **Demo migrated:** `graphs/gpu/rich_text_demo.json` rewritten to use `Text2D` with `anim_mode=1` (wave). Kept the Composite + NoiseTexture + Bloom composition downstream; just replaced the RichText node with a Text2D → Render2D chain. Visually verified — "VIVID" waves over the noise wash.

## What's explicitly NOT in scope (documented for later)

- **Custom font loading for Text2D.** RichText had a `font` param; Text2D still assumes `JetBrainsMono-Regular.ttf`. Easy to port when needed.
- **Multi-line text layout.** RichText supported `line_height` + newlines. Text2D is still single-line. A ~1-day sub-project when someone wants it.
- **Background fill for Text2D.** RichText had `bg_r/g/b/a`. Users can compose with a Shape2D rectangle behind Text2D; not worth baking into the operator.
- **`char_spacing` param.** RichText had an em-unit horizontal spacing. Not ported; not used by any surviving graph.
- **Per-instance shape_sides in `InstanceData2D`.** ShapeField's route-(a) bucketing avoids this; keeps Render2D's current contract.
- **Scaling ShapeField past 64 instances.** Intentionally kept as the differentiator vs Instancer2D.

## Verification

1. **Build:** `cmake --build build --target shape_field text_2d vivid` — clean.
2. **ShapeField smoke:** `graphs/gpu/shape_field_simple.json` loads; `get_graph_errors` empty; interface capture shows a ring of 12 hexagons.
3. **Text2D animation smoke:** `graphs/gpu/rich_text_demo.json` loads; capture shows "VIVID" with wave-animated y offsets. Cycle `anim_mode` 0→4 via `set_param` and the expected effects (typewriter progressive reveal, scatter fly-in, fade ramp) all engage.
4. **Tests:** `test_instanced_shapes_lanes` + `test_instanced_shapes_phase6` both pass after the output-port assertion update.
5. **operator_docs:** `ShapeField` output port is `drawable` of `VividDrawable2D`. `Text2D` lists 3 new Animation params.

## Phase E fully closed

With E.7 complete, every planned sub-phase has shipped:

- E.1 — foundation (VividDrawable2D, emitters, Render2D)
- E.2 — modifiers (Transform2D, aspect correction, z_layer, blend modes)
- E.3 — GPU-driven sources (Particles2D, Flocking2D)
- E.4 — text (Text2D + Render2D TEXT pipeline variant)
- E.5 — 2D/3D parity (InstanceNoise2D, InstancesFromLanes2D, shared `instance_algorithms.h`)
- E.6 — discoverability polish (ARCHITECTURE.md section, `@tip`/`@recipe` metadata, demo meta blocks)
- E.7 — architectural polish (ShapeField rewrite, RichText subsumption) — this phase.

The 2D drawable pipeline is now internally consistent: every legacy duplicate retired, every remaining legacy texture-chain operator is legitimately a post-processor whose state *is* a texture.
