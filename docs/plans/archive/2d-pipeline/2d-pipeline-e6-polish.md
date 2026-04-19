# Phase E.6 — Demo Polish + UI Discoverability

**Status:** COMPLETE 2026-04-19.

## What shipped

- **ARCHITECTURE.md §5.7.3 "2D Drawable Pipeline"** (new section, ~60 lines). Explains `VividDrawable2D` as a tagged-union record, the Emitter → Modifier → Instancer → Render pipeline shape, a table of operator roles, six canonical recipes (from "one shape on screen" through "lane-driven instancing" and "compute particles"), ordering/blend semantics, and pointers to demo graphs and the E.1–E.6 plan docs.
- **Curated `meta` blocks on 7 demo graphs** in `graphs/gpu/`: `particles_2d_demo`, `flocking_2d_demo`, `instancer_2d_noise_demo`, `instances_from_lanes_2d_demo`, `shape_field_simple` (renamed from `instanced_shapes_simple`), plus the two new starter graphs below. All carry `title`, `description`, `tags`, `difficulty`, `domains`, `featured_rank` (48–54), `category: instancing`, `family: 2D drawable pipeline`, and `role` so they surface properly in File → Open Example.
- **Two new pedagogical demos:**
  - `shape_emitter_intro.json` — the "hello world" 2D graph: single ShapeEmitter → Render2D → video_out.
  - `instancer_2d_grid_demo.json` — clean 64-circle tiling, ShapeEmitter → Instancer2D ← InstanceGrid2D → Render2D. Verified rendering end-to-end.
- **`@tip`/`@recipe`/`@common_companions`/`@best_used_with`/`@family`/`@see`/`@pitfall` doxygen tags on all 12 Phase E operators** — ShapeEmitter, SpriteEmitter, DrawableMerge, InstanceGrid2D, InstanceNoise2D, InstancesFromLanes2D, Instancer2D, Transform2D, Particles2D, Flocking2D, Render2D, ShapeField. Verified surfacing via MCP `operator_docs Particles2D` — tips, recipes, pitfalls, related, common_companions, best_used_with, and operator_family all populate from source comments with no runtime changes.
- **Renamed `graphs/gpu/instanced_shapes_simple.json` → `shape_field_simple.json`** to match the operator rename from Phase E.5.

## Deferred from E.6 scope

- Custom thumbnails for Phase E operators — default GPU output is adequate for discovery. Nice-to-have for a later polish pass.
- A dedicated "2D Drawable" or "Instancing" subtab in the add-node browser — current domain filtering is sufficient. Revisit if user feedback shows discovery pain.

## Original plan below (preserved for reference)

Master plan at `docs/plans/2d-pipeline-redesign.md`. Sub-phases: E.2, E.3, E.5 complete. This is the final polish pass to make the 2D drawable pipeline discoverable to users.

## Context

Phases E.1–E.5 shipped the full 2D drawable pipeline: emitters, Instancer2D, Transform2D, Render2D, GPU-driven Particles2D/Flocking2D, CPU-side InstanceNoise2D/InstancesFromLanes2D, plus the ShapeField rename. The machinery works end-to-end.

What's missing is **discovery**: a new user poking at the "Add Node" browser has no way to know that ShapeEmitter → Instancer2D → Render2D is the canonical 2D instancing recipe, or which operators belong together. The operator-docs system exposes `tips` / `recipes` / `common_companions` / `best_used_with` fields, but none of the Phase E operators populate them. Demo graphs exist but don't carry `meta` blocks to surface in the File → Open Example UI.

E.6 closes that gap without touching new code paths.

## Scope

| Item | Hours | Priority |
|------|-------|----------|
| 1. ARCHITECTURE.md "2D Drawable Pipeline" section      | 2–3 | **High** |
| 2. `meta` blocks on the 5 existing Phase E demo graphs | 1–2 | **High** |
| 3. 2 new pedagogical single-operator demos             | 1–2 | Medium |
| 4. `@tip/@recipe/@common_companions` on the 12 Phase E operators | 2–3 | **High** |
| 5. Memory + master-plan status updates                 | 0.25 | — |

**Total: ~7–10 hours.** Not the full 2 days — the other "nice to haves" (thumbnails, browser categorization subtab) are deferred to a post-alpha polish pass since domain filtering already makes operators findable.

**Out of scope:**
- Custom thumbnails for Phase E operators (default GPU output texture is fine for discovery).
- Browser categorization subtab (would need a new category-metadata system; not worth the churn pre-alpha).
- `VividOperatorDescriptor` API additions — existing doxygen-tag-extracted metadata (`operator_source_docs.cpp`) is already sufficient and live.

## Work items

### E.6.1 — ARCHITECTURE.md 2D drawable pipeline section (2–3h)

