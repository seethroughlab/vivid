# Phase 5: Replace CadenceBridge With Explicit AudioFrameBridge Semantics

## Summary

Refactor the bridge and executors so they implement the explicit graph model directly. The old reused scalar side channels are removed; bridge payloads become explicit and keyed by node and port.

## Implementation Changes

### Rename and reposition the bridge

- Rename `CadenceBridge` to `AudioFrameBridge`
- Update includes, type names, docs, and runtime references accordingly

### Redesign snapshot storage

- In `src/runtime/snapshot_types.h`:
  - `ParamSnapshot` stores frame→audio bridge payloads by destination node and port:
    - held scalars for `hold`
    - lane/string/custom snapshots for `snapshot`
  - `AnalysisSnapshot` stores audio→frame bridge payloads by source node and port:
    - scalar metrics for `last_sample`, `rms`, `peak`
    - waveform lane snapshots
    - lane/string/custom snapshots
- Do not reuse the old `float_outputs` path
- Do not preserve any signal-ordinal-based storage shape

### Make push/pull bridge-kind aware

- `AudioFrameBridge::push_to_audio()`:
  - `hold` fills a temporary audio buffer for the destination input
  - `snapshot` copies lane/string/custom payloads
- `AudioFrameBridge::pull_from_audio()`:
  - `last_sample` writes scalar to frame input or param
  - `rms` writes scalar to frame input or param
  - `peak` writes scalar to frame input or param
  - `waveform` writes a downsampled lane array to the frame destination
  - `snapshot` copies lane/string/custom payloads

### Simplify the executors

- `AudioExecutor`:
  - remove float CV population entirely
  - remove signal extraction loops
  - route only `AUDIO_BUFFER` direct edges
  - consume bridge inputs through explicit bridge payload storage
- `FrameExecutor`:
  - route `SCALAR`, lane, string, and custom direct edges only
  - inject bridge outputs before frame evaluation

Essential paths:
- `src/runtime/snapshot_types.h`
- `src/runtime/audio_executor.cpp`
- `src/runtime/cadence_bridge.*` -> `audio_frame_bridge.*`

## Test Plan

- Add end-to-end tests for each bridge kind:
  - `hold`
  - `last_sample`
  - `rms`
  - `peak`
  - `waveform`
  - `snapshot`
- Verify no executor path depends on removed float side-channel state
- Verify explicit bridge semantics are now the only frame↔audio mechanism left

## Assumptions and Defaults

- Bridge storage is keyed by node and port, not by legacy scalar ordinals
- GPU remains outside this bridge and stays on the frame side
