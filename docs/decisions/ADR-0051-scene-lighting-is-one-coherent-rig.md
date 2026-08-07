# ADR-0051: Scene Lighting Is One Coherent Rig

Status: proposed

Date: 2026-08-06

> **Origin.** Raised by an evaluation of the lighting operator(s) — `Light3D` plus the lighting
> path in `Render3D` and `SDF3D`. Every finding below was verified against captured frames on a
> running app, not inferred from reading code. The evaluation is summarised in the Context.

## Context

Vivid has exactly one lighting operator. `Light3D`
(`app/operators/packages/vivid-3d/light3d.cpp`) is a scene-fragment source with no input port; it
emits a `VividSceneFragment` tagged `LIGHT`. `Render3D` walks the scene tree in
`collect_fragments` (`render_3d.cpp:1140`), gathers up to `kMaxLights = 4` lights into a
`LightsUniform` (`render_3d.cpp:1064`), and shades through five shader variants (Blinn-Phong
plain + instanced, Cook-Torrance textured, and two IBL variants). Billboards are deliberately
unlit. `SDF3D` renders through a custom pipeline and carries a *second, independent* copy of the
same light plumbing (`sdf3d.cpp:50`).

Much of this works well. Point, spot and directional lights all render; `intensity`, colour,
`radius` attenuation, `spot_angle`, `spot_blend` and spot aiming all behave correctly; a second
light contributes additively; and shadows are correct for a single directional light. The spot
cone in particular is genuinely good — clean soft-edged falloff.

But the model underneath is not one rig. It is two conventions for aim, two shadow assumptions,
two light-uniform layouts, and an ambient term nobody can reach.

### The four defects

**1. `dir_x/y/z` do nothing on a Directional light — and every shipped demo is mis-authored
because of it.** For `type = Directional`, `collect_fragments` derives the direction from the
*translation column* of the composed transform (`render_3d.cpp:1168`); the entire "Direction"
param group is spot-only. Sweeping `dir_*` from `(0,-1,0)` to `(-1,1,-1)` on a directional light
produced byte-identical frames (matching hash, brightness, contrast and colour-spread).

Auditing the five shipped demo projects, **every one** authors its key light this way:

| demo | type | `pos_*` — the *real* direction | `dir_*` — authored, ignored |
| --- | --- | --- | --- |
| blob | Directional | **0.5, 1.0, 0.8 (default)** | -0.4, -0.7, -0.5 |
| crystal | Directional | **default** | -0.4, -0.6, -0.5 |
| lattice | Directional | **default** | -0.4, -0.8, -0.45 |
| spectrum | Directional ×2 | **both default** | -0.3,-0.8,-0.5 / 0.6,-0.2,0.4 |
| storm | Directional ×2 | **both default** | -0.5,-0.7,-0.5 / 0.6,0.2,0.4 |

`spectrum` and `storm` therefore have no key/fill separation at all: both lights sit on the
identical default axis and simply sum into one brighter light. The authored intent in those files
was never expressed on screen.

**2. A second directional light corrupts the first one's shadow.** `ensure_shadow_maps` allocates
a single, non-array depth texture. The per-light loop (`render_3d.cpp:1555-1590`) renders *every*
shadow pass into that same view with `depthLoadOp = Clear`, reusing one `shadow_camera_ubo_`.
There is one command encoder and one submit per frame (`gpu/visual_graph.cpp:629`), so all
`wgpuQueueWriteBuffer` calls land before any recorded command: the last light's matrices and the
last light's depth win. Yet the fragment shader still samples that single map per-light via
`shadow.light_vp[i]` (`operator_api/gpu_3d.h:615`).

Demonstrated with the illumination held constant — a second light added at `intensity = 0`, so it
contributes no light but still occupies a shadow slot:

| one light | + a dark second light |
| --- | --- |
| compact shadow under the object | shadow ~4× too long, displaced off the object |

Frame brightness moved only 0.6014 → 0.5986, confirming the *lighting* was unchanged and only the
shadow geometry broke. A symmetric two-light rig happens to look right by coincidence — the
mirrored lookup lands where the correct shadow would be — which is why this has gone unnoticed
despite key/fill being the standard setup.

**3. Point and spot lights never cast shadows.** `if (cl.light_type > 0.5f) continue;`
(`render_3d.cpp:1566`) skips them. A spotlight pool with an object floating in it casting nothing
reads as a projected texture rather than a light, and a shadow is most of what sells a spot.

**4. `Light3D` has no effect on `SDF3D` geometry.** SDF3D uploads its own hardcoded directional
light every frame (`sdf3d.cpp:740`). A red light, a blue light from the opposite side, and
`intensity = 0` produced three identical frames. Mixing SDF and mesh geometry in one scene lights
them from two unrelated rigs. The root cause is structural: `SDFLightsUniform` is 208 bytes
(`sdf3d.cpp:50`) while Render3D's `LightsUniform` is 272 — the layouts cannot be shared even if
the wiring existed.

