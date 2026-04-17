# Phase 3: Renderer Architecture

## Summary

Define the internal engine behind `WavetableLayer`. This phase locks the data layout, render flow, scalar/SIMD backend contract, and hot-loop rules so implementation can proceed without revisiting core architectural decisions.

## Implementation Changes

- Build the renderer around structure-of-arrays state for voices and unison lanes:
  - phases
  - phase increments
  - gains
  - pan gains
  - active masks
  - table-selection state
  - modulation ramp state
- Rebuild a compact active render list once per block so inactive voices are excluded from the hot loop.
- Use renderer-specific wavetable storage with guard samples to keep lookup code branch-light and pointer-stable.
- Use linear interpolation in the production core.
- Use quantized mip selection in the fast path.
- Evaluate non-pitch-critical position/warp smoothing at an 8-sample control rate with linear interpolation inside the sub-block.
- Accumulate directly into stereo output buffers inside the renderer.

## Backend Contract

Two backends share one behavior spec:

- scalar backend:
  - canonical correctness reference
  - same feature set as SIMD
  - used on unsupported targets and in equivalence testing
- `Highway` backend:
  - production SIMD path
  - processes voices/unison lanes in fixed-width batches
  - uses the same render contract and output semantics as scalar

`WavetableLayer` is the first major consumer of the shared SIMD foundation from Phase 1. That shared foundation remains reusable by future dense audio operators, but this phase only specifies the wavetable renderer.

Backend selection rules:

- selected once per block based on build support and runtime-capable batch shape
- never exposed to the graph or user surface
- scalar cleanup or masked SIMD handles tail elements

Hot-loop rule:

- there is no mode branching inside the SIMD inner loop

Any mode-specific choice must be resolved before entering the innermost render kernel.

## Architectural Constraints

- Legacy interaction modes are not part of the new renderer.
- Stereo summing is internal to `WavetableLayer`.
- Per-sample work is reserved for pitch/phase-critical paths only.
- Helper accelerators such as `Accelerate` may be used only behind the same renderer contract and only where benchmarked wins are clear.

## Dependencies

- [Phase 1: Dependency and Build Foundation](./phase-1-dependency-and-build-foundation.md)
- [Phase 2: WavetableLayer Operator Surface](./phase-2-wavetablelayer-operator-surface.md)

## Test Plan

- Add scalar backend correctness tests for representative mono and stereo-unison voice sets.
- Add scalar vs SIMD equivalence coverage with tolerance-based comparison for RMS, peak, stereo balance, and average sample difference.
- Add tests for control-rate position/warp stepping to verify the agreed 8-sample behavior.
- Add coverage ensuring excluded legacy interaction modes are absent from the renderer contract.

## Acceptance Criteria

- Scalar and SIMD backends share one behavior contract.
- The renderer design is explicit enough to implement without reopening data-layout decisions.
- The inner-loop branch policy is locked.
- Legacy interaction modes are confirmed out of scope for the new renderer.

## Not In This Phase

- No package-side migration content yet.
- No canonical instrument/module decisions.
- No final benchmark thresholds or release gates.
- No legacy operator status changes.

## Assumptions and Defaults

- Scalar is the correctness reference backend.
- `Highway` is the production SIMD path.
- Linear interpolation is acceptable for the production core even if the old path used more expensive interpolation.
