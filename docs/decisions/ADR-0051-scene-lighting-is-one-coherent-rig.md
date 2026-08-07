# ADR-0051: Scene Lighting Is One Coherent Rig

Status: accepted (Phases 1–5 implemented; Phases 1b and 2c deferred — see As Built)

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

**1. `dir_x/y/z` do nothing on a Directional light — and every demo that lights a scene is
mis-authored because of it.** For `type = Directional`, `collect_fragments` derives the direction
from the *translation column* of the composed transform (`render_3d.cpp:1168`); the entire
"Direction" param group is spot-only. Sweeping `dir_*` from `(0,-1,0)` to `(-1,1,-1)` on a
directional light produced byte-identical frames (matching hash, brightness, contrast and
colour-spread).

Auditing the demo generators, **every one that adds a Light3D** authors its key light this way:

| generator | type | `pos_*` — the *real* direction | `dir_*` — authored, ignored |
| --- | --- | --- | --- |
| `blob.py` | Directional | **0.5, 1.0, 0.8 (default)** | -0.4, -0.7, -0.5 |
| `crystal.py` | Directional | **default** | -0.4, -0.6, -0.5 |
| `lattice.py` | Directional | **default** | -0.4, -0.8, -0.45 |
| `spectrum.py` | Directional ×2 | **both default** | -0.3,-0.8,-0.5 / 0.6,-0.2,0.4 |

`spectrum` therefore has no key/fill separation at all: both lights sit on the identical default
axis and simply sum into one brighter light. The authored intent was never expressed on screen.

The bundled projects under `examples/demos/projects/` currently contain **no** Light3D — they are a
different, later demo set (drift, generative-fields, geometry, grid, signal, surge-lead). So no
shipped `project.json` needs the migration; it exists for user projects and for saved sessions on
other branches, where a directional Light3D's aim really is stored in `pos_*`.

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
- `light3d.cpp`: declare which params each type reads via the existing `visible_when_eq` /
  `visible_when_ne` helpers (`operator.h:253-266`) — Position is meaningless on Directional,
  Direction on Point, the Spot group on both.

**Phase 1b — make param visibility real (split out).** The `visible_when_*` helpers and their
descriptor fields already exist and `VIVID_REGISTER` copies them through
(`operator.h:825-831`), but **no operator used them and nothing consumes them** — they are dead
plumbing today. Honouring them is not a lighting change: the param panel lays out by param *index*
(`node_param_row(i, ...)` in `ui/session_view.cpp`), and `ui/layout.h` is deliberately shared so
draw and hit-test agree, so hiding a param requires a visible-index mapping threaded through
layout, draw and hit-test on both the panel and the node body. That is a cross-cutting UX feature
worth its own slice for all 67 operators, and burying it inside a lighting fix would risk exactly
the misaligned-click-target bug the shared-layout convention exists to prevent. Light3D declares
its predicates now so the intent is recorded and lights up for free when the consumer lands.

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

### Phase 5 — Verify the demos now light as authored

Run the four generators that add lights (`examples/demos/{blob,crystal,lattice,spectrum}.py`),
each of which builds the graph against a live app via `save_project` / `save_geo`
(`examples/demos/vivid_demo.py:470-486`), and capture a frame of each. Their authored `dir_*`
values start taking effect with **no source edits** — Phase 1 makes the existing intent live, and
`spectrum` gains real key/fill separation for the first time. Re-tune only where the now-live key
direction reads worse than the accident it replaces.

Smaller than first scoped: the bundled demo projects carry no Light3D, so there is no saved
lighting to regenerate and no showcase re-shoot implied by this ADR.

## Consequences

- Directional lights in pre-v5 files keep their current look via migration. The four
  light-bearing demo generators deliberately change look, because their authored intent finally
  takes effect rather than being migrated away.
- A v5 session file is refused by an older Vivid (`SessionVersionStatus::TooNew`). That is the
  intended policy, not a regression.
- A `Light3D` added fresh still lights a scene identically to adding no light at all: its `dir_*`
  defaults are `-normalize(0.5, 1, 0.8)`, the exact key direction Render3D's built-in no-light
  fallback uses. Verified at a frame-signature delta of 0.00001.
- Shadow-map memory grows 4× at a given resolution (four layers) — worth noting against the 2048²
  default.
- Point and omni lights remain shadowless until 2c is picked up.
- Param visibility stays declared-but-inert until Phase 1b lands the consumer.

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

