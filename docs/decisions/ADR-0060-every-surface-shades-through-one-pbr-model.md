# ADR-0060: Every Surface Shades Through the Same PBR Model

Status: accepted (Phase 1 implemented; Phases 2–3 proposed — see As Built)

Date: 2026-08-16

> **Origin.** Raised while wiring triplanar PBR maps (#355) onto the `blob` demo's SDF metaballs and
> then asking how Vivid's material shading compares to Unity / Unreal / TouchDesigner. Every claim
> below was read out of the shader source on this branch, not inferred. The short version: Vivid
> already *has* a professional PBR pipeline — it just isn't the one the SDF metaballs use.

## Context

Vivid has two geometry renderers, and they do not shade the same way.

**`Render3D` (meshes) is already modern PBR.** Its textured pipeline evaluates a Cook-Torrance
microfacet BRDF — `D_GGX · G_Smith · F_Schlick / (4·NdotL·NdotV)` (`render_3d.cpp:638-641`) — with
the metallic-roughness F0 convention `mix(vec3f(0.04), base_color, metallic)`
(`render_3d.cpp:600`). Those three BRDF terms live in a **shared** WGSL preamble, `PBR_BRDF_WGSL`
(`app/operators/packages/vivid-3d/operator_api/gpu_3d.h:529`, with `D_GGX` at :532, `G_Smith` at
:539, `F_Schlick` at :547 — "Phase 6c"). On top of that, Render3D has full **image-based lighting**:
irradiance + prefiltered environment cubemaps and a split-sum BRDF LUT (`render_3d.cpp:678-687`,
terms at :809-815), fed from an `ENVIRONMENT` scene fragment carrying `ibl_irradiance`,
`ibl_prefiltered`, `ibl_brdf_lut`, `ibl_intensity` (`render_3d.cpp:1254`, :1549, :1681-1684). This is
the same shading model — GGX specular, Schlick Fresnel, split-sum IBL — that Unity (URP/HDRP),
Unreal, and TouchDesigner's PBR MAT use.

**`SDF3D` (raymarched metaballs) shades through its own Blinn-Phong.** The raymarch fragment computes
`shininess = pow(2.0, (1.0 - roughness) * 7.0) + 2.0` and `specular = pow(max(dot(N, H), 0.0),
shininess)` (`sdf3d.cpp:428`, :451), sums it with a Lambert diffuse and a **flat constant ambient**
(`sdf3d.cpp:430`), and returns. There is no Fresnel term, no microfacet distribution or geometry
term, no energy normalization, and — critically — **no IBL**: `SDF3D` never looks at the
`ENVIRONMENT` fragment, so an SDF surface has nothing to reflect. It does get one thing right: the
same `specular_color = mix(vec3f(0.04), base_color, m_metallic)` F0 setup as the mesh path
(`sdf3d.cpp:426`). So it has the metallic-roughness *workflow* but the wrong specular *lobe* and no
environment response.

This split has a concrete, now-visible cost. ADR-0055's triplanar PBR maps (#355) — albedo, metallic,
roughness, normal, emission inputs on `SDF3D` (`sdf3d.cpp:834-841`) — are authored in the standard
metallic-roughness workflow, the same maps you would paint for Unreal or glTF. But on the SDF path:

- the **roughness** map drives an ad-hoc `pow(2, …)` shininess curve, not GGX, so a value painted for
  a physical result reads differently than the artist (or the map's source engine) intended;
- the **metallic** map has no environment to reflect, so metal reads as a coloured direct-light
  highlight rather than a reflective surface — exactly the "why doesn't it look like chrome?" symptom
  seen in the `blob` tight beauty shot;
- a mesh and a metaball lit by the same rig in the same scene **do not match**, because one runs GGX
  + IBL and the other runs Blinn-Phong with flat ambient.

This is the same structural defect ADR-0051 diagnosed for lighting — *two independent copies of the
plumbing that drift* — one layer up. ADR-0051 Phase 3 unified the light **uniform** (`LightsUniform`
now lives once in `gpu_3d.h:606-623`, aliased by both renderers) so SDF and mesh geometry take the
same lights. But it stopped at the uniform: the **shading math** stayed divergent. Render3D went on
to gain GGX (Phase 6c) and IBL (Phase 6f); SDF3D kept the Blinn-Phong it started with. The BRDF that
would unify them already exists, already compiles, and already sits in a shared preamble that SDF3D
`#include`s for its lights but not for its shading.

## Decision

**Every surface shades through the same PBR model.** An SDF metaball and a triangle mesh in one scene
run the same Cook-Torrance GGX BRDF, take the same Fresnel, and reflect the same environment. This
finishes the unification ADR-0051 began — from the light uniform through to the BRDF and the
environment — rather than maintaining a second, weaker shading path for raymarched geometry.

Three phases, ordered so the cheapest, most physically-correcting change ships first, and each is
independently shippable.

### Phase 1 — GGX + Fresnel on the SDF path (BRDF parity)

`SDF3D` concatenates the existing `PBR_BRDF_WGSL` preamble (it already concatenates `LIGHTS_3D_WGSL`)
and replaces its Blinn-Phong specular block (`sdf3d.cpp:426-462`) with the shared Cook-Torrance
evaluation — the identical `D_GGX(NdotH, r) * G_Smith(NdotV, NdotL, r) * F_Schlick(HdotV, F0) /
(4·NdotL·NdotV)` the mesh path runs at `render_3d.cpp:638-641`. The diffuse term becomes
`diffuse_color / PI * NdotL` to pair with it. `m_roughness` and `m_metallic` (which the triplanar
maps already produce, `sdf3d.cpp:398-407`) feed straight in.

This is mostly **reuse and deletion**, not new math: the functions exist, are tested on the mesh
path, and are in scope for the SDF shader the moment its preamble includes them. It immediately makes
the #355 roughness/metallic maps mean the same thing they mean everywhere else, and adds the
grazing-angle Fresnel rim that a dielectric or metal surface should have. WGSL-internal, so **no
operator ABI change**.

### Phase 2 — Image-based lighting on the SDF path (reflections)

Give the SDF raymarch the same split-sum IBL the meshes have. Bind the `ENVIRONMENT` fragment's
`ibl_irradiance` + `ibl_prefiltered` cubemaps, the `ibl_brdf_lut`, and the `IBLParams` uniform into
`SDF3D`'s pipeline as a new bind group, mirroring Render3D's group 2 (`render_3d.cpp:682-687`). At the
hit point, add the two split-sum terms — `irradiance * diffuse_color` and `prefiltered *
(specular_color * brdf.x + brdf.y)` (`render_3d.cpp:810-815`) — reflecting the view ray about
`m_world_normal` for the specular lookup. This is what turns "coloured highlight" into "reflective
metal", and it replaces SDF3D's flat `0.15` ambient with a real image-based ambient — the same
direction ADR-0051 Phase 3 took when it made ambient "a real control" rather than a hardcoded grey.

The cubemaps and the BRDF LUT already exist (Render3D generates and consumes them); the work is
sharing them into the SDF pipeline and computing the reflection vector, not building IBL from scratch.
Larger than Phase 1 because it adds a bind group and per-hit cubemap samples to the raymarch.

### Phase 3 — One environment reflects in every surface

Ensure a **single** `ENVIRONMENT` source lights and reflects in both mesh and SDF geometry, so a
metaball reflects the same world a mesh beside it does. `Render3D` already consumes the `ENVIRONMENT`
fragment; after Phase 2, `SDF3D` does too — this phase makes sure one scene-level environment reaches
both and that changing it changes both.

Add a **cheap procedural fallback environment** — a dark-stage gradient cubemap (ADR-0058's near-black
stage with the cyan→magenta signal accents) generated once — so SDF metals have something on-brand to
reflect even when no HDR/equirect environment is loaded. Without this, `has_environment = 0` leaves
metals looking as flat as they do today; with it, the default look already reads as a reflective
surface on Vivid's own stage.

## Consequences

- The triplanar PBR maps (#355) become physically meaningful on the SDF path: roughness is GGX
  roughness (matching the value's meaning in the engine the map was authored for), and metallic
  reflects the environment instead of only catching direct highlights.
- A metaball and a mesh in one scene finally match — same BRDF, same Fresnel, same environment —
  closing the shading half of ADR-0051's "one coherent rig", not just the lighting half.
- **Existing SDF looks will shift.** A GGX highlight is not a Blinn-Phong highlight (tighter core,
  longer tail), and Fresnel lifts the edges; scenes tuned against the old lobe — the `blob` demo
  among them — will want a light/material re-tune. This is the same "authored intent finally takes
  effect, so the look changes on purpose" trade ADR-0051 Phase 5 recorded.
- IBL adds per-hit cubemap samples to the SDF raymarch. Meshes already pay this; the SDF path is the
  more expensive place to pay it (one shaded hit per pixel, but the march to get there is not free).
  Gate on `has_environment` so a scene with no environment pays nothing.
- ABI: Phase 1 is WGSL-internal (no change). Phase 2 adds an internal bind group to `SDF3D`'s pipeline
  and, if a fragment field is needed to carry the env handles to the SDF op, an additive one under the
  additive-only rule (ADR reference: operator ABI stays additive).
- **Out of scope, recorded so the boundary is explicit:** clearcoat / anisotropy / sheen / subsurface
  shading models (Unreal & HDRP have these); area lights; screen-space or planar reflections;
  per-object environments; multi-scatter GGX energy compensation. This ADR brings the SDF path to
  single-scatter GGX + split-sum IBL **parity with Vivid's own mesh path** — the same bar Unity URP
  and TouchDesigner's PBR MAT set — not beyond it.

## Verification

Build with `cmake --build app/build -j 10` (bounded — an unbounded `-j` thrashes the self-hosted
runner) and run the **full** ctest suite, not a subset. Drive the app by direct binary path
(`app/build/vivid.app/Contents/MacOS/vivid` with `VIVID_NO_RECOVER=1`); `open -a` can launch a stale
copy. Frame-diffing a *playing* demo is not a sound instrument (ADR-0051's methodological note) — use
static scenes for the mechanism checks.

1. **Phase 1** — an `SDF3D` sphere and a `Render3D` mesh sphere, same roughness-ramp map, same light:
   the GGX highlight must narrow/widen identically across the two (today they diverge — one is GGX,
   one is `pow(2,·)` Blinn-Phong). A roughness sweep changes the highlight width monotonically; a
   grazing view brightens the rim (Fresnel), which it does not today.
2. **Phase 2** — an `SDF3D` metal ball (metallic ≈ 1, low roughness) in a scene with an `ENVIRONMENT`:
   the environment must appear reflected in the surface. Today the same scene renders **byte-identical
   with and without** the environment — that is the regression this phase removes. Cross-check against
   a `Render3D` metal ball: the two should match within tolerance.
3. **Phase 3** — one `ENVIRONMENT` in a scene containing both a mesh and an SDF metaball: both reflect
   it, and changing the environment (or its intensity) changes both surfaces. With no environment
   loaded, the procedural dark-stage fallback still gives SDF metals a visible on-brand reflection.
4. Re-run the `blob` demo generator against a live app and capture the tight beauty shot: the
   triplanar liquid-metal material should now read as reflective, and the demo's lights/material
   re-tuned to the GGX lobe where the old Blinn-Phong tuning reads worse.

## As Built

- **Phase 1 (GGX + Fresnel on the SDF path) — implemented.** `SDF3D`'s shader assembly now
  concatenates `PBR_BRDF_WGSL` after `LIGHTS_3D_WGSL` (`sdf3d.cpp`, one added line in the
  `CUSTOM_CAMERA_3D_WGSL + LIGHTS_3D_WGSL + kSDF3DShader` chain — the same preamble Render3D's textured
  path adds at `render_3d.cpp:2727`). The Blinn-Phong shading block was replaced by the shared
  Cook-Torrance evaluation: `spec = D_GGX·G_Smith·F_Schlick / (4·NdotV·NdotL)`, `F0 = mix(0.04,
  base_color, metallic)`, and an energy-conserving `kD = (1-F)(1-metallic)` Lambert diffuse
  (`… kD * base_color / PI + spec …`) — byte-identical to `render_3d.cpp:638-654`. `PI` is defined
  only in `PBR_BRDF_WGSL`, so there is no redefinition against the other preambles; `saturate` was
  already in scope. WGSL-internal only — **no operator ABI change** (stays additive).

  Verified: **100/100 ctest green** (0 failures), plus live frame captures. A minimal `Image → SDF3D →
  Render3D` sphere with a roughness/normal map went from a broad Blinn-Phong wash to a tight, bright
  GGX highlight with a sharp glossy core; the `blob` demo's tight beauty shot now reads as wet liquid
  metal — sharp magenta/cyan speculars with correct falloff — where the same maps previously produced a
  flat colour wash. This confirms the ADR's premise that the GGX lobe + Fresnel, on their own (before
  IBL), are enough to make the metallic-roughness maps read as intended.

  Consistent with the ADR's warning, the `blob` demo's look shifted (brighter, tighter highlights); its
  lights are still tuned to the old Blinn-Phong lobe and may want a light re-tune, tracked with the
  demo, not this ADR.

- **Phases 2 (IBL) and 3 (one environment + dark-stage fallback) — proposed, not started.**
