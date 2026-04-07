# Phase 4B: Remove VividCadenceCapability, Signal Ordinals, and Enforce Explicit Bridges

## Summary

Remove the dual-cadence type system (`VividCadenceCapability`), the scalar CV routing infrastructure (signal ordinals), and the Phase 2 temporary port type aliases. Enforce explicit bridge rules in the compiler so cross-cadence edges require a `bridge` field. Bump the operator ABI version.

## Implementation Changes

### Remove VividCadenceCapability

- In `src/operator_api/types.h`:
  - delete `VividCadenceCapability` typedef and `VIVID_CADENCE_*` defines
  - remove `cadence_capability` from `VividOperatorDescriptor`
  - remove Phase 2 temporary aliases (`VIVID_PORT_SIGNAL`, `VIVID_PORT_AUDIO`)
  - bump `VIVID_OPERATOR_ABI_VERSION`
- In `src/operator_api/operator.h`:
  - remove cadence capability auto-detection from `VIVID_REGISTER` macro
- In `src/runtime/compiled_graph.h`:
  - remove `cadence_capability` from `CompiledNode`
- In `src/runtime/graph_compiler.cpp`:
  - remove all `cadence_capability` reads and writes
- In `src/runtime/operator_creator.cpp`:
  - remove cadence_capability references
- In `src/runtime/builtin_operators.cpp`:
  - remove cadence_capability from builtin descriptors
- In `src/ui/graph_snapshot.h`:
  - remove `cadence_capability` from snapshot structs

### Remove signal ordinals

- In `src/runtime/compiled_graph.h`:
  - remove `from_signal_ordinal` and `to_signal_ordinal` from `CompiledEdge`
  - remove `SignalOutputMapping` struct and `signal_output_extractions` from `AudioNodeState`
- In `src/runtime/graph_compiler.cpp`:
  - remove signal ordinal computation pass
  - remove float I/O initialization in `init_audio_state()`

### Remove scalar↔audio_buffer compatibility

- In `src/operator_api/types.h`:
  - remove `SCALAR ↔ AUDIO_BUFFER` case from `vivid_port_type_compatible()`
  - after this change, SCALAR and AUDIO_BUFFER are incompatible types

### Enforce explicit bridge rules in compiler

- In `src/runtime/graph_compiler.cpp` (edge compilation pass):
  - cross-cadence edge without `bridge` → drop connection with diagnostic
  - same-cadence edge with `bridge` → drop connection with diagnostic
  - invalid bridge/type combination → drop connection with diagnostic

Essential paths:
- `src/operator_api/types.h`
- `src/operator_api/operator.h`
- `src/runtime/graph_compiler.cpp`
- `src/runtime/compiled_graph.h`

## Test Plan

- Update `test_graph_compiler_init.cpp` — remove signal ordinal and float I/O tests
- Update `test_operator_sweep.cpp` — remove cadence_capability checks
- Add bridge enforcement tests to `test_graph_compiler.cpp`:
  - cross-cadence without bridge → rejected
  - same-cadence with bridge → rejected
- Rework `test_signal_port.cpp` (currently tests float CV paths)
- ABI version bump causes stale dylibs to fail with clear error message

## Assumptions and Defaults

- Phase 4A has already landed (CadenceOverride removed)
- All paired operators are already split into `_fr` / `_au` (Phase 3)
- Signal ordinal removal does not affect operator behavior yet — float CV plumbing is still present in audio_executor and cadence_bridge until Phase 4C
