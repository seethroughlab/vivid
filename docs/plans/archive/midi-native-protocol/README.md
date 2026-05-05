# Native note protocol + polyphonic composability

## Status

| Phase | Title | Status | Notes |
|---|---|---|---|
| 1 | Native note protocol + emitter/synth migration | complete | Native note IDs and note-driven synth internals shipped. |
| 2 | Synth breakouts + composable poly control replacement | complete | Standardized advanced breakout lanes + `NoteBreakout` helper shipped. |
| 3 | Graph migration + `VoiceAllocator` removal | complete | Allocator removed; advanced graphs route through synth breakouts or `NoteBreakout`. |
| 4 | Tracker expression authoring UX | complete | Per-cell `pb`/`pr`/`tb` lanes; FX_TONE_PORTA emits PITCH_BEND; WavetableLayer consumes pressure (→ amplitude) and timbre (→ wavetable position). |
| 5 | Cleanup + universal expression coverage | complete | Dead headers + `VividMidiBuffer` deleted; `vivid::VoiceAllocator<N>` template renamed to `VoiceTable<N>`; legacy LANE_ARRAY note outputs gone from MidiInput / Tracker / NotePattern / ChordProgression; Arpeggiator native-rewrite (notes_in/notes_out + snapshot-and-bake expression); pressure_to_amp + per-synth timbre wired into all 8 voice synths. |

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

### Phase 4 — Tracker expression authoring UX
**[phase-4-tracker-expression.md](phase-4-tracker-expression.md)**

Tracker patterns now carry per-cell `pb`/`pr`/`tb` anchor lanes (toggleable per pattern via `Cmd+Shift+P/R/T`); the playback path linearly interpolates between anchors and emits native PITCH_BEND/PRESSURE/TIMBRE events. `FX_TONE_PORTA` was migrated from the legacy `current_pitch` lane broadcast to PITCH_BEND emission so all per-note pitch movement now flows through one canonical path. WavetableLayer is the first synth that audibly consumes the new lanes (pressure → amplitude, timbre → wavetable position), and `NoteBreakout` exposes `voice_pitch_bend` / `voice_pressure` / `voice_timbre` lane outputs for downstream graph routing.

### Phase 5 — Cleanup + universal expression coverage
**[phase-5-cleanup-and-universal-expression.md](phase-5-cleanup-and-universal-expression.md)**

Closes the seams left by Phases 1–4. Pre-migration dead code is gone (`phase_to_midi.h`, `midi_helpers.h`, `VividMidiBuffer`, the stale `phase_to_midi.json` site doc); the internal `vivid::VoiceAllocator<N>` template is renamed to `vivid::VoiceTable<N>` so it doesn't collide with the deleted graph operator's name; legacy LANE_ARRAY note outputs (`notes`/`velocities`/`gates`/`pitch_bends`/`pressures`/`slides`/`expressions`/`channels`) are removed from MidiInput, Tracker, NotePattern, and ChordProgression; the Arpeggiator gets a full native rewrite (`notes_in` → held-set keyed by note_id → `notes_out` with snapshot-and-bake PRESSURE/TIMBRE on each emitted step); and pressure_to_amp + per-synth timbre mappings are wired into all 8 voice synths (FmSynth/Sampler/SP404/Slicer in core; AnalogOsc/WavetableOsc/SubOsc/NoiseLayer in vivid-wavetable). After Phase 5 a newcomer reading any operator sees one obvious surface for note routing and one obvious knob for each per-note expression dimension.

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
11. **Phase 4 product choices (resolved during implementation).** Tracker authoring uses the rich route — dedicated per-channel anchor lanes (`pb` / `pr` / `tb`) toggleable per pattern with linear interpolation between anchors. The audible-impact gate is satisfied by wiring `WavetableLayer` to read `slot.pressure` (amplitude scale) and `slot.timbre` (wavetable position offset) plus exposing `voice_pitch_bend` / `voice_pressure` / `voice_timbre` breakout lanes on `NoteBreakout`. `FX_TONE_PORTA` emits incremental PITCH_BEND events on top of the existing per-tick interpolation arithmetic, so the legacy lane broadcast is no longer the canonical pitch path. Demo: [`graphs/audio/tracker_expression_demo.json`](../../../graphs/audio/tracker_expression_demo.json).
12. **Phase 5 per-synth expression mappings (resolved during implementation).** Pressure → amplitude is uniform across all 8 voice synths via a `pressure_to_amp` param (range 0..1, default 0.5; voice gain scales by `1 + depth × slot.pressure`). Timbre maps per synth to the most natural spectral knob: FmSynth `timbre_to_mod_index`, AnalogOsc `timbre_to_pwm`, WavetableOsc `timbre_to_position`, SubOsc `timbre_to_level`, NoiseLayer `timbre_to_tone`, Sampler/SP404/Slicer `timbre_to_pitch` (semitones). Drum operators stay opt-out (percussive single-shots have no musical use for pressure/timbre). Arpeggiator's snapshot-and-bake design samples held-source PRESSURE/TIMBRE at step-fire time and emits them on the new step's note_id — live expression updates from the input do not propagate (route through `NoteBreakout` if you need that).

## Glossary

- **native note protocol** — vivid's internal note/event transport, built around stable `note_id`s and per-note expression rather than MIDI 1.0 channel semantics.
- **note_id** — uint64 allocated by the emitter at note-on, stable through note-off and all intermediate expression updates. Re-triggering the same pitch produces a fresh id.
- **synth breakouts** — the standardized advanced outputs on voice synths (`voices_out`, `voice_ids`, `voice_gates`, `voice_velocities`, `voice_freqs`) that preserve graph-level per-voice composition without making them the default user path.
- **`NoteBreakout`** — a lightweight helper operator that consumes `notes_in` and exposes the non-audio `voice_*` lanes when multiple downstream consumers need shared per-note control state, without paying synth/audio-render cost.
- **allocator-era wiring** — the legacy `VoiceAllocator -> frequencies/gates/velocities/lane_ids` graph pattern (removed in Phase 3 / Phase 5; described here for historical context).
- **`vivid::VoiceTable<N>`** — the internal C++ template (formerly `vivid::VoiceAllocator<N>`, renamed in Phase 5 PR1) used by every voice synth + `NoteBreakout` to track per-note slots keyed by `note_id`. Not graph-visible; lives in `src/operator_api/voice_table.h`.

## Related references

- Native note transport: `src/operator_api/note_types.h` (`VividNoteBuffer`, `VividNoteEvent`)
- Internal voice table: `src/operator_api/voice_table.h`
- Emission + breakout helpers: `operators/shared/sequencer/note_helpers.h`, `operators/shared/sequencer/voice_breakouts.h`
- Current alias table: `src/runtime/graph/operator_aliases.cpp`
- Operator-editors plan (parallel multi-phase plan, similar shape): `docs/plans/operator-editors/README.md`
