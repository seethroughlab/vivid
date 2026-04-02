# Advanced API Features

## ChildOp\<T\> — Composite Operators

Embed an operator as a persistent member variable inside another operator. Frame-cadence only.

```cpp
#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"

struct ModulatedGain : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "ModulatedGain";
    static constexpr bool kTimeDependent = true;

    vivid::ChildOp<LFO> lfo;
    vivid::Param<float> depth{"depth", 0.5f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&depth);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        lfo.set_param("frequency", 2.0f);
        lfo.process(ctx);  // inherits time/frame from parent
        float mod = lfo.output("value");
        ctx->output_values[0] = ctx->input_values[0] * (1.0f - depth.value * mod);
    }
};
```

### ChildOp API
| Method | Description |
|--------|-------------|
| `set_param(name, value)` | Set child param by name |
| `set_param(index, value)` | Set child param by index |
| `set_input(name, value)` | Set child float input |
| `set_input_lane_data(name, data, length)` | Set child lane input |
| `process(parent_ctx)` | Run child (inherits time/frame) |
| `output(name)` | Read child float output |
| `output_lane_data(name)` | Read child lane output data |
| `output_lane_length(name)` | Read child lane output length |
| `op()` | Direct access to underlying operator instance |

## Custom Port Types

Typed opaque data for passing complex payloads between operators (e.g. media streams, 3D scene fragments). Two transports are available:

- `VIVID_PORT_TRANSPORT_CUSTOM_VALUE` — small structs (≤256 bytes) copied by value
- `VIVID_PORT_TRANSPORT_CUSTOM_REF` — opaque pointer via shared handle registry (any size)

```cpp
#include "operator_api/type_id.h"
#include "operator_api/port_type_registry.h"

// Producer
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    out.push_back(VIVID_CUSTOM_PORT("media_stream", VIVID_PORT_OUTPUT, vivid::MediaStreamV1, VIVID_PORT_TRANSPORT_CUSTOM_REF));
}

void process_frame(const VividFrameContext* ctx) override {
    auto* stream = static_cast<vivid::MediaStreamV1*>(ctx->custom_outputs[0]);
    // write stream data...
}

// Consumer
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    out.push_back(VIVID_CUSTOM_PORT("media_stream", VIVID_PORT_INPUT, vivid::MediaStreamV1, VIVID_PORT_TRANSPORT_CUSTOM_REF));
}

void process_frame(const VividFrameContext* ctx) override {
    auto* stream = static_cast<const vivid::MediaStreamV1*>(ctx->custom_inputs[0]);
    if (stream) { /* read stream */ }
}

// Register the type (at file scope)
VIVID_DESCRIBE_REF_TYPE(vivid::MediaStreamV1)
```

Type safety: `vivid_port_type<T>()` produces a compile-time FNV-1a hash of the C++ type with a high-bit marker. The runtime rejects connections between incompatible custom types.

## MIDI Types

```cpp
#include "operator_api/midi_types.h"

struct VividMidiMessage {
    uint8_t  status;                // e.g. 0x90 = note-on ch1
    uint8_t  data1;                 // note number
    uint8_t  data2;                 // velocity
    uint8_t  reserved;
    uint32_t frame_offset_samples;  // sample offset within buffer
};

struct VividMidiBuffer {
    VividMidiMessage messages[64];  // VIVID_MIDI_BUFFER_CAPACITY
    uint32_t count;
};
```

## Input Events (Mouse/Keyboard)

```cpp
#include "operator_api/input_state.h"

void process_frame(const VividFrameContext* ctx) override {
    const VividInputState* input = vivid_input(ctx);
    if (!input) return;

    // Current mouse position (normalized [0,1] texture coords)
    float mx = input->mouse_x;
    float my = input->mouse_y;

    // Process events
    for (uint32_t i = 0; i < input->event_count; i++) {
        const VividInputEvent& e = input->events[i];
        if (e.type == VIVID_INPUT_MOUSE_BUTTON && e.action == 1) {
            // mouse click at (e.mouse_x, e.mouse_y)
        }
    }
}
```

## Media Streams

