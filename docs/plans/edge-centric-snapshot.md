# Plan — Edge-centric snapshot indexing (gated; do not build preemptively)

## Position

This is a performance optimization that should be built **only when measured demand exists**. Building it now would be the wrong engineering: it adds real-time-thread complexity and risk to optimize a regime we're not in. The current per-node `ParamSnapshot` (`src/runtime/graph/snapshot_types.h:43–49`) is wasteful for *sparse* cross-cadence wiring but **not incorrect**. The dominant cost in the cross-cadence path is the frame→audio invocation multiplier, not snapshot indexing.

This document is design-on-file. Execute only when a trigger below fires.

## Context

`AudioFrameBridge` moves data between the frame cadence (~60 Hz main thread) and audio cadence (~48 kHz RT thread) via lock-free double-buffered snapshots. `ParamSnapshot` is indexed **per audio node**:

```cpp
struct ParamSnapshot {
    std::vector<std::vector<float>> node_params;               // [audio_node_idx][param_idx]
    std::vector<std::vector<BridgeLaneSlot>> lane_inputs;       // [audio_node_idx][input_port_idx]
    std::vector<std::vector<std::string>> input_string_values;
    std::vector<std::vector<CustomPortSnapshot>> custom_inputs;
    std::vector<bool> solo_active_set;
};
```

The bridge allocates these for **every** audio node, even nodes with zero cross-cadence edges, and pre-allocates lane storage as `[audio_count] × [port_count] × laneCap`. For sparse cross-cadence wiring this wastes allocation and cache lines. `AnalysisSnapshot` is correctly per-node (rms/peak/waveform are intrinsic node properties).

## Trigger (any one)
- Audio node count regularly exceeds ~10 with *sparse* cross-cadence wiring; or
- A profiler shows `AudioFrameBridge::push_to_audio()` / `pull_from_audio()` or the callback snapshot copy ≥ ~2% of the audio-thread budget; or
- Visible allocation/fragmentation pressure from the nested `std::vector<std::vector<T>>`.

## Design (when triggered)

- **`src/runtime/graph/snapshot_types.h`:** add
  ```cpp
  struct EdgeSnapshotSlot { uint32_t to_node; uint32_t to_port; float value; BridgeLaneSlot lane; };
  ```
  Replace `ParamSnapshot`'s per-node arrays with a flat `std::vector<EdgeSnapshotSlot>` aligned 1:1 to the compiler's `frame_to_audio_edges`, plus sparse side-tables for string/custom edges. `AnalysisSnapshot` stays per-node.
- **`src/runtime/audio/audio_frame_bridge.{h,cpp}}`** *(note: `TODO.md` calls this `cadence_bridge.h/.cpp` — that name is stale; the real file is `audio_frame_bridge`)*: size storage to edge count in `build()`, pre-wire each slot's lane buffer into the flat lane storage; `push_to_audio()` iterates `frame_to_audio_edges` directly, dropping the per-node `node_to_snapshot_idx_` indirection for params.
- **`src/runtime/graph/audio_executor.cpp` (~486–535):** scatter edge slots into each consumer's `audio_local_params` / `c_in_lane_views`, with a per-node default-reset first so params not edge-driven this frame keep their last value (not zero).

## Risk & verification
Real-time correctness is the hazard: all storage must be preallocated in `build()`; the audio callback stays allocation-free and lock-free. The per-node default-reset must be correct so a param not edge-driven this frame retains its last value.

- Add a unit test asserting per-edge output equals the current per-node output for a known graph **before** deleting the old path.
- A/B: `record_audio_to_wav` + `compare_audio_to_reference` must be bit-stable on an existing multi-synth graph.
- `detect_dropouts` under load → no new xruns.
- Microbenchmark `push_to_audio()` on a synthetic 32-audio-node / 4-bridge graph (the trigger scenario) before and after.

## Files
`src/runtime/graph/snapshot_types.h`, `src/runtime/audio/audio_frame_bridge.{h,cpp}`, `src/runtime/graph/audio_executor.cpp`.
