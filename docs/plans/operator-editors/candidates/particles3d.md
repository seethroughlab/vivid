# Particles3D Editor (vivid-3d)

## Context

`Particles3D` lives in `vivid-3d/operators/gpu/particles3d/particles3d.cpp`. It has roughly 17+ params spanning emission (count, rate, lifetime), dynamics (speed, gravity, spread, drag, curl noise strength/scale/speed/octaves), shape (shape mode, elongation, size, bounds), appearance (r/g/b/a, emission, unlit), and a learning-mode flag. The inspector surfaces all of them as a scrolling knob list. Parameter interactions (e.g., curl noise affecting spread, elongation affecting apparent speed) are invisible until the user watches the output.

## High-level approach

A live preview pane on the left (half the window) shows a miniature running particle system driven by the current params. The right side groups the params into **clusters** (Emission, Motion, Noise, Shape, Appearance) — each cluster a compact set of aligned sliders/fields rather than a flat list. When the user edits a param, the preview updates instantly.

The preview is not a gizmo — it's a non-interactive rendered view. No transform handles, no orbit controls in v1. Adding those requires the editor to own a real 3D viewport, which is a platform conversation, not a per-operator one (see README "not a platform redesign" note).

## Editor layout

```
┌──────────────────────────────────────────────────────────────┐
│ top bar: preview scale · reset · A/B                         │
├───────────────────────────┬──────────────────────────────────┤
│                           │  Emission                        │
│                           │    count · rate · lifetime       │
│                           │                                  │
│   live preview            │  Motion                          │
│   (particles running)     │    speed · gravity · drag · ... │
│                           │                                  │
│                           │  Noise                           │
│                           │    strength · scale · speed · .. │
│                           │                                  │
│                           │  Shape                           │
│                           │    shape · elongation · size · ..│
│                           │                                  │
│                           │  Appearance                      │
│                           │    r/g/b/a · emission · unlit    │
└───────────────────────────┴──────────────────────────────────┘
```

Default window ~1100×640; min ~800×480.

## Interactions

### Mouse
- Click a param field → edit numerically.
- Drag a slider → update live.
- Scroll on a field → nudge.
- Click cluster header → collapse cluster (more screen real estate for the preview).

### Keyboard
- Tab / shift-tab: move focus across fields.
- Arrow keys on a focused field: nudge value.
- `R`: reset focused param to default.
- `Space`: pause/unpause the preview (useful for capturing an exact configuration without particles animating away).

### Live feedback
- Preview runs continuously at ~60 fps via the editor context's frame-tick hook.
- `reset` in the top bar clears the preview's particle buffer so the user sees a fresh burst.

## Data model recap

From `vivid-3d/operators/gpu/particles3d/particles3d.cpp` (confirm names at implementation time):
- Emission: `count`, `emission_rate`, `lifetime`
- Motion: `speed`, `gravity`, `spread`, `drag`
- Noise: `curl_strength`, `noise_scale`, `noise_speed`, `noise_octaves`
- Shape: `shape`, `elongation`, `size`, `bounds`
- Appearance: `r`, `g`, `b`, `a`, `emission`, `unlit`
- Other: `learning_mode`

## Implementation

### Files (in vivid-3d repo)
- `operators/gpu/particles3d/particles3d.cpp` — add `VIVID_EDITOR(Particles3D)`.
- Split into core + entry if the entry file is monolithic today.
- `particles3d_editor.cpp` — new; cluster painting, live preview integration.
- `particles3d_editor_shared.{h,cpp}` — new; pure-logic helpers: cluster-to-layout map, slider range/unit formatting.
- `CMakeLists.txt` — update the particles3d target with the new sources.

### State on the core
- `editor_cluster_collapsed_` — six bits for the five (or however many) clusters.
- `editor_preview_paused_`
- `editor_a_snapshot_` / `editor_b_snapshot_` — optional 17-param snapshots for A/B compare.

### Preview mechanism
The crux is whether the editor can host a live 3D render. Two options:
1. **Host-provided 3D preview surface** — the editor context exposes a way to get a texture handle that renders a scratch scene containing a Particles3D instance. If such a surface exists (check `src/operator_api/editor_ui.h`), use it.
2. **CPU approximation** — render a simplified 2D approximation of the particle flow. Cheap to implement, lies to the user. Only acceptable if option 1 is unavailable.

If neither option is clean, the editor can still ship without a preview and focus on the cluster grouping (already a significant UX improvement). Revisit when the platform gains real 3D preview support.

### Shared-helpers reuse
- Slider rendering + unit formatting — promote to `operators/shared/editor_ui/` if multiple 3D operators end up with cluster layouts (Material3D probably will).

### Tests
- `tests/test_particles3d_editor_helpers.cpp` — cluster layout math, snapshot round-trip.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: the dedicated editor is the canonical authoring surface; inspector becomes passive preview + "Open Editor" button.

- Particles3D doesn't have dense per-cell params — its 17+ params are all top-level cluster controls (count, lifetime, speed, noise, etc.). No `VIVID_DISPLAY_HIDDEN` pass is needed; every param legitimately belongs in both the inspector list and the editor's clusters.
- The retirement is about *not building* a parallel interactive live-preview pane into the inspector. The inspector keeps the default param list for quick tweaks; the editor owns the organized clustered layout and any live preview.
- If there's no thumbnail today, add a small static glyph (e.g., a particle-burst silhouette) so the inspector shows what the node does without running a live preview — the live preview belongs exclusively in the editor.
- Double-entry concern: make sure no custom inspector paint is added later "just to preview the emission". Live visual lives in the editor.

## Deferred / out of scope

- 3D gizmos for bounds / emission shape.
- Manipulating particle start positions directly.
- Recording / scrubbing a particle timeline.
- MIDI learn or modulation-source assignment inside the editor.

## Open questions

- Does the editor API expose a way to embed a live 3D render view? If not, the editor is valuable even with only cluster-organized params — the knob-flat inspector is the primary pain. Ship that first.
- Color fields (r/g/b/a): plug into the ColorBands color-picker helpers once those land. Consistency across the editor ecosystem > per-operator variation.
