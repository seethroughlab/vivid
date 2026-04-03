# Vivid Runtime Architecture

## Component Map

```
main.cpp
 ├── GpuContext          — WebGPU/Dawn init, surface, FrameState
 ├── OperatorRegistry    — type name → OperatorLoader, deferred probe
 ├── Graph               — serializable scene description (NodeDef/ConnectionDef)
 ├── RuntimeCore         — graph compilation, frame-rate execution, audio frame bridge
 │    ├── GraphCompiler  — 7-pass pipeline: Graph → CompiledGraph
 │    ├── CompiledGraph  — unified node/edge representation, shared by both executors
 │    ├── FrameExecutor  — frame-rate + GPU node processing (~60 Hz)
 │    └── AudioFrameBridge — double-buffered snapshot bridge (frame ↔ audio)
 ├── AudioEngine         — audio device lifecycle (thin facade over AudioExecutor)
 │    └── AudioExecutor  — audio-rate node processing (~48 kHz)
 ├── RuntimeAPI          — high-level commands over graph+runtime+audio
 ├── ControlServer       — HTTP JSON-RPC server, drains requests each frame
 ├── HotReloader         — background compile thread, staged dylib swap
 ├── PackageManager      — install/link/rebuild packages
 └── PackageCompiler     — clang++/cmake compile drivers
```

## Two Cadences

| Cadence | Thread            | Rate                       | Executor                       |
|---------|-------------------|----------------------------|--------------------------------|
| Frame   | Main              | ~60 Hz                     | `FrameExecutor::tick()`        |
| Audio   | Audio (miniaudio) | 48 kHz / 256-sample buffer | `AudioExecutor::audio_callback()` |

GPU nodes run at frame cadence on the main thread, with `GpuContext` providing the command encoder.

All operators come from the same `OperatorRegistry`/`OperatorLoader` system.
Audio-cadence operators must never allocate or block.

Lane-bearing values participate in this cadence model without creating a second collection system.
Cross-cadence transport copies lane-bearing values through `AudioFrameBridge` snapshots, but the
lane model itself remains the same on both sides of the boundary.

## Main Loop (main.cpp)

Each frame:
1. `control_server.process_requests(runtime_api, graph, runtime, registry, ...)` — drain HTTP queue, apply topology changes
2. `runtime_api.apply_midi_mappings()` — map CC values to params
3. `runtime_api.tick_quantized_switch()` — fire pending variation switches on beat/bar boundaries
4. `runtime_api.tick_state_presets()` — apply state machine → preset transitions
5. `runtime.pre_tick_audio_sync(time)` — pull audio analysis into frame-rate nodes, push display params
6. `gpu_context.begin_frame()` → `runtime.tick(time, delta, frame, gpu_state)` → `gpu_context.end_frame()`
7. `runtime.post_tick_audio_sync()` — snapshot frame-rate outputs into ParamSnapshot for audio consumption

## Startup Sequence

1. Parse CLI args / load settings
2. `gpu_context.init(window, w, h)` — create WebGPU device
3. `registry.scan_deferred(builtins_dir)` — probe all built-in dylibs
4. `registry.scan_shader_operators(presets_dir)` — register built-in `.wgsl` shader operators
5. `pm.scan_installed()` — probe user packages
6. `graph.load(path)` — parse JSON graph
7. if the graph has a saved path, `registry.scan_shader_operators(<graph_dir>/filters, mark_user=true)` — register project-local shader operators
8. `runtime.build(graph, registry)` — compile graph, instantiate operators, resolve edges
9. `runtime.allocate_gpu_textures(device, w, h, format)` — allocate per-node textures
10. `audio_engine.build(runtime)` — build AudioExecutor from CompiledGraph
11. `audio_engine.start()` — start miniaudio device
12. `control_server.start(9876)` — start HTTP server
13. Enter main loop

## Topology Changes

Topology changes (`add_node`, `remove_node`, `connect`, `disconnect`) are buffered by `RuntimeAPI`
via a `pending_topology_change_` flag. They are only applied between frames via
`RuntimeAPI::apply_pending()` (called inside `ControlServer::process_requests()`).

This ensures the compiled graph and audio engine are never mutated while `tick()` is running.

The same transactional expectation now applies to graph-wide rebuild flows:

- `reload()` restores the previous graph/runtime state if load or rebuild fails
- `apply_snapshot_json()` restores the previous graph/runtime state if parse or rebuild fails
- package mutation flows that affect the active graph rebuild through the same transactional path

## Hot Reload Path

1. `.cpp` file watcher (or MCP `rebuild_package`) triggers `hot_reloader.queue_rebuild(target_name)`
2. Background compile thread: `cmake --build --target <name>` → staged .dylib in `/tmp/vivid_staging/`
3. `hot_reloader.poll_ready()` returns `ReloadResult` with `staged_dylib_path`
4. Main thread: `runtime.reload_operator(type_name, registry, new_path)` — swap dylib, preserve params
5. `audio_engine.pre_reload_operator(type_name)` / `audio_engine.post_reload_operator(type_name, registry)` — same for audio nodes

Shader-backed `.wgsl` operators use a split hot-reload path:

- body-only edits stay inside `WgslFilterBase` and recompile the shader in place
- header-shape edits are watched at the runtime level, rescan the affected shader-operator directory,
  and rebuild the current graph transactionally

Hot reload is intentionally conservative after the audit hardening work:

- success requires both runtime-side and audio-side reload to succeed
- incompatible descriptor changes are rejected rather than partially reusing stale runtime metadata
- malformed plugins and custom-type registration failures are surfaced through registry diagnostics

## Key Invariants

- One `CompiledGraph` is shared (read) by both `FrameExecutor` and `AudioExecutor`. No parallel independent builds.
- Audio thread reads `ParamSnapshot` atomically via `AudioFrameBridge`; never touches `CompiledNode` state directly.
- GPU textures are allocated once at build time and reallocated on window resize or node addition via `needs_gpu_realloc_`.
- `OperatorRegistry::find()` may trigger a lazy dlopen for deferred entries.
- UI-facing graph views should remain faithful to graph truth, including broken connections, rather
  than silently dropping unresolved edges from snapshots.
