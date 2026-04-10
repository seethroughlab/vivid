# Phase 1: Dependency and Build Foundation

## Summary

Add the cross-platform SIMD foundation in Vivid core before any new wavetable operator work begins. This phase establishes `Highway` as a Vivid-managed internal dependency, makes it available to linked package builds, and locks the scalar/SIMD build contract for macOS and Windows. `WavetableLayer` is the first major consumer, but this phase is intentionally broader: it creates reusable SIMD infrastructure for other dense audio operators too.

## Implementation Changes

- Add `Highway` to Vivid's dependency manifest and CMake plumbing in `/Users/jeff/Developer/vivid`.
- Expose `Highway` to package builds through the package compiler and generated package build environment so `/Users/jeff/Developer/vivid-wavetable` and future linked packages can consume one known version from core-managed infrastructure.
- Define a scalar/SIMD backend contract:
  - scalar backend always builds
  - SIMD backend is enabled when `Highway` is available for the target platform
  - package code must compile cleanly with SIMD disabled
- Add a compile-time feature flag for the SIMD backend and a separate internal gate for optional macOS helper acceleration.
- Lock the internal API policy:
  - `Highway` types and headers do not leak into public runtime or operator APIs
  - SIMD remains an implementation detail, not a user-facing feature
  - future first-party operators may use `Highway` directly or through a thin internal helper layer if repeated kernels emerge
- Keep any `Accelerate` usage isolated to optional helper paths only:
  - benchmark-gated
  - macOS-only
  - never required for correctness or feature parity

## Build and Interface Rules

- The canonical renderer contract must be implementable with scalar-only code.
- SIMD support is an internal optimization layer, not a public feature toggle.
- Package authors must not need package-local vendoring for `Highway`.
- Windows and macOS must use the same production renderer architecture, even if the macOS build adds optional helper acceleration later.
- `Highway` should be usable by other heavy core audio operators after this phase without inventing a second dependency path.

## Dependencies

- None. This phase is the foundation for all later phases.

## Test Plan

- Verify Vivid core builds on macOS with scalar-only configuration.
- Verify Vivid core builds on macOS with `Highway` enabled.
- Verify the package compiler can build a linked package that includes `Highway` headers from the Vivid-managed dependency path.
- Verify a representative package build still succeeds when SIMD is disabled.
- Add one narrow build/test target or smoke operator proving `Highway` can be consumed from package code without leaking its types through public APIs.
- Add at least one non-wavetable smoke target proving the shared dependency path is reusable outside the wavetable redesign.

## Acceptance Criteria

- Core builds on macOS and Windows with scalar enabled.
- Package builds can include `Highway` without package-local dependency management.
- Scalar-only and SIMD-enabled builds both compile cleanly.
- No public runtime/operator API leaks `Highway` types.
- At least one non-wavetable smoke target proves the shared dependency path is reusable.

## Not In This Phase

- No `WavetableLayer` operator yet.
- No new wavetable renderer implementation.
- No package content migration.
- No performance claims beyond successful build plumbing.
- No broad "SIMD every operator" program yet; later operator adoption is follow-on work.

## Assumptions and Defaults

- `Highway` is the primary SIMD abstraction because Windows portability matters.
- Scalar fallback remains mandatory.
- `Accelerate` is optional and additive only.
- `WavetableLayer` remains the first implementation target and primary motivation for this phase.
