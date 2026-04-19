# Phase E.5 — 2D Instancing Parity + Shared Layout Math

**Status:** COMPLETE 2026-04-19.

## What shipped

- **`InstanceNoise2D`** (`operators/gpu/instance_noise_2d/`, ~155 LOC). Perturbs `InstanceArray2D` via golden-ratio-offset hash-based value noise on each instance's mat3x2 transform. Position jitter added to the translation column; rotation jitter pre-multiplies the linear block (rotates around instance center); scale jitter scalar-multiplies the linear block. Time-dependent, pointwise.
- **`InstancesFromLanes2D`** (`operators/gpu/instances_from_lanes_2d/`, ~115 LOC). 9 optional lane-array inputs (`pos_x, pos_y, scale_x, scale_y, rotation, color_r/g/b/a`) packed into an `InstanceArray2D` bundle. Modulo-wrap sampling for short lanes, sensible defaults for unconnected ports.
- **`instance_algorithms.h`** (`src/operator_api/`). Header-only `vivid::instancing::{grid_2d, circle_2d, line_2d, grid_3d}`. Used by `InstanceGrid2D` (core), `InstanceGrid` (vivid-3d), and `Instancer3D`'s legacy lane-path (vivid-3d). ~100 lines of duplicated layout math collapsed to one source of truth.
- **`InstancedShapes` → `ShapeField` rename.** Directory, file, class, `kName`, and debug labels renamed. CMake target `shape_field`. JSON-graph alias `"Instanced Shapes" → "ShapeField"` registered in `src/runtime/operators/builtin_operators.cpp` so existing saved graphs keep loading. Demo graphs rewritten in-place.
- **Demos:** `graphs/gpu/instancer_2d_noise_demo.json` (ShapeEmitter → Instancer2D ← InstanceNoise2D ← InstanceGrid2D, 64 hexagons with visible position/rotation/scale jitter) and `graphs/gpu/instances_from_lanes_2d_demo.json` (3 × SpreadNoise → InstancesFromLanes2D → Instancer2D ← ShapeEmitter, 48 circles driven by per-attribute noise lanes). Both verified rendering end-to-end.
- **Tests:** `test_instanced_shapes_lanes`, `test_instanced_shapes_phase6` updated to expect the new name; a stale port-count assertion in the lanes test (still asserting 5 of what is now 7 lane inputs since Phase 6) was fixed. Both pass.

## Original plan below (preserved for reference)

Detail plan for the Phase C subsumption into Phase E. Master plan at `docs/plans/2d-pipeline-redesign.md`; previous sub-phases at `docs/plans/2d-pipeline-e2-modifiers.md` and `docs/plans/2d-pipeline-e3-particles.md`.

## Context

Phases E.1–E.3 delivered the 2D drawable pipeline's **foundation** (`VividDrawable2D`, `InstanceArray2D`, emitters, Render2D, Transform2D) and its **GPU-driven sources** (Particles2D, Flocking2D). What's still missing to reach 2D/3D parity is the **CPU-side instancing family**:

| Capability | 3D (vivid-3d)            | 2D (core) — current state |
|------------|--------------------------|---------------------------|
| Layout generator  | `InstanceGrid` (Grid/Circle/Line/Grid3D) | `InstanceGrid2D` ✓ (E.1) |
| Noise modifier    | `InstanceNoise`          | **missing** — E.5.1       |
| Lane-array adapter| `InstancesFromLanes`     | **missing** — E.5.2       |
| Terminal drawer   | `Instancer3D` / `MeshDraw` | `Instancer2D` ✓ (E.1)   |

Phase B established the canonical 3D recipe as `Shape3D → Instancer3D ← InstanceNoise ← InstanceGrid`. Phase E.5 delivers the same recipe for 2D: `ShapeEmitter → Instancer2D ← InstanceNoise2D ← InstanceGrid2D`.

