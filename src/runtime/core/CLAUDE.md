# Runtime Core

## Purpose

This directory contains the application entry point, the main frame loop, and the core services that orchestrate Vivid's lifecycle. Everything starts here — GPU initialization, operator discovery, graph loading, and the per-frame tick that drives the entire system.

## Key Files

| File | Role |
|------|------|
| `main.cpp` | Entry point, GLFW window creation, frame loop orchestration |
| `main_internal.h` | Shared types across main.cpp and its helper files |
| `main_helpers.cpp` | UI update helpers, window management, asset loading |
| `main_async_graph.cpp` | Async graph transactions (background recompilation) |
| `main_menu_actions.cpp` | macOS native menu and action handlers |
| `main_package_browser.cpp` | Package browser panel UI logic |
| `runtime_core.h/cpp` | `RuntimeCore` — owns `CompiledGraph`, `FrameExecutor`, `AudioFrameBridge`; orchestrates build/tick |
| `runtime_bootstrap.h/cpp` | Startup: discovers operators, initializes `OperatorRegistry`, loads packages and shader operators |
| `hot_reload.h/cpp` | Watches for `.dylib` changes, reloads operators preserving parameter state |
| `file_watcher.h/cpp` | Monitors operator source directories for changes (via efsw, cross-platform) |
| `source_index.h/cpp` | Indexes operator source files for opdev source search |
| `tool_discovery.h/cpp` | Finds installed build tools (cmake, clang++) for package compilation |
| `settings.h/cpp` | User preferences: theme, layout, analysis mode, hot-reload flag |
| `undo_manager.h/cpp` | Graph undo/redo history (before/after JSON snapshots) |
| `file_drop_registry.h/cpp` | Maps file extensions to operators' `file_param` handlers for drag-and-drop |
| `shared_handle_registry.h` | Registry for custom port type opaque pointers |
| `build_console.h` | Build output panel shared between package compilation and test runs |
| `crash_guard.h` | RAII guard to suppress UI during crash recovery |
| `editor_detect.h/cpp` | Detects if running inside an IDE (Xcode, VS Code) |

## How It's Organized

### Startup Sequence

1. Parse CLI arguments (graph file, workspace, settings)
2. Initialize GPU context (`GpuContext::init()`)
3. Bootstrap operator registry — scan core operators, packages, and shader operators
4. Load workspace (recent graphs, settings, asset library)
5. Create and start control server (port 9876)
6. Load initial graph (if provided on command line)
7. Enter GLFW frame loop

### Per-Frame Tick Order

Each frame, `main.cpp`'s tick lambda runs roughly these steps:

1. Apply MIDI mappings to live parameters
2. Tick quantized session launches (beat/bar-synced clip/scene/cue changes and cue follow actions)
3. Acquire GPU surface (`gpu.begin_frame()`)
4. Poll hot-reload (if enabled)
5. Pre-audio sync (frame→audio bridge update)
6. Normalize input state (mouse coords → texture UV space)
7. **Runtime tick** — calls `FrameExecutor::tick()` on the compiled graph
8. Capture thumbnails (GPU node outputs → thumbnail cache)
9. Tick state-preset machine (auto-recall on state transitions)
10. Capture/recording/analysis (post-tick, textures are fresh)
11. Post-audio sync (audio→frame bridge read)
12. UI update (graph editor, inspector, dialogs)
13. GPU submit (command encoder → device queue)
14. Present (swap surface to display)

### main.cpp Helper Files

`main.cpp` is large, so implementation is split by concern:
- `main_helpers.cpp` — UI frame update, window resize handling, asset loading
- `main_async_graph.cpp` — async graph load/reload with background compilation
- `main_menu_actions.cpp` — macOS menu bar actions (open, save, export, etc.)
- `main_package_browser.cpp` — package browser panel rendering and interaction

### RuntimeCore

`RuntimeCore` is the coordination hub. It owns the `CompiledGraph` and `FrameExecutor`, holds the `AudioFrameBridge` for cross-cadence communication, and provides `build()` (compile a graph) and `tick()` (execute one frame) methods. It does not own the `AudioEngine` — that's managed separately in `main.cpp` and passed the `AudioFrameBridge`.

## Relationships

- **Owns:** `CompiledGraph`, `FrameExecutor`, `AudioFrameBridge`
- **Coordinates:** `AudioEngine` (audio executor), `ControlServer` (HTTP requests drained each frame)
- **Uses:** `OperatorRegistry` (type→loader), `Graph` (data model), `PackageManager` (package lifecycle)

## See Also

- `docs/runtime/architecture.md` — overall runtime architecture and main loop design
- `docs/runtime/runtime_core.md` — `RuntimeCore` API and build/tick contract
- `docs/runtime/hot_reload.md` — hot-reload behavior and state preservation
