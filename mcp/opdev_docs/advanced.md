# Advanced API Features

## ChildOp\<T\> — Composite Operators

Embed an operator as a persistent member variable inside another operator. Control domain only.

```cpp
#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"

struct ModulatedGain : vivid::ControlOperatorBase {
    static constexpr const char* kName = "ModulatedGain";
    static constexpr bool kTimeDependent = true;

    vivid::ChildOp<LFO> lfo;
    vivid::Param<float> depth{"depth", 0.5f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&depth);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
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
| `set_input_spread(name, data, length)` | Set child spread input |
| `process(parent_ctx)` | Run child (inherits time/frame) |
| `output(name)` | Read child float output |
| `output_spread_data(name)` | Read child spread output data |
| `output_spread_length(name)` | Read child spread output length |
| `op()` | Direct access to underlying operator instance |

## Handle Ports

Typed opaque pointers for passing complex data between operators (e.g. MIDI buffers, media streams, 3D scene fragments).

```cpp
#include "operator_api/type_id.h"

// Producer
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    out.push_back(VIVID_HANDLE_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
}

void process(const VividProcessContext* ctx) override {
    auto* buf = static_cast<VividMidiBuffer*>(ctx->output_handles[0]);
    // write MIDI messages...
}

// Consumer
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    out.push_back(VIVID_HANDLE_PORT("midi_in", VIVID_PORT_INPUT, VividMidiBuffer));
}

void process(const VividProcessContext* ctx) override {
    auto* buf = static_cast<const VividMidiBuffer*>(ctx->input_handles[0]);
    if (buf) { /* read messages */ }
}
```

Type safety: `vivid_type_id<T>()` produces a compile-time FNV-1a hash of the C++ type. The runtime rejects connections between incompatible handle types.

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

void process(const VividProcessContext* ctx) override {
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

Used by movie operators to share playback state across domains (audio ↔ GPU).

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

Override `draw_thumbnail` to render a custom preview in the node graph:

```cpp
void draw_thumbnail(const VividThumbnailContext* ctx) override {
    // ctx->pixels: RGBA8 buffer (140×88), row-major
    // ctx->output_values, ctx->param_values available
}
```

Use `VIVID_THUMBNAIL(ClassName)` alongside `VIVID_REGISTER(ClassName)` to export the entry point.