- **Phase 1 (one rule for aim) — implemented.** `collect_fragments` now derives a directional
  light's aim from `light_direction` through a shared `aim_from_fragment` helper — the same
  rotate-by-upper-3×3 the spot branch used — and negates it into the toward-light vector the
  shaders expect. Parenting a light under a `Transform3D` now swings its beam, which it never did.
  A degenerate aim falls back to straight down instead of emitting `normalize(vec3(0))`. `dir_*`
  defaults to `-normalize(0.5, 1, 0.8)` so fallback parity is preserved exactly.
  Schema v5 + `migrate_node_params` move a pre-v5 directional light's aim from `pos_*` to `dir_*`;
  the load path runs it on a copy, since `session_from_json` must not mutate a caller's document.

  Verified: 85/85 ctest green, plus 8 live frame-signature checks — `dir_*` now aims a directional
  light (delta 0.20084, **was exactly 0.00000**), `pos_*` no longer does (0.00000), a zero aim stays
  lit, a default Light3D still matches the no-light fallback (0.00001), and spot/point behaviour is
  unregressed.

  Two corrections to what this ADR first assumed, both found during implementation:
  - `visible_when_*` was **dead plumbing** — declared, copied into the descriptor by
    `VIVID_REGISTER`, used by no operator and read by no UI. Honouring it needs a visible-index
    mapping across the index-based param layout, so it is split out as Phase 1b.
  - The mis-authored lights live in the demo **generators**, not in any bundled project — the
    shipped project set changed and carries no Light3D. Phase 5 shrank accordingly.

- **Phase 2a + 2b (shadows) — implemented.** The directional shadow map is now a
  `kMaxShadowCasters`-layer depth array: each caster renders into its own layer view and into its
  own block of shadow-camera UBO slots, so neither the depth nor the matrices are clobbered by the
  next caster before the frame's single submit. `ShadowData` gained a `shadow_slot` table
  (light index → layer, -1 for none) because casters no longer line up with light indices; it is a
  `vec4i` rather than `array<i32,4>`, since a uniform array of scalars carries a 16-byte stride and
  would silently misread. Spot lights now cast through `compute_spot_light_vp` — a perspective
  frustum down the cone. `Light3D` gained a `cast_shadow` param, distinct from the existing
  geometry-side `cast_shadow`.

  One trap worth recording: the spot's first implementation cast *nothing*, because a near plane of
  0.05 against a far of 30 crushed the whole scene into ~0.002 of depth range — less than the
  default `shadow_bias` of 0.005, so no occluder ever registered. A perspective shadow map needs
  its near plane pushed OUT (`far * 0.05`, clamped), not pulled in.

  Verified: 85/85 ctest, 0 GPU validation errors, and 8 live checks measured at the PIXEL level —
  a whole-frame average or an 8×8 hash cannot distinguish "a small shadow appeared" from "nothing
  happened" (the frame-signature delta for a correct spot shadow is 0.002). Headline: adding a
  second caster that emits no light now changes the first light's shadow by **0 pixels, max delta
  0**, where it previously stretched it ~4× and slid it off the object. Two opposite casters
  produce two correctly-placed, correctly-tinted shadows. A spot shadow moves 0.17% of pixels by up
  to 155 levels; `cast_shadow=Off` removes a light's shadow while leaving its illumination within
  0.0002 brightness; a point light still casts nothing either way.

- **Phase 3 (one rig for every shading path) — implemented.** `LightData` / `LightsUniform` now
  live in `gpu_3d.h` immediately beside the `LIGHTS_3D_WGSL` preamble they mirror, so the CPU
  struct and the shader declaration are edited together. Render3D and SDF3D both alias the shared
  pair; SDF3D's private copy was 208 bytes and could not represent a spot cone, behind a comment
  claiming it matched Render3D "exactly" — the drift was invisible precisely because the two
  definitions sat in different files.

  `VividSceneFragment` gained `custom_lights_ubo`, the light-side twin of the existing
  `custom_camera_ubo` channel. SDF3D publishes its lights buffer and Render3D fills it with the
  scene's collected lights, so SDF and mesh geometry are lit by one rig. SDF3D still writes a
  default first — operators run before Render3D, whose later queue write wins — so an SDF rendered
  by something that supplies no lights still looks as it did.

  Ambient became a fourth `Light3D` type: colour × intensity summed across every Ambient light in
  the scene, occupying no shading slot and casting nothing. The hardcoded `0.15` grey survives only
  as the fallback when a scene places none. One consequence worth its own guard: an ambient-only
  scene must NOT get the fallback key light, or the renderer would override an author who
  deliberately asked for flat fill.

  Verified: 85/85 ctest, 0 GPU validation errors, 6 live checks. A Light3D's colour, direction and
  intensity all now reach SDF geometry — the three frames that demonstrate it were **byte-identical
  before this phase**. An Ambient light at 0 darkens the scene below the old built-in grey
  (0.3809 → 0.32302) and tints it when raised (→ 0.40901); an ambient-only scene with a black
  ambient renders at brightness 0.0, proving no fallback light was conjured. Phases 1 and 2 both
  re-verified green.

