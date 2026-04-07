# Post-Migration Clean-Break Cleanup Plan

## Summary

The fixed-cadence migration is functionally complete, and the repository now mostly reads as fixed-cadence-native.

This note records the **remaining** cleanup needed to make the codebase feel, to a newcomer, as though it had always been designed this way.

The earlier version of this cleanup plan overstated what remained. Most of the broad cleanup tracks are already complete. What is left is a small convergence pass in three places:

1. one stale canonical runtime reference doc
2. a few stale SVG diagrams
3. internal shared operator implementation types that still implement both frame and audio interfaces

This is now a **post-migration polish plan**, not a runtime redesign, compatibility effort, or continuation of the seven migration phases.

## Already Complete

The following cleanup work no longer needs to be tracked as active:

- Migration docs have already been moved under `docs/archive/fixed-cadence-migration/`.
- Primary terminology has been updated in active docs and operator-authoring docs:
  - `VIVID_PORT_SCALAR`
  - `VIVID_PORT_AUDIO_BUFFER`
  - `AudioFrameBridge`
- Active API comments no longer teach the old “audio-capable / promotion” model.
- Old `input_float_values` / `output_float_values` names no longer appear in active code.
- Broad fixed-cadence cleanup work should now be treated as archival history, not an active migration stream.

The repo is no longer in the state this document originally described. What remains is narrower and more specific.

## Remaining Cleanup

### 1. Rewrite `docs/vivid-runtime-architecture.md`

This is the biggest remaining active-doc mismatch.

`docs/vivid-runtime-architecture.md` still describes the old runtime in several places, including:

- `active_cadence` user-facing assignment rules
- `Auto-inference` / promotion logic
- `AUDIO_ONLY` / `FRAME_ONLY` cadence classification text
- `float_input_values`
- `float_outputs`
- multi-interface operator descriptions that no longer match the current fixed-cadence design

This file needs a full pass, not piecemeal edits.

Rewrite intent:

- describe the runtime as fixed-cadence only
- remove promotion/inference language entirely
- describe `AudioFrameBridge` and explicit bridge edges as the only frame/audio crossing model
- align context field tables, graph compilation, and execution sections with current code

### 2. Update stale SVG diagrams

The only remaining old terminology outside the archive shows up in diagrams.

Update these files:

- `docs/diagrams/architecture-overview.svg`
- `docs/diagrams/cadence-bridge.svg`
- `docs/diagrams/data-flow.svg`

Required changes:

- rename `CadenceBridge` labels to `AudioFrameBridge`
- remove any old dual-cadence wording from comments or visible labels
- ensure the diagrams visually match the fixed-cadence documentation set

`docs/diagrams/fixed-cadence-tick.svg` is already the canonical tick diagram and should remain so.

### 3. Resolve the internal shared-implementation layer

This is the main remaining code-level fossil.

Public fixed-cadence wrappers are already in place, but multiple shared implementation structs still implement both frame and audio interfaces. Current examples include:

- `operators/control/clock/clock.h`
- `operators/control/mseg/mseg.h`
- `operators/control/arpeggiator/arpeggiator.cpp`
- similar shared implementation units for other `_fr` / `_au` operator pairs

The cleanup direction should be:

- move shared behavior into cadence-neutral implementation helpers (`*_core.h`, utility functions, or plain state structs)
- keep `_fr` and `_au` wrappers as the only operator-shaped types that implement runtime interfaces
- stop using dual-interface shared structs as the primary implementation container

Follow-on polish:

- the remaining child-op/shared-implementation naming should avoid transitional cadence-era wording
- rename those terms only if the shared-implementation cleanup lands, so terminology stays consistent

## Acceptance Criteria

This cleanup is complete when the remaining active surfaces match the fixed-cadence model without requiring the archived migration notes.

### Doc and terminology gates outside the archive

Outside `docs/archive/`, these should return no hits:

- `CadenceBridge`
- `VIVID_PORT_SIGNAL`
- `VIVID_PORT_AUDIO\b`
- `input_float_values`
- `output_float_values`
- `audio-capable`
- `dual-cadence`
- `cadence_capability`

### Runtime-reference gate

`docs/vivid-runtime-architecture.md` should no longer contain:

- `active_cadence == Audio`
- `active_cadence == Auto`
- `Auto-inference`
- `AUDIO_ONLY`
- `FRAME_ONLY`
- `float_input_values`
- `float_outputs`

### Internal implementation gate

Run a focused search for operator structs that still implement both:

- `FrameProcessable`
- `AudioProcessable`

Treat remaining matches under `operators/control/` and `operators/shared/` as the cleanup list for the internal implementation refactor.

## Assumptions and Defaults

- This file remains under `docs/archive/fixed-cadence-migration/` as an archival note describing the remaining cleanup after the migration.
- Historical migration docs are allowed to keep old terminology because they are implementation records.
- The target standard is: active code and active docs should read as fixed-cadence-native, even if archived docs still describe the migration history.
- Remaining cleanup priority:
  1. `docs/vivid-runtime-architecture.md`
  2. stale SVG diagrams
  3. internal shared operator implementation refactor
