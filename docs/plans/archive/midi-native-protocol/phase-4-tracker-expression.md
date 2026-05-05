# Phase 4 — Tracker expression authoring UX

**Status**: complete. Tracker now authors per-note pitch_bend / pressure / timbre as anchor cells with linear interpolation between anchors; playback emits native PITCH_BEND/PRESSURE/TIMBRE events; `WavetableLayer` consumes pressure (→ amplitude) and timbre (→ wavetable position); and `FX_TONE_PORTA` was migrated off the legacy `current_pitch` lane broadcast onto canonical PITCH_BEND emission.

**See also**: [README.md](README.md) for the migration overview and [phase-1-wire-format.md](phase-1-wire-format.md) for the transport contract this UI targets.

## Resolved

The four open product choices listed below were resolved during implementation:

1. **Single FX column vs. richer automation lanes** → **Rich route.** Each pattern carries an `expression_lane_mask` bitmask with bits `kLanePb`/`kLanePr`/`kLaneTb`. The mask is toggled per pattern via `Cmd+Shift+P/R/T` (the cursor skips hidden lanes and snaps back to `Note` if the lane it sits on is hidden). When visible, the lane renders as a 4-character column in the channel ribbon next to `Note`/`Velocity`/`Effect`.
2. **Per-cell scalar values vs. anchored/interpolated curves** → **Anchored cells with linear interpolation in tick space.** A cell whose lane field equals `kExprEmpty` (`INT16_MIN`) is "no anchor"; between anchors, `tracker_expression::interpolate()` produces the per-tick value. Before the first anchor in a pattern the lane is unset (no events emitted). After the last anchor the lane holds.
3. **How expression is shown in the grid without overwhelming note entry** → Lanes default hidden per-pattern; existing patterns load identically (zero-byte difference). The columns only appear when the user opts in via the toggle keybinds, so clean note entry remains the default UX.
4. **Clipboard semantics** → Whole-cell copy/paste in `tracker_editor_shared` already memcpys `TrackerCell` structs, so the new fields ride along automatically. Pasting into a pattern with a different `expression_lane_mask` paints all fields anyway — the data is preserved even when the lanes are hidden.

### Audible-impact wiring (the gate this phase had to pass)

- `WavetableLayer` reads `slot.pressure` to scale the per-voice envelope (`1 + pressure_to_amp_depth * pressure`) and `slot.timbre` to offset the wavetable position (`timbre * timbre_to_position_depth`). Both new params (`pressure_to_amp`, `timbre_to_position`) default to `0.5`/`0.5` so a fresh patch immediately responds to expression.
- `NoteBreakout` exposes three new advanced LANE_ARRAY OUTPUT ports — `voice_pitch_bend`, `voice_pressure`, `voice_timbre` — alongside the existing four. `vivid_sequencers::emit_voice_breakouts_from_sorted` was extended to populate the additional lanes from `slot.pitch_bend_semis`/`slot.pressure`/`slot.timbre`.
- `FX_TONE_PORTA` (and the porta-up/porta-down family) emit incremental PITCH_BEND events per tick on top of the existing pitch-slide arithmetic, so MidiInput-authored and Tracker-authored bends interoperate at the same MPE-style ±48-semi raw int16 scale.

### Demo

[`graphs/audio/tracker_expression_demo.json`](../../../graphs/audio/tracker_expression_demo.json) — Clock(96 BPM) → Tracker(rate=1/16, speed=6) → WavetableLayer → audio_out. The 8-row pattern walks C4→E4→G4→C5 with ±~6-semi pitch wobble, a pressure swell that peaks on row 4, and a timbre sweep that climbs through row 7. End-to-end coverage: `tests/integration/test_tracker_expression_demo.cpp` renders ~1.6s, asserts non-silent output, and asserts the mid-pattern segment has a measurably different RMS and brightness profile than the pre-pressure segment.

## Goal

Extend the Tracker editor so users can author per-note pitch bend, pressure, and timbre intentionally inside the native note model, without making Tracker expression authoring part of the critical path for the core migration.

## Why deferred

1. The important architectural work is the transport + composability model. Tracker UX should build on top of a stable internal contract, not define it.
2. The Tracker editor is a substantial dedicated-editor codebase, so expression authoring is real product/UI work rather than a small transport follow-up.
3. Tracker does **not** get credit for expression authoring until playback actually emits native per-note expression events. Existing note-on/note-off output and internal pitch bookkeeping are not enough.

## Preconditions

Do not schedule this phase until:

- Phase 1's native note transport is live
- synths and/or `NoteBreakout` expose the stable per-note surfaces needed for validation
- the team is happy with the names and semantics of pitch bend, pressure, and timbre in the internal note protocol

## Scope when it starts

This phase should decide and implement a concrete Tracker authoring surface for:

- per-note pitch bend
- per-note pressure
- per-note timbre

Open product choices still to resolve before implementation:

1. **Single FX column vs. richer automation lanes**
2. **Per-cell scalar values vs. anchored/interpolated curves**
3. **How expression is shown in the grid without overwhelming note entry**
4. **Clipboard semantics for notes + expression**

## Likely implementation directions

Two viable routes remain:

### Compact route

- reuse the FX column
- add explicit codes for pitch/pressure/timbre behaviors
- lower UI cost, lower authoring depth

### Rich route

- add dedicated automation lanes or a split-pane editor
- support clearer editing and visualization
- higher UI cost, better long-term expressive authoring

Either route must target the native-note event model rather than legacy MIDI-only semantics.

## Tests and acceptance criteria

When Phase 4 is scheduled, require:

- Tracker data round-trip coverage for the chosen expression representation
- playback tests proving authored pitch/pressure/timbre become real per-note native events
- editor interaction tests for entry, editing, selection, copy/paste, and deletion
- at least one checked-in demo/preset pattern that demonstrates the new expression authoring workflow clearly

## Out of scope

- Making Tracker authoring a blocker for the Phase 1-3 migration
- Claiming existing `FX_TONE_PORTA` behavior already solves per-note expression export
- Expanding into MIDI 2.0 export, live MPE recording, or broader performance-capture workflows unless they become explicit follow-up phases
