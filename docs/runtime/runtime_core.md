# RuntimeCore — Graph Compilation and Frame-Rate Execution

## Overview

`RuntimeCore` (runtime_core.h/cpp) takes a `Graph` and `OperatorRegistry`, compiles them into a
`CompiledGraph`, and coordinates frame-rate execution. It delegates actual processing to
`FrameExecutor` and cross-cadence communication to `AudioFrameBridge`.

It also owns the live graph-metronome transport state. The persisted `Graph.metronome` definition
is save/load metadata; execution-time beat/bar phase is sampled from a lock-free runtime snapshot
that both the frame side and audio side read directly.

## Build

```cpp
bool build(const Graph& graph, OperatorRegistry& registry);
```

1. Compiles `Graph` → `CompiledGraph` via `GraphCompiler::compile()` (7-pass algorithm)
2. Builds `AudioFrameBridge` snapshot buffers from the compiled graph
3. Configures `FrameExecutor` with source directory for file param resolution
4. Seeds the live metronome state on first build, but later topology rebuilds preserve the current
   transport unless a caller explicitly resets it from loaded graph metadata

For async/UI-driven topology work, `RuntimeCore` also exposes a split prepare/adopt path:

```cpp
struct PreparedBuild {
    std::unique_ptr<CompiledGraph> compiled_graph;
    std::filesystem::path graph_base_dir;
};

bool prepare_build(const Graph& graph, OperatorRegistry& registry,
                   PreparedBuild& out, std::string* error = nullptr) const;
void adopt_prepared_build(PreparedBuild prepared);
```

This lets callers compile a candidate graph off the main interaction path, then
adopt the prepared result on the main thread without recompiling. `adopt_prepared_build()`
rebuilds the `AudioFrameBridge`, reapplies `operators_src_dir`, and resets solo state
exactly like a normal `build()`.

This seam is now used by both:
- async UI add-node transactions
- async UI graph-load/open/reload transactions

In both cases the worker thread prepares a candidate `CompiledGraph`, while the main thread keeps
the live runtime running until commit time. The commit step still owns audio shutdown/restart,
GPU texture allocation, and any graph-identity bookkeeping.

Operator availability preparation is now expected to happen before `prepare_build()` via the
shared `OperatorPreparationService`. That keeps async UI transactions and blocking runtime callers
on the same deferred-load path instead of having each flow call `OperatorRegistry` directly.

## Tick

```cpp
void tick(double time, double delta_time, uint64_t frame,
          void* gpu_state = nullptr,
          PostNodeFn on_gpu_node = nullptr,
          const VividInputState* input = nullptr);
```

1. Calls `FrameExecutor::tick()` — processes all `Cadence::Frame` nodes in topological order
   using the current live metronome sample
2. Propagates control→audio param wires on `CompiledNode` for inspector display
3. Checks `needs_gpu_realloc_` flag for topology-driven texture reallocation

The audio side receives the same transport through `AudioExecutor`, which samples the same
runtime-owned metronome store at audio-callback time. This is what makes BPM edits live and
phase-continuous without recompiling the graph.

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
