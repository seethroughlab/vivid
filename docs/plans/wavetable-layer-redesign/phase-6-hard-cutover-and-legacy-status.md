# Phase 6: Hard Cutover and Legacy Status

## Summary

Once `WavetableLayer` passes the macOS Release benchmark and correctness gates, make it the only canonical production wavetable path in active docs, modules, and examples. This phase defines the cutover rules and constrains the legacy coexistence window to migration only. It does not require simultaneous SIMD retrofits for unrelated audio operators or Windows-port validation.

## Implementation Changes

- Make `WavetableLayer` the canonical production path in active package docs, examples, and reference instruments.
- Hide legacy wavetable operators from recommended docs/examples.
- Mark remaining legacy wavetable operators and modules clearly as `legacy` in package docs and catalog metadata where applicable.
- Require all new production-oriented content to use `WavetableLayer`.
- Require old production-oriented content to either:
  - migrate to `WavetableLayer`, or
  - move to explicit archive/legacy status

## Legacy Policy During Cutover

- Short coexistence is allowed only for migration.
- Legacy `WavetableOsc` content may remain active only when it depends on explicitly excluded advanced features.
- Any legacy example still shown in active docs must be labeled as a legacy/advanced path, never as the recommended performance path.

## Dependencies

- [Phase 4: Package Migration and Reference Instrument](./phase-4-package-migration-and-reference-instrument.md)
- [Phase 5: Validation, Benchmarks, and Cross-Platform Gates](./phase-5-validation-benchmarks-and-cross-platform-gates.md)

## Test Plan

- Review active docs/examples to ensure there is one canonical wavetable production path.
- Verify new content checklists and package review norms reject new production content built on the legacy stack.
- Validate catalog/browser metadata, if present, reflects `legacy` status clearly for superseded surfaces.

## Acceptance Criteria

- There is one canonical wavetable production path in active docs.
- New production-oriented content uses `WavetableLayer`.
- Legacy wavetable operators/modules are clearly marked and confined to migration or excluded-feature use.
- The Windows port remains unblocked by the cutover because `WavetableLayer` keeps the same public surface and a portable Highway/scalar fallback boundary.

## Not In This Phase

- No final deletion yet.
- No indefinite support promise for legacy operators.
- No re-expansion of the legacy path because of migration convenience.

## Assumptions and Defaults

- The cutover happens only after Phase 5 gates are met.
- Hard cutover is the intended strategy, not a tentative recommendation.
- Active docs may mention legacy surfaces only to explain migration or excluded advanced features.
- Windows validation is a future port-readiness gate, not a blocker for this macOS cutover phase.
