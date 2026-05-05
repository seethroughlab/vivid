# ColorBands Editor

## Context

`ColorBands` (`operators/gpu/color_bands/`) renders N (up to 6) color bands as horizontal or vertical stripes with optional scrolling. The inspector has 18 RGB scalar knobs (`c0_r`, `c0_g`, `c0_b` through `c5_r`, `c5_g`, `c5_b`), `band_count`, `orientation`, `scroll_speed`, `softness`. Picking colors via three scalar knobs per slot is hostile.

Small Tier-2 job. Introduces a *swatch-picker* idiom that doesn't exist elsewhere in the seed operators but generalizes to future palette/gradient operators.

## High-level approach

Six color swatches in a row (or column, matching `orientation`). Each swatch shows its color; clicking opens an inline HSV picker centered on the swatch. Drag between swatches to copy. A live preview of the rendered output (respecting `orientation`, `softness`, scroll) fills most of the window beneath the swatches.

The inline color picker can be a compact HSV triangle + RGB numeric fields, or a rectangle + hue slider. Either works; the simpler design wins.

## Editor layout

```
┌──────────────────────────────────────────────────────────────┐
│ top bar: band_count · orientation · scroll_speed · softness   │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│   [c0] [c1] [c2] [c3] [c4] [c5]    ← swatches, click to pick│
│                                                              │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│           live preview of rendered bands                     │
│           (full band pattern, respects orientation)          │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│ side panel (right ~220px): selected swatch · HSV + RGB edits │
│                              · reset / copy / paste          │
└──────────────────────────────────────────────────────────────┘
```

Default window ~900×480; min ~640×360.

## Interactions

### Mouse
- Click swatch → select + open inline picker.
- Drag from one swatch to another → copy color.
- Shift+click → swap colors.
- Drag in HSV area → edit hue/saturation.
- Drag value/lightness slider → edit.

### Keyboard
- Arrow left/right: select next/previous swatch.
- `H` / `S` / `V` + scroll (or +/-): nudge channel.
- `R` / `G` / `B` + scroll: nudge RGB channel directly.
- `Delete`: reset swatch to a neutral gray.
- Cmd+C / Cmd+V on a swatch: copy/paste single color.

### Live feedback
- Preview panel re-renders on every change.
- `scroll_speed` animates the preview so the user can see motion.

## Data model recap

From `operators/gpu/color_bands/color_bands.cpp`:
- `band_count` (2..6), `orientation` (horizontal/vertical), `scroll_speed`, `softness`
- Per slot: `c{N}_r`, `c{N}_g`, `c{N}_b` (18 floats total)

## Implementation

### Files
- `operators/gpu/color_bands/color_bands.cpp` — add `VIVID_EDITOR(ColorBands)`. ColorBands is a GPU operator, so the editor's preview must drive a texture through the operator's own GPU pipeline, not paint pixels directly. Read how MSEG (also GPU-adjacent) handles this before committing; if it's non-trivial, the preview can be a CPU approximation for v1.
- `color_bands_core.{h,cpp}` — split the entry cpp into core + entry (matching other operators).
- `color_bands_editor.cpp` — new.
- `color_bands_editor_shared.{h,cpp}` — new; HSV ↔ RGB conversion (there's probably one in `operators/shared/` already — check before rolling your own), swatch hit-test.

### State on the core
- `editor_selected_swatch_` (0..5)
- `editor_picker_mode_` (HSV vs RGB numeric)
- `editor_clipboard_color_` (one float triple)

### Shared-helpers reuse
- HSV↔RGB conversion — look in `src/common/` and `operators/shared/` first.
- Color picker — standalone; not reused from editor helpers.

### Tests
- `tests/operators/test_color_bands_editor_helpers.cpp` — HSV↔RGB round-trip, swatch hit-test at variable layout widths.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: dedicated editor is the only interactive authoring surface; inspector becomes passive preview + "Open Editor" button.

- No `color_bands_inspector.cpp` today — ColorBands is a single-file GPU operator. Retirement is preventative: don't add a parallel swatch-picker to the inspector when the editor exists.
- Mark the 18 `c{N}_{r,g,b}` scalar params as `VIVID_DISPLAY_HIDDEN` in `collect_params`. Picking RGB values individually from the flat list is specifically what the editor replaces.
- Keep `band_count`, `orientation`, `scroll_speed`, `softness` visible — those are meaningful quick scrubs.
- The existing GPU output already provides the live visual; the inspector's passive preview is the node's thumbnail (running-bands animation). Make sure the thumbnail renders so the inspector still shows *what the palette looks like* without exposing the RGB knobs.

## Deferred / out of scope

- Gradient interpolation modes (linear vs bezier). ColorBands doesn't have them today.
- Palette import (from ASE, GPL, coolors.co URLs). Rainy-day polish.
- Saving named palettes to factory presets. Existing preset system applies; no editor-specific work.
- Alpha channel. ColorBands is RGB; adding alpha is a core behavior change.

## Open questions

- Preview accuracy: CPU approximation vs. actually driving the GPU operator. CPU is faster to ship; GPU is truthful. Start with CPU; file a follow-up to route the live GPU output into the editor preview if the inaccuracy is noticeable.
- Softness visualization: on the swatches themselves (e.g., feathered edges) or only in the preview? Preview only; swatches stay as flat rectangles for clarity.
