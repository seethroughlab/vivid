# Phase 2 — Synth breakouts + composable poly control replacement

**Status**: planned. Preserves advanced per-voice patching while making synths the default/simple path.

**See also**: [README.md](README.md) and [phase-1-wire-format.md](phase-1-wire-format.md).

## Goal

Make every voice synth expose the same advanced per-voice breakout surface so new users can stay on the simple `note source -> synth -> audio_out` path, while advanced users still have first-class graph composition tools.

This phase is about **adding the replacement surface**, not deleting the old one.

It also has to pin down the UI affordance for those ports. Do not start exposing breakout outputs broadly until the descriptor/UI layer can mark them as advanced/secondary.

## Standardized synth breakout outputs

Every voice synth should expose the same advanced outputs:

- `voices_out`
- `voice_ids`
- `voice_gates`
- `voice_velocities`
- `voice_freqs`

Lock the alignment contract:

- breakouts are emitted in **active-note order sorted by `note_id`**
- all `voice_*` lanes and `voices_out` channels stay aligned to that order
- the graph contract must not depend on local slot index, stealing order, or synth-specific voice-cap behavior

Expression policy for Phase 2:

- keep pitch bend / pressure / timbre in the native note transport from Phase 1
- do **not** standardize `voice_pitch_bend`, `voice_pressure`, or `voice_timbre` breakout lanes yet
- add graph-visible expression breakout lanes only in the same phase that introduces a concrete downstream consumer for them
- `voice_freqs` is the canonical pitch-tracking surface for current downstream consumers

## Synth responsibilities

For each voice synth:

- keep the primary/simple path obvious: `notes_in` + `output`
- expose the breakout outputs as secondary/advanced ports
- keep current lane-note inputs during this phase so migration can happen incrementally
- ensure breakout ordering is deterministic and shared across all outputs

UI/documentation expectations for the plan:

- add a minimal port-priority/grouping mechanism to the descriptor/UI layer before or alongside breakout rollout
- the simple/default visible surface remains small
- advanced breakout ports are collapsed, grouped, or otherwise presented as power-user affordances rather than the primary story
- call out explicitly in implementation notes that the temporary transition state will make inspectors look more cluttered until Phase 3 deletes the old ports

## Shared-control replacement: `NoteBreakout`

Introduce a lightweight `NoteBreakout` operator that consumes `notes_in` and exposes the non-audio per-voice control lanes:

- `voice_ids`
- `voice_gates`
- `voice_velocities`
- `voice_freqs`

Use `NoteBreakout` when one native note stream needs to drive multiple downstream operators that share polyphonic control state, especially where there is no single synth instance that should own the breakout lanes.

`NoteBreakout` is not the default user story. It exists to preserve advanced composability and layered-control workflows that `VoiceAllocator` currently enables.

Lock the cost/role distinction:

- `NoteBreakout` should be cheaper than instantiating a synth solely to harvest breakout lanes
- it carries no oscillator state, no synth envelope state, and no audio render path
- synth breakouts are for when a synth already owns the notes; `NoteBreakout` is for shared-control fanout without inventing a silent audio source

## Downstream consumer updates

Update the plan so allocator-era consumers move toward the new breakout surface:

- `EnvelopeAu` keeps its current input names; the canonical wiring becomes `voice_gates -> gate` and `voice_ids -> lane_ids`
- `Filter` / `DualFilter` keytracking consumes `voice_freqs`
- `VoiceMixer` / `VoiceDrive` consume `voices_out`
- per-voice amplitude and velocity shaping continues through `voice_velocities` plus envelope outputs

The result should preserve these advanced workflows:

- per-note envelopes with stable identity
- polyphonic keytracking
- explicit per-voice mixing/drive chains
- one note stream driving multiple synths/operators without losing shared per-note control data

## Explicit non-goals for Phase 2

Do **not** do these in this phase:

- delete `VoiceAllocator`
- delete synth lane-note inputs
- bulk-rewrite checked-in graphs
- remove alias entries tied to still-live surfaces

First land the replacement composability surface and verify it in tests and a small number of migration probes.

## Tests and acceptance criteria

Required test coverage:

- all voice synths expose the standardized `voice_*` outputs
- `voices_out` stays aligned with the non-audio breakout lanes by `note_id`
- descriptor/UI tests cover the new advanced-port affordance so breakout ports do not all present as primary by default
- `voice_gates -> gate` and `voice_ids -> lane_ids` on `EnvelopeAu` behave like the current stable per-note envelope path
- `voice_freqs -> Filter/DualFilter` preserves polyphonic keytracking
- `voices_out -> VoiceMixer/VoiceDrive` preserves explicit per-voice processing
- `NoteBreakout` exposes the same non-audio per-voice state as the source note stream seen by synths
- one native note stream can drive multiple synths/operators without per-voice ordering drift

Manual sanity:

1. `note source -> synth -> audio_out` remains the obvious simple patch.
2. `note source -> NoteBreakout -> EnvelopeAu/Filter/...` preserves advanced shared-control composition.
3. Two synths fed from the same note stream expose compatible `voice_*` breakout ordering.
4. Inspectors show breakout ports as advanced/secondary rather than as the default top-level surface.

## Risks

- **Breakout ordering bugs.** Any mismatch between `voices_out` and non-audio `voice_*` lanes will make advanced graphs feel unpredictable.
- **UI sprawl.** If the advanced-port affordance slips, Phase 2 will temporarily create the largest synth inspector surface vivid has had.
- **Half-migrated semantics.** Leaving old and new surfaces live simultaneously is necessary here, but the docs must clearly describe which one is the intended long-term model.
