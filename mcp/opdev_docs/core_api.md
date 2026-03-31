# Core Operator API

## Operator Registration

Every operator must:
1. Define `static constexpr const char* kName` — the type name shown in the graph
2. Define `static constexpr bool kTimeDependent` — set `true` if the operator reads `ctx->time`
3. Inherit from `vivid::OperatorBase` and implement one or more capability interfaces: `vivid::FrameProcessable`, `vivid::AudioProcessable`, or `vivid::GpuProcessable`
4. Override `collect_params()` and `collect_ports()` to declare params and ports
5. Override the capability-specific process method
6. End the `.cpp` file with `VIVID_REGISTER(ClassName)`

Optionally declare lane behavior (defaults to `VIVID_LANE_POINTWISE` if omitted):
```cpp
static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
```

Lane behavior classes:
| Constant | Meaning |
|----------|---------|
| `VIVID_LANE_POINTWISE` | Processes each lane independently (default). Runtime may lift to N lanes. |
| `VIVID_LANE_STRUCTURAL` | Creates, reshapes, or filters lane sets (e.g., voice allocator, collection generator). |
| `VIVID_LANE_REDUCTION` | Collapses many lanes into fewer (e.g., voice mixer, sum). |
| `VIVID_LANE_KERNEL` | Reads full lane set with cross-lane access (e.g., smoothing, convolution). |

```cpp
#include "operator_api/operator.h"

struct MyOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "MyOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> amount{"amount", 0.5f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0] * amount.value;
    }
};

VIVID_REGISTER(MyOp)
```

## Param\<T\> Types

| Type | Constructor | Value Access |
|------|------------|--------------|
| `Param<float>` | `{"name", default, min, max}` | `.value` (float) |
| `Param<int>` | `{"name", default, min, max}` | `.value` (float), `.int_value()` (int) |
| `Param<int>` (enum) | `{"name", default, {"label0", "label1", ...}}` | `.int_value()` — index into labels |
| `Param<bool>` | `{"name", default}` | `.value > 0.5f`, `.bool_value()` |
| `Param<FilePath>` | `{"name", "default_path"}` | `.str_value` (std::string) |
| `Param<TextValue>` | `{"name", "default_text"}` | `.str_value` (std::string) |

Params are declared as member variables. The runtime syncs `ctx->param_values` into each `Param<T>.value` before every process call.

## Port Types

### Built-in Types

| Constant | Wire Type | Usage |
|----------|-----------|-------|
| `VIVID_PORT_SIGNAL` | `control_float` | Scalar control signals |
| `VIVID_PORT_AUDIO` | `audio_float` | Audio sample buffers |
| `VIVID_PORT_TEXTURE` | `gpu_texture` | GPU textures |
| `VIVID_PORT_LANE_ARRAY` | lane array | Variable-length float arrays (lane-bearing data transport) |
| `VIVID_PORT_STRING` | string | UTF-8 strings |
| `VIVID_PORT_STRING_SPREAD` | string spread | Variable-length string arrays |

### Custom Types

Operators can define custom port types for exchanging arbitrary typed data (media streams, meshes, compute buffers, etc.) through the graph. Custom type IDs are generated at compile time via `vivid_port_type<T>()`.

Two transport modes are available:

| Transport | Constant | Usage |
|-----------|----------|-------|
| **CUSTOM_VALUE** | `VIVID_PORT_TRANSPORT_CUSTOM_VALUE` | Small structs copied by value (≤256 bytes) |
| **CUSTOM_REF** | `VIVID_PORT_TRANSPORT_CUSTOM_REF` | Opaque pointer via shared handle registry (any size) |

Declare custom ports with the `VIVID_CUSTOM_PORT` macro:
```cpp
VIVID_CUSTOM_PORT("media_stream", VIVID_PORT_INPUT, vivid::MediaStreamV1, VIVID_PORT_TRANSPORT_CUSTOM_REF)
```

Register custom types by adding `VIVID_DESCRIBE_REF_TYPE(T)` at file scope (for `CUSTOM_REF` types):
```cpp
VIVID_DESCRIBE_REF_TYPE(vivid::MediaStreamV1)
```

Custom inputs/outputs are accessed via `ctx->custom_inputs[i]` and `ctx->custom_outputs[i]`.

### Port Descriptor

```cpp
VividPortDescriptor {
    const char* name,
    VividPortType type,
    VividPortDirection direction,  // VIVID_PORT_INPUT or VIVID_PORT_OUTPUT
    VividPortTransport transport,  // how the payload is conveyed
    uint32_t payload_size,         // sizeof(T) for custom types
    const char* type_name,         // human-readable type name for custom types
    uint8_t channels               // audio: 0=auto, 1=mono, 2=stereo
};
```

## Semantic Metadata

Params can carry optional semantic metadata for tooling/introspection:

```cpp
MyOp() {
    vivid::semantic_tag(frequency, "frequency_hz");
    vivid::semantic_shape(frequency, "scalar");
    vivid::semantic_unit(frequency, "Hz");
    vivid::semantic_intent(frequency, "animation_rate");
}
```

- **semantic_tag**: Category (e.g. `"frequency_hz"`, `"gate"`, `"color_rgba"`, `"amplitude_linear"`)
- **semantic_shape**: Data shape (e.g. `"scalar"`, `"vec2"`, `"color"`, `"event"`)
- **semantic_unit**: Physical unit (e.g. `"Hz"`, `"s"`, `"dB"`)
- **semantic_intent**: Free-form hint (e.g. `"input_gain"`, `"dc_offset"`)

## Display Hints

Control how params appear in the inspector:
```cpp
vivid::display_hint(param, VIVID_DISPLAY_KNOB);      // circular knob
vivid::display_hint(param, VIVID_DISPLAY_XY_PAD);     // 2D pad (pair consecutive x/y)
vivid::display_hint(param, VIVID_DISPLAY_COLOR);       // color swatch (triple r/g/b)
vivid::param_group(param, "Envelope");                 // collapsible group
vivid::layout_row(param, 2, 0);                        // 2 columns, this is column 0
```

## Capability Interfaces

All operators inherit from `vivid::OperatorBase` and implement one or more capability interfaces. `VIVID_REGISTER` auto-detects capabilities via `std::is_base_of`.

| Capability Interface | Process Method | Execution Environment |
|---------------------|---------------|----------------------|
| `vivid::FrameProcessable` | `process_frame(const VividFrameContext* ctx)` | Control (main thread, ~60 Hz) |
| `vivid::AudioProcessable` | `process_audio(const VividAudioContext* ctx)` | Audio (audio thread, per-buffer) |
| `vivid::GpuProcessable` | `process_gpu(const VividGpuContext* ctx)` | GPU (main thread, ~60 Hz) |

An operator implementing both `FrameProcessable` and `AudioProcessable` is "dual-cadence" (audio-capable) and receives callbacks at both frame rate and audio rate.

## ABI Version

Current ABI version: `VIVID_OPERATOR_ABI_VERSION = 4`. The runtime checks this on load.
