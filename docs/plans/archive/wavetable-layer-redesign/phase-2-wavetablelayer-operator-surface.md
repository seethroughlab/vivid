# Phase 2: WavetableLayer Operator Surface

## Summary

Freeze the public surface of `WavetableLayer` before renderer work begins. This phase defines exactly what the new production operator is responsible for, what it excludes, and how package authors should use it in place of `WavetableOsc + VoiceMixer`.

## Public Operator Interface

`WavetableLayer` exposes these required inputs:

- `frequencies`
- `gates`
- `velocities`
- `lane_ids`
- `pitch_mod`
- `position_mod`
- `warp_mod`
- `pitch_mod_audio`
- `position_mod_audio`
- `warp_mod_audio`
- `voice_gain_audio`

Output:

- stereo `output`

Behavioral rules:

- `WavetableLayer` owns polyphonic voice rendering, unison, stereo-pair rendering, and stereo summing.
- `WavetableLayer` does not emit one channel per voice.
- `WavetableLayer` replaces `WavetableOsc + VoiceMixer` in production instruments.
- External filters and envelopes remain outside the operator.

## V1 Feature Boundary

Supported in v1:

- builtin/custom wavetable selection
- position and warp
- unison voices, spread, and stereo rendering
- phase reset and randomized start
- detune and portamento
- drift
- per-voice pitch/position/warp modulation
- external `voice_gain_audio`

Explicitly excluded from v1:

- `interaction_mode` FM / PM / RM / AM
- feedback-dependent warp behavior using `last_sample`

Those excluded features remain on legacy `WavetableOsc` only.

## Authoring Guidance

- New production-oriented instruments in `/Users/jeff/Developer/vivid-wavetable` should be designed around `WavetableLayer`, not `WavetableOsc + VoiceMixer`.
- `WavetableOsc` remains valid only for advanced or exploratory patches that need excluded legacy features.
- Package docs and examples must describe `WavetableLayer` as the recommended performance path as soon as the migration phase begins.

## Dependencies

- [Phase 1: Dependency and Build Foundation](./phase-1-dependency-and-build-foundation.md)

`WavetableLayer` is the first production use of the shared core SIMD foundation established in Phase 1. This phase does not broaden that foundation into a general operator-by-operator optimization program.

## Test Plan

- Add operator registration/load coverage for `WavetableLayer`.
- Add surface-level tests confirming the full input/output contract and stereo-only output behavior.
- Add serialization/introspection coverage if operator metadata surfaces need to present the new ports.
- Verify excluded legacy features are not quietly reintroduced into the v1 surface.

## Acceptance Criteria

- The exact operator surface is frozen before renderer work begins.
- The supported/excluded feature boundary is explicit and documented.
- Package authoring guidance clearly replaces `WavetableOsc + VoiceMixer` with `WavetableLayer` for production use.

## Not In This Phase

- No renderer implementation details.
- No scalar/SIMD backend design.
- No benchmark targets yet.
- No legacy removal work beyond documenting the feature boundary.

## Assumptions and Defaults

- `WavetableLayer` is stereo-output-only.
- `voice_gain_audio` is part of the initial public surface, not a later add-on.
- External filters and envelopes remain graph operators.
