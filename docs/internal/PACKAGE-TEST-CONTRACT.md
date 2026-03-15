# Package Test Contract

## Purpose

This document is the core source of truth for package-test ownership after
Phase 2 hardening. It explains what the manifest test surface means, what the
generic runner supports, and when package-local CMake / CTest remains the
correct home for package coverage.

## Contract Summary

The manifest test surface stays:

- `tests.graphs`
- `tests.cpp`

The design is intentionally hybrid:

- keep a lightweight generic core runner for the supported subset
- fail unsupported shapes early and explicitly
- keep heavier package-specific C++ coverage in package-local CMake / CTest

This is a contract clarification, not a package build-system redesign.

## Ownership Split

### `tests.graphs`

Use this for:

- graph smoke tests
- graph contract checks
- package example coverage that should load through current core semantics

Requirements:

- package-relative paths
- `.json` files
- valid graph files

Representative result codes:

- `graph_passed`
- `graph_needs_gpu`
- `graph_needs_audio`
- `graph_load_failed`
- `graph_build_failed`
- `graph_node_error`
- `unsupported_graph_test_shape`

### Manifest `tests.cpp`

Use this for:

- lightweight package tests that fit the generic runner
- self-contained package checks with a standalone `main()`
- tests that only need:
  - Vivid headers
  - package `operators/` headers
  - declared vendored include dirs

Supported subset:

- package-relative `.cpp` files
- single-source test entrypoints
- no package-local CMake target selection
- no framework-specific link environment

Representative result codes:

- `cpp_passed`
- `missing_test_file`
- `unsupported_test_extension`
- `path_outside_package`
- `unsupported_cpp_test_shape`
- `cpp_compile_failed`
- `cpp_runtime_failed`
- `cpp_runtime_launch_failed`
- `cpp_runtime_abnormal`

### Package-local CMake / CTest

Use this for:

- framework-driven tests (`gtest`, `catch2`, `doctest`, etc.)
- multi-source test binaries
- custom link dependencies
- package-specific runtime environments
- anything the generic runner should not try to emulate

This remains the canonical home for heavier package-specific C++ coverage.

## Generic Runner Rules

The generic runner now hardens these cases explicitly:

- `missing_test_file`
- `unsupported_test_extension`
- `path_outside_package`
- `duplicate_test_entry`
- `unsupported_cpp_test_shape`

That means unsupported manifest tests fail deterministically and early instead
of presenting as ambiguous compile errors.

## Reporting Expectations

Package-test output should make three things obvious:

1. what passed / failed / skipped
2. why a specific manifest test was rejected
3. whether package-local CMake / CTest is still expected for some coverage

Package-level `notes` is the mechanism for whole-package guidance, including:

- no manifest tests declared
- some manifest `tests.cpp` entries are outside the generic runner contract

## Ecosystem Check

Core now carries a representative ecosystem-level package-contract test that
exercises three shapes:

- graph-only manifest tests
- lightweight manifest `tests.cpp`
- heavier C++ tests that are intentionally classified as unsupported by the generic runner

The purpose is to catch contract drift in core even when sibling-repo CI stays green.
