# Phase 2: Editor Window Manager

## Goal

Add the runtime machinery that can open, draw, and close a native editor window on demand, calling the Phase 1 `vivid_draw_editor` entry point each frame. No host UI yet — opening is driven by a temporary debug hook (e.g. a keyboard shortcut or a CLI test command) so we can validate the window plumbing in isolation before wiring the inspector button in Phase 3.

## Context

The codebase already has a working template for a secondary GLFW/WGPU window: `src/runtime/debug/output_window.{h,cpp}`. It creates its own `GLFWwindow`, its own `WGPUSurface`, uses the main `FullscreenBlit` pipeline, and shares device/queue with the primary window.

Editor windows follow the same shape but instead of blitting a texture they run a `Renderer2D` pass and call into the operator's `draw_editor`.

Single-window assumptions that need relaxing (from exploration):

- `WindowUserData` (`src/runtime/core/window_manager.h`) is effectively per-app; GLFW callbacks route input to a single `graph_ui`. This must become per-window via `glfwSetWindowUserPointer`.
- `Renderer2D` is not intrinsically window-bound, but it is initialized once against the main surface format. Editor windows need their own instance with a matching surface format (typically the same — BGRA8Unorm).
- The main frame pipeline in `main.cpp` (~lines 3165–3231) is written against one surface. Editor windows piggy-back after the primary frame completes.

## Scope

- Introduce `EditorWindowManager` owning a vector of `EditorWindow` instances.
- `EditorWindow` owns: `GLFWwindow*`, `WGPUSurface`, `Renderer2D`, per-window input state, a queue of `VividInputEvent`, and a reference to the `OperatorLoader` + instance pointer it draws for.
- Per-frame: main window renders → for each editor window, acquire surface, begin pass, populate `VividEditorContext`, call `draw_editor`, flush `Renderer2D`, present.
- Lifecycle: open, close (via OS close button, via `request_close`, via owner delete), destroy all on graph unload, destroy all on dylib reload.
- No host UI wiring. A hidden debug entry point (e.g. environment variable or unused menu item) opens an editor for a named node so we can exercise the code before Phase 3.

## Design

### New files

- `src/runtime/core/editor_window_manager.h`
- `src/runtime/core/editor_window_manager.cpp`

### `EditorWindow` (internal)

```cpp
struct EditorWindow {
    std::string      node_id;                     // key
    GLFWwindow*      glfw = nullptr;
    WGPUSurface      surface = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
    std::unique_ptr<Renderer2D> r2d;

    // Input accumulated by GLFW callbacks, drained on each draw
    std::vector<VividInputEvent> pending_events;
    VividInspectorMouse          mouse{};         // maintained incrementally

    // Links to the operator instance
    OperatorLoader*  loader = nullptr;            // not owned
    void*            instance = nullptr;          // not owned

    // Metadata captured at open
    VividEditorMetadata meta{};
};
```

### `EditorWindowManager` API

```cpp
class EditorWindowManager {
public:
    EditorWindowManager(GpuContext& gpu, /* main window refs */);
    ~EditorWindowManager();

    // Returns true if the editor was opened (or already open and focused).
    bool open(const std::string& node_id,
              OperatorLoader& loader,
              void* instance);

    void close(const std::string& node_id);
    void close_all();                // called on graph unload / shutdown
    void close_for_node(const std::string& node_id);  // called on node delete
    void close_for_loader(OperatorLoader* loader);    // called on hot reload

    // Called once per frame after the primary window finishes its frame.
    void tick(double time, const RuntimeState& runtime);

private:
    std::vector<std::unique_ptr<EditorWindow>> windows_;
    // ...
};
```

### Per-frame tick

For each `EditorWindow`:

1. If `glfwWindowShouldClose` → enqueue for destruction, skip.
2. Acquire the next surface texture view (same pattern as `OutputWindow::draw`).
3. Begin a command encoder and render pass against that view.
4. Build a `VividEditorContext`:
   - `surface_width/height` from the current GLFW framebuffer size.
   - `dpi_scale` from GLFW content scale.
   - `draw` populated via `populate_draw_api(api, *r2d)` (already exists in `src/ui/rendering/renderer_2d.cpp`).
   - `commands` populated with `set_param` / `set_string_param` forwarders into the running `RuntimeAPI` (or whichever layer the inspector currently calls — verify at implementation time).
   - `theme` filled from the same theme source used by the inspector.
   - `param_values`, `output_values`, `string_param_values` copied from the `CompiledGraph` snapshot for the node.
   - `events` pointed at the pending-events vector.
   - `mouse` reflecting the last known state.
   - `time`, `wants_keyboard`, `request_close`.
