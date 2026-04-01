# RuntimeCore — Graph Compilation and Frame-Rate Execution

## Overview

`RuntimeCore` (runtime_core.h/cpp) takes a `Graph` and `OperatorRegistry`, compiles them into a
`CompiledGraph`, and coordinates frame-rate execution. It delegates actual processing to
`FrameExecutor` and cross-cadence communication to `AudioFrameBridge`.

## Build

```cpp
bool build(const Graph& graph, OperatorRegistry& registry);
```

1. Compiles `Graph` → `CompiledGraph` via `GraphCompiler::compile()` (7-pass algorithm)
2. Builds `AudioFrameBridge` snapshot buffers from the compiled graph
3. Configures `FrameExecutor` with source directory for file param resolution

## Tick

```cpp
void tick(double time, double delta_time, uint64_t frame,
          void* gpu_state = nullptr,
          PostNodeFn on_gpu_node = nullptr,
          const VividInputState* input = nullptr);
```

1. Calls `FrameExecutor::tick()` — processes all `Cadence::Frame` nodes in topological order
2. Propagates control→audio param wires on `CompiledNode` for inspector display
3. Checks `needs_gpu_realloc_` flag for topology-driven texture reallocation

## GPU Texture Management

```cpp
void allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                           WGPUTextureFormat format, WGPUTextureUsage extra_usage = 0);
int find_gpu_sink() const;
int find_effective_gpu_sink() const;  // solo-aware
```

Per-node textures allocated at build time. Filter nodes inherit upstream size.
`needs_gpu_realloc_` triggers reallocation after topology changes.

## Solo Mode

Session-only (not serialized). Restricts GPU/audio output to one node and its upstream dependencies:

```cpp
void set_solo(int node_idx);   // -1 to clear
```

Uses BFS over `upstream_nodes` to build the active set, which is forwarded to both
`FrameExecutor` (skip logic) and `AudioFrameBridge` → `AudioExecutor` (via `ParamSnapshot`).

## Hot-Reload

```cpp
bool reload_operator(const std::string& type_name, OperatorRegistry& registry,
                     const std::string& new_dylib_path);
```

1. Saves param values by name for all instances of `type_name`
2. Destroys old instances
3. Reloads dylib via `OperatorRegistry`
4. Recreates instances with param reconciliation via `GraphCompiler::init_frame_state()`
5. Marks affected nodes dirty to force downstream recompute

On dylib reload failure, recreates instances from the old loader so nodes keep running.

## Key Types

See `compiled_graph.h` for `CompiledNode`, `CompiledEdge`, `CompiledGraph`, `Cadence`, and
`EdgeTransport`. These replaced the earlier `NodeState` and `Wire` types.
