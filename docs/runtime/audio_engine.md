# AudioEngine — Audio Thread and Cross-Domain Bridge

## Overview

`AudioEngine` (audio_engine.h/cpp) runs audio operators on a dedicated audio thread via miniaudio.
It maintains a parallel `AudioNodeState` array built from the same `Graph` as the `Scheduler`,
communicates with the main thread through two lock-free double-buffers:

- **`ParamSnapshot`** (control → audio): param values, float CV inputs, spread/string/custom inputs
- **`AnalysisSnapshot`** (audio → control): RMS, peak, waveform, float outputs, error state

## Constants

```cpp
static constexpr uint32_t kBufferSize = 256;   // frames per audio callback
static constexpr uint32_t kSampleRate = 48000;
```

## Key Structs

### `AudioNodeState`
Per-operator audio state. Notable fields:
- `input_buffers` / `output_buffers`: `[port][sample]` float arrays, pre-allocated
- `in_ptrs` / `out_ptrs`: `float*` arrays passed to `VividAudioContext`
- `float_input_values` / `float_input_count`: VIVID_PORT_FLOAT input ports (cross-domain CV)
- `float_output_values` / `float_output_count`: audio-domain `VIVID_PORT_SIGNAL` scalar outputs (audio → control)
- `spread_inputs` / `spread_outputs`: `SpreadSnapshot[input_port_idx]`
- `custom_input_values` / `custom_output_ptrs`: custom-type port values
- `errored`, `error_message[256]`: fixed-size, no heap allocation on audio thread

### `SpreadSnapshot`
```cpp
struct SpreadSnapshot {
    static constexpr uint32_t kMaxLength = 64;
    float data[kMaxLength] = {};
    uint32_t length = 0;
};
```

### `ParamSnapshot` (double-buffered)
Written by main thread (`push_params`), read by audio thread:
```cpp
struct ParamSnapshot {
    std::vector<std::vector<float>> node_params;          // [audio_node_idx][param_idx]
    std::vector<std::vector<float>> float_input_values;   // [audio_node_idx][float_input_ordinal]
    std::vector<std::vector<SpreadSnapshot>> spread_inputs;
    std::vector<std::vector<std::string>> input_string_values;
    std::vector<std::vector<CustomPortSnapshot>> custom_inputs;
    std::vector<bool> solo_active_set;
};
```
Two snapshots: `snapshots_[2]`. `active_` (atomic int) selects which one the audio thread reads.
Main thread writes to `1 - active_`, then flips `active_`.

### `AnalysisSnapshot` (double-buffered)
Written by audio thread, read by main thread via `analysis_read()`:
```cpp
struct AnalysisSnapshot {
    static constexpr uint32_t kWaveformSamples = 1024;
    std::vector<float> rms, peak;        // [audio_node_idx]
    std::vector<std::array<float, 1024>> waveform;
    std::vector<std::vector<SpreadSnapshot>> spread_outputs;
    std::vector<std::vector<float>> float_outputs;
    std::vector<bool> errored;
    std::vector<std::array<char, 256>> error_msgs;
};
```

## Wire Types

### Within Audio Domain
- `AudioWire` — audio buffer port to audio buffer port (with `scale`, `from_channels`/`to_channels`)
- `AudioFloatPortWire` — FLOAT output → FLOAT input (scalar, once per buffer)
- `AudioCustomWire` — custom-type output → custom-type input
- `AudioSpreadWire` — spread output → spread input

### Cross-Domain (Control → Audio)
- `CrossDomainWire` — control output port → audio param index
- `CrossDomainSpreadWire` — control spread output → audio spread input
- `CrossDomainStringWire` — control string output → audio string input
- `CrossDomainFloatPortWire` — control float output → audio FLOAT input port
- `CrossDomainCustomWire` — control custom output → audio custom input (supports `VIVID_PORT_TRANSPORT_AUDIO_SAFE` bounded copy)

## `AudioEngine` API

### Build
```cpp
bool build(const Graph& graph, OperatorRegistry& registry, const Scheduler& scheduler);
```
- Instantiates audio operators (those with `is_audio = true` in scheduler)
- Resolves all wire types (intra-audio + cross-domain)
- Pre-allocates all buffers (no audio-thread allocation after build)
- Negotiates channel counts for multi-channel wires
- Sets up auto-duplication groups for mono operators in stereo chains

### Start / Stop
```cpp
bool start(bool use_null_device = false);  // use_null_device=true for tests (no hardware)
void shutdown();
```

### Main-Thread Methods (called each frame)
```cpp
void push_params(const Scheduler& scheduler);             // snapshot control params → audio
void update_sources(double time, const Scheduler& scheduler); // push cross-domain wire values
void inject_analysis(Scheduler& scheduler);               // push analysis back to control domain
const AnalysisSnapshot& analysis_read() const;            // read current analysis snapshot
```

### Pause / Resume (for hot-reload)
```cpp
void pause();   // stop miniaudio callback, wait for drain
void resume();  // restart after dylib swap
```

### Recording Tap
Lock-free SPSC ring buffer (10 sec @ 48kHz stereo):
```cpp
void start_recording_tap();
void stop_recording_tap();
uint64_t available_recorded_samples() const;
uint64_t pop_recorded_samples(float* dst, uint64_t max_samples);
```

### Underrun Detection
```cpp
uint32_t underrun_count() const;         // total buffer underruns since start
bool last_buffer_underrun() const;       // true if most recent callback underran
```

## Auto-Duplication (`AutoDupGroup`)

When a mono audio operator is wired to a stereo chain, the engine auto-duplicates:
- Creates N instances (one per channel), `instances[0]` = primary
- Each instance processes a mono slice; outputs are merged back into the stereo buffer
- `is_mono_autodup = true` on the `AudioNodeState`

## Thread Safety Invariants

- Audio thread: reads `snapshots_[active_]`, writes `analysis_snapshots_[1 - analysis_active_]`
- Main thread: writes `snapshots_[1 - active_]`, reads `analysis_snapshots_[analysis_active_]`
- **Never** read `Scheduler::nodes_` from the audio thread
- All audio-thread buffers are pre-allocated; no `new`/`delete` in `audio_callback()`
- Error messages use `char[256]` arrays (not `std::string`) to avoid heap allocation

## Recent Hardening Guarantees

- audio-domain `VIVID_PORT_SIGNAL` outputs now support both internal bridge paths:
  scalar-written outputs can write `ctx->output_float_values[...]` directly, while
  buffer-backed outputs can still rely on last-sample extraction from `output_buffers[...]`
- the engine preserves explicit scalar signal writes and only auto-extracts from
  `output_buffers` when the scalar slot was left untouched by the operator

- control-to-audio `FLOAT` inputs are now snapshotted through `ParamSnapshot` just like other
  cross-domain audio inputs; they are no longer written live into `AudioNodeState` from the main thread
- hot reload on audio operators is rollback-safe for compatible descriptor changes and explicitly
  rejects incompatible descriptor edits
- custom ports crossing into audio are treated as bounded audio-safe snapshots only; the audio
  thread does not dereference runtime-owned objects directly
