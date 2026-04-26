# Native note protocol + polyphonic composability

## Status

| Phase | Title | Status | Notes |
|---|---|---|---|
| 1 | Native note protocol + emitter/synth migration | planned | Introduces native note IDs and note-driven synth internals. |
| 2 | Synth breakouts + composable poly control replacement | planned | Keeps the simple synth path while preserving per-voice graph composition. |
| 3 | Graph migration + `VoiceAllocator` removal | planned | Deletes allocator-era wiring only after the replacement surface is proven. |
| 4 | Tracker expression authoring UX | deferred | Valuable follow-up, but not on the critical path for the transport/composability migration. |

## Context

Today vivid has two overlapping polyphonic stories:

1. **Direct synth playback** via note streams (`notes_out -> notes_in`, or today's `midi_out -> midi_in` during migration) that are easy to understand but currently limited in expressive identity.
2. **Allocator-era lane graphs** (`VoiceAllocator -> frequencies/gates/velocities/lane_ids`) that preserve per-voice composition but make the default user path harder to read and teach.

That split makes vivid harder than it needs to be for new users and forces every voice synth to carry two mental models. The right long-term direction is:

- **One obvious note path** for new users: `note source -> synth -> audio_out`
- **No loss of per-voice composability** for advanced users
- **Native internal note semantics** inside the graph, rather than MIDI 1.0 channel semantics
- **MIDI/MPE as adapter boundaries**, not the internal graph contract

The plan below keeps vivid interesting by treating composability as a first-class design goal, while still making the synth path the default story.

## Phase summary

### Phase 1 — Native note protocol + emitter/synth migration
**[phase-1-wire-format.md](phase-1-wire-format.md)**

Introduce a native internal note transport built around stable `note_id`s and per-note expression events. Migrate note emitters to allocate and preserve `note_id`s, and migrate voice synth internals to key voice state by `note_id`. This phase does **not** remove any graph-visible polyphonic control surfaces yet.

### Phase 2 — Synth breakouts + composable poly control replacement
**[phase-2-synth-breakouts-and-poly-composability.md](phase-2-synth-breakouts-and-poly-composability.md)**

Standardize advanced synth breakout outputs (`voices_out`, `voice_ids`, `voice_gates`, `voice_velocities`, and `voice_freqs`) so per-voice patching remains possible without keeping allocator-era note inputs as the canonical model. Add `NoteBreakout` as the shared-control helper for note streams that need to feed multiple downstream operators.

### Phase 3 — Graph migration + `VoiceAllocator` removal
**[phase-3-graph-migration-and-allocator-removal.md](phase-3-graph-migration-and-allocator-removal.md)**

Migrate checked-in presets and demo graphs to the new simple/advanced patterns, then remove `VoiceAllocator` and the old synth lane-note inputs once the replacement surface has been verified across envelopes, filters, mixers, and explicit per-voice processing.

### Phase 4 — Tracker expression authoring UX (deferred)
**[phase-4-tracker-expression.md](phase-4-tracker-expression.md)**

Build the dedicated Tracker authoring surface for pitch bend, pressure, and timbre editing. This remains deferred until the native note transport and composable synth breakout model are settled.

## Cross-cutting decisions

These apply to every phase. Don't reopen them inside individual phase plans without revising this index first.

1. **No backwards compatibility for pre-migration graphs or dylibs.** vivid is still alpha. We should optimize for the right long-term surface, not preserve old port names or wire types.
2. **Internal note transport is native.** Native-note operators should move toward `notes_in` / `notes_out` terminology rather than continuing to describe the internal graph contract as MIDI.
3. **Per-note identity is first-class.** Every per-note event carries a non-zero `note_id`, stable from note-on through note-off.
4. **Breakout alignment is by active-note order sorted by `note_id`.** The graph contract must not depend on synth-local slot order or stealing heuristics.
5. **Synths are the primary advanced breakout surface.** New users should not need a routing helper to get sound. Advanced users can reveal per-voice breakout ports when needed.
6. **Phase 2 must add a minimal advanced-port affordance.** The synth breakout surface should not land until the UI/descriptor layer can mark those ports as advanced/secondary rather than showing all of them as primary.
7. **Expression is native transport first, graph surface later.** Pressure, timbre, and raw pitch-bend remain part of the native note protocol, but they do not become standardized breakout lanes until a concrete downstream consumer earns them.
8. **`NoteBreakout` is the shared-control helper.** Use it when one native note stream needs to drive multiple downstream operators that consume non-audio per-voice state. It should be cheaper than instantiating a synth solely to obtain breakout lanes.
9. **`VoiceAllocator` is removed only after replacement surfaces exist.** At minimum that means `voice_ids`, `voice_gates`, `voice_velocities`, and `voice_freqs` are live and proven in migrated graphs.
10. **External MIDI/MPE lives at the boundary.** `MidiInput`, `MidiFilePlayer`, `MidiOutput`, and any future MPE-specific operators adapt between external MIDI semantics and the internal native-note contract.

## Glossary

- **native note protocol** — vivid's internal note/event transport, built around stable `note_id`s and per-note expression rather than MIDI 1.0 channel semantics.
- **note_id** — uint64 allocated by the emitter at note-on, stable through note-off and all intermediate expression updates. Re-triggering the same pitch produces a fresh id.
- **synth breakouts** — the standardized advanced outputs on voice synths (`voices_out`, `voice_ids`, `voice_gates`, `voice_velocities`, `voice_freqs`) that preserve graph-level per-voice composition without making them the default user path.
- **`NoteBreakout`** — a lightweight helper operator that consumes `notes_in` and exposes the non-audio `voice_*` lanes when multiple downstream consumers need shared per-note control state, without paying synth/audio-render cost.
- **allocator-era wiring** — the legacy `VoiceAllocator -> frequencies/gates/velocities/lane_ids` graph pattern that currently carries most advanced per-voice composition.

## Related references

- Current wire format: `src/operator_api/midi_types.h`
- Current slot helper: `src/operator_api/voice_allocator.h`
- Current MIDI helpers: `operators/shared/sequencer/midi_helpers.h`
- Current alias table: `src/runtime/graph/operator_aliases.cpp`
- Operator-editors plan (parallel multi-phase plan, similar shape): `docs/plans/operator-editors/README.md`