### The five gaps

- **No ambient control.** Hardcoded `0.15` grey in two places (`render_3d.cpp:1525`,
  `sdf3d.cpp:743`). There is no Ambient light type and no Render3D parameter, so a genuinely dark
  scene or a tinted fill is unreachable.
- **The light ceiling fails silently.** `if (lights.size() < kMaxLights)`
  (`render_3d.cpp:1158`) drops lights 5+ with no diagnostic — contrary to ADR-0019, under which
  nothing fails silently.
- **A directional light at the origin goes dark.** Zero-length translation leaves the direction at
  `(0,0,0)`, so the shader evaluates `normalize(vec3(0))` → NaN and the light contributes nothing.
  It degrades softly rather than crashing, but it is a trap for anyone animating position through
  zero — exactly what an audio-reactive mapping does.
- **The operator does not document itself.** `Light3D` has an empty `summary` and `keywords` in
  `site/reference.json`, and its reference preview is a plain tan cube on black that does not read
  as a light. The catalog sources `summary` from an optional `static constexpr kSummary`
  (`operator.h:556-562`); the doxygen `@brief` comments in the vivid-3d sources are read by
  nothing. All 14 ops with empty summaries are vivid-3d package ops.
- **The audit harness cannot run.** `tools/operator_audit/audit.py:221` references a `Gradient`
  operator no longer in the catalog, so `audit.py` crashes for *every* operator — the ADR-0042
  Definition-of-Done check cannot verify any of this work until it is fixed.

## Decision

Make scene lighting **one coherent rig**: one rule for aim, one uniform layout consumed by every
shading path including SDF, shadows that survive more than one light, and no silent failures.

Five phases, ordered so that each is independently shippable and the earliest phase fixes what is
most visible to users.

### Phase 1 — One rule for aim

A light's aim is `dir_*`. Its position is `pos_*`. Directional uses aim; Point uses position; Spot
uses both. One rule, no exceptions.

- `collect_fragments` (`render_3d.cpp:1167-1177`): the Directional branch reads
  `node->light_direction` rotated by the composed upper-3×3 — the same code the Spot branch
  already uses at `render_3d.cpp:1177-1197` — instead of the translation column. This collapses
  two conventions into one rather than adding a third.
- Guard the degenerate aim in C++: a zero-length direction falls back to a canonical `(0,-1,0)`,
  so a light animated through the origin can never emit `normalize(vec3(0))` → NaN. One place,
  because every shading path consumes the same uniform.
- `light3d.cpp`: apply the **existing** `visible_when_eq` / `visible_when_ne` helpers
  (`operator.h:253-266`, which this operator simply does not use today) so Position hides for
  Directional and Direction hides for Point. No new UI machinery is required.

**Migration.** Bump `kSessionSchemaVersion` 4 → **5** (`persist.h:30`). The existing
`migrate_param_value` (`persist.h:60`) is per-param and pure, so it cannot copy `pos_*` into
`dir_*` — it never sees the sibling params. Add a sibling
`migrate_node_params(int file_ver, const std::string& op_type, nlohmann::json& params)` that
rewrites the params object before the per-param loop at `persist.cpp:955-963`, keeping the same
pure-and-testable shape as `legacy_vop_name` and `migrate_param_value`. ADR-0016's Composite
`mode` rescale is the precedent for "a saved value whose meaning changed".

Rule: `file_ver < 5 && op_type == "Light3D" && type == Directional` →
`dir_* := -normalize(pos_*)` (the old `pos_*` pointed *toward* the light, `dir_*` aims *away*
from it), `pos_* := 0`. Existing projects therefore keep their current look.

### Phase 2 — Shadows survive more than one light

**2a — the corruption fix.** `dir_shadow_tex_` becomes a 4-layer depth array. Each directional
light renders into its own layer view, and the shadow-camera UBO gains per-light slot offsets so
one light's matrices are no longer clobbered by the next light's writes before submit. The five
shader variants' `dir_shadow_map` becomes `texture_depth_2d_array`, and `sample_shadow_dir`
(`gpu_3d.h:615`) selects the layer from the `light_idx` it already receives.
`create_shadow_map_texture` (`gpu_3d.h:67`) gains an additive `layers` parameter.

**2b — spot lights cast.** Drop the `light_type > 0.5f` skip for spot (`render_3d.cpp:1566`) and
compute a perspective light-VP from the cone angle into the same array. Near-free once 2a lands,
and it is what makes a spotlight read as a light rather than a projected texture.

**2c — point/omni shadows: deferred.** Omni shadows need six faces per light, a cube-array target,
and a distance-based compare rather than the current depth compare. That cost is not justified
before 2a/2b are in and measured. Recorded here so the gap is explicit rather than forgotten.

