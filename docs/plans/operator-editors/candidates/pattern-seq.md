# PatternSeq Editor

## Context

`PatternSeq` (`operators/control/pattern_seq/`) is a 16-step value sequencer that emits scalars with a user-configurable range (-10000..+10000). Like Sequencer, it exposes the 16 per-step params as a flat list in the inspector, with no grid, no interpolation preview, no range visualization. Unlike Sequencer, its value range is much wider and users typically care about bezier-shaped progressions (risers, falls, ducks).

Mid-weight job. Reuses the grid vocabulary from Sequencer but with a taller, curve-like rendering.

## High-level approach

A vertical-bar grid like Sequencer, but:
- Taller per-cell rendering (the value scrub is the whole interaction — no gate lane).
- A live optional "interpolated curve" overlay if the operator has a smoothing mode (or if the user requests a visual hint).
- Y-axis labels show the real range (respecting `min` / `max` / scale).

Not a full curve editor — steps are discrete — but the editor communicates *value* rather than abstract 0..1 position, and it's shaped for quick pattern-painting.

## Editor layout

```
┌────────────────────────────────────────────────────────────┐
│ top bar: steps · min · max · rate · range indicator        │
├────────────────────────────────────────────────────────────┤
│  max ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─           │
│                                                            │
│   value columns (16 wide, faders)                          │
│                                                            │
│                                                            │
│  min ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─           │
├────────────────────────────────────────────────────────────┤
│ side panel (right ~220px): cursor step value · quick-fill  │
│   (ramp up / ramp down / random / flat)                    │
└────────────────────────────────────────────────────────────┘
```

Default window ~880×440; min ~620×320.

## Interactions

### Mouse
- Click + drag a cell vertically → set value.
- Drag across cells → paint (each cell takes the value at the mouse's y at that moment).
- Shift+click extends selection rect.

### Keyboard
- Arrow keys: move cursor.
- Up/Down: nudge cursor value (step = (max-min)/100).
- Shift+up/down: coarse nudge (step = (max-min)/10).
- `0`..`9`: set value to fractional position (digit / 9).
- `R`: fill selection with a ramp between anchor.value and cursor.value.
- `Z`: zero selection (set to 0.0, not min — zero is often musically useful).
- `F`: flat selection (set every cell to cursor.value).
- Cmd+C / Cmd+V: copy/paste rectangular selection (1×N vector).

### Live feedback
- Current step highlighted while running.
- Y-axis labels show real units based on the operator's range.

## Data model recap

From `operators/control/pattern_seq/pattern_seq_core.h`:
- `steps` (1..16)
- Per-step value params (~16)
- Clock/rate config
- Min/max range config (if present) — otherwise assume unipolar 0..1 and trust operator logic.

## Implementation

### Files
- `operators/control/pattern_seq/pattern_seq.cpp` — `VIVID_EDITOR(PatternSeq)`.
- `operators/control/pattern_seq/pattern_seq_core.h` — editor metadata + state.
- `operators/control/pattern_seq/pattern_seq_editor.cpp` — new.
- `operators/control/pattern_seq/pattern_seq_editor_shared.{h,cpp}` — new; value-to-pixel mapping, ramp fill, rect selection.
- `cmake/operators.cmake` — add new sources.

### State on the core
- `editor_cursor_step_`, `editor_selection_anchor_step_`
- `editor_selection_` rect (1D column range)
- `selection_clipboard_` (float vector)

### Shared-helpers reuse
- Selection math / clipboard (from shared editor_ui helpers once extracted).
- Value-to-pixel mapping with user-supplied min/max is distinct enough to live locally — promote later if three editors end up using it.

### Tests
- `tests/operators/test_pattern_seq_editor_helpers.cpp` — value/pixel mapping, ramp fill math, selection clipboard.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: dedicated editor is the only interactive authoring surface; inspector becomes passive preview + "Open Editor" button.

- Mark all 16 per-step value params as `VIVID_DISPLAY_HIDDEN` in `collect_params`. The flat-list rendering is the specific UX this plan exists to replace.
- Keep `steps`, `min`, `max` (if present), clock/rate config, and any range/smoothing params visible in the default inspector list.
- No `pattern_seq_inspector.cpp` exists today; retirement is preventative — don't add a parallel compact editor.
- Add or extend the thumbnail so it renders a miniature value-strip preview. Use the same value-to-pixel helper the editor uses (in `pattern_seq_editor_shared`) so the two surfaces can't diverge.

## Deferred / out of scope

- Bezier / spline interpolation between steps (the operator is stepwise today; adding curves is a behavior change, not an editor change).
- Modulation-source visualization on the plane.
- Pattern presets / banks.
- Variable step widths (non-uniform step durations).

## Open questions

- Does PatternSeq have a smoothing mode? If so, the editor should optionally overlay the smoothed curve. If not, don't fake it.
- Polarity handling for the y axis (bipolar → zero-centered axis). Match Sequencer's handling so the two feel consistent.