**File:** `docs/ARCHITECTURE.md` — add a section after §5.7 (Operator contract) titled "2D Drawable Pipeline".

Cover:
- What a `VividDrawable2D` is (tagged-union record; SHAPE/SPRITE/TEXT; ABI-stable).
- Pipeline shape: **Emitter → Modifier → Instancer → Render**.
- Canonical recipes:
  - Single shape: `ShapeEmitter → Render2D → video_out`
  - Tiled: `ShapeEmitter → Instancer2D ← InstanceGrid2D → Render2D`
  - Tiled + jittered: `ShapeEmitter → Instancer2D ← InstanceNoise2D ← InstanceGrid2D → Render2D`
  - Lane-driven: `SpreadNoise × N → InstancesFromLanes2D → Instancer2D ← ShapeEmitter → Render2D`
  - GPU-driven: `Particles2D → Render2D` (compute-shader owns the instance buffer)
  - Legacy SDF field: `ShapeField → Render2D` (self-contained, no instancing pipeline wiring needed)
- When to mix with texture-chain operators (Bloom, Feedback etc.): Render2D's output IS a standard texture, so texture-chain continues from there.
- Pointer to demo graphs in `graphs/gpu/*_demo.json`.

Mirror the style of existing ARCHITECTURE.md sections (§5.7 operator contract, §5.9 lanes).

### E.6.2 — Demo graph `meta` blocks (1–2h)

Add a `meta` block at the top of each of these graphs:

- `graphs/gpu/particles_2d_demo.json`
- `graphs/gpu/flocking_2d_demo.json`
- `graphs/gpu/instancer_2d_noise_demo.json`
- `graphs/gpu/instances_from_lanes_2d_demo.json`
- `graphs/gpu/instanced_shapes_simple.json` (ShapeField demo — rename file to `shape_field_simple.json` for clarity)

Each `meta` block follows the pattern from `graphs/README.md` (`id`, `title`, `description`, `tags`, `difficulty`, `domains`, `featured_rank`, `content_kind`).

### E.6.3 — Two new pedagogical demos (1–2h)

**`graphs/gpu/shape_emitter_intro.json`** — the simplest possible drawable-pipeline graph. Single ShapeEmitter (a hexagon) → Render2D → video_out. Difficulty: "beginner", featured_rank: high. This is the "hello world" of the 2D pipeline.

**`graphs/gpu/instancer_2d_grid_demo.json`** — ShapeEmitter → Instancer2D ← InstanceGrid2D (Grid layout, no noise) → Render2D. Shows clean tiling without the added complexity of jitter. Difficulty: "beginner", featured_rank: next after shape_emitter_intro.

### E.6.4 — Operator `@tip/@recipe/@common_companions` tags (2–3h)

Add doxygen metadata tags to the 12 Phase E operators (`ShapeEmitter`, `SpriteEmitter`, `DrawableMerge`, `InstanceGrid2D`, `Instancer2D`, `Transform2D`, `Render2D`, `Particles2D`, `Flocking2D`, `InstanceNoise2D`, `InstancesFromLanes2D`, `ShapeField`).

Minimum per operator:
- 1–2 `@tip` lines (usage hints)
- 1 `@recipe` line (canonical wiring example)
- `@common_companions` (comma-separated operator names)
- `@best_used_with` where relevant

These are extracted by `src/runtime/operators/operator_source_docs.cpp` and surfaced through `operator_docs` MCP + the inspector UI. No code changes; comments only.

### E.6.5 — Memory + master-plan updates (15 min)

Append E.6 completion to this file. Update `docs/plans/2d-pipeline-redesign.md` status table. Update `~/.claude/projects/-Users-jeff-Developer-vivid/memory/project_instancing_ux.md` to note Phase E is fully complete bar E.4 (Text rendering, 3-week separate sub-project).

## Verification

1. **Build:** no code changes in E.6.1–E.6.3; E.6.4 only modifies `.cpp` comments, a cmake rebuild is sufficient. Any operator that used `brief`/`body`/tag metadata already parses, so new tags should not fail.
2. **MCP check:** `operator_docs Particles2D` should surface the new tips/recipes/companions. Same for each of the 12.
3. **Graphs:** load each demo via `load_graph` → `get_graph_errors` clean. Validate `meta.featured_rank` surfaces in File → Open Example (manual visual check).
4. **Docs render:** `ARCHITECTURE.md` renders in GitHub preview without broken headings.
5. **ctest:** full suite green.

## Scope bound

- ~1.5 days.
- Closes the discoverability gap for Phase E.
- Nothing new in C++ API surface. No runtime changes.
- Deferred to a future polish pass: custom thumbnails, browser subtabs, in-app tutorial.
