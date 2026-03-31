# Control Domain

Control operators run on the main thread at frame rate (~60 Hz). They process scalar values, spreads, strings, and handles.

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
| `output_values` | `float*` | Float output ports, indexed by output port order — **write your outputs here** |
| `input_lanes` | `VividLanePort*` | Spread input ports (`.data`, `.length`) |
| `output_lanes` | `VividLanePort*` | Spread output ports (write `.data`, set `.length`) |
| `input_handles` | `void**` | Handle input ports |
| `output_handles` | `void**` | Handle output ports |
| `input_string_values` | `const char**` | String input ports |
| `output_string_values` | `const char**` | String output ports (write pointers here) |
| `input_string_lanes` | `VividStringSpreadPort*` | String spread inputs |
| `output_string_lanes` | `VividStringSpreadPort*` | String spread outputs |
| `file_param_values` | `const char**` | File/text param string values |
| `input` | `void*` | Cast to `VividInputState*` for interactive operators |
| `shared_handles` | `VividSharedHandleService*` | Process-wide handle service |
| `lane_count` | `uint32_t` | Runtime lane count (max input spread length, 1 = scalar) |
| `lane_index` | `uint32_t` | Always 0 (no per-lane frame lifting yet) |
| `lane_set_id` | `uint32_t` | Lane provenance (0 = scalar, nonzero = upstream structural node) |

### Write-back Fields
| Field | Type | Description |
|-------|------|-------------|
| `preferred_tex_width` | `uint32_t` | Request texture reallocation (0 = no change) |
| `preferred_tex_height` | `uint32_t` | Request texture reallocation (0 = no change) |

## Port Indexing

Port indices are counted separately for inputs and outputs, in the order declared in `collect_ports()`. Only ports of the matching type contribute to each index array:

- Float ports → `input_values[i]` / `output_values[i]`
- Spread ports → `input_lanes[i]` / `output_lanes[i]`
- String ports → `input_string_values[i]` / `output_string_values[i]`
- Handle ports → `input_handles[i]` / `output_handles[i]`

## Spread Ports

```cpp
// Reading input spread
const VividLanePort& sp = ctx->input_lanes[0];
for (uint32_t i = 0; i < sp.length; i++) {
    float val = sp.data[i];
}

// Writing output spread
VividLanePort& out = ctx->output_lanes[0];
out.length = count;  // must not exceed capacity
for (uint32_t i = 0; i < count; i++) {
    out.data[i] = computed_value;
}
```

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
        out.push_back({"input",  VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0] * factor.value;
    }
};

VIVID_REGISTER(Multiply)
```
