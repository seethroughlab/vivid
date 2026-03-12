# Core Operator API

## Operator Registration

Every operator must:
1. Define `static constexpr const char* kName` — the type name shown in the graph
2. Define `static constexpr bool kTimeDependent` — set `true` if the operator reads `ctx->time`
3. Inherit from one domain base class: `ControlOperatorBase`, `AudioOperatorBase`, or `GpuOperatorBase`
4. Override `collect_params()` and `collect_ports()` to declare params and ports
5. Override the domain-specific process method
6. End the `.cpp` file with `VIVID_REGISTER(ClassName)`

```cpp
#include "operator_api/operator.h"

struct MyOp : vivid::ControlOperatorBase {
    static constexpr const char* kName = "MyOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> amount{"amount", 0.5f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
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

| Enum | Wire Type | Usage |
|------|-----------|-------|
| `VIVID_PORT_FLOAT` | `control_float` | Scalar control signals |
| `VIVID_PORT_AUDIO` | `audio_float` | Audio sample buffers |
| `VIVID_PORT_TEXTURE` | `gpu_texture` | GPU textures |
| `VIVID_PORT_SPREAD` | spread | Variable-length float arrays |
| `VIVID_PORT_STRING` | string | UTF-8 strings |
| `VIVID_PORT_STRING_SPREAD` | string spread | Variable-length string arrays |
| `VIVID_PORT_HANDLE` | handle | Typed opaque pointers (use `VIVID_HANDLE_PORT` macro) |

Port descriptor fields:
```cpp
VividPortDescriptor {
    const char* name,
    VividPortType type,
    VividPortDirection direction,  // VIVID_PORT_INPUT or VIVID_PORT_OUTPUT
    uint32_t handle_type_id,       // non-zero for HANDLE ports (FNV-1a of C++ type)
    uint8_t channels                // audio: 0=auto, 1=mono, 2=stereo
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

## Base Classes

| Base Class | Process Method | Domain |
|-----------|---------------|--------|
| `ControlOperatorBase` | `process(const VividProcessContext* ctx)` | Control (main thread, ~60 Hz) |
| `AudioOperatorBase` | `process_audio(const VividAudioContext* ctx)` | Audio (audio thread, per-buffer) |
| `GpuOperatorBase` | `process_gpu(const VividGpuContext* ctx)` | GPU (main thread, ~60 Hz) |

## ABI Version

Current ABI version: `VIVID_OPERATOR_ABI_VERSION = 2`. The runtime checks this on load.
