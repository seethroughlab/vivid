# Phase 4: Remove Dual-Cadence Infrastructure and Enforce Explicit Bridges

## Summary

This is the first half of the atomic runtime break. Remove the old dual-cadence architecture from the core and make explicit bridge edges the only legal frame↔audio crossing mechanism.

## Implementation Changes

### Remove dual-cadence infrastructure

- In `src/operator_api/types.h`:
  - delete `VividCadenceCapability`
  - delete `VIVID_CADENCE_*`
  - remove `input_float_values` and `output_float_values` from `VividAudioContext`
  - bump `VIVID_OPERATOR_ABI_VERSION`
- In `src/operator_api/operator.h`:
  - remove auto-detection of audio-capable status in `VIVID_REGISTER`
- In `src/runtime/cadence_types.h`:
  - delete `CadenceOverride`
- In `src/runtime/graph.h`:
  - remove `NodeDef.cadence_override`
- In `src/runtime/compiled_graph.h`:
  - remove `CompiledNode.cadence_capability`
  - remove `CompiledNode.original_cadence_override`
  - remove audio float-CV and signal extraction state from `AudioNodeState`

### Enforce explicit bridge rules in the compiler

- In `src/runtime/graph_compiler.cpp`:
  - delete the cadence inference/promotion pass entirely
  - parse `ConnectionDef.bridge` into `CompiledEdge.bridge_kind`
  - reject cross-cadence edges without `bridge`
  - reject same-cadence edges with `bridge`
  - reject invalid bridge/type combinations
  - remove direct scalar↔audio-buffer compatibility
  - remove signal ordinals

### Remove cadence override surfaces

- Remove cadence override support from:
  - `src/runtime/control_server.cpp`
  - `src/runtime/runtime_api.cpp`
  - `src/ui/node_graph_draw.cpp`
  - `src/ui/node_graph_input.cpp`
  - `src/runtime/subgraph_module.cpp`
  - any operator-creation or graph-editing helper that still assumes node cadence switching

Cleanup gate after this phase:
- Grep active runtime code for:
  - `AUDIO_CAPABLE`
  - `CadenceOverride`
  - `input_float_values`
  - `output_float_values`
  - `signal_output_extractions`
  - `from_signal_ordinal`
  - `to_signal_ordinal`
- None of those may remain in active runtime paths

Essential paths:
- `src/operator_api/types.h`
- `src/runtime/graph_compiler.cpp`
- `src/runtime/compiled_graph.h`

## Test Plan

- Compiler rejects legacy implicit frame↔audio connections
- No node/runtime cadence override machinery remains
- ABI version bump causes stale dylibs to fail cleanly
- Grep cleanup gate passes

## Assumptions and Defaults

- This phase lands atomically with Phase 5 and Phase 6
- There is no compatibility mode left active after the cutover
