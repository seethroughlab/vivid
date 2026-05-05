# Material3D Editor (vivid-3d)

## Context

`Material3D` lives in `vivid-3d/operators/gpu/material3d/material3d.cpp`. It exposes PBR parameters (roughness, metallic, emission), shading-mode selection (PBR / toon / unlit / custom), and appearance settings (base color, tint, opacity). The inspector paints knobs; without a live preview sphere, tuning PBR values is guesswork — the same `roughness=0.3` reads very differently under different lighting.

## High-level approach

A preview sphere (or small set of primitive shapes) rendered with the current material settings dominates the window. Param clusters sit on the right. The preview uses a standard studio-lighting setup the user cannot edit — this is the "material sphere" convention from every 3D app ever shipped.

Like Particles3D, the preview relies on the platform giving us a render surface. If that's not available, ship v1 with organized clusters and no live preview — still a meaningful step up from the flat-knob inspector.

## Editor layout

```
┌────────────────────────────────────────────────────────────┐
│ top bar: shading mode · lighting preset · reset            │
├─────────────────────────┬──────────────────────────────────┤
│                         │  Base                            │
│                         │    r · g · b · opacity           │
│    material sphere      │                                  │
│    (preview render)     │  Surface                         │
│                         │    roughness · metallic          │
│                         │                                  │
│                         │  Emission                        │
│                         │    color · intensity             │
│                         │                                  │
│                         │  Mode-specific                   │
│                         │    (toon / custom params)        │
└─────────────────────────┴──────────────────────────────────┘
```

Default window ~980×600; min ~760×460.

## Interactions

### Mouse
- Drag on a slider → update.
- Click color field → open picker (reuse ColorBands' helpers once they exist).
- Scroll on a slider → nudge.
- Click preview sphere → optionally cycle shape (sphere → cube → torus) if multiple primitive shapes are supported; otherwise click does nothing.

### Keyboard
- Arrow keys on focused field: nudge.
- `Tab` / `Shift+Tab`: move focus.
- `R`: reset focused param.
- `L`: cycle lighting preset (studio / sunny / night / dramatic).

### Live feedback
- Preview re-renders every frame when any param changes.
- Shading-mode switch swaps which cluster block is visible on the right.

## Data model recap

From `vivid-3d/operators/gpu/material3d/material3d.cpp` (confirm at implementation time):
- `shading_mode` (PBR / toon / unlit / custom)
- PBR: `roughness`, `metallic`, `base_color_r/g/b`, `opacity`
- Emission: `emission_r/g/b`, `emission_intensity`
- Toon: `toon_bands`, `toon_rim_width`, `toon_rim_intensity` (confirm)
- Custom shader bindings (if supported)

## Implementation

### Files (in vivid-3d repo)
- `operators/gpu/material3d/material3d.cpp` — add `VIVID_EDITOR(Material3D)`.
- Split into core + entry.
- `material3d_editor.cpp` — new; preview sphere painting, cluster panels.
- `material3d_editor_shared.{h,cpp}` — new; cluster layout, shading-mode-conditional field visibility.
- `CMakeLists.txt` — update the material3d target.

### State on the core
- `editor_lighting_preset_` (0..3)
- `editor_preview_shape_` (sphere / cube / torus)
- `editor_cluster_collapsed_` (per cluster)

### Preview mechanism
Same platform question as Particles3D. If the editor API supports a render surface, use it. Otherwise ship a clusters-only v1. Don't invent a parallel 3D renderer inside the editor.

### Shared-helpers reuse
- Color picker — must share ColorBands' helpers once those exist. Do not reinvent HSV/RGB conversion locally.
- Cluster layout — if Particles3D lands first and promotes cluster helpers, reuse from there.

### Tests
- `tests/test_material3d_editor_helpers.cpp` — cluster layout, shading-mode-conditional visibility rules.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: the dedicated editor is the canonical authoring surface; inspector becomes passive preview + "Open Editor" button.

- Material3D's params are top-level PBR controls, not a dense grid — no `VIVID_DISPLAY_HIDDEN` pass needed. Every param belongs in both the inspector list and the editor's clusters.
- Retirement is preventative: don't add a small material-preview sphere to the inspector to "give users a quick visual". A preview sphere is an editor feature only. The inspector preview is the node's static thumbnail.
- Shading-mode-conditional visibility (e.g. toon-only params) can stay in the inspector via `visible_when_eq` hints against `shading_mode`; that's static hinting, not an interactive surface.

## Deferred / out of scope

- Texture slot authoring (drag a texture in, assign to a binding).
- Node-graph-style shader editor for custom shading mode.
- Environment map preview.
- Multiple preview shapes simultaneously (side-by-side).
- Baking / exporting materials.

## Open questions

- Does the operator API expose a texture handle for an embedded preview, or does the editor paint into the main canvas only? If the latter, ship without a preview and let the user evaluate changes by looking at the actual graph output. Not ideal, but not a blocker.
- Lighting presets: hardcoded in the editor, or sourced from a shared resource? Hardcoded v1; four studio setups is enough.
- Should the editor support opening against a specific 3D scene the user is composing, so the preview shows "what this material looks like on my actual mesh"? Out of scope — requires the host to expose scene context. Park under future work.
