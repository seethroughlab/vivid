# Phase 4: Package Migration and Reference Instrument

## Summary

Move the new engine into package reality. This phase creates the first production-style `WavetableLayer` instruments in `/Users/jeff/Developer/vivid-wavetable`, defines the benchmark/listening fixtures, and stops teaching the old wavetable stack as the recommended performance recipe. The broader shared SIMD foundation from Phase 1 remains follow-on infrastructure for other operators, not additional scope for this phase.

## Implementation Changes

- Build at least one serious production-style pad or instrument on `WavetableLayer`.
- Do not retrofit the old `DualWavetablePad` module as the primary acceptance fixture.
- Define the first reference instrument set:
  - one production pad for single-instance validation
  - one multi-instance stress fixture derived from the same design goals
- Update package docs and examples so `WavetableLayer` is presented as the recommended path for production polyphonic wavetable instruments.
- Mark old wavetable operators/modules as `legacy` while migration is in progress.
- Ensure new package content does not reintroduce `VoiceMixer` as the preferred production reduction step for wavetable voices.

## Content Policy

- New production-oriented content must use `WavetableLayer`.
- Existing `WavetableOsc` content may remain only while it is either:
  - still needed for excluded advanced features, or
  - awaiting explicit migration or archival
- Reference instruments used for benchmarks and listening tests must remain stable across future optimization passes.

## Dependencies

- [Phase 2: WavetableLayer Operator Surface](./phase-2-wavetablelayer-operator-surface.md)
- [Phase 3: Renderer Architecture](./phase-3-renderer-architecture.md)

## Test Plan

- Add package-level correctness tests around the new reference instrument(s).
- Add package integration tests proving `WavetableLayer` works with external envelopes and filters in realistic graph/module assemblies.
- Add doc/example checks ensuring active package guidance no longer teaches `WavetableOsc + VoiceMixer` as the preferred performance path.

## Acceptance Criteria

- At least one serious production-style instrument uses `WavetableLayer`.
- Benchmark/listening fixtures are defined and stable.
- Docs/examples stop teaching `WavetableOsc + VoiceMixer` as the preferred performance recipe.
- Legacy wavetable operators/modules are explicitly labeled during migration.

## Not In This Phase

- No final hard cutover yet.
- No deletion of legacy operators/modules.
- No final release gate signoff without Phase 5 benchmarks.

## Assumptions and Defaults

- The benchmark/listening fixtures live in `/Users/jeff/Developer/vivid-wavetable`.
- Legacy labeling appears in package docs/examples before removal happens.
- `DualWavetablePad` may still be compared for sound/perf, but it is not the canonical acceptance fixture for the new path.
