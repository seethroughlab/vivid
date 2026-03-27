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
| `input_spreads` | `VividSpreadPort*` | Cross-cadence spread inputs from control |
| `output_spreads` | `VividSpreadPort*` | Spread outputs |
| `input_handles` | `void**` | Handle inputs |
| `input_string_values` | `const char**` | String inputs |
| `file_param_values` | `const char**` | File/text param values |
| `shared_handles` | `VividSharedHandleService*` | Process-wide handle service |

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
    out.push_back({"input",  VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  0, 1});
    out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, 0, 1});

    // Stereo (channels = 2)
    out.push_back({"stereo_out", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, 0, 2});
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
        out.push_back({"input",  VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  0, 1});
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, 0, 1});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        float g = gain.value;
        for (uint32_t i = 0; i < ctx->buffer_size; i++)
            out[i] = in[i] * g;
    }
};

VIVID_REGISTER(Gain)
```

## Thread Safety

Audio operators run on the audio thread — avoid allocations, locks, and I/O in `process_audio()`. Use `main_thread_update(double time)` for non-realtime work (file I/O, buffer pre-fill). The runtime calls `main_thread_update` on the main thread before each frame.
