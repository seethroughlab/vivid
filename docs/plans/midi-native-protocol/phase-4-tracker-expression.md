# Phase 4 — Tracker expression authoring UX

**Status**: deferred. The native note transport and composable synth breakout model come first; richer Tracker authoring should land only after those contracts are settled.

**See also**: [README.md](README.md) for the migration overview and [phase-1-wire-format.md](phase-1-wire-format.md) for the transport contract this UI will eventually target.

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
