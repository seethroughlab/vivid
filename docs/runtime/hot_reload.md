# Hot Reload — Background Compile and Dylib Swap

## Overview

`HotReloader` (hot_reload.h/cpp) enables live recompilation of operators without restarting.
It runs a single background compile thread that processes a queue of cmake targets.

The main thread queues targets and polls for results each frame. When a result is ready,
the main thread pauses the audio engine, swaps the dylib, and resumes.

## `HotReloader` API

```cpp
bool start(const std::string& build_dir);  // build_dir = where cmake was invoked
void stop();

void queue_rebuild(const std::string& target_name);
std::vector<ReloadResult> poll_ready();    // call each frame from main thread
```

## `ReloadResult`

```cpp
struct ReloadResult {
    std::string target_name;
    std::string staged_dylib_path;  // empty on failure
    std::string error_output;       // compiler stderr on failure
    bool success;
};
```

## Compile Thread

The background thread runs `compile_thread()`:
1. Waits on `queue_cv_` for a target to appear in `build_queue_`
2. Pops target, moves to `in_flight_targets_`
3. Executes: `cmake --build <build_dir> --target <target_name>` (or package compile fn)
4. On success: copies `.dylib` to a staged path (`<staging_dir>/<target>.<counter>.dylib`)
5. Pushes `ReloadResult` to `results_` (protected by `result_mutex_`)

## Deduplication

- `queued_targets_` prevents re-queuing a target already in the queue
- `in_flight_targets_` tracks what's currently being compiled
- `deferred_targets_` holds targets queued while the same target was in-flight — re-queued after completion
- `reload_counters_` provides unique staging filenames so old dylibs aren't overwritten while in use

## Package Compiler Integration

```cpp
using PackageCompileFn = std::function<ReloadResult(const std::string& target_name)>;
void set_package_compiler(PackageCompileFn fn);
```

When a target matching `"pkg:<name>:<op>"` is queued, the `PackageCompileFn` callback is invoked
instead of `cmake --build`. This allows package operators to be recompiled with the same
`PackageCompiler` logic used for initial installation.

## Main Thread Reload Flow

Called from main loop after `poll_ready()` returns results:

```cpp
// For each ReloadResult r:
if (r.success) {
    std::string type_name = registry.type_name_for_target(r.target_name);
    audio_engine.pause();
    bool reload_ok = runtime.reload_operator(type_name, registry, r.staged_dylib_path);
    bool audio_ok = reload_ok ? audio_engine.reload_operator(type_name, registry) : false;
    audio_engine.resume();
}
```

Success is only treated as real success when both runtime-side and audio-side reloads succeed.
Failed reloads leave the previous loader active when possible and surface diagnostics through
`OperatorRegistry`.

## `RuntimeCore::reload_operator()`

For all `NodeState` entries with matching `type_name`:
1. Snapshot current `param_values` and string params
2. Call `loader->destroy_instance(ns.instance)` on the old instance
3. Call `registry.reload_operator(type_name, new_dylib_path)` to swap the `OperatorLoader`
4. Call `reinit_node_state(ns, new_desc, &param_overrides)` — recreates instance with preserved params

Param preservation: values from the old instance are passed as `param_overrides` to `init_node_state()`,
so params that exist in the new descriptor retain their values. New params get their default values.

Hot reload only supports descriptor-compatible edits. Port layout changes or incompatible parameter
shape changes are rejected explicitly rather than partially reusing the previous runtime metadata.

## `AudioEngine::reload_operator()`

Same pattern, but for `AudioNodeState` entries.
Requires the engine to be paused (audio callback stopped) to avoid data races.

The audio reload path preserves the existing instance when the replacement dylib fails validation,
and targeted regression tests cover:

- compatible reload with preserved params
- rejected incompatible descriptor reload
- safe rollback to the previous audio operator after rejection

## File Watcher Integration

`FileWatcher` (file_watcher.h/cpp) monitors source directories with efsw.
On source modification, creation, move, or deletion, it resolves the source file → cmake
target name and calls `hot_reloader.queue_rebuild(target_name)`. Directory-level watches
cover editor rename/delete/recreate save flows without macOS-specific kqueue reopen logic.

WGSL shader files have a separate hot-reload path: they are re-read in-place by the GPU operator
without a dylib recompile.

## Staging Directory

`staging_dir_` = `<build_dir>/vivid_staging/`. Created by `start()`.
Each reload gets a unique filename: `<staging_dir>/<target>.<counter>.dylib`.
The counter prevents the old dylib from being overwritten while it may still be mapped.
