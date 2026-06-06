# Control Domain

Control operators run on the main thread at frame rate (~60 Hz). They process scalar values, lane arrays, strings, and custom ports.

## Capability Interface

```cpp
struct MyControlOp : vivid::OperatorBase, vivid::FrameProcessable {
    void process_frame(const VividFrameContext* ctx) override;
};
```

## VividFrameContext Fields

### Read-only Inputs
| Field | Type | Description |
|-------|------|-------------|
| `time` | `double` | Elapsed time in seconds |
| `delta_time` | `double` | Time since last frame |
| `frame` | `uint64_t` | Frame counter |
| `param_values` | `float*` | Indexed by param descriptor order (auto-synced to Param<T>.value) |
| `input_values` | `float*` | Float input ports, indexed by input port order |
| `output_values` | `float*` | Float output ports, indexed by output port order — **write scalar outputs here** |
| `values` | `const VividValueView*` | **Value API** — one input value per input port (scalar or many; read via `vivid_value_floats`/`_strings` + `vivid_value_count`). The canonical many-valued input. |
| `value_outputs` | `VividValueOutput*` | **Value API** — one output builder per output port (`vivid_value_output_floats`/`_set_string` + `vivid_value_output_commit`). The canonical many-valued output. |
| `input_lanes` | `const VividLaneView*` | _Legacy (removed Phase 7)_ — use `values` instead. |
| `output_lanes` | `VividLaneOutput*` | _Legacy (removed Phase 7)_ — use `value_outputs` instead. |
| `custom_inputs` | `void**` | Custom-port inputs (`CUSTOM_VALUE` / `CUSTOM_REF`) |
| `custom_outputs` | `void**` | Custom-port outputs (`CUSTOM_VALUE` / `CUSTOM_REF`) |
| `input_string_values` | `const char**` | String input ports |
| `output_string_values` | `const char**` | String output ports (write pointers here) |
| `input_string_lanes` | `const VividStringLaneView*` | String lane array inputs (`.data`, `.length`, `.lane_set_id`, `.flags`) |
| `output_string_lanes` | `VividStringLaneOutput*` | String lane array outputs (runtime-owned builder: `.handle`, `.resize()`, `.set()`, `.commit()`) |
| `file_param_values` | `const char**` | File/text param string values |
| `input` | `void*` | Cast to `VividInputState*` for interactive operators |
| `shared_handles` | `VividSharedHandleService*` | Process-wide handle service |
| `lane_count` | `uint32_t` | Runtime lane count (max input lane array length, 1 = scalar) |
| `lane_index` | `uint32_t` | Current lane in LoopBased (0 = scalar or first lane) |
| `lane_set_id` | `uint32_t` | Lane provenance (0 = scalar, nonzero = upstream structural node) |

### Write-back Fields
| Field | Type | Description |
|-------|------|-------------|
| `preferred_tex_width` | `uint32_t` | Request texture reallocation (0 = no change) |
| `preferred_tex_height` | `uint32_t` | Request texture reallocation (0 = no change) |

## Port Indexing

Port indices are counted separately for inputs and outputs, in the order declared in `collect_ports()`. Only ports of the matching type contribute to each index array:

- Float ports → `input_values[i]` / `output_values[i]`
- Lane array ports → `input_lanes[i]` / `output_lanes[i]`
- String ports → `input_string_values[i]` / `output_string_values[i]`
- Custom ports → `custom_inputs[i]` / `custom_outputs[i]`

## Many-Valued Ports — the value API

A port carries one *value* that may be Scalar or Many. Read inputs via
`ctx->values[port]` and write outputs via `ctx->value_outputs[port]`
(`#include "operator_api/value_view.h"`):

```cpp
// Reading an input value (scalar → count 1; many → count N)
const VividValueView* in = &ctx->values[0];
const float* v = vivid_value_floats(in);
uint32_t n = vivid_value_count(in);
for (uint32_t i = 0; i < n; ++i) { float x = v[i]; /* ... */ }

// Writing an output value (builder)
float* buf = vivid_value_output_floats(&ctx->value_outputs[0], count);
if (buf) {
    for (uint32_t i = 0; i < count; ++i) buf[i] = computed_value;
    vivid_value_output_commit(&ctx->value_outputs[0], count);
}
```

A many-valued output port currently still declares `VIVID_PORT_LANE_ARRAY` (the port
type is removed in Phase 7); the I/O above is the same regardless.

> _Legacy (removed Phase 7):_ `ctx->input_lanes[i]` (`.data`/`.length`) +
> `ctx->output_lanes[i]` (`.resize`/`.commit`) — superseded by the value API above.

## Example: Simple Control Operator

```cpp
#include "operator_api/operator.h"

struct Multiply : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "Multiply";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> factor{"factor", 1.0f, -100.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&factor);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0] * factor.value;
    }
};
```
