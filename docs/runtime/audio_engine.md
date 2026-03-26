# AudioEngine / AudioExecutor — Audio Thread Execution

## Overview

Audio-rate processing is split across two classes:

- **`AudioEngine`** (audio_engine.h/cpp) — lifecycle wrapper that manages start/stop/pause/resume
  and delegates to `AudioExecutor`
- **`AudioExecutor`** (audio_executor.h/cpp) — the real audio-thread processing loop, miniaudio
  integration, buffer management, auto-duplication, and analysis extraction

Both operate on the shared `CompiledGraph`. Audio-cadence nodes (`active_cadence == Cadence::Audio`)
store their buffer state directly in `CompiledNode` fields (`audio_buffers_in/out`, channel counts,
float CV values, etc.).

## Constants

```cpp
static constexpr uint32_t kBufferSize = 256;   // frames per audio callback chunk
static constexpr uint32_t kSampleRate = 48000;
```

## Cross-Cadence Communication

Communication between frame and audio worlds uses `CadenceBridge`, which maintains two
double-buffered snapshot pairs with lock-free atomic index flips:

- **`ParamSnapshot`** (frame → audio): param values, float CV inputs, spreads, strings, custom
  ports, solo active set
- **`AnalysisSnapshot`** (audio → frame): RMS, peak, waveform ring buffers, float scalar outputs,
  spread outputs, error state

### Frame → Audio

`CadenceBridge::push_to_audio()` iterates `frame_to_audio_edges` (snapshot edges) and copies
frame-side output values into the inactive `ParamSnapshot`, then publishes with release semantics.

### Audio → Frame

`CadenceBridge::pull_from_audio()` reads the published `AnalysisSnapshot` and injects values into
frame-side `CompiledNode` outputs via `audio_to_frame_edges`. Bumps `generation` on receiving nodes
to trigger frame-executor recomputation.

## Edge Transport

Edges between nodes are classified at compile time:

- **`EdgeTransport::Direct`** — same cadence; value copied during the owning executor's pass
- **`EdgeTransport::Snapshot`** — cross cadence; routed through `CadenceBridge`

Partitioned into four index lists in `CompiledGraph`: `frame_direct_edges`, `audio_direct_edges`,
`frame_to_audio_edges`, `audio_to_frame_edges`.

## Audio Callback

`AudioExecutor::audio_callback()` processes audio-order nodes in chunks of `kBufferSize`:

1. Apply `ParamSnapshot` (params, float inputs, spreads, strings, custom ports)
2. For each node in `audio_order`:
   - Zero input buffers
   - Route upstream audio via `audio_direct_edges` (with channel negotiation)
   - Call `process_audio()` (or per-channel auto-dup for mono operators in stereo chains)
3. Extract sink node output to device buffer
4. Compute per-node analysis (RMS, peak, waveform ring buffer)
5. Publish `AnalysisSnapshot`

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
