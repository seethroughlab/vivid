# Tracker Editor

## Context

`Tracker` (`operators/control/tracker/`) is a classic multi-channel tracker-style pattern sequencer. Up to 8 channels × 64 patterns × N rows per pattern, each cell carrying note + velocity + effect columns. The existing custom inspector (`tracker_inspector.cpp`) already paints a compact pattern grid, but the inspector surface is too small for real editing: only a handful of rows fit vertically, column headers compete with other inspector content, and keyboard-driven entry (the tracker's natural input mode) collides with the rest of the node-graph sidebar.

This is the most ambitious editor in the Tier-1 set — if the shared helpers survive Tracker, they're validated for every other grid operator.

## High-level approach

A full-canvas tracker pattern grid: rows down, channels across, columns within each channel for note / velocity / effect. The metaphor is the pattern editor from Protracker / Renoise / FastTracker — a spreadsheet that scrolls vertically, with a cursor that moves cell-to-cell and accepts immediate keyboard input. Pattern and channel selectors live above the grid.

This editor is keyboard-first. The mouse is a fallback for cell selection and scrolling; note/velocity/effect entry should be typeable as in any tracker.

## Editor layout

```
┌──────────────────────────────────────────────────────────────────┐
│ top bar: pattern N/64  │ channel mute mask  │ speed/rate │ legend │
├──────────────────────────────────────────────────────────────────┤
│  row# │ ch0: note vel fx │ ch1: note vel fx │ ... │ ch7: ...     │
│  ───────────────────────────────────────────────────────────────  │
│   00  │ C-4  80  ---     │ ---  --  ---     │ ... │ ...          │
│   01  │ ---  --  0F2     │ E-4  7F  ---     │ ... │ ...          │
│   02  │ --- playhead-----│ ------------------│ ... │ ...          │ ← current row
│   03  │ ...                                                       │
│   ...                                                             │
│                                                                   │
├──────────────────────────────────────────────────────────────────┤
│ side panel: editing mode · step · oct · current cell info         │
└──────────────────────────────────────────────────────────────────┘
```

Default window ~1200×720; min ~900×500. The row grid is the whole center region, fixed-width monospace columns.

## Interactions

### Keyboard — the primary input
- Note entry: piano-row keys (`z`,`s`,`x`,`d`,`c`,`v`,`g`,`b`,`h`,`n`,`j`,`m`) for an octave, `,` ends it. Uppercase / shift bumps octave.
- Velocity column: digits 0–F (hex) type into the two-char velocity field.
- Effect column: three chars, first alphanumeric (effect id), next two hex (value).
- `Delete` / `.`: clear the current cell (note off; or `---`).
- Arrow keys: move cursor row/column.
- Tab / shift-tab: jump across channels.
- `+` / `-`: next / previous pattern.
- `Cmd+C` / `Cmd+V`: copy/paste row range (selection via Shift+arrows).
- `Cmd+Z`: (covered by existing graph undo — just ensure every cell edit is a single command).

### Mouse
- Click cell → cursor. Shift+click extends selection to a row range.
- Wheel → scroll pattern.
- Double-click column header → solo channel (bit in `mute_mask`).
- Drag column header → rearrange channels? **Out of scope v1.**

### Live feedback
- Playhead row (the `current_row_` state already on the core) drawn as a highlighted row band.
- `edit_pattern` param determines which pattern is painted. The playhead may be on a different pattern mid-playback; show both if so.

## Data model recap

From `operators/control/tracker/tracker_core.h`:
- `edit_pattern` (0..63), `edit_channel` (0..7), `mute_mask` (0..255)
- `pattern_data` — text value holding the full pattern data (serialized)
- `speed` (1..16 ticks/row), `rate` (1/1..1/32T)
- `current_row_` and `ticks_per_row_` stay (used by `compute()`). Remove `insp_cursor_row_` and `insp_scroll_row_` — they served the retired inspector. Editor introduces its own `editor_cursor_row_` / `editor_cursor_col_` / `editor_scroll_row_`.

`pattern_data` is the ground truth. The editor parses it on every frame (or caches on change), edits cells in a mutable representation, and serializes back on every edit. `sync_pattern_data()` already exists on the core — the editor calls it after every mutation.

## Implementation

### Files
- `operators/control/tracker/tracker.cpp` — add `VIVID_EDITOR(Tracker)`.
- `operators/control/tracker/tracker_core.h` — add `editor_metadata()`, `draw_editor()`, and editor state fields.
- `operators/control/tracker/tracker_editor.cpp` — new; grid painting, input, serialization bridge.
- `operators/control/tracker/tracker_editor_shared.{h,cpp}` — new; pure-logic helpers: cell hit-test, keyboard-to-note mapping, selection range, copy/paste block.
- `operators/control/tracker/tracker_inspector.cpp` — **delete** (mirrors DrumSequencer retiring `drum_sequencer_inspector.cpp` in phase 4). The inspector's interactive pattern grid is superseded by the editor; the inspector becomes a passive preview + "Open Editor" button. Drop the source from `cmake/operators.cmake`'s `tracker_au` target too.
- `cmake/operators.cmake` — add the new sources to the `tracker_au` target.

### State on the core
- `editor_cursor_row_`, `editor_cursor_channel_`, `editor_cursor_field_` (0=note, 1=velocity, 2=effect)
- `editor_cursor_effect_char_` (0..2 within the three-char effect slot)
- `editor_scroll_row_` (topmost row visible)
- `editor_selection_row_lo_`, `editor_selection_row_hi_`
- `editor_row_clipboard_` (parsed rows, not raw text — simpler to paste across patterns of different widths)
- `editor_octave_` (0..8, shifts piano-row entry)

### Shared-helpers reuse
- Rectangle selection math (from DrumSequencer shared).
- Keyboard dispatch (direction keys + shortcuts).
- **Not** clipboard — tracker rows are not cell matrices; implement row-range clipboard locally.

### Tests
- `tests/operators/test_tracker_editor_helpers.cpp` — keyboard-to-note mapping, row-range clipboard serialization, cursor clamping on pattern width change.
- `tests/operators/test_tracker_editor.cpp` — end-to-end input → `pattern_data` round-trip.

## Inspector retirement

Mirror DrumSequencer's phase-4 move: delete the custom inspector, leave the inspector as a passive preview + "Open Editor" button.

- Delete `operators/control/tracker/tracker_inspector.cpp` and remove it from the `tracker_au` target in `cmake/operators.cmake`.
- Delete the `insp_*` state fields on `TrackerCore`; they served only the deleted paint code.
- Ensure `pattern_data` (the giant text blob) is marked `VIVID_DISPLAY_HIDDEN` in `collect_params` — it's unreadable as a text field and authoring happens exclusively in the editor.
- Keep `rate`, `speed`, `base_channel`, `channel_mode`, `clock_source`, `edit_pattern`, `edit_channel`, `mute_mask` visible in the default param list — those are useful scrubs from the inspector.
- The existing `draw_thumbnail` (if present) provides the passive preview. Enhance it to show a miniature pattern grid if it's currently thin.

## Deferred / out of scope

- Channel rearrangement via header drag.
- Per-channel instrument / sample assignment (Tracker doesn't track this today; it's a pattern-only operator).
- Block loop / pattern loop playback within the editor.
- Undo granularity finer than what the host graph-edit undo provides.
- Column-group collapsing (hide velocity or effect columns).

## Open questions

- How wide should each channel column be? Fixed (monospace) is simplest; responsive sizing based on window width would stretch unnecessarily. Fixed for v1.
- What happens on `edit_pattern` change mid-editing? Cursor stays at same row if it exists in the new pattern; otherwise clamp.
- Should the editor expose a "follow playhead" toggle? Yes — soft default on, user-togglable via keyboard. Defer the toggle to v2 if it bloats v1.
