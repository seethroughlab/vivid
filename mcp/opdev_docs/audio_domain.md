# Audio Domain

Audio operators run on the audio thread, processing sample buffers at audio rate (default 256 samples at 48 kHz).

## Capability Interface

```cpp
#include "operator_api/operator.h"

struct MyAudioOp : vivid::OperatorBase, vivid::AudioProcessable {
    void process_audio(const VividAudioContext* ctx) override;
};
```

## VividAudioContext Fields

| Field | Type | Description |
|-------|------|-------------|
| `time` | `double` | Elapsed time in seconds |
| `delta_time` | `double` | Time since last frame |
| `frame` | `uint64_t` | Frame counter |
| `param_values` | `float*` | Indexed by param descriptor order (auto-synced) |
| `input_buffers` | `float**` | `[port_idx][sample]` — planar layout |
| `output_buffers` | `float**` | `[port_idx][sample]` — planar layout |
| `buffer_size` | `uint32_t` | Samples per buffer (typically 256) |
| `sample_rate` | `uint32_t` | Sample rate (typically 48000) |
| `input_channel_counts` | `const uint8_t*` | Per-port channel count (NULL = all mono) |
| `output_channel_counts` | `const uint8_t*` | Per-port channel count (NULL = all mono) |
| `input_lanes` | `const VividLaneView*` | Cross-cadence lane inputs from control (`.data`, `.length`, `.lane_set_id`, `.flags`) |
| `output_lanes` | `VividLaneOutput*` | Lane outputs (runtime-owned builder: `.handle`, `.resize()`, `.commit()`) |
| `custom_inputs` | `void**` | Custom-port inputs (`CUSTOM_VALUE` / `CUSTOM_REF`) |
| `input_string_values` | `const char**` | String inputs |
| `file_param_values` | `const char**` | File/text param values |
| `shared_handles` | `VividSharedHandleService*` | Process-wide handle service |
| `lane_count` | `uint32_t` | Number of lanes (1 = not lane-lifted) |
| `lane_index` | `uint32_t` | Which lane this invocation processes (0..lane_count-1) |
| `lane_set_id` | `uint32_t` | Lane provenance (0 = scalar) |
| `lane_id` | `uint32_t` | Stable identity for identity-bearing lane sets (0 = positional) |
| `lane_state_fn` | function pointer | Get per-lane persistent state keyed by lane_id |
| `allocate_lane_id_fn` | function pointer | Allocate a fresh lane_id (structural operators) |
| `retire_lane_id_fn` | function pointer | Retire a lane_id for deferred cleanup |

## Lane Lifting

Mono audio operators that receive multi-channel or multi-lane inputs are automatically **lane-lifted**: the runtime creates N instances and processes each lane independently. Each instance sees `lane_index` identifying its lane and `lane_count` for the total.

For per-lane persistent state (e.g., oscillator phase), use `vivid_lane_state()`:
```cpp
struct Voice { double phase; float freq; };
Voice& v = *vivid_lane_state(ctx, ctx->lane_id, Voice);
```

Each lifted lane receives a distinct `lane_id` (derived positional ID), so `vivid_lane_state()` returns per-lane-distinct storage. These IDs are not allocator-managed identities — they don't survive graph rebuilds. For true identity-bearing state (voice allocation, portamento), use `lane_id` values from a structural operator's `lane_ids` lane-array output.

**Strategy-independent convention:** New operators should use `vivid_lane_state()` for all per-lane persistent state, even when expecting to be lifted. This makes the operator compatible with future runtime execution strategies (loop-based, GPU compute) without source changes.

## Planar Buffer Layout

Audio buffers are planar: for a stereo port, channel 0 occupies `buffer[0..buffer_size-1]` and channel 1 occupies `buffer[buffer_size..2*buffer_size-1]`.

```cpp
// Stereo processing
uint8_t ch_count = ctx->output_channel_counts ? ctx->output_channel_counts[0] : 1;
for (uint8_t ch = 0; ch < ch_count; ch++) {
    float* out = ctx->output_buffers[0] + ch * ctx->buffer_size;
    for (uint32_t i = 0; i < ctx->buffer_size; i++) {
        out[i] = /* ... */;
    }
}
```

## Port Declaration with Channel Count

```cpp
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    // Mono input/output (channels = 1)
    out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,
                   VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1});
    out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1});

    // Stereo (channels = 2)
    out.push_back({"stereo_out", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
}
```

## Example: Simple Audio Operator

```cpp
#include "operator_api/operator.h"

struct Gain : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "Gain";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 2.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        float g = gain.value;
        for (uint32_t i = 0; i < ctx->buffer_size; i++)
            out[i] = in[i] * g;
    }
};
```

## Thread Safety

Audio operators run on the audio thread — avoid allocations, locks, and I/O in `process_audio()`. Use `main_thread_update(double time)` for non-realtime work (file I/O, buffer pre-fill). The runtime calls `main_thread_update` on the main thread before each frame.