Add a per-light `cast_shadow` parameter. Today only the geometry side has one, on
`VividSceneFragment`; there is no way to say "this fill light does not cast".

### Phase 3 — One rig for every shading path

- Hoist a **canonical `LightsUniform` into `gpu_3d.h`**, replacing both Render3D's private 272-byte
  copy (`render_3d.cpp:1064`) and SDF3D's incompatible 208-byte variant (`sdf3d.cpp:50`). The
  layout divergence is the reason the two cannot share lights today, so unifying the struct is the
  actual fix, not a tidy-up.
- Add `custom_lights_ubo` to `VividSceneFragment`, mirroring the existing `custom_camera_ubo`
  field (`gpu_3d.h:397`). Render3D writes the collected lights into it exactly as it already
  injects the camera for custom pipelines at `render_3d.cpp:1651-1664`. SDF3D publishes its
  `lights_ubo_` there and deletes its hardcoded light. Purely additive, so the operator ABI stays
  at **17** under the additive-only rule.
- **Ambient becomes a real control**: a fourth `type = Ambient` on `Light3D`. Ambient then lives on
  an object in the scene and is mappable like any other light, rather than being a global header —
  consistent with how the rest of the scene graph works. Render3D keeps the `0.15` grey purely as
  the no-ambient-light fallback, so existing scenes are unchanged.

### Phase 4 — Nothing fails silently; the operator documents itself

- Lights beyond the ceiling raise an ADR-0019 diagnostic (node badge + leveled log) instead of
  vanishing. Evaluate raising `kMaxLights` 4 → 8 — the uniform cost is 528 bytes — while capping
  shadow casters at 4, so key/fill/rim plus accents fits within a rig that still shadows.
- Declare `kSummary` + `kKeywords` on `Light3D`, and on the other 13 vivid-3d ops with empty
  summaries. The work is mechanical (the prose already exists as doxygen `@brief` comments that
  nothing reads) and it is what makes `site/reference.json` honest for the whole package.
- Re-shoot the `Light3D` reference preview (`site/assets/reference/light3d.png`) so it reads as a
  light.
- Fix `tools/operator_audit/audit.py:221`. Until the harness runs, none of the above can be
  checked against the ADR-0042 Definition of Done.

### Phase 5 — Re-author the demos

Re-run the five generators (`examples/demos/{blob,crystal,lattice,spectrum,storm}.py`), each of
which builds the graph against a live app and calls `save_project` via `save_geo` / `save_demo`
(`examples/demos/vivid_demo.py:470-486`). Their authored `dir_*` values start taking effect with
**no source edits** — Phase 1 makes the existing intent live. `spectrum` and `storm` gain real
key/fill separation for the first time.

Recorded as a follow-up rather than folded in: the nine showcase clips on CloudFront were captured
under the old lighting and will eventually want a re-shoot through `site/scripts/*`.

## Consequences

- Directional lights in pre-v5 files keep their current look via migration. The five demos
  deliberately change look, because they are regenerated from source rather than migrated.
- A v5 session file is refused by an older Vivid (`SessionVersionStatus::TooNew`). That is the
  intended policy, not a regression.
- Shadow-map memory grows 4× at a given resolution (four layers) — worth noting against the 2048²
  default.
- Point and omni lights remain shadowless until 2c is picked up.
- The showcase videos diverge from the shipped demos until they are re-shot.

## Verification

Each phase carries its own acceptance check. Build with `cmake --build app/build -j` and run the
**full** ctest suite, not a subset — a removed API has broken test targets before. Drive the app
by direct binary path (`app/build/vivid.app/Contents/MacOS/vivid` with `VIVID_NO_RECOVER=1`);
`open -a` can launch a stale copy.

1. **Phase 1** — a persist round-trip test in `app/tests/test_persist_chain_migration.cpp`
   asserting a v4 Light3D migrates to the v5 aim; plus a live check that sweeping `dir_*` on a
   Directional light changes the frame (today it returns a byte-identical hash).
2. **Phase 2** — the regression that exposed the bug: one light, versus the same light plus a
   second at `intensity = 0`. The shadow must be identical in both. Then two opposite lights, with
   two correctly-placed shadows. Then a spot light over a floating object, which must cast.
3. **Phase 3** — an SDF3D scene lit by a `Light3D`: changing the light's colour, moving it, and
   setting `intensity = 0` must each change the frame. All three currently render identically.
4. **Phase 4** — `uv run tools/operator_audit/audit.py Light3D` runs to completion and reports
   PASS; `site/generate_reference.py` yields a non-empty `summary` for the vivid-3d ops.
5. **Phase 5** — re-run each demo generator against a live app and capture a frame per demo.

## As Built

_(to be filled in as phases land)_
