# Vivid Runtime Architecture

## Component Map

```
main.cpp
 ├── GpuContext          — WebGPU/Dawn init, surface, FrameState
 ├── OperatorRegistry    — type name → OperatorLoader, deferred probe
 ├── Graph               — serializable scene description (NodeDef/ConnectionDef)
 ├── Scheduler           — live execution graph (NodeState/Wire), control+GPU tick
 ├── AudioEngine         — audio thread, ParamSnapshot bridge
 ├── RuntimeAPI          — high-level commands over graph+scheduler+audio
 ├── ControlServer       — HTTP JSON-RPC server, drains requests each frame
 ├── HotReloader         — background compile thread, staged dylib swap
 ├── PackageManager      — install/link/rebuild packages
 └── PackageCompiler     — clang++/cmake compile drivers
```

## Three Domains

| Domain   | Thread       | Rate             | Executor           |
|----------|-------------|------------------|--------------------|
| Control  | Main         | ~60 Hz           | `Scheduler::tick()`|
| Audio    | Audio (miniaudio) | 48 kHz / 256 frames | `AudioEngine::audio_callback()` |
| GPU      | Main         | ~60 Hz           | `Scheduler::tick()` + `GpuContext` |

All three domains run operators from the same `OperatorRegistry`/`OperatorLoader` system.
Audio operators run on a dedicated thread and must never allocate or block.

## Main Loop (main.cpp)

Each frame:
1. `control_server.process_requests(api, graph, scheduler, registry, ...)` — drain HTTP queue, apply topology changes
2. `api.apply_midi_mappings()` — map CC values to params
3. `api.tick_quantized_switch()` — fire pending variation switches on beat/bar boundaries
4. `api.tick_state_presets()` — apply state machine → preset transitions
5. `audio_engine.push_params(scheduler)` — snapshot control params into audio double-buffer
6. `audio_engine.update_sources(time, scheduler)` — push cross-domain wire values
7. `gpu_context.begin_frame()` → `scheduler.tick(time, delta, frame, gpu_state)` → `gpu_context.end_frame()`
8. `audio_engine.inject_analysis(scheduler)` — push RMS/peak/waveform back to control domain

## Startup Sequence

1. Parse CLI args / load settings
2. `gpu_context.init(window, w, h)` — create WebGPU device
3. `registry.scan_deferred(builtins_dir)` — probe all built-in dylibs
4. `registry.scan_wgsl_presets(presets_dir)` — register data-driven WGSL filters
5. `pm.scan_installed()` — probe user packages
6. `graph.load(path)` — parse JSON graph
7. `scheduler.build(graph, registry)` — instantiate all operators, resolve wires
8. `scheduler.allocate_gpu_textures(device, w, h, format)` — allocate per-node textures
9. `audio_engine.build(graph, registry, scheduler)` — build audio subgraph
10. `audio_engine.start()` — start miniaudio device
11. `control_server.start(9876)` — start HTTP server
12. Enter main loop

## Topology Changes

Topology changes (`add_node`, `remove_node`, `connect`, `disconnect`) are buffered by `RuntimeAPI`
via a `pending_topology_change_` flag. They are only applied between frames via
`RuntimeAPI::apply_pending()` (called inside `ControlServer::process_requests()`).

This ensures the scheduler and audio engine are never mutated while `tick()` is running.

The same transactional expectation now applies to graph-wide rebuild flows:

- `reload()` restores the previous graph/runtime state if load or rebuild fails
- `apply_snapshot_json()` restores the previous graph/runtime state if parse or rebuild fails
- package mutation flows that affect the active graph rebuild through the same transactional path

## Hot Reload Path

1. File watcher (or MCP `rebuild_package`) triggers `hot_reloader.queue_rebuild(target_name)`
2. Background compile thread: `cmake --build --target <name>` → staged .dylib in `/tmp/vivid_staging/`
3. `hot_reloader.poll_ready()` returns `ReloadResult` with `staged_dylib_path`
4. Main thread: `scheduler.reload_operator(type_name, registry, new_path)` — swap dylib, preserve params
5. `audio_engine.reload_operator(type_name, registry)` — same for audio nodes

Hot reload is intentionally conservative after the audit hardening work:

- success requires both scheduler-side and audio-side reload to succeed
- incompatible descriptor changes are rejected rather than partially reusing stale runtime metadata
- malformed plugins and custom-type registration failures are surfaced through registry diagnostics

## Key Invariants

- `Scheduler::nodes_` and `AudioEngine::nodes_` are **parallel but independent** builds from the same `Graph`.
- Audio thread reads `ParamSnapshot` atomically; never touches `Scheduler::nodes_` directly.
- GPU textures are allocated once at build time and reallocated on window resize or node addition via `needs_gpu_realloc_`.
- `OperatorRegistry::find()` may trigger a lazy dlopen for deferred entries.
- UI-facing graph views should remain faithful to graph truth, including broken connections, rather
  than silently dropping unresolved edges from snapshots
