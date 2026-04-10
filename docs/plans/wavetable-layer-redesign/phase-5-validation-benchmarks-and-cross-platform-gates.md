# Phase 5: Validation, Benchmarks, and Cross-Platform Gates

## Summary

Prove that `WavetableLayer` is both correct and production-usable before cutover. This phase defines the equivalence tests, benchmark fixtures, performance thresholds, and macOS/Windows gates that must be satisfied before the old path stops being the recommended one. Wider reuse of the shared SIMD foundation by other operators is intentionally outside the release gate for this migration.

## Required Validation

- scalar vs SIMD equivalence tests for the agreed `WavetableLayer` v1 surface
- correctness tests for:
  - mono voice render
  - stereo unison render
  - drift-enabled render
  - position/warp control-rate stepping
  - external `voice_gain_audio`
- package integration tests using the reference instrument(s)
- cross-platform build/test coverage on macOS and Windows

## Benchmark Fixtures

Benchmark targets:

- one production pad instance
- four simultaneous instances of the same production-grade fixture or its agreed stress variant

Each benchmark report must include:

- buffer size
- sample rate
- machine/OS summary
- backend used: scalar or SIMD
- overall `audio_load`
- XRUN count
- top audio nodes from runtime telemetry

## Performance Gates

- single-instance target: `audio_load <= 0.30`
- four-instance target: stable playback with `xruns == 0`
- `WavetableLayer` must materially outperform the legacy `WavetableOsc + VoiceMixer` stack on the agreed fixture set

## Dependencies

- [Phase 3: Renderer Architecture](./phase-3-renderer-architecture.md)
- [Phase 4: Package Migration and Reference Instrument](./phase-4-package-migration-and-reference-instrument.md)

## Test Plan

- Add scalar vs SIMD equivalence tests with tolerance-based assertions.
- Add cross-platform CI or documented manual validation targets for macOS and Windows.
- Add benchmark harnesses or scripts that produce stable, comparable reports.
- Re-run the reference fixtures after each major renderer change and store the summarized results in the phase workstream.

## Acceptance Criteria

- No phase closes without published benchmark numbers against the agreed fixture set.
- Both macOS and Windows builds meet the supported v1 feature surface.
- Single-instance and four-instance targets are met on the agreed validation setup.
- SIMD and scalar backends remain behaviorally comparable within agreed tolerances.

## Not In This Phase

- No cutover in active docs/modules until the gates are met.
- No legacy removal.
- No expansion of the v1 feature surface beyond the frozen interface.

## Assumptions and Defaults

- Runtime telemetry added in Vivid core is the source of truth for per-node timing.
- Performance acceptance is measured on the agreed benchmark fixture set, not ad hoc patches.
- The same gates apply before recommending the new path on Windows and macOS.
