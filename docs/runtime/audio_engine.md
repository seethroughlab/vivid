# AudioEngine / AudioExecutor — Audio Thread Execution

## Overview

Audio-rate processing is split across two classes:

- **`AudioEngine`** (audio_engine.h/cpp) — lifecycle wrapper that manages start/stop/pause/resume
  and delegates to `AudioExecutor`
- **`AudioExecutor`** (audio_executor.h/cpp) — the real audio-thread processing loop, miniaudio
  integration, buffer management, auto-duplication, and analysis extraction

Both operate on the shared `CompiledGraph`. Audio-cadence nodes (`active_cadence == Cadence::Audio`)
store their buffer state directly in `CompiledNode` fields (`audio_buffers_in/out`, channel counts,
bridge-accessible scalar inputs, etc.).

## Audio Configuration

```cpp
CompiledGraph::audio_buffer_size  // default 256, configurable from app settings
CompiledGraph::audio_sample_rate  // fixed at 48000 for now
```

`AudioEngine` and `AudioExecutor` read these values from the active `CompiledGraph` at build time.
Buffer size is persisted in `settings.json` and applied by rebuilding the live runtime; sample rate
remains fixed.

## Cross-Cadence Communication

Communication between frame and audio worlds uses `AudioFrameBridge`, which maintains two
double-buffered snapshot pairs with lock-free atomic index flips:

- **`ParamSnapshot`** (frame → audio): audio-node parameter snapshots, held scalar bridge values,
  lane data via `BridgeLaneSlot` (pre-allocated flat buffers, default capacity `kDefaultLaneCapacity` = 1024),
  string/custom snapshots, and solo active set
- **`AnalysisSnapshot`** (audio → frame): RMS, peak, waveform ring buffers, scalar bridge
  payloads, lane outputs via `BridgeLaneSlot`, and error state

These snapshots are runtime transport, not a second multiplicity model. They carry the same
lane-bearing values described in the top-level architecture, packaged into audio-safe transfer
structures for the cadence boundary. Lane data uses pre-allocated `BridgeLaneSlot` storage
wired during `AudioFrameBridge::build()` — the audio callback reads lane data directly from
bridge slots (zero-copy, no heap allocation).

### Frame → Audio

`AudioFrameBridge::push_to_audio()` iterates `frame_to_audio_edges` (snapshot edges) and copies
frame-side output values into the inactive `ParamSnapshot`, then publishes with release semantics.
This is the runtime transport for explicit audio-frame bridge edges such as `hold` and
`snapshot`.

### Audio → Frame

`AudioFrameBridge::pull_from_audio()` reads the published `AnalysisSnapshot` and injects values into
frame-side `CompiledNode` outputs via `audio_to_frame_edges`. Bumps `generation` on receiving nodes
to trigger frame-executor recomputation. This is how audio analysis and audio-to-frame scalar bridge
payloads cross back into the frame execution world.

## Edge Transport

Edges between nodes are classified at compile time:

- **`EdgeTransport::Direct`** — same cadence; value copied during the owning executor's pass
- **`EdgeTransport::Snapshot`** — cross cadence; routed through the explicit audio-frame bridge

Partitioned into four index lists in `CompiledGraph`: `frame_direct_edges`, `audio_direct_edges`,
`frame_to_audio_edges`, `audio_to_frame_edges`.

## Audio Callback

`AudioExecutor::audio_callback()` processes audio-order nodes in chunks of the configured audio
buffer size:

1. Apply `ParamSnapshot` — populate `c_in_lane_views` directly from bridge `BridgeLaneSlot` pointers (zero-copy), apply params, strings, custom ports
2. For each node in `audio_order`:
   - Zero input buffers
   - Route upstream audio via `audio_direct_edges` (with channel negotiation)
   - Route lane data via `LaneBufferRef` sharing (zero-copy for same-cadence direct edges)
   - Build lane views: prefer `input_lane_refs` (direct routing) > bridge views (snapshot) > empty
   - Call `process_audio()` — lane-lifted (InstancePerLane), LoopBased (pre-allocated scratch), or normal
   - Publish RT-safe node telemetry (`last_block_total_us`, `last_process_us`, EMA block time, budget %, lane count, retained lane-state entry count)
3. Extract sink node output to device buffer
4. Compute per-node analysis (RMS, peak, waveform ring buffer)
5. Publish `AnalysisSnapshot`

Before node processing begins for a block, the executor also sweeps any lane IDs that were retired
during the previous callback. Retirement is lane-identity-wide: reclaiming one voice ID clears the
per-lane state that downstream audio nodes accumulated for that note.

The retained lane-state entry count is tracked per node inside `LaneStateService` and surfaced through
diagnostics/introspection. This count reflects retained `(node_idx, lane_id)` slots, not unique notes
across the graph.

## Auto-Duplication

When a mono audio operator appears in a multi-channel chain, `AudioExecutor` creates per-channel
instances (`AutoDupGroup`). Each channel is deinterleaved, processed independently, and interleaved
back. `is_mono_autodup = true` on the `CompiledNode`.

## Implicit Analysis Ports

Audio-cadence nodes automatically receive three implicit output ports (added by `GraphCompiler`):
`rms`, `peak`, `waveform`. These are populated by the audio executor after each buffer and
bridged to the frame world via `AnalysisSnapshot`.

## Thread Safety

- Audio thread: reads `ParamSnapshot`, writes `AnalysisSnapshot`
- Main thread: writes `ParamSnapshot`, reads `AnalysisSnapshot`
- All audio-thread buffers are pre-allocated; no heap allocation in `audio_callback()`
- Lane data: bridge slots are pre-allocated flat buffers; audio reads via pointer (no copy). `LaneBufferRef` retain/release uses lock-free atomics (never deallocates). LoopBased scratch vectors (`loop_lane_ids`, `loop_in_ptrs`, `loop_out_ptrs`) are pre-allocated during `build()`.
- Error messages use `char[256]` arrays to avoid `std::string` allocation on audio thread
- Custom ports use bounded audio-safe snapshots; audio thread never dereferences runtime objects

## Recording Tap

Lock-free SPSC ring buffer (10 sec @ 48kHz stereo):

```cpp
void start_recording_tap();
void stop_recording_tap();
uint64_t available_recorded_samples() const;
uint64_t pop_recorded_samples(float* dst, uint64_t max_samples);
```
