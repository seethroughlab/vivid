# Phase 4A: Remove Cadence Override Surface and Delete Inference Pass

## Summary

Remove the cadence override mechanism and the cadence inference/promotion pass. These are dead code since Phase 3 made all operators single-cadence. This is safe preparatory cleanup with no behavioral change.

## Implementation Changes

### Delete CadenceOverride enum

- In `src/runtime/cadence_types.h`:
  - delete `CadenceOverride` enum

### Remove from graph model

- In `src/runtime/graph.h`:
  - remove `NodeDef.cadence_override`
- In `src/runtime/graph.cpp`:
  - remove cadence override JSON parsing
  - remove cadence override JSON serialization
  - silently ignore `"cadence"` key in loaded graphs for backward compat

### Delete cadence inference pass

- In `src/runtime/graph_compiler.cpp`:
  - delete Pass 2.5: Cadence inference (the fixed-point promotion loop)
  - simplify initial cadence assignment to:
    - `has_process_gpu` → Frame
    - `has_process_audio && !has_process_frame` → Audio
    - `has_process_frame && !has_process_audio` → Frame
    - both → compile error (should not happen after Phase 3)
  - remove `original_cadence_override` from `CompiledNode` in `compiled_graph.h`

### Remove cadence override API

- In `src/runtime/runtime_api.h` / `.cpp`:
  - delete `set_cadence_override()` method
- In `src/runtime/control_server.cpp`:
  - delete `set_cadence_override` handler
  - remove cadence override from node info responses
- In `src/runtime/runtime_command_sink.h` / `src/ui/ui_command_sink.h`:
  - remove `set_cadence_override` from command interfaces

### Remove cadence override UI

- In `src/ui/node_graph_draw.cpp`:
  - delete `draw_inspector_cadence()` method
- In `src/ui/node_graph_input.cpp`:
  - delete cadence selector click handler
- In `src/ui/graph_snapshot.h`:
  - remove `cadence_override` from snapshot structs

### Remove from misc surfaces

- `src/runtime/subgraph_module.cpp` — remove cadence override handling
- `src/runtime/main.cpp` — remove cadence override reference

Essential paths:
- `src/runtime/cadence_types.h`
- `src/runtime/graph_compiler.cpp`
- `src/runtime/graph.h`
- `src/runtime/graph.cpp`

## Test Plan

- Update `test_cadence_inference.cpp` — remove legacy InferredAudio migration test
- Update `test_control_server.cpp` — remove cadence override test cases
- Grep: `CadenceOverride` must not appear in active code

## Assumptions and Defaults

- All operators are single-cadence after Phase 3
- Old graphs with `"cadence"` key load without error (field silently ignored)
- No cadence override UI or API remains