5. Call `loader->draw_editor(instance, &ctx)`.
6. `r2d->flush()`, submit commands, `wgpuSurfacePresent`.
7. If `ctx.request_close == 1` or `glfwWindowShouldClose` now true, enqueue for destruction.

After the iteration, destroy enqueued windows.

### Per-window input routing (refactor)

Currently `WindowUserData` is stored globally-ish and all GLFW callbacks forward to the single `graph_ui`. Minimal refactor:

- Introduce a small `WindowInputSink` interface with `on_mouse_move`, `on_mouse_button`, `on_scroll`, `on_key`, `on_char`, etc.
- Primary window's `WindowUserData` points to a sink backed by `graph_ui`.
- Each `EditorWindow` sets `glfwSetWindowUserPointer` to its own `EditorWindow*`. GLFW callbacks look up the sink via the user pointer and dispatch accordingly.
- Editor's sink appends a `VividInputEvent` to `pending_events` and updates `mouse`. Events are drained at the end of each draw.

### Main-loop integration

In `src/runtime/core/main.cpp`:

- Construct `EditorWindowManager` after `GpuContext` is created (~line 1243 area).
- Call `editor_windows.tick(time, runtime)` once per frame, after the primary window's `gpu.end_frame()` completes.
- Call `editor_windows.close_all()` on graph unload; `close_for_loader(...)` when a dylib reload event fires; `close_for_node(...)` from the graph delete path.

### Temporary open hook

Add a debug entry point that opens an editor for a node without requiring Phase 3's UI work. Simplest: a keyboard shortcut (e.g. Cmd+Shift+E while a node is selected) that calls `EditorWindowManager::open(...)` if the selected node's loader `has_editor()`. Keep it behind an `#ifdef VIVID_EDITOR_DEBUG` or a build flag to keep it out of release; it will be replaced by the real host UI in Phase 3.

## Files

| Change | Path |
|---|---|
| New manager + window | `src/runtime/core/editor_window_manager.{h,cpp}` (new) |
| Per-window input sink refactor | `src/runtime/core/window_manager.{h,cpp}` |
| Main-loop construction + tick + unload-close hooks | `src/runtime/core/main.cpp` |
| CMake: add new files | `src/runtime/core/CMakeLists.txt` (or root app CMake as appropriate) |
| Reference only (do not modify beyond reading) | `src/runtime/debug/output_window.{h,cpp}` |

## Acceptance Criteria

1. With the debug open-hook, pressing the shortcut on a node whose operator declares `VIVID_EDITOR` opens a new OS window. The window is resizable, can be moved to a second monitor, and has an OS close button.
2. The editor receives a sensible `VividEditorContext` each frame: its `surface_width/height` matches the GLFW framebuffer, events land in the queue, param values reflect the graph.
3. Clicking / dragging / typing in the editor reaches the operator via `ctx.events` and `ctx.mouse`; calling `ctx.commands.set_param` from inside the operator updates the graph and the inspector live.
4. Closing the OS window → `EditorWindow` is destroyed, no GPU validation errors, no leaked `WGPUSurface`.
5. Deleting the owning node → editor window closes.
6. Unloading a graph → all editor windows close.
7. Rebuilding the operator's package → editor window closes; reopening after reload works.
8. Primary window rendering and input are unaffected by whether editor windows are open.
9. `ctest` passes (in background).

## Dependencies

- **Phase 1** must be merged. Loader must expose `has_editor()` / `draw_editor()` / `editor_metadata()`.

## Out of Scope for This Phase

- Inspector "Open Editor" button (Phase 3).
- Settings persistence of window size/position (Phase 3).
- Any operator actually implementing `draw_editor` — use a stub operator or a throwaway implementation to smoke-test rendering (Phase 4 is the real first adopter).
