# ADR-0041: Procedural 3D Scene Graph for Audio-Reactive Visuals

Status: proposed

Date: 2026-07-28

## Context

The Vivid 4 showcase demos (ADR-0037) shipped and are live on vivid.seethroughlab.com. Even after the
per-note reactivity rebuild (Instancer/Emitter/Solids + transport sources), they read as **sparse and
screensaver-like**, and the audio-reactivity is often not **obvious** — a viewer cannot clearly see the
music causing the form. That undersells "MCP-native creative coding app" (ADR-0040): the product can do
far more than the demos show.

The desired richness is **procedural and generative, computed in real time from math + audio** — not VJ-
style post-processing (feedback trails, kaleidoscope, displacement) applied to source footage. Pre-
rendered video is at most a minor accent. Two supporting facts shape the decision:

1. **A legibility principle** (from a survey of Resolume/VDMX/TouchDesigner and the procedural-graphics
   literature): reactivity reads as *caused by the sound* only when it is **punctual** (a note-on
   triggers a visible spawn/burst) or **monotonic-and-large** (bass drives an obvious scale/inflation).
   Subtle continuous modulation of many parameters reads as generic wiggle. The band→role convention is
   bass→scale/impact, mids→motion/color, highs→detail/sparkle, note-ons→spawn/seed, quiet→empty.

2. **Most of the needed capability already exists on a compatible ABI.** The classic-generation packages
   — above all the sibling package **`vivid-3d`** — implement a complete, mature **shared 3D scene-graph
   system** (single camera, multi-light Blinn-Phong, shadows, IBL, fog, and *depth-correct* composition
   of many objects into one buffer), plus GPU curl-noise particles, 3D instancing, an audio/data→per-
   instance **lane bridge**, **SDF raymarching** that depth-composites with polygons, and an audio-
   reactive vertex **deformer**. These operators use the same operator ABI as the trunk
   (`OperatorBase`/`GpuProcessable`, `Param<T>`, `VividGpuContext`, `VIVID_DECLARE_CUSTOM_REF_TYPE` value
   edges). Classic's 2D branch additionally has portable metaballs, reaction-diffusion, a fluid solver,
   cellular automata, a slit-scan "time machine", trails, scopes, LUT/colormap, and an `fft_analysis`
   op that emits a per-bin spectrum as a data lane.

The clips must also be **bigger, longer, and multi-scene** (1080p, ~20–30s, performing intro→build→drop→
outro in one recording) so the visual evolves with the music. The capture machinery already supports
this with small changes: output resolution is set by the Output node's `height` param (no C++ change),
`export_video` is non-blocking with a server-side auto-stop, and `perform()` steps scenes bar-quantized —
they simply are not wired together today.

## Decision

Make procedural, generative 3D the rich visual substrate for Vivid, by **reviving and vendoring the
`vivid-3d` scene-graph into the trunk**, wiring the **trunk's live audio** into it, and rebuilding the
showcase demos as multi-scene 1080p performances. This is a **port-and-integrate** effort, not a build-
from-scratch one.

Specifically:

