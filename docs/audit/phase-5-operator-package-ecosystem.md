# Phase 5 — Operator, Package, And Ecosystem Audit

## Scope Reviewed

- loader and ABI surfaces
- package lifecycle
- metadata fidelity
- package authoring and extension workflows
- sibling-package smoke compatibility

## Evidence Gathered

- Current repo state during Phase 5:
  - branch: `master`
  - worktree: dirty only from the active audit docs and the earlier screenshot-smoke follow-up docs/tests
- Ecosystem inventory:
  - `ctest --test-dir build -N | rg "test_(operator_creator|operator_destination_policy|operator_loader|operator_info_cache|file_drop_registry|package_stress|package_compiler|package_catalog|package_manager|package_scope_resolver|package_scope_registry|package_scaffolder|package_update_logic|package_test_runner|package_contract_ecosystem|builtin_operators|audio_dsp_api|string_ports|export_pipeline)"`
  - discovered `19` targeted Phase 5 lanes:
    - `test_export_pipeline`
    - `test_operator_creator`
    - `test_operator_destination_policy`
    - `test_audio_dsp_api`
    - `test_operator_loader`
    - `test_file_drop_registry`
    - `test_package_stress`
    - `test_package_compiler`
    - `test_package_catalog`
    - `test_package_manager`
    - `test_package_scope_resolver`
    - `test_package_scope_registry`
    - `test_package_scaffolder`
    - `test_package_update_logic`
    - `test_package_test_runner`
    - `test_package_contract_ecosystem`
    - `test_string_ports`
    - `test_builtin_operators`
    - `test_operator_info_cache`
- Focused Phase 5 bundle:
  - `ctest --test-dir build -R "test_operator_creator|test_operator_destination_policy|test_operator_loader|test_operator_info_cache|test_file_drop_registry|test_package_stress|test_package_compiler|test_package_catalog|test_package_manager|test_package_scope_resolver|test_package_scope_registry|test_package_scaffolder|test_package_update_logic|test_package_test_runner|test_package_contract_ecosystem|test_builtin_operators|test_audio_dsp_api|string_ports|test_export_pipeline" --output-on-failure"`
  - result: `19/19` passed
- Lifecycle/authoring rerun:
  - `ctest --test-dir build -R "test_operator_loader|test_package_manager|test_package_compiler|test_package_scaffolder|test_package_test_runner|test_package_contract_ecosystem|test_package_stress" --output-on-failure"`
  - result: `7/7` passed
- Real sibling-package smoke evidence through the core harness:
  - staged temporary smoke directories under `/tmp/vivid_pkg_smoke/*` with symlinks to:
    - core dylibs from `/Users/jeff/Developer/vivid/build`
    - package dylibs from each sibling package `build/` directory
  - `/Users/jeff/Developer/vivid/build/test_demo_graphs /Users/jeff/Developer/vivid-sequencers/graphs`
    - result: `9` passed, `0` failed, `4` skipped
  - `/Users/jeff/Developer/vivid/build/test_demo_graphs /Users/jeff/Developer/vivid-drums/graphs`
    - result: `2` passed, `0` failed, `2` skipped
  - `/Users/jeff/Developer/vivid/build/test_demo_graphs /Users/jeff/Developer/vivid-wavetable/graphs`
    - result: `8` passed, `1` failed, `0` skipped
    - failing case: `wavetable_midi_demo.json`
    - observed stderr included:
      - `MidiInCore::initialize: error creating OS-X MIDI client object (-10833)`
      - `[MidiInput] RtMidi init error ...`
    - the run also showed unresolved package operators such as `WavetableSynth`, indicating the sibling package build in this workspace is not currently loading cleanly against the active core build
- Direct contract evidence from current docs and implementation:
  - [package_system.md](/Users/jeff/Developer/vivid/docs/runtime/package_system.md) defines install/link/rebuild/unlink/test flows, manifest `tests.graphs` / `tests.cpp`, package scopes, and transactional package mutation expectations
  - [operator_loader.md](/Users/jeff/Developer/vivid/docs/runtime/operator_loader.md) defines loader failure codes, ABI mismatch handling, deferred probe/full-load behavior, package provenance, and file-drop metadata preservation
  - [PACKAGE-SMOKE-TEST.md](/Users/jeff/Developer/vivid/docs/testing/PACKAGE-SMOKE-TEST.md) defines the external package smoke protocol through `test_demo_graphs`
  - [test_package_manager.cpp](/Users/jeff/Developer/vivid/tests/test_package_manager.cpp) covers install/list/uninstall lifecycle, preflight failure classification, and cleanup/rollback
  - [test_package_contract_ecosystem.cpp](/Users/jeff/Developer/vivid/tests/test_package_contract_ecosystem.cpp) covers manifest graph tests, lightweight manifest C++ tests, and explicit rejection of heavier package-local CMake/CTest shapes
  - [test_operator_loader.cpp](/Users/jeff/Developer/vivid/tests/test_operator_loader.cpp) covers load/unload, invalid/null descriptor handling, move semantics, deferred loading, and custom-port registration
  - [test_file_drop_registry.cpp](/Users/jeff/Developer/vivid/tests/test_file_drop_registry.cpp) covers file-drop metadata ordering, invalid handler filtering, and extension matching
  - [test_operator_creator.cpp](/Users/jeff/Developer/vivid/tests/test_operator_creator.cpp) covers naming validation, per-domain scaffolds, and CMake patching
