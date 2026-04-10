# WavetableLayer Redesign

## Read This First

This directory tracks a clean-break redesign of Vivid's production wavetable path. The current `WavetableOsc + VoiceMixer` stack remains available only as a legacy/flexible surface during migration; the target architecture is a new `WavetableLayer` operator designed for batched polyphonic rendering, portable SIMD acceleration, and eventual Windows parity.

## Summary

Current runtime telemetry shows the bottleneck is architectural, not incremental:

- `WavetableOsc` dominates audio callback time on real polyphonic pads.
- `VoiceMixer`, `DualFilter`, and runtime routing are comparatively small.
- The current stack is still a generic scalar graph-friendly oscillator path rather than a dedicated synth render engine.

This migration creates a new canonical production path with these goals:

- production-capable polyphonic wavetable rendering on macOS and Windows
- one shared renderer contract with scalar fallback and portable SIMD acceleration
- a reusable SIMD foundation in Vivid core for future dense audio operators
- direct stereo output from the voice engine instead of per-voice audio fanout
- a hard cutover after migration, followed by removal of superseded wavetable operators

Non-goals:

- preserving `WavetableOsc + VoiceMixer` as the production performance path
- keeping legacy wavetable operators indefinitely
- moving filters or envelopes inside the new operator in v1

## Locked Decisions

- `Highway` is a Vivid core-managed internal SIMD dependency because Windows portability matters.
- `WavetableLayer` is the first major consumer of that SIMD foundation, not the only intended consumer.
- Scalar fallback is mandatory and is the correctness reference backend.
- `Accelerate` is optional and additive only.
- `WavetableLayer` is stereo-output-only and internally owns unison plus stereo summing.
- External filters and envelopes remain graph operators.
- `WavetableOsc` remains only as a legacy path for excluded advanced features.
- Legacy wavetable operators are planned for removal after the hard cutover.

## Phase Plans

### [Phase 1: Dependency and Build Foundation](./phase-1-dependency-and-build-foundation.md)

Add `Highway` as a Vivid-managed dependency, expose it to package builds, and lock the scalar/SIMD build contract for macOS and Windows.

### [Phase 2: WavetableLayer Operator Surface](./phase-2-wavetablelayer-operator-surface.md)

Define the exact public operator surface and freeze the v1 feature boundary before renderer work begins.

### [Phase 3: Renderer Architecture](./phase-3-renderer-architecture.md)

Define the internal scalar + SIMD renderer contract, data layout, control-rate strategy, and branch-light hot loop rules.

### [Phase 4: Package Migration and Reference Instrument](./phase-4-package-migration-and-reference-instrument.md)

Build the first package-side production content on `WavetableLayer` and stop teaching `WavetableOsc + VoiceMixer` as the preferred performance recipe.

### [Phase 5: Validation, Benchmarks, and Cross-Platform Gates](./phase-5-validation-benchmarks-and-cross-platform-gates.md)

Define correctness, equivalence, benchmark, and platform gates that must be met before cutover.

### [Phase 6: Hard Cutover and Legacy Status](./phase-6-hard-cutover-and-legacy-status.md)

Make `WavetableLayer` the canonical path in active docs, modules, and examples, and confine the old stack to explicit legacy status.

### [Core SIMD Follow-Ons](./core-simd-follow-ons.md)

Rank likely next adopters of the shared SIMD foundation after the wavetable migration is established.

### [Post-Cutover Legacy Removal Plan](./post-cutover-legacy-removal-plan.md)

Remove superseded wavetable operators, modules, docs, and benchmarks once cutover criteria are met.

## Sequencing

```text
Phase 1  (dependency/build)         ─── foundation in vivid core
Phase 2  (operator surface)         ─── freeze public interface
Phase 3  (renderer architecture)    ─── implementable engine design
Phase 4  (package migration)        ─── new production content in vivid-wavetable
Phase 5  (validation/benchmarks)    ─── prove perf + correctness on macOS/Windows
Phase 6  (hard cutover)             ─── make WavetableLayer canonical
Removal  (delete legacy path)       ─── remove superseded operators and content
```

Phases 1-3 define the new foundation. Phase 4 proves the package-side path. Phase 5 is the release gate. Phase 6 is the deliberate product/documentation cutover. Removal happens only after the cutover gates are satisfied, but it is part of the intended outcome, not optional cleanup.

## Review Expectations

Each phase doc in this directory must include:

- explicit acceptance criteria
- exact tests or benchmarks required before closing the phase
- dependencies on earlier phases
- a short "Not In This Phase" section to prevent scope creep

## Assumptions and Defaults

- Active planning docs for this migration live under `docs/plans/wavetable-layer-redesign/`.
- This is a Vivid-level migration stream because dependency and package-build plumbing live in `/Users/jeff/Developer/vivid` even though the operator implementation lives in `/Users/jeff/Developer/vivid-wavetable`.
- `/Users/jeff/Developer/vivid-wavetable` remains the canonical synth implementation repo.
- The migration strategy is hard cutover, not long-term coexistence.
