# Sequencer Editor

## Context

`Sequencer` (`operators/control/sequencer/`) is a 32-step value-and-gate sequencer that drives any numeric parameter or scalar input. Today it exposes all 64 step params (`step_value[0..31]` + `step_gate[0..31]`) as a flat list of knobs and toggles in the inspector. There is no grid, no current-step indicator, no drag-to-paint. Authoring a pattern requires clicking through 64 separate controls.

This is the *simplest* viable Tier-1 job and the right shakedown for the shared-editor helpers after DrumSequencer.

## High-level approach

A two-lane vertical-bar sequencer. The main canvas is a grid: N columns (one per active step, respecting `steps`) × 2 rows — the value row as vertical faders, the gate row as on/off cells. The current step is highlighted live. Click-drag paints values across cells; click toggles gates. All familiar step-sequencer muscle memory applies.

The editor is deliberately smaller than DrumSequencer's (no pattern banks, no probability, no roll, no velocity — just value + gate), so it's the forcing function for the shared helpers without drowning them in features.

## Editor layout

```
┌───────────────────────────────────────────────────────────────────┐
│ top bar: steps scrub │ range hint │ polarity │ keyboard legend    │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│   value row (vertical faders, 0..1 normalized)                    │
│                                                                   │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│   gate row (1-cell toggles, same column width)                    │
├───────────────────────────────────────────────────────────────────┤
│  side panel (right ~220px): cursor step readout + selection info  │
└───────────────────────────────────────────────────────────────────┘
```

Default window ~900×420; min ~600×300.

## Interactions

### Mouse
- Click a value cell → anchor + cursor on that cell; set value by vertical position.
- Drag across value cells → paint (each cell takes the value at the mouse y at the moment the cursor is over it).
- Click a gate cell → toggle; drag continues the first cell's new state.
- Shift+click extends selection rect (reuses DrumSequencer's anchor/cursor model).

### Keyboard
- Arrow keys: move cursor within active row.
- Tab / shift-tab: switch between value and gate rows.
- Space: toggle gate at cursor (in either row).
- Enter: set value cell to 1.0 (or clear to 0 if already 1.0).
- `0`–`9`: set value to `digit / 9` across selection.
- Cmd+C / Cmd+V: copy/paste rectangular selection (2 values × N cells).

### Live feedback
- Current step column tinted darker across both rows while the sequencer runs.
- The active-step tint reads `step` output at frame cadence via `VividEditorContext`.

## Data model recap

From `operators/control/sequencer/sequencer_core.h`:
- `steps` — 1..32 (kMaxSteps)
- `step_value[32]` — float 0..1
- `step_gate[32]` — float 0..1 (threshold 0.5)
- Rate / amplitude / offset / polarity / midi_channel — not part of the editor; they stay as visible params in the default inspector list.

## Implementation

### Files
- `operators/control/sequencer/sequencer.cpp` — add `VIVID_EDITOR(Sequencer)` next to `VIVID_REGISTER`.
- `operators/control/sequencer/sequencer_core.h` — add `editor_metadata()` / `draw_editor()` declarations and editor state members (cursor, anchor, selection rect, clipboard).
- `operators/control/sequencer/sequencer_editor.cpp` — new; window painting + input.
- `operators/control/sequencer/sequencer_editor_shared.{h,cpp}` — new; pure-logic helpers (hit-test, selection rect, keyboard dispatch) mirroring DrumSequencer's split so tests can exercise them headless. If the shared-helpers extraction (see README) has landed by then, re-export from `operators/shared/editor_ui/` and keep this file thin.
- `cmake/operators.cmake` — add the new source files to the `sequencer` target. (Verify the operator target name; DrumSequencer is `drum_sequencer_au` because it's audio-rate. Sequencer appears to be frame-rate — adjust accordingly.)

### State on the core
- `editor_cursor_step_`, `editor_cursor_row_` (0=value, 1=gate)
- `editor_selection_anchor_step_`, `editor_selection_anchor_row_`
- `editor_selection_` (rect)
- `selection_clipboard_` (2×N float values)

### Reuse
Use the DrumSequencer shared helpers for:
- Anchor/cursor selection rectangle math
- Rectangular copy/paste
- Shift+arrow selection extension
- Digit-prefix value entry (reusing the `P + digit` pattern for value entry on `0`..`9`)

### Tests
- `tests/operators/test_sequencer_editor.cpp` — keyboard/mouse workflows via the shared helpers (no live runtime).
- Extend `test_audio_sequencer_graph.cpp` if behavior coverage is thin.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: the dedicated editor becomes the **only** interactive authoring surface. The inspector keeps a passive preview (the existing `draw_thumbnail` — enhance if thin) plus the host's "Open Editor" button, nothing more.

- Change the `display_hint` on `step_value[0..31]` and `step_gate[0..31]` in `collect_params` (`sequencer_core.h:162,167`) from `VIVID_DISPLAY_STEP_SEQ` to `VIVID_DISPLAY_HIDDEN`. The compact step-seq widget disappears; the 64 cell params stop cluttering the default list.
- Also hide `steps` (currently `VIVID_DISPLAY_STEP_SEQ`) — or keep it visible as a simple int scrubber, since it's useful from the inspector too. Recommend: keep `steps` visible as a plain knob, hide the grid.
- Remove the `visible_when_eq(..., source, 0)` conditionals on the cell params since they won't render at all anyway. Keep the `source` toggle visible so users can still switch internal/external from the inspector.
- No inspector paint code exists for Sequencer today (no `sequencer_inspector.cpp`), so there's nothing to delete — the cleanup is entirely in `collect_params`.

## Deferred / out of scope

- Multi-pattern banks (A/B). Sequencer has no such concept today; don't invent one.
- Probability / ratcheting. Not in Sequencer's data model.
- Curve interpolation between steps. Current behavior is stepwise; adding curves would be a core behavior change, not an editor change.
- Lane-spreads. If `step_value` grows lane dimensions later, revisit; out of scope here.

## Open questions

- Should bipolar polarity render the fader as centered-zero rather than bottom-zero? Probably yes — read `polarity` param and flip the rendering. Param stays visible in the default inspector list; editor responds to it.
- `steps` scrub at the top of the editor: cursor and selection must clamp as `steps` shrinks. Handle in the shared helpers if they don't already.
