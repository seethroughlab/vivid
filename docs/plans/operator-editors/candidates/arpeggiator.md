# Arpeggiator Editor

## Context

`Arpeggiator` (`operators/control/arpeggiator/`) has a mode selector, rate/gate/swing/latch controls, and — importantly — a **2-row × 8-column matrix** of per-step modifiers: `vel_0..7` (0..1 velocity scales) and `tr_0..7` (-24..+24 semitone transposes). The transpose row is hidden from the inspector by default (`mod_steps` gates the rendering); authoring per-step accent patterns requires hunting through hidden knobs.

A small editor surfaces the hidden matrix, shows the current mode's arp pattern visually, and lets the user drag velocity bars and click transposes directly.

## High-level approach

A single compact canvas: the 2×8 modifier matrix on the left, an arp-pattern diagram on the right that visualizes how the selected mode + octaves + rate unfold over time. The mode diagram is live — the current step pulses.

This is deliberately smaller than Sequencer / DrumSequencer. The editor's job is to unhide the matrix and give the mode a visual identity.

## Editor layout

```
┌────────────────────────────────────────────────────────────┐
│ top bar: mode · rate · octaves · gate · swing · latch      │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  vel   │ ▇ ▄ ▆ ▂ ▇ ▅ ▃ ▇ │    mode diagram              │
│         ───────────────                                    │
│  tr    │ 0 +7 0 -5 0 0 +12 0 │    (live step pulse)      │
│                                                            │
├────────────────────────────────────────────────────────────┤
│ side panel (right ~220px): cursor cell readout + reset     │
└────────────────────────────────────────────────────────────┘
```

Default window ~760×360; min ~600×280.

## Interactions

### Mouse
- Click + drag a velocity cell vertically → set `vel_N` (0..1).
- Click a transpose cell → focus; then use arrow up/down or `[`/`]` to nudge semitone.
- Scroll on a transpose cell → nudge semitone.
- Shift+click extends selection across multiple cells (reuses shared helpers).

### Keyboard
- Arrows: move cursor within the 2×8 grid.
- Enter / digits: type a new value (0..9 → `vel_N` = digit/9; signed digits typed in transpose row → `tr_N`).
- `Delete`: reset cell to default (vel=1.0, tr=0).
- Cmd+C / Cmd+V: copy/paste rectangular selection.

### Live feedback
- Mode diagram on the right: renders the current arp pattern as a horizontal ribbon of note-step glyphs. Current step tinted. Reading `note` / `vel` outputs (scalar outputs of the operator) + mode info.

## Data model recap

From `operators/control/arpeggiator/arpeggiator_core.h`:
- `mode` (Up/Down/UpDown/DownUp/Random/Order/Converge/Diverge/RandomNoRepeat/OrderDown)
- `octaves` (1..4), `rate` (1/1..1/16T), `gate_length`, `swing`, `latch`
- `mod_steps` (1..8) — active length of the modifier pattern
- `vel_0..vel_7` — per-step velocity scale
- `tr_0..tr_7` — per-step semitone transpose
- `clock_source`, `midi_channel`
- Ports: `notes`/`velocities` (lane array input + output), `note`/`vel` (scalar output)

## Implementation

### Files
- `operators/control/arpeggiator/arpeggiator.cpp` — add `VIVID_EDITOR(Arpeggiator)`.
- `operators/control/arpeggiator/arpeggiator_core.h` — add `editor_metadata()` / `draw_editor()` declarations and editor state.
- `operators/control/arpeggiator/arpeggiator_editor.cpp` — new.
- `operators/control/arpeggiator/arpeggiator_editor_shared.{h,cpp}` — new; pure-logic helpers (grid hit-test, selection rect, arp pattern generation for the diagram).
- `cmake/operators.cmake` — add new sources to the arpeggiator target.

### State on the core
- `editor_cursor_col_` (0..7), `editor_cursor_row_` (0=vel, 1=tr)
- `editor_selection_anchor_col_`, `editor_selection_anchor_row_`
- `editor_selection_` rect
- `selection_clipboard_` (2×N matrix of floats)

### Shared-helpers reuse
- Rectangle selection (from DrumSequencer shared).
- Rectangular copy/paste.
- Digit-prefix value entry.

### Tests
- `tests/operators/test_arpeggiator_editor_helpers.cpp` — cell hit-test, clipboard round-trip, mode pattern generation for each of the 10 modes matches the operator's `compute()` reference output.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: dedicated editor is the only interactive authoring surface; inspector becomes passive preview + "Open Editor" button.

- No `arpeggiator_inspector.cpp` exists today; the retirement is about preventing a mini matrix from being painted into the inspector as a parallel editing surface.
- Mark all 16 per-step modifier params (`vel_0..7`, `tr_0..7`) as `VIVID_DISPLAY_HIDDEN` in `collect_params`. The transpose row is already hidden in places; make this uniform. The 2×8 matrix is editor-only.
- Keep `mode`, `octaves`, `rate`, `gate_length`, `swing`, `latch`, `mod_steps`, `clock_source`, `midi_channel` visible — these are useful quick scrubs from the inspector.
- If there's no thumbnail today, add a small mode-diagram thumbnail that renders the arp pattern as a horizontal ribbon of step glyphs (reusing whatever mode helper the editor uses, so the preview can't drift).

## Deferred / out of scope

- Per-step gate independently from `gate_length`.
- Pattern length beyond 8 steps.
- External-chord ghost display (the editor shows modifier values, not the live chord being fed in).
- Custom modes / user-programmable pattern. Modes stay fixed.
- Multi-pattern banks.

## Open questions

- The mode diagram requires replaying the arp pattern generator without a live runtime. Factor that generator out of `compute()` into `arpeggiator_editor_shared` (or into `operators/shared/sequencer/` if general enough) so the diagram doesn't drift from playback behavior.
- `mod_steps` shrink: selection and cursor must clamp. Shared helper.
- Should the diagram show note pitches, or just abstract step positions? Abstract positions — the operator works on whatever chord is fed in, so absolute pitch isn't known.
