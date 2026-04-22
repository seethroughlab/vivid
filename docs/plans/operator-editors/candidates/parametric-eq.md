# ParametricEQ Editor

## Context

`ParametricEQ` (`operators/audio/parametric_eq/parametric_eq.cpp`) is a 4-band EQ with per-band freq/gain/Q/type (16 float knobs + 4 type selectors + `band_count`). Today the inspector lays them out as a flat knob grid — no response curve, no spectrum reference, no way to see how two adjacent bands interact. This is the standard EQ UX gap every DAW closed two decades ago.

Unlike the grid editors in Tier 1, this is a **curve-on-a-plane** editor — a different vocabulary from DrumSequencer/Sequencer/Tracker. Expect the shared helpers not to apply here; the payoff is introducing a second reusable editor vocabulary that ColorBands (swatches) and Material3D (preview spheres) can borrow from later.

## High-level approach

A frequency-response curve on a log-frequency / linear-dB plane. Each band is a draggable node; x = frequency, y = gain. Q is edited by drag-Q gesture (scroll on the node, or shift-drag vertical). Band type cycles via a context action or via per-node buttons pinned just below the node. The composite response curve updates live as the user drags. An optional spectrum overlay (input signal magnitude) helps the user target specific resonances.

## Editor layout

```
┌────────────────────────────────────────────────────────────────┐
│ top bar: band_count · A/B compare · bypass · legend            │
├────────────────────────────────────────────────────────────────┤
│  +24dB ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─    │
│                                                                │
│         ●1  ────╮                                              │
│              ╱    ╲                                            │
│   0dB ──────╱────●2────────────●3────────────●4──────────────  │
│                                                   ╲_           │
│                                                     ╲__        │
│  -24dB ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─    │
│        20Hz    100Hz    1kHz    10kHz    20kHz                │
├────────────────────────────────────────────────────────────────┤
│  side panel (right ~260px): selected band — freq/gain/Q/type  │
│    (numeric edit + type cycle buttons)                        │
└────────────────────────────────────────────────────────────────┘
```

Default window ~1000×540; min ~720×400.

## Interactions

### Mouse
- Click node → select band.
- Drag node → update `freq_N` (x, log) + `gain_N` (y, linear dB).
- Scroll on node → adjust `q_N`.
- Alt+scroll (or shift+drag vertical on selected node) → alternative Q gesture for trackpads without scroll.
- Double-click node → cycle `type_N` through peak → low shelf → high shelf → LP → HP.
- Click empty plane → deselect.
- Right-click node → context menu with reset / delete (if band_count shrink).

### Keyboard
- Arrow keys: nudge selected band frequency (left/right, quantized log steps) and gain (up/down, 0.5 dB).
- Shift+arrow: coarse nudge (octave for freq, 3 dB for gain).
- `1`..`4`: select band N.
- `Q` + scroll or `+`/`-`: Q up/down.
- `T`: cycle type.
- `Delete` / `Backspace`: reset band to defaults (no, not delete — band_count governs active count).

### Live feedback
- Composite response curve redrawn on every parameter change.
- Optional spectrum overlay from the input signal — the host supplies an analysis snapshot through `VividEditorContext` if available; skip gracefully if not.
- Bypass toggle fades the curve for visual "before" comparison (A/B).

## Data model recap

From `operators/audio/parametric_eq/parametric_eq.cpp`:
- `band_count` (1..4)
- Per band N in 1..4: `freq_N` (20..20000 Hz), `gain_N` (-24..+24 dB), `q_N` (0.1..20), `type_N` (peak / low shelf / high shelf / LP / HP)
- Semantic metadata already present (`frequency_hz` / `scalar` / `VIVID_DISPLAY_KNOB`) — the editor reads it to format axis labels and units.

Bands beyond `band_count` are painted dimmed; drag activates them by bumping `band_count`? Or require explicit activation via the top bar? **v1: explicit activation only** — dragging a dimmed node does nothing. Simpler, less-surprising default.

## Implementation

### Files
- `operators/audio/parametric_eq/parametric_eq.cpp` — single-file operator today. Split into:
  - `parametric_eq.cpp` — entry + `VIVID_REGISTER` + `VIVID_EDITOR`.
  - `parametric_eq_core.{h,cpp}` — the core struct + `compute()` (matches other dual-cadence operators' pattern).
  - `parametric_eq_editor.cpp` — editor painting + input.
  - `parametric_eq_editor_shared.{h,cpp}` — pure-logic helpers: log-freq ↔ pixel mapping, biquad-to-pixel response sampling, band hit-test.
- `cmake/operators.cmake` — update the `parametric_eq` target to include the new files.

### State on the core
- `editor_selected_band_` (0..3, -1 = none)
- `editor_drag_band_` (-1 when idle)
- `editor_bypass_` (transient, editor-only; does not bind to a persisted param)
- `editor_a_snapshot_` — optional A/B snapshot of all 16 band params; toggled via top bar.

### Shared-helpers reuse
- None from DrumSequencer (different idiom).
- Factor out the response-curve math to `parametric_eq_editor_shared.*` so tests can exercise the log/dB mapping and pixel sampling without the editor.
- If ColorBands / Material3D arrive after ParametricEQ, consider promoting the log-axis helpers if reusable.

### Tests
- `tests/operators/test_parametric_eq_editor_helpers.cpp` — log/dB mapping round-trip, band hit-test at arbitrary cursor positions, response curve sampling matches the compiled biquad response at N reference points.
- No runtime audio test needed for the editor; `compute()` already has coverage.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: dedicated editor is the interactive authoring surface; inspector becomes passive preview + "Open Editor" button.

- No `parametric_eq_inspector.cpp` exists today — ParametricEQ is a single-file operator using the default param list. The retirement here is about *preventing* a parallel interactive inspector from being added later, not deleting code.
- Mark the 16 per-band params (`freq_N`, `gain_N`, `q_N`, `type_N` for N in 1..4) as `VIVID_DISPLAY_HIDDEN` in `collect_params`. The editor is the canonical place to set them; the flat knob list today is the specific UX this plan exists to replace. Keep `band_count` visible for quick enable/disable.
- The existing thumbnail (if present) should render a miniature response curve as the inspector preview. If there's no thumbnail today, add one driven by the same `parametric_eq_editor_shared` response-sampling math so the preview and editor curve can't diverge.

## Deferred / out of scope

- Spectrum input overlay (requires an analysis hook through `VividEditorContext`; if unavailable on first landing, ship without and add later).
- Mid/side or stereo-split editing.
- More than 4 bands.
- Pre/post visualization (the signal is already processed — adding a pre-plot requires a second analysis stream).
- Preset browser — `factory_presets.json` already exists and is recalled via the host's standard preset menu; editor can defer its own preset UI to later.
- Frequency-grid customization (1/3 oct guidelines, etc.).

## Open questions

- Pixel response sampling: render by evaluating the composite biquad at N log-spaced frequencies, or by reading an existing magnitude-response helper? If one exists in the runtime, use it; otherwise compute in the editor. Either way keep the sampling in `parametric_eq_editor_shared` so tests can lock it down.
- Should Q be read-only from the plane (node radius), or editable directly on the plane? Node radius visually encoded + scroll-to-edit is the pragmatic answer. Don't try to drag Q from the node outline.