1. **Vendor the vivid-3d scene-graph spine** in-tree as a first-class trunk package under
   `app/operators/packages/` (not an external loadable package). The spine is: the `VividSceneFragment`
   value edge (a scene-graph-on-a-wire, mirroring the trunk's existing `VividMesh` custom-ref type),
   `SceneMerge` (compose objects + lights), `OrbitCamera` (camera as scalar outputs, audio/mouse-
   drivable), and **`Render3D`** — the sole compositor that owns one camera and one shared depth buffer
   and draws every object lit, shadowed, and fogged.

2. **Port a curated content-op set** onto that spine (the focused first wave): `Shape3D` (procedural
   primitives), `Particles3D` (GPU curl-noise particles), `Instancer3D` + `InstancesFromLanes` +
   `InstanceGrid`/`InstanceNoise` (instancing + the audio/data→per-instance bridge), `SDF3D`
   (raymarched liquid-metal hero, depth-composited), and `Deformer` (audio-mode vertex displacement).

3. **Wire the trunk's audio into the ports so reactivity is obvious.** Port classic `fft_analysis` (or
   extend the trunk's `mini_fft.h`) to emit a per-bin spectrum lane, feed it through `InstancesFromLanes`
   → `Instancer3D` for a literal "I can see the sound" spectrum of forms; bridge the trunk's
   `VividSignal.fired` note-ons into `Particles3D` emission (punctual bursts); map bands/transport to the
   bold couplings (bass→particle turbulence and SDF radius; `transport.bar_phase`→camera orbit). Add a
   small build-fresh iq cosine-palette helper for shared, designed color.

4. **Ship multi-scene 1080p capture.** Add a per-showcase `performance` spec (scene order + bars-each) to
   the showcase registry, and rework the capture helper to run one long non-blocking `export_video`
   while `perform()` drives the scene sequence, so a whole ~20–30s multi-scene song records in one clip.

5. **Rebuild 2–3 demos** on the scene graph, each pairing **one bold generative mechanism with one bold
   audio coupling**, evolving across scenes (e.g. "Spectral Ridge", "Curl Storm", "Liquid Metal").

Existing trunk 3D operators (`mesh`, `solids`, `model`, `mesh_render`, `mesh_displace`) are **not**
replaced or converted in the first wave; they keep their fixed internal cameras and coexist. The scene
graph is the new rich path alongside them.

Detailed operator list, file-level integration points, audio-wiring plumbing, phasing, demo recipes,
risks, and the end-to-end verification path live in the planning blueprint at
`~/.claude/plans/the-clips-still-aren-t-zazzy-quokka.md`.

## Alternatives Considered

- **Build a procedural visual suite from scratch.** Rejected. `vivid-3d` already implements the hardest
  piece (a shared camera + depth-correct lit/fogged scene composition) plus particles, instancing, the
  lane bridge, SDF raymarching, and an audio deformer — on a compatible ABI. Porting is far cheaper and
  lower-risk than rebuilding.

- **Manufacture richness with VJ post-processing (feedback/kaleidoscope/displacement on footage).**
  Rejected as the primary technique. It produces richness from source clips rather than generative
  structure; the goal is visuals computed live from the music, with source video as at most a minor
  accent.

- **Keep vivid-3d as an external, loadable package.** Rejected for the first wave. Vendoring in-tree is
  simplest to build, ship, gate, and showcase; the trunk owns the code. (An external-package path can be
  reconsidered later if the catalog grows.)

- **Port the entire vivid-3d catalog (and the 2D richness siblings) up front.** Deferred. A focused spine
  + curated ops ships sooner and de-risks the ABI reconciliation before pulling in SSAO/DoF/boolean/sweep,
  the physics2d/glitch/plexus 2D ops, and vivid-ml. These are explicit later phases.

- **Only make the clips bigger/longer without new visual capability.** Rejected. Resolution and length do
  not fix sparseness or unobvious reactivity; the generative substrate is the point.

## Consequences

- **New value-edge types enter the trunk ABI** (`VividSceneFragment`, `InstanceArray3D`), added the same
  additive way `gpu_types.h` declares `VividMesh`/`VividComputeBuffer`. This touches operator codegen /
  package registration and must be verified (the `core-visuals/vivid-package.json` manifest is currently
  stale and is not the source of truth).

- **A camera-ownership inversion** for scene-graph ops: content ops emit fragments (no camera, no self-
  clear) and `Render3D` owns the camera/clear/depth. This is the opposite of the trunk's per-op immediate
  cameras, and is why the existing 3D ops are left untouched in the first wave.

- **Feasibility is gated on Phase-0 spikes** before broad commitment: reconciling `gpu_3d.h`'s header
  deps (`gpu_common.h`, `type_id.h`, `port_type_registry.h`, `linmath.h`) against the trunk; wgpu-native
  feature support (`Depth32Float`, `textureSampleCompare` — vivid-3d ships a manual-PCF workaround, large
  storage buffers for 10⁵–10⁶ particles, `texture_2d_array`); headless 1080p/60 throughput (SDF3D
  raymarch is the heavy one, budget-gate with reduced internal resolution + upscale); and the FFT
  spectrum-lane → GPU-storage-buffer promotion path. Status flips to `accepted` once Phase 0 clears.

- **The showcase re-shoot reuses the existing delivery pipeline** (`reference_showcase_media_pipeline`):
  HLS encode → S3 `vivid-showcase-media` → CloudFront, embedded via the hls.js player, with the
  `master_gain` audio-headroom fix carried forward.

### Implementation phases

- **Phase 0 — spikes:** vendor `gpu_3d.h` + `Render3D` + `SceneMerge` + one `Shape3D`; render a lit cube
  with the shared camera; run the wgpu-feature and 1080p/60 checks; prove the FFT-lane path.
- **Phase 1 — focused first wave:** the spine + curated ops + audio wiring + palette helper + multi-scene
  1080p harness + rebuild 2–3 demos and re-shoot.
- **Phase 2:** rest of vivid-3d (Shape/Sweep/Boolean/mesh-import/Material3D/SSAO/DoF/DepthMask), build-
  fresh strange-attractors + superformula, port classic `time_machine` (slit-scan) and a video mosaic/
  crop op, and 2D richness (physics2d audio-metaballs/reaction-diffusion/fluid, glitch FX).
- **Phase 3:** `ChildOp<LFO>` self-modulation infra, plexus, vivid-ml depth/segmentation into the scene,
  the full catalog, GPU-storage-buffer spectrum lane, and scene-wide palette-phase coupling.

## References

- Planning blueprint (exhaustive detail): `~/.claude/plans/the-clips-still-aren-t-zazzy-quokka.md`
- Source packages to port: `../vivid-3d` (scene graph), `git show vivid-classic:operators/control/fft_analysis/…`
- Related: ADR-0037 (showcase demos gate the website), ADR-0040 (MCP-native creative coding is the public
  promise), ADR-0016 (a shader file is an operator).
