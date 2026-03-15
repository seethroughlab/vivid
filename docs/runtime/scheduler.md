# Scheduler — Live Execution Graph

## Overview

`Scheduler` (scheduler.h/cpp — lives in the same header as `NodeState`/`Wire`) takes a `Graph` and
`OperatorRegistry` and builds a live execution graph. It runs `tick()` once per frame on the main
thread, evaluating all control and GPU operators in dependency order.

## Key Structs

### `NodeState`
The live runtime state of one operator instance:
- **Identity**: `node_id`, `type_name`, `loader`, `instance` (opaque operator pointer)
- **Port config**: `input_port_count`, `output_port_count`, all `_values`/`_types`/`_indices` maps
- **Generation-based cooking**: `time_dependent`, `is_gpu`, `is_audio`, `generation`, `last_processed_gen`
- **Spread data**: `output_spreads`/`input_spreads` (per tick, `[port_idx] → float[]`)
- **GPU resources**: `gpu_texture`, `gpu_texture_view`, `gpu_tex_width/height`, `is_gpu_sink`
- **Custom ports**: `custom_input_port_indices`, `resolved_custom_inputs`, `custom_output_buf`
- **Error state**: `errored`, `error_message` (set in try/catch in `tick()`, cleared on reload)
- **Missing operator**: `missing_operator = true` when graph references an unknown type (no crash)

### `Wire`
Connects two `NodeState` ports by index:
```cpp
struct Wire {
    uint32_t from_node_idx, from_port_idx;
    uint32_t to_node_idx, to_port_idx;
    bool sources_param, targets_param;       // wire feeds a param slot, not an output port
    bool sources_file_param, targets_file_param;
    bool is_texture_wire, is_custom_wire, is_string_wire, is_string_spread_wire;
    float from_min, from_max, to_min, to_max;
    bool clamp;
};
```

### `ParamLockFlags`
```cpp
PARAM_LOCK_NONE    = 0
PARAM_LOCK_WIRES   = 1  // wire cannot override this param
PARAM_LOCK_PRESETS = 2  // preset recall cannot override this param
PARAM_LOCK_ALL     = 3
```

## `Scheduler` API

### Build
```cpp
bool build(const Graph& graph, OperatorRegistry& registry);
```
- Instantiates all operators (`loader->create_instance()`)
- Resolves connections to `Wire` structs (with index lookup, remap, port type validation)
- Computes topological sort order for deterministic tick execution
- Populates `upstream_nodes` for each `NodeState`
- Marks `is_gpu_sink` (GPU node with ≥1 texture input and 0 texture outputs)

### Tick
```cpp
void tick(double time, double delta_time, uint64_t frame,
          void* gpu_state = nullptr,
          PostNodeFn on_gpu_node = nullptr,
          const VividInputState* input = nullptr);
```
- Increments global generation counter
- For each node in topo order: propagates wire values, calls `loader->process()` or `loader->process_gpu()`
- Generation-based memoization: non-time-dependent nodes skip re-evaluation if all inputs unchanged
- `on_gpu_node` callback fires after each GPU node's `process_gpu()` — used for thumbnail capture

### GPU Texture Management
```cpp
void allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                           WGPUTextureFormat format, WGPUTextureUsage extra_usage = 0);
int find_gpu_sink() const;          // first GPU sink node index (-1 if none)
int find_effective_gpu_sink() const; // solo-aware version
```
Per-node textures are allocated at `allocate_gpu_textures()` time. Each GPU node gets its own
`WGPUTexture` (size from `NodeDef::tex_width/height`, falling back to `default_w/h`).
`needs_gpu_realloc_` flag triggers reallocation on next frame after topology changes.

### Solo Mode
Session-only (not serialized). Restricts GPU output to one node:
```cpp
void set_solo(int node_idx);         // -1 to clear
int solo_node_idx() const;
bool is_solo_active() const;
const std::vector<bool>& solo_active_set() const;  // [node_idx] → included in solo path
```

### Hot-Reload
```cpp
bool reload_operator(const std::string& type_name, OperatorRegistry& registry,
                     const std::string& new_dylib_path);
```
For all nodes with matching `type_name`:
1. Captures current param values
2. Destroys old instance
3. Updates `OperatorRegistry` with new dylib
4. Recreates instance via `reinit_node_state()` with param overrides

### External Injection (for testing / control-server inspection)
```cpp
void inject_external_output(uint32_t node_idx, uint32_t port_idx, float value);
void inject_external_spread(uint32_t node_idx, uint32_t port_idx, const float* data, uint32_t length);
```

## Topo Sort

The scheduler performs a topological sort (Kahn's algorithm) at `build()` time.
Cycles are detected and reported. Test: `tests/test_topo_sort.cpp`.

## `PostNodeFn` Callback

```cpp
using PostNodeFn = std::function<void(uint32_t node_idx, const std::string& node_id,
                                      WGPUTextureView texture_view)>;
```
Called after each GPU node's `process_gpu()`. Used by `main.cpp` for thumbnail capture
(passes to `CaptureCoordinator`).

## Audio Domain Interaction

Audio operators appear in `Scheduler::nodes_` (for param wiring from control domain) but
`is_audio = true` nodes are **not** processed during `Scheduler::tick()`.
They exist only so that control-domain output ports can feed cross-domain wires.
The `AudioEngine` has its own parallel `AudioNodeState` array.
