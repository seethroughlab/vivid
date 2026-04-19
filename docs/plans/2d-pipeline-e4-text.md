# Phase E.4 — TextLabel + Render2D TEXT variant

**Status: COMPLETE 2026-04-19.**

Originally scoped as a 3-week sub-project in the master plan. Shipped in ~1 day because most of the infrastructure already existed.

Master plan at `docs/plans/2d-pipeline-redesign.md`; previous sub-phases at `docs/plans/2d-pipeline-e2-modifiers.md`, `.../2d-pipeline-e3-particles.md`, `.../2d-pipeline-e5-parity.md`, `.../2d-pipeline-e6-polish.md`.

## What shipped

- **`GlyphInstance2D`** struct added to `src/operator_api/gpu_2d.h` (64 bytes — `transform[6] + _pad[2] + uv_rect[4] + color[4]`). Laid out to match the `sprite_instanced` storage-buffer pattern so Render2D's text pipeline variant reuses the same bind-group layouts.
- **`drawable_text()` helper** added to `gpu_2d.h` — sets the TEXT payload (type, blend, atlas view, glyph buffer, glyph count) in one call.
- **Render2D TEXT pipeline variant** (`kRender2DTextInstancedShader`). New shader module `shader_text_instanced_`; reuses `pipe_layout_sprite_instanced_` (identical binding shape). The fragment shader samples an R8Unorm atlas and uses its red channel as coverage over the per-glyph colour.
- **Pipeline cache extended** from (`is_sprite`, `is_instanced`, `blend`) → (`is_text`, `is_sprite`, `is_instanced`, `blend`). 5 variants × 4 blend modes = up to 20 entries. 5 ALPHA pipelines are pre-created in `lazy_init`.
- **Render2D dispatch extended** — TEXT drawables are collected and drawn via a dedicated branch that binds the atlas + glyph storage buffer and issues `Draw(6, glyph_count)`. TEXT drawables don't carry their own sampler; Render2D owns a shared `text_sampler_` (linear, clamp-to-edge).
- **`TextLabel` operator** (~330 LOC, `operators/gpu/text_label/text_label.cpp`). Persistent 1024×1024 R8Unorm glyph atlas baked once (lazy, on first `process_gpu`) using `stb_truetype` at 64px raster height. Stores per-glyph metrics (UV rect, x0/y0 offset, bitmap w/h, advance). On each frame where `text`, `font_size`, position, anchor, or colour changes, walks the string and builds a `GlyphInstance2D[]` buffer uploaded via `wgpuQueueWriteBuffer`. Supports 9 anchors (TopLeft / Top / TopRight / Left / Centre / Right / BottomLeft / Bottom / BottomRight) by shifting the pen origin + baseline relative to `(position_x, position_y)`.
- **Params:** `text` (string, default `"vivid"`), `font_size` (NDC, default 0.12), `position_x/y`, `anchor`, `r/g/b/a`.
- **Demo:** `graphs/gpu/text_label_demo.json` renders "vivid 0.1" centered, 0.25 NDC tall, warm white on near-black. Verified end-to-end — glyphs clean, upright, correctly anti-aliased via linear atlas sampling.

## What's explicitly NOT in scope

- Kerning, ligatures, shaping, RTL, BiDi, vertical text, line wrapping.
- Unicode beyond ASCII 32–126.
- Per-glyph animation (RichText's domain — separate operator).
- MSDF / distance-field rendering.
- Shared-atlas cache across multiple TextLabels (each emitter bakes its own — fine for pre-alpha).
- Deprecating the legacy `Text` / `RichText` operators. They still work; a future phase can decide whether to unify or leave both.

## Bugs caught during verification

- **First capture rendered upside down.** The transform's y-scale was mistakenly negative (`-half_h`) and the `cy` calculation added `y0 * ndc_per_px` instead of subtracting. Root cause: inconsistent Y-axis conventions between NDC (+Y up) and raster glyph metrics (+Y down). Fixed by making `half_h` positive and computing `cy = baseline_y - (y0 * ndc_per_px + half_h)` — the minus aligns raster-down with NDC-up. The UV mapping (atlas v=0 at quad local +Y, atlas v=1 at local −Y) was already correct and needed no change.

## Known limitations

- **String editing UX.** `Param<vivid::TextValue>` for `text` works via MCP `set_string_param` but the inspector widget for string params is minimal. An inspector text field is a separate UX pass.
- **Atlas pixel size is fixed at 64px.** Glyphs at very large `font_size` show linear-filter blur; at very small sizes they shimmer. A future upgrade would be either (a) rebake the atlas at a target pixel size derived from `font_size` × viewport resolution, or (b) swap to MSDF for scale-invariance.
- **One atlas per TextLabel.** If a graph has 10 TextLabels with the same font, there are 10 copies of the same 1MB atlas. A shared cache keyed on `(font_path, raster_px)` would be easy but adds coordination and is deferred.

## Verification

1. **Build:** `cmake --build build --target render_2d text_label vivid` — clean, no warnings-as-errors.
2. **Smoke:** `text_label_demo.json` loaded via MCP → capture shows "vivid 0.1" rendered correctly.
3. **Coexistence:** Render2D's SHAPE / SPRITE / SHAPE_INSTANCED / SPRITE_INSTANCED variants unchanged — existing demos (particles, flocking, instancer_grid, etc.) continue to render identically.
4. **Metadata:** `operator_docs TextLabel` surfaces `@tip` / `@recipe` / `@common_companions` / `@best_used_with` / `@family` entries.

## Phase E closes

With E.4 complete, Phase E has delivered every sub-phase it originally scoped:
- E.1 — foundation
- E.2 — modifiers (Transform2D, aspect, z_layer, 4 blend modes)
- E.3 — GPU-driven sources (Particles2D, Flocking2D)
- E.4 — text (this phase)
- E.5 — 2D/3D parity (InstanceNoise2D, InstancesFromLanes2D, shared header, ShapeField rename)
- E.6 — docs + discoverability

The 2D drawable pipeline is now feature-complete for pre-alpha. The known `Transform2D` per-instance-local translation limitation (documented in E.2) is the only carried-over caveat.
