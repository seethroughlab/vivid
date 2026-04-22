# Arpeggiator Editor

## Context

`Arpeggiator` (`operators/control/arpeggiator/`) has a mode selector, rate/gate/swing/latch controls, and a 2-row × 8-column matrix of per-step modifiers (`vel_0..7`, `tr_0..7`). The transpose row is hidden in the inspector; authoring per-step patterns requires hunting through knobs.

This editor is also a **feature expansion** — Xfer Records' Cthulhu is the inspiration. v1 adds one conceptually new authoring dimension (per-step Note Override) plus a per-step Gate lane, and widens the matrix from 8 to 16 steps. Future-feature candidates (Cthulhu's Harmony, Chord Mode, polymetric per-lane length, Note Output Prevention, scale-degree-aware Pitch, shape-based Note values, etc.) are catalogued in [arpeggiator-future.md](arpeggiator-future.md).

## High-level approach

Full-canvas grid: 4 lanes × 16 steps. The Note Override lane is the Cthulhu-inspired addition that lets each step individually pick a pool note, mute, or fall through to the global `mode`. Mode stays; it becomes the "default step value" for steps left at the sentinel.

This is DrumSequencer/Sequencer scale — bigger than the earlier "surface the hidden matrix" framing.

## Editor layout

```
┌──────────────────────────────────────────────────────────────────────┐
│ top bar: mode · rate · octaves · gate · swing · latch · mod_steps    │
├──────────────────────────────────────────────────────────────────────┤
│  note │ —  —  2  —  M  —  —  —  —  —  —  —  —  —  —  — │ side:     │
│  vel  │ ▇  ▄  ▆  ▂  ▇  ▅  ▃  ▇  ▇  ▇  ▇  ▇  ▇  ▇  ▇  ▇ │  cursor   │
│  tr   │ 0 +7  0 -5  0  0 +12 0  0  0  0  0  0  0  0  0 │  readout  │
│  gate │ ━━ ━━ ━━ ━━ ━━ ━━ ━━ ━━ ━━ ━━ ━━ ━━ ━━ ━━ ━━ ━━│  + mode   │
│                                                         │  diagram  │
└──────────────────────────────────────────────────────────────────────┘
```

Default window ~1000×420; min ~720×320.

## Interactions

Mirror the DrumSequencer / Sequencer / Tracker vocabulary already carried by the editor-UI toolkit.

### Mouse
- Click a cell → cursor there. Shift+click extends selection rect across rows/cols.
- Drag inside a cell vertically → adjust continuous-value lanes (vel, tr, gate) based on mouse-y-in-cell.
- Click on a Note Override cell → cycles `—` (follow) → `1..8` → `M` (mute) → `—`.

### Keyboard
- Arrows / Tab move cursor; Shift+arrow extends selection.
- Digits `0..9` set values per lane: vel → `digit/9`; transpose → nudge within ±24; gate → `digit/9`; note_override → `0..8` direct (9 = mute).
- Enter toggles at cursor (note_override cycles; others set max).
- Space clears selection to defaults.
- Delete resets cells to defaults.
- Cmd+C / Cmd+V copy/paste rectangular selection.

### Live feedback
- Side panel mode-diagram ribbon: renders which pool note the current mode picks per step, using the same `vivid_sequencers::arp_pattern_index` helper that `compute()` uses. Can't drift from playback.
- Current step highlighted across all 4 lanes via the operator's `step` output.

## Data model (v1)

From `operators/control/arpeggiator/arpeggiator_core.h`:

**Unchanged top-level (kept as-is):**
- `mode` (10 scan patterns), `octaves` (1..4), `rate` (9 divisions), `gate_length`, `swing`, `latch`
- `clock_source`, `midi_channel`
- Ports: `notes`/`velocities`/`gates` lane-array I/O, `note`/`vel`/`gate`/`step` scalar output, `midi_out` custom-ref

**Widened:**
- `mod_steps` — range `1..8` → `1..16` (default stays 8).
- `vel_0..vel_7` → `vel_0..vel_15`.
- `tr_0..tr_7` → `tr_0..tr_15`.

