# Phase 5: Validation, Benchmarks, and Cross-Platform Gates

## Summary

Prove that `WavetableLayer` is both correct and production-usable before cutover. This phase defines the equivalence tests, benchmark fixtures, performance thresholds, and macOS Release gate that must be satisfied before the old path stops being the recommended one. Windows validation is deferred to the future Windows port, but the renderer boundary must remain portable. Wider reuse of the shared SIMD foundation by other operators is intentionally outside the release gate for this migration.

## Required Validation

- scalar vs optimized-backend equivalence tests for the agreed `WavetableLayer` v1 surface
- correctness tests for:
  - mono voice render
  - stereo unison render
  - drift-enabled render
  - position/warp control-rate stepping
  - external `voice_gain_audio`
- package integration tests using the reference instrument(s)
- macOS build/test coverage for the current production gate
- portability review confirming the public API and renderer backend boundary remain usable by a future Windows backend

## Benchmark Fixtures

Benchmark targets:

- one production pad instance
- four simultaneous instances of the same production-grade fixture or its agreed stress variant

Each benchmark report must include:

- buffer size
- sample rate
- machine/OS summary
- backend used, with accepted Phase 5 attribution via build/config inference:
  `Accelerate` on macOS when enabled, `Highway` as portable SIMD fallback, and scalar as the correctness/fallback backend
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

- Add scalar vs optimized-backend equivalence tests with tolerance-based assertions.
- Add macOS CI or documented manual validation targets for the current production gate.
- Record Windows as a future port-readiness gate rather than a blocker for Phase 6.
- Add benchmark harnesses or scripts that produce stable, comparable reports.
- Re-run the reference fixtures after each major renderer change and store the summarized results in the phase workstream.

## Acceptance Criteria

- No phase closes without published macOS Release benchmark numbers against the agreed fixture set.
- macOS builds meet the supported v1 feature surface.
- Windows validation is deferred to a future Windows-port gate and does not block Phase 6, provided no public API or renderer-boundary decision prevents a future Windows backend.
- Single-instance and four-instance targets are met on the agreed validation setup.
- Optimized backends and scalar remain behaviorally comparable within agreed tolerances.

## Not In This Phase

- No cutover in active docs/modules until the gates are met.
- No legacy removal.
- No expansion of the v1 feature surface beyond the frozen interface.
- No runtime/control-server backend telemetry plumbing; build/config backend inference is accepted for Phase 5.

## Assumptions and Defaults

- Runtime telemetry added in Vivid core is the source of truth for per-node timing.
- Performance acceptance is measured on the agreed benchmark fixture set, not ad hoc patches.
- Release build type is mandatory for performance acceptance runs.
- `Accelerate` is the macOS optimized backend when available, `Highway` remains the portable SIMD fallback, and scalar remains the correctness/fallback backend.
- The same production-performance gates should be re-applied when the Windows port begins, but that work is outside this Phase 5 closure.