Additionally, `InstanceGrid3D` and `InstanceGrid2D` share ~100 lines of near-identical grid/circle/line math (also duplicated inside `Instancer3D`'s legacy lane-path). E.5 extracts that into a shared `instance_algorithms.h` header so 2D and 3D stay in sync.

Finally, the existing `InstancedShapes` core operator (759 LOC, self-contained SDF shape-field emitter with per-instance LFO pools) is confusingly named — it predates the Phase E drawable pipeline and does not plug into `InstanceArray2D`. E.5 renames it to `ShapeField` so "Instancer2D" is unambiguously the 2D terminal, and keeps a JSON-type alias so existing graphs load.

## Design decisions

1. **InstanceNoise2D transform semantics.** 3D `InstanceNoise` adds jitter to separate `position[3] / rotation_y / scale[3]` fields. 2D stores a single `mat3x2` affine transform per instance (position/rotation/scale all baked in). Decomposition is lossy; composing noise onto the existing matrix is the clean path. **Design:**
   - **Position jitter:** adds a 2D noise vector to the matrix's translation column directly (`m[2] += noise_pos`).
   - **Rotation jitter:** pre-multiplies the 2×2 linear block by `R(noise_rot)` so each instance rotates about its own center (not about the world origin).
   - **Scale jitter:** scalar-multiplies the 2×2 linear block by `(1 + noise_scale)`.
   - Net per-instance: `M' = [R(θ_n) · S(1 + s_n) · L_M | t_M + p_n]` where `L_M` is the linear block and `t_M` the translation.
   - This is deterministic, composable, and matches user intuition: "wiggle each instance in place." Rotation around origin would require a separate operator (not in scope).
2. **InstanceNoise2D noise model:** port the hash-based smooth value noise from 3D `InstanceNoise` verbatim (`hash_float`, `value_noise`, golden-ratio per-instance phase offset). Advance a single `time += dt * speed` accumulator; sample three decorrelated axes (pos_x/pos_y + pos_y/time / rot / scale). Drops 3D's per-axis position noise unification into a single 2D noise call.
3. **InstancesFromLanes2D ports:** 9 lane-array inputs (`pos_x, pos_y, scale_x, scale_y, rotation, color_r, color_g, color_b, color_a`) + 1 output (`instances: InstanceArray2D`). Mirrors 3D's 11-port layout minus `pos_z`, `scale_z`, and `rot_x`. Same modulo-wrap sampling behaviour for short lanes; same defaults for missing lanes (pos/rot→0, scale→1, color→opaque white). No params.
4. **Shared layout-math header.** Extract `compute_grid_2d`, `compute_circle_2d`, `compute_line_2d`, and `compute_grid_3d` into `include/operator_api/instance_algorithms.h` as inline functions. Caller passes an index + count + spacing and gets back a `vec2` (2D) or `vec3` (3D). Header-only; no new dylib. Delete duplicates in `InstanceGrid2D`, `InstanceGrid3D`, and `Instancer3D`'s legacy lane path.
5. **`InstancedShapes` → `ShapeField` rename.** Atomic file rename (`operators/gpu/instanced_shapes/` → `operators/gpu/shape_field/`), class rename, `kName = "ShapeField"`, update `cmake/operators.cmake`, update factory-preset path. Add a **JSON-load alias**: `operator_info_cache` or the graph loader should map `"InstancedShapes"` → `"ShapeField"` on deserialization so existing saved graphs keep working. Demo graphs that reference `InstancedShapes` get rewritten in-place.
6. **No Instancer2D changes.** Instancer2D already takes an `InstanceArray2D` input (E.1); InstanceNoise2D and InstancesFromLanes2D emit that type, so they drop in with zero Instancer2D work.

## Work items

### E.5.1 — InstanceNoise2D (½ day)

**New file:** `operators/gpu/instance_noise_2d/instance_noise_2d.cpp` (~160 LOC, mirrors 3D).

- Port `InstanceNoise` (`/Users/jeff/Developer/vivid-3d/operators/gpu/instance_noise/instance_noise.cpp`) verbatim, dropping Z and rot_x axes.
- Params: `position_jitter` (0–2 NDC), `rotation_jitter` (0–2π rad), `scale_jitter` (0–2), `speed` (0–20), `seed` (0–99999). Same ranges / defaults as 3D where applicable; tighter `position_jitter` default (0.05) appropriate for NDC.
- Port: `instances` in/out (`InstanceArray2D`).
- Per-instance noise phase = `golden_ratio * instance_id + seed`.
- Apply noise via the decision-1 composition rule on each `InstanceData2D.transform`.
- `kTimeDependent = true`, `kLaneBehavior = VIVID_LANE_STRUCTURAL`.

**Registered in** `cmake/operators.cmake` under the "2D drawable-pipeline operators (Phase E)" section.

### E.5.2 — InstancesFromLanes2D (½ day)

**New file:** `operators/gpu/instances_from_lanes_2d/instances_from_lanes_2d.cpp` (~120 LOC, mirrors 3D).

- Port `InstancesFromLanes` (`/Users/jeff/Developer/vivid-3d/operators/gpu/instances_from_lanes/instances_from_lanes.cpp`).
- 9 lane-array inputs + 1 `instances` output. No params.
- For each instance i in `[0, max_lane_length)`: sample each lane with `i % lane.size()`; compose `InstanceData2D.transform = T(pos_x, pos_y) * R(rotation) * S(scale_x, scale_y)`; pack color.
- Migration entrypoint for legacy per-attribute lane wiring; parallels the 3D adapter exactly.
- Registered same section of `cmake/operators.cmake`.

### E.5.3 — Shared `instance_algorithms.h` (1 day)

**New file:** `include/operator_api/instance_algorithms.h` — header-only.

```cpp
namespace vivid::instancing {

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

// Returns position for instance i in an N-instance grid, centred at origin, NDC.
inline Vec2 grid_2d(uint32_t i, uint32_t count, float spacing);
inline Vec2 circle_2d(uint32_t i, uint32_t count, float radius);
inline Vec2 line_2d(uint32_t i, uint32_t count, float spacing);
inline Vec3 grid_3d(uint32_t i, uint32_t count, float spacing);

} // namespace vivid::instancing
```

Implementations extracted verbatim from:
- `InstanceGrid2D` lines 62–89 → `grid_2d`, `circle_2d`, `line_2d`.
- `InstanceGrid3D` (vivid-3d) lines 72–121 → `grid_3d`, and validated to match 2D versions for shared layouts.

**Modified:**
- `operators/gpu/instance_grid_2d/instance_grid_2d.cpp` — replace 3 inline blocks with `vivid::instancing::*` calls.
- `vivid-3d/operators/gpu/instance_grid/instance_grid.cpp` — same.
- `vivid-3d/operators/gpu/instancer3d/instancer3d.cpp` (legacy lane path, lines 182–231) — same.

Header lives in **core** (`/Users/jeff/Developer/vivid/include/operator_api/`) so both core and vivid-3d pull from one source. vivid-3d's CMake already sees core includes on the path.

### E.5.4 — `InstancedShapes` → `ShapeField` rename (½ day)

- Rename directory: `operators/gpu/instanced_shapes/` → `operators/gpu/shape_field/`.
- Rename source file: `instanced_shapes.cpp` → `shape_field.cpp`.
- Rename class: `InstancedShapes` → `ShapeField`.
- Update `kName = "ShapeField"`.
- Update `cmake/operators.cmake` target name and paths.
- Update `factory_presets.json` path reference.
- Add JSON-type alias in `src/runtime/operators/operator_info_cache.{h,cpp}` (or equivalent resolver): when loading a graph, if `type == "InstancedShapes"`, silently resolve to `"ShapeField"`. Log a deprecation warning to the console once per session.
- Grep-and-rewrite any demo graph that references the old name (`graphs/**/*.json`). About 3–5 files expected.
- Document in `docs/ARCHITECTURE.md` if `InstancedShapes` is called out there; otherwise no doc touch needed.

### E.5.5 — Verification + demo (½ day)

**New demo graph:** `graphs/gpu/instancer_2d_noise_demo.json` — canonical 3-operator recipe showing `ShapeEmitter → Instancer2D ← InstanceNoise2D ← InstanceGrid2D → Render2D → video_out`. Demonstrates the full Phase E.5 family in one graph.

**New demo graph:** `graphs/gpu/instances_from_lanes_2d_demo.json` — `SpreadNoise × 9 lanes → InstancesFromLanes2D → Instancer2D ← ShapeEmitter → Render2D`. Shows the migration-adapter path from legacy lane-array wiring.

**Regression check:** load every existing graph that referenced `InstancedShapes`; confirm the alias resolves silently and renders identically.

**ctest:** both new demos added to the `test_demo_graphs` glob.

### E.5.6 — Memory + doc updates (15 min)

- Append E.5 completion entry to this file (status + what shipped).
- Update `docs/plans/2d-pipeline-redesign.md` status table.
- Update `~/.claude/projects/-Users-jeff-Developer-vivid/memory/project_instancing_ux.md`.

## Critical files

**New (in core):**
- `operators/gpu/instance_noise_2d/instance_noise_2d.cpp`
- `operators/gpu/instances_from_lanes_2d/instances_from_lanes_2d.cpp`
- `include/operator_api/instance_algorithms.h`
- `graphs/gpu/instancer_2d_noise_demo.json`
- `graphs/gpu/instances_from_lanes_2d_demo.json`

**Renamed (in core):**
- `operators/gpu/instanced_shapes/` → `operators/gpu/shape_field/`

**Modified (in core):**
- `cmake/operators.cmake` — 2 new entries; 1 rename.
- `operators/gpu/instance_grid_2d/instance_grid_2d.cpp` — delegate to shared header.
- `src/runtime/operators/operator_info_cache.{h,cpp}` — add JSON type alias map (or wherever graph-load type resolution lives).
- Existing demo graphs that reference `InstancedShapes` — rewrite to `ShapeField`.

**Modified (in vivid-3d):**
- `operators/gpu/instance_grid/instance_grid.cpp` — delegate to shared header.
- `operators/gpu/instancer3d/instancer3d.cpp` — legacy lane path delegates to shared header.

**Reuse / reference (do not copy blindly):**
- `vivid-3d/operators/gpu/instance_noise/instance_noise.cpp` — noise algorithm template.
- `vivid-3d/operators/gpu/instances_from_lanes/instances_from_lanes.cpp` — lane-sampling template.
- `operators/gpu/instance_grid_2d/instance_grid_2d.cpp` — existing 2D layout-math source of truth for the shared header.
- `include/operator_api/gpu_2d.h` — `InstanceArray2D`, `InstanceData2D`, `instance_array_port`.

**Do not touch:**
- `Instancer2D` (already takes `InstanceArray2D` — no changes needed).
- `Render2D`, Transform2D, emitters — upstream of this work.
- Particles2D, Flocking2D — GPU-driven, don't route through CPU instancing.

## Verification

1. **Build:** `cmake --build build --target instance_noise_2d instances_from_lanes_2d shape_field instance_grid_2d` — all dylibs produced clean. Run `cmake --build build --target vivid-3d` afterwards to confirm the shared header doesn't break the 3D package.
2. **Smoke via MCP:**
   - Load `instancer_2d_noise_demo.json`; confirm ~64 circles in a jittered grid, visibly wiggling.
   - Load `instances_from_lanes_2d_demo.json`; confirm per-lane-driven placement.
   - Load any pre-existing graph with `InstancedShapes`; confirm the alias logs a deprecation once and renders identically.
3. **Unit registration:** `operator_docs InstanceNoise2D`, `operator_docs InstancesFromLanes2D`, `operator_docs ShapeField` via MCP — confirm ports/params/descriptions.
4. **ctest:** full suite green, including both new demos.
5. **Parity check:** side-by-side `3d_instancer_demo.json` and `instancer_2d_noise_demo.json` — confirm the recipe shape is structurally identical (same generator/modifier/terminal pattern).

## Scope bound

- ~3 days of focused work (½ + ½ + 1 + ½ + ½ + 0.25).
- Closes the 2D/3D parity gap for CPU-side instancing.
- **Not in scope:** `Instancer2D` feature additions (palettes, rotation presets from 3D's `Instancer3D`), per-instance-shape variation in `ShapeField`, GPU-driven modifiers (a hypothetical `InstanceNoise2D_GPU`), Phase E.6 (UI discoverability), Phase D (UX polish).

## Known limitations going in

- **Rotation jitter rotates around instance center, not a user-specified pivot.** Matches 3D behaviour. Pivot control would need a future `InstancePivot2D` modifier.
- **No per-instance z_layer jitter.** `z_layer` lives on the drawable, not the instance record; can't be jittered by InstanceNoise2D. Consistent with 3D.
- **`ShapeField` keeps its 759-LOC internal architecture.** The rename is cosmetic + alias; the operator's ChildOp LFO pool design isn't revisited here. A future "port ShapeField to emit an InstanceArray2D instead of drawing directly" is a separate phase.
- **Alias is one-way.** Saved graphs always deserialize as `ShapeField`. If the user downgrades vivid to a pre-E.5 version, their saved graphs break. Acceptable pre-alpha.

## Open questions

1. **Alias location.** Where exactly does graph-load type resolution live — `operator_info_cache`, `graph_loader`, or the `RuntimeAPI` command dispatch? Pick the layer that sees the JSON `type` field before the node is instantiated. (Investigate `src/runtime/core/main_menu_actions.cpp` file-open path or `src/runtime/graph/graph_loader` if it exists.)
2. **`instance_algorithms.h` in `include/operator_api/` vs `src/common/`.** Core operators include `operator_api/*`. vivid-3d operators also include `operator_api/*` through the package CMake. `operator_api/` is the right home. Confirm by grep: vivid-3d CMake path setup.
3. **Deprecation warning cadence.** Log once per session total, once per unique old-name type, or once per load? Start with once-per-session-per-type. Cheap, informative, non-spammy.