**New:**
- `note_override_0..15` — int `0..9`: `0` = follow `mode` (sentinel; preserves existing graphs), `1..8` = force pool index, `9` = mute step.
- `gt_0..15` — float `0..1`, default 1.0. Multiplies global `gate_length` at compute.

Param-index strategy: all existing indices (0..24) stay stable. New params append past existing ones. Graphs saved before this change load with all new params at their defaults (= no behavior change).

## Implementation

### Files
- `operators/control/arpeggiator/arpeggiator.cpp` — add `VIVID_EDITOR(Arpeggiator)`.
- `operators/control/arpeggiator/arpeggiator_core.h` — add `editor_metadata()` / `draw_editor()` declarations and editor state.
- `operators/control/arpeggiator/arpeggiator_editor.cpp` — new.
- `operators/control/arpeggiator/arpeggiator_editor_shared.{h,cpp}` — new; pure-logic helpers (grid hit-test, selection rect, arp pattern generation for the diagram).
- `cmake/operators.cmake` — add new sources to the arpeggiator target.

### State on the core
- `editor_cursor_step_` (0..15), `editor_cursor_row_` (0=note_override, 1=vel, 2=tr, 3=gate)
- `grid_state_` (`vivid::ui::GridState` — supplies anchor + drag semantics)
- `editor_selection_` (`editor_ui::Selection`)
- `selection_clipboard_` — 4-lane × 16-step float matrix plus a `has_content` flag

### Shared-helpers reuse
- `editor_ui::Selection` + `cursor_move` + `clamp_editor_state` — from `operators/shared/editor_ui/selection.h`.
- `vivid::ui::ui_step_grid` for click + drag + shift-extend (editor_ui.h).
- `vivid::editor_keys::*` — no local GLFW constants.
- `vivid_sequencers::arp_pattern_index` — the same mode-pattern resolver used by `compute()`, so the side-panel diagram can't drift.

### Tests
- `tests/operators/test_arpeggiator_editor_helpers.cpp` — param-index math, note-override decode (`0` → follow, `1..8` → pool index, `9` → mute), clamp on mod_steps shrink, selection clipboard round-trip.
- `tests/operators/test_arpeggiator_editor.cpp` — synthesized context e2e: backward-compat (default params behave as today's arp), note override forces specific pool index, mute silences a step, gate multiplier shortens effective gate, keyboard/mouse flows.

## Inspector retirement

Mirror DrumSequencer/Sequencer/Tracker phase-4: retire the `VIVID_INSPECTOR` path. Dedicated editor is the only interactive authoring surface.

- `arpeggiator.cpp`: swap `VIVID_INSPECTOR(Arpeggiator)` → `VIVID_EDITOR(Arpeggiator)`.
- `arpeggiator_core.h`: delete the `draw_inspector()` override (~140 lines of custom painting) and the `dragged_vel_`/`dragged_tr_` inspector-drag state fields.
- `collect_params`: mark every per-step param (`vel_N`, `tr_N`, `note_override_N`, `gt_N`) as `VIVID_DISPLAY_HIDDEN`. Keep `mode`, `octaves`, `rate`, `gate_length`, `swing`, `latch`, `mod_steps`, `clock_source`, `midi_channel` visible for quick scrubs from the inspector.
- `draw_thumbnail()` stays — it's the passive preview.

## Deferred / out of scope (v1)

The detailed future-features plan is in [arpeggiator-future.md](arpeggiator-future.md). Summary of what's explicitly deferred:

- Per-step **octave**, **probability**, **late** (microtiming).
- **Harmony** lane (second note per step) — polyphonic output.
- **Chord Mode** (whole-chord output per step) — polyphonic output.
- **Per-lane pattern length** + per-lane **Clock Div** — polymetric authoring.
- **Multiple pattern slots** (A/B to 12-slot like Cthulhu).
- **Note Output Prevention** — post-filter key-block.
- **Shape-based Note Override values** (up/down/fingered-top/etc. as per-step values).
- **Rand Sel** (Cthulhu's bidirectional per-step deviation on Note Override).
- **Pitch-with-scale-degrees** (requires chord-root analysis).
- **Position reset** marker, **Retrigger policy**, **Free-rate mode**.

v1 explicitly doesn't ship any of the above. The future doc ranks them by cost and value so whoever picks them up has a ready ordering.
