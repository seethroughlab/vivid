# Phase 2: Rename Port Types to SCALAR and AUDIO_BUFFER

## Summary

Rename the two numeric port kinds to reflect their real execution shapes while keeping their numeric values unchanged. This phase is mechanical and wide, but it intentionally does not change runtime semantics yet.

## Implementation Changes

- In `src/operator_api/types.h`:
  - rename `VIVID_PORT_SIGNAL` to `VIVID_PORT_SCALAR`
  - rename `VIVID_PORT_AUDIO` to `VIVID_PORT_AUDIO_BUFFER`
  - keep both integer values unchanged (`0u`, `1u`)
  - keep old names as temporary `#define` aliases during this phase
- Rename usages across the active codebase:
  - runtime/compiler/executors
  - operators
  - tests
  - UI/introspection strings
  - docs in the active migration scope
- Update helper text so the public vocabulary becomes:
  - scalar
  - audio buffer
- Do not change any of the following yet:
  - `vivid_port_type_compatible()`
  - float CV side-channel paths
  - cadence inference/promotion
  - bridge execution behavior
  - embedded/child operator float-CV helper plumbing

Essential paths:
- `src/operator_api/types.h`
- `src/runtime/control_server.cpp`
- `src/ui/node_graph_draw.cpp`

## Test Plan

- Full build still passes with temporary aliases present
- Active code uses `VIVID_PORT_SCALAR` / `VIVID_PORT_AUDIO_BUFFER`
- Grep for `VIVID_PORT_SIGNAL` / `VIVID_PORT_AUDIO` only finds:
  - the temporary aliases in `types.h`
  - intentionally deferred migration notes, if any
- Existing float side-channel behavior still works unchanged after the rename

## Assumptions and Defaults

- This phase is naming-only
- Compatibility between scalar and audio buffer remains temporarily unchanged
- Float side-channel fields and helpers remain intentionally active after the rename
- Temporary aliases are removed before the Phase 4 cutover is finalized