- **Phase 4 (fail loud; document itself) — implemented.**

  *The ceiling reports itself.* Render3D counts every light it walks and, past `kMaxLights`, reports
  "scene has N lights; only 4 are shaded (M ignored)". This needed the same kind of wiring Phase 1b
  did: `vivid_report_gpu_error` and the `operator_errored` / `operator_error_msg` ABI fields
  existed, and operators could set them, but **nothing read them back** — a second piece of dead
  ADR-0019 plumbing. `VisualGraph::render` now copies the message into `VisualNode::runtime_error`
  after each `process_gpu`, and `VisualNode::error()` returns it, which is the extension point that
  function's own comment invited ("when another op kind grows a runtime error, this is where it
  goes"). Unlike the param-visibility case, this was contained enough to do here, and it is
  general: any operator can now surface a per-frame runtime problem on its node and over MCP.
  `kMaxLights` was left at 4 — raising it is a separate judgement now that exceeding it is visible
  rather than silent.

  *The ops document themselves.* All 14 vivid-3d operators gained `kSummary` + `kKeywords`. They
  all had prose already, in doxygen `@brief` comments that nothing parses; the catalog reads an
  optional `kSummary` none of them declared. `site/reference.json` now has **0 operators missing a
  summary**, down from 14.

  *The harness runs.* `audit.py`'s perf baseline named the removed `Gradient` op, so the harness
  died before auditing anything. It now resolves an input-free GPU op from the live catalog and
  degrades to a skipped baseline rather than taking the run down. A second instance of the same bug
  was hiding one layer deeper — `scaffolds.Sources._CANON["texture"]` also named `Gradient`, which
  broke every texture-input op (Blur, CRT, Composite…) even once the baseline was fixed. Both now
  use `NoiseField`.

  *The preview reads as a light.* `light3d.png` was a tan cube on black — a picture of the cube the
  scaffold lit, not of the light. Preview-only dressing poses a warm spot pool with visible falloff
  on a ground plane. The audit's own scaffold stays neutral, so its param sweep is not biased by a
  pose chosen to look good.

  Verified: 85/85 ctest, 0 GPU validation errors, 6 live checks — the diagnostic appears past the
  ceiling, names the number dropped, and **clears** when the scene comes back under it. All four
  phases re-verified together, green.

  Known and expected: `audit.py Light3D` reports WARN with `no-visible-change` for `radius`,
  `pos_*`, `spot_*` and `cast_shadow`. Those are exactly the params a DIRECTIONAL light correctly
  ignores, and the scaffold uses the default type. The harness has no way to know that — the
  `visible_when_*` predicates that would tell it are not exposed over `list_operators`. Exposing
  them there is the cheapest real consumer of that metadata and belongs with Phase 1b.

- **Phase 5 (the demos light as authored) — verified; no re-tuning needed.** All four
  light-bearing generators (`blob`, `crystal`, `lattice`, `spectrum`) were built against a live app
  with `build(save=False)` — nothing written, since there is no saved demo lighting to regenerate.
  All four build and render correctly, and none looks worse.

  The visible impact is **smaller than this ADR first implied**, for a reason worth recording.
  Measuring the angle between each authored aim and the pre-ADR accident:

  | light | angle from the accident |
  | --- | --- |
  | `blob` key | 4.6° |
  | `crystal` key | 5.9° |
  | `spectrum` key | 7.3° |
  | `lattice` key | 8.9° |
  | **`spectrum` fill** | **114.1°** |

  Four of the five authored aims land within 9° of the direction the bug happened to produce — all
  of them are "down and to the left-back", and so is `-normalize(0.5, 1, 0.8)`. So for those, no
  visible change is the *correct* outcome, not a failed fix. The one substantive case is
  `spectrum`'s fill at 114°: it finally lights from the opposite side instead of piling onto the
  key, which is precisely the "no key/fill separation" defect the evaluation found. Its overall
  brightness moves from 0.022 to 0.052 with the authored aim live.

  A methodological note for anyone verifying visual work here: **frame-diffing a playing demo is
  not a sound instrument**. These scenes animate, so the same-setting animation noise floor swung
  between 1.8% and 20.5% of pixels across runs depending on where in the music the capture landed —
  swamping a lighting change and making a threshold-based pass/fail unreproducible. The angle above
  is the deterministic measure of how much a demo *should* shift, and Phase 1's static-scene checks
  are what actually prove the mechanism. Heavily emissive geometry compounds this: direct lighting
  is a minor term in these particular demos' final pixels.

- **Phases 1b and 2c — deferred, with the reasons recorded above.** 1b: honour `visible_when_*` in
  the UI (and expose it over `list_operators`, which would also stop the audit harness reporting
  type-inapplicable params as dead). 2c: omni/point cube shadows.
