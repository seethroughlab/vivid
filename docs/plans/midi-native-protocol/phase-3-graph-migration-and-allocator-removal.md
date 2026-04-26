# Phase 3 — Graph migration + `VoiceAllocator` removal

**Status**: planned. Deletes allocator-era wiring only after the replacement surfaces are live and verified.

**See also**: [README.md](README.md), [phase-1-wire-format.md](phase-1-wire-format.md), and [phase-2-synth-breakouts-and-poly-composability.md](phase-2-synth-breakouts-and-poly-composability.md).

## Goal

Migrate checked-in presets and demo graphs to the new simple/advanced polyphony patterns, then remove `VoiceAllocator` and the old synth lane-note input path once the replacement model has been proven.

This phase is intentionally gated by evidence. It is where deletion happens, not where replacement surfaces are invented.

## Removal gate

`VoiceAllocator` can be removed only after migrated presets/demos prove all of the following:

- simple synth chains remain easy to read and patch
- envelopes still have stable per-voice triggering
- filters still support poly keytracking
- `VoiceMixer` / `VoiceDrive` still support explicit per-voice processing
- shared note streams can still drive multiple synths/operators without losing coherent per-note control state

If those conditions are not met, the plan should pause here and fix the replacement surface rather than forcing the deletion through.

## Migration targets

Rewrite checked-in content into two canonical patterns:

### Simple pattern

`note source -> synth -> audio_out`

Use this wherever the graph does not need explicit per-voice processing or shared-control fanout.

### Advanced pattern

Use synth breakouts and/or `NoteBreakout` where per-voice composition is required:

- `voice_gates -> EnvelopeAu/gate` and `voice_ids -> EnvelopeAu/lane_ids`
- `voice_freqs -> Filter/DualFilter`
- `voices_out -> VoiceMixer/VoiceDrive`
- `NoteBreakout` for shared per-note control state across multiple downstream consumers

The migration should prefer the simple path whenever it preserves the musical intent of the checked-in graph.

## Removals

Once the migration gate is satisfied, remove:

- the `VoiceAllocator` operator
- synth lane-note input ports:
  - `frequencies`
  - `gates`
  - `velocities`
  - `lane_ids`
  - lane note-routing modulation ports such as lane `pitch_mod` / `position_mod` / `warp_mod`
- docs, recipes, tests, and examples that present allocator-era wiring as canonical

Deletion expectations:

- remove alias entries only when the aliased targets are actually gone
- keep unrelated alias cleanup out of scope unless it materially blocks the migration
- keep the internal slot allocator helper if synth implementations still need it

## Graph/doc/test updates

Update checked-in content and supporting docs so the repo consistently teaches the new model:

- presets and demos use either the simple synth path or the new breakout-based advanced path
- operator docs and recipes no longer recommend `VoiceAllocator` as the default polyphony story
- surface-contract tests pin the new breakout names instead of allocator-era note inputs
- integration tests assert that no checked-in graph still depends on `VoiceAllocator`

## Tests and acceptance criteria

Required test coverage:

- all migrated checked-in graphs load cleanly
- representative presets produce audio after migration
- no checked-in graph still references `VoiceAllocator`
- no checked-in graph still depends on synth lane-note input ports
- integration tests cover both:
  - simple synth chains
  - advanced breakout/`NoteBreakout` polyphonic graphs
- migrated advanced graphs use current downstream port names (`gate`, `lane_ids`, etc.) rather than introducing extra renames where they do not buy clarity

Manual sanity:

1. Open several migrated preset graphs and confirm they read more clearly than the allocator-era versions.
2. Verify at least one advanced graph still demonstrates explicit per-voice composition.
3. Confirm `VoiceAllocator` no longer appears in package manifests, graph docs, or recommended patch recipes.

## Risks

- **Migration script edge cases.** Some graphs will likely need manual cleanup rather than a blind rewrite.
- **Composability regressions hidden by audio output.** “Graph makes sound” is not enough; the tests must verify stable per-voice control behavior.
- **Over-correcting toward simplicity.** The migration should simplify the default path without erasing the kinds of advanced patches that make vivid interesting.
