# Phase 6: Migrate Graphs and Replace the Test Suite Around the New Model

## Summary

Migrate first-party graphs and package graphs to explicit bridge edges and new operator names, then rewrite the tests so the fixed-cadence architecture becomes the only active model validated by the repo.

## Implementation Changes

### One-shot migration tool

Provide a migration command or script for:
- core graphs
- first-party package graphs

Node migration rules:
- collapsed operator -> keep bare name
- paired operator:
  - if legacy `cadence_override == Audio`, rewrite to `<name>_au`
  - otherwise run the legacy compiler once and map effective cadence to:
    - `<name>_au`
    - `<name>_fr`
- remove `cadence_override` from all nodes

Connection migration rules:
- same-cadence connection -> direct edge, no `bridge`
- frame scalar -> audio buffer -> `bridge: "hold"`
- audio buffer -> frame scalar -> `bridge: "last_sample"`
- lane/string/custom frame↔audio crossings -> `bridge: "snapshot"`

### Replace the tests

- Delete `tests/test_cadence_inference.cpp`
- Rewrite cadence bridge coverage around explicit bridge kinds
- Add migration tests:
  - legacy graph -> migrated graph
  - migrated graph round-trip
- Add operator registration tests:
  - all `_fr` and `_au` names
  - no bare paired-name registrations remain

Essential paths:
- `graphs/`
- `tests/`
- first-party package graph locations used by linked package tests

## Test Plan

- Migrated first-party core graphs compile and run
- Migrated first-party package graphs compile and run
- No active graph contains:
  - `cadence_override`
  - bare names for paired operators
  - implicit frame↔audio crossings
- Migration tests verify the effective-cadence mapping step for legacy `Auto` nodes
- `_fr` means fixed frame execution world and `_au` means fixed audio execution world for all migrated paired operators

## Assumptions and Defaults

- The migration tool may invoke the legacy compiler/runtime model during migration only
- Migration correctness is part of the deliverable, not deferred follow-up cleanup