```cpp
#include "operator_api/media_stream.h"

struct vivid::MediaStreamV1 {
    uint64_t handle_id;
    uint64_t session_ptr;       // direct pointer to media session
    uint64_t source_generation;
    uint32_t schema_version;    // = 1
    uint32_t flags;
    MediaClockV1 clock;         // local_time_s, duration_s, speed, playing, loop_enabled
};
```

Used by movie operators to share playback state across cadences (audio ↔ GPU).

## Shared Handle Service

Process-wide handle lifecycle management for cross-operator data sharing:

```cpp
const VividSharedHandleService* svc = ctx->shared_handles;
uint64_t id = svc->create("my_type", payload_ptr, generation);
svc->retain(id);
VividSharedHandleEntry entry = svc->resolve(id);
svc->release(id);
svc->invalidate(id, new_generation);
```

## GPU Types (gpu_types.h)

Structured GPU resource types for handle ports:

- `VividGpuBuffer` — GPU buffer with usage flags
- `VividComputeBuffer` — Compute buffer with element count/stride
- `VividMesh` — Vertex + optional index buffer with topology
- `VividVertexAttribute` — Vertex attribute layout

## main_thread_update

Optional override for non-audio-thread work (file I/O, AVFoundation decoding, ring buffer pre-fill):

```cpp
void main_thread_update(double time) override {
    // Called on main thread before each frame
    // File params are synced before this call
}
```

## Custom Thumbnails

See `docs/runtime/custom_thumbnails.md` for the thumbnail API.

## Canonical Examples

Reference operators for advanced patterns — study these when implementing specific capabilities.

| Pattern | Example Operator(s) |
|---|---|
| ChildOp\<T\> composites | `control/modulated_gain` |
| Custom value ports | `control/step_counter`, `control/sample_hold` (use `VIVID_CUSTOM_VALUE_PORT`) |
| Custom ref ports | `gpu/mesh_warp` (use `VIVID_CUSTOM_REF_PORT`) |
| MIDI input | `control/midi_input`, `audio/midi_file_player` |
| File drop params | `gpu/texture_loader`, `gpu/lut_apply`, `gpu/svg_render` |
| Input events (mouse/keyboard) | `control/mouse`, `control/keyboard` |
| Cross-cadence AV sync | `gpu/movie_file_in`, `audio/movie_file_audio` |
| GPU compute buffers | `gpu/texture_analysis` |
| Custom thumbnails | `control/envelope`, `control/clock`, `control/smooth` |
| Audio analysis / FFT | `control/fft_analysis`, `audio/audio_analysis` |

## Lane Behavior and Identity-Bearing Lane Sets

### Declaring Lane Behavior

```cpp
static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;
```

If omitted, the operator defaults to `VIVID_LANE_POINTWISE`.

### Behavior Classes

- **Pointwise** (default): processes one lane at a time. The runtime may create N instances for multi-lane inputs (lane lifting). Most operators are pointwise.
- **Structural**: creates, reshapes, or filters lane sets. Outputs get a fresh lane-set provenance. Example: voice allocator, collection generator.
- **Reduction**: collapses many lanes into fewer. Example: voice mixer, sum.
- **Kernel**: reads the full lane set with cross-lane access. Not lane-lifted; runs as a single instance with full lane data. Example: lane smoothing, FFT-bin interpolation.

### Per-Lane Persistent State

Audio operators can use `vivid_lane_state()` for persistent state keyed by lane identity (not positional index):

```cpp
struct Voice { double phase; float current_freq; bool was_gated; };
uint32_t lid = /* lane_id from upstream allocator */;
Voice& v = *vivid_lane_state(ctx, lid, Voice);
// v.phase, v.current_freq, etc. survive across callbacks for this lane_id
```

The state is zero-initialized on first access and stable until the lane_id is retired.

### Identity Allocation and Retirement

Structural operators that manage voice lifecycle call:
```cpp
uint32_t new_id = ctx->allocate_lane_id_fn(ctx->lane_state_service);  // fresh identity
ctx->retire_lane_id_fn(ctx->lane_state_service, old_id);              // deferred cleanup
```

Lane IDs are monotonic `uint32_t` values. Retirement triggers deferred cleanup on the next frame tick.