- Historical boundary:
  - this phase uses current command evidence and current loader/package/testing contracts only
  - the previous role-binding-era audit is background context, not proof

## Findings

### 1. Operator loading and diagnostics are healthy in the core contract lanes

- Classification: `pass`
- Current read:
  - `test_operator_loader`, `test_operator_info_cache`, `test_builtin_operators`, `test_operator_destination_policy`, `test_audio_dsp_api`, and `test_string_ports` all passed in the focused bundle
  - the lifecycle rerun also kept `test_operator_loader` green
- Why it matters:
  - Phase 5 needs confidence that operators still load from current source/header contracts, that loader failures remain classifiable, and that built-in/operator metadata surfaces remain coherent for authoring and runtime use

### 2. Package lifecycle and transactional mutation behavior are healthy in the core package lanes

- Classification: `pass`
- Current read:
  - `test_package_manager`, `test_package_compiler`, `test_package_scope_resolver`, `test_package_scope_registry`, `test_package_update_logic`, and `test_package_stress` all passed
  - the lifecycle rerun also kept `test_package_manager`, `test_package_compiler`, and `test_package_stress` green
- Why it matters:
  - these are the core proofs that install/link/rebuild/unlink/package-churn flows remain transactional and usable after the recent simplification work

### 3. Package test contract fidelity is healthy

- Classification: `pass`
- Current read:
  - `test_package_test_runner` and `test_package_contract_ecosystem` both passed in the focused bundle and rerun
  - the current contract still distinguishes:
    - manifest `tests.graphs`
    - lightweight manifest `tests.cpp`
    - heavier package-local CMake/CTest cases that should be rejected explicitly rather than treated as generic runner input
- Why it matters:
  - package authors need a stable, predictable contract for what the core runner owns and what stays in package-local test infrastructure

### 4. Authoring and extension workflows are healthy

- Classification: `pass`
- Current read:
  - `test_operator_creator`, `test_package_scaffolder`, and `test_export_pipeline` all passed
  - file-drop and custom-port authoring surfaces also remained green through `test_file_drop_registry` and the relevant loader tests
- Why it matters:
  - the core product promise depends on operator/package authoring staying cheap and reliable; Phase 5 needs evidence that scaffolding and extension surfaces still work as intended

### 5. File-drop metadata handling remains healthy

- Classification: `pass`
- Current read:
  - `test_file_drop_registry` passed in the focused bundle
  - the registry still filters invalid file-drop registrations, keeps priority ordering, and preserves case-insensitive extension matching
- Why it matters:
  - file-drop behavior is one of the higher-friction ecosystem seams because it depends on deferred metadata surviving loader/registry boundaries cleanly

### 6. External sibling-package compatibility is mixed and needs follow-up

- Classification: `pass with defer`
- Current read:
  - `vivid-sequencers` smoke was clean: `9` passed, `0` failed, `4` skipped
  - `vivid-drums` smoke was clean within expected no-GPU limits: `2` passed, `0` failed, `2` skipped
  - `vivid-wavetable` smoke was not clean: `8` passed, `1` failed, `0` skipped
  - the failing wavetable run showed both:
    - a concrete MIDI-environment failure in `wavetable_midi_demo.json`
    - unresolved `WavetableSynth` loads, indicating the local sibling package build is not currently aligned with the active core build
- Why it matters:
  - the core operator/package contract lanes are healthy, but the broader sibling-package ecosystem still needs one compatibility pass before release confidence should extend to those packages as a group

## Required Fixes For Release

- None established by the core Phase 5 contract lanes.

## Deferred Follow-Ups

- Rebuild and revalidate sibling packages intended to ship alongside the current core, especially `vivid-wavetable`, against the active runtime ABI and headers.
- Re-run sibling-package smoke on a machine with the required MIDI/device environment for package graphs that depend on live MIDI initialization.
- Keep heavier package-local CMake/CTest cases outside the generic manifest runner unless the core contract is intentionally expanded.

## Signoff Status

- `pass with defer`
