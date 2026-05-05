# Phase 2: Editor Window Manager

## Goal

Add the runtime machinery to open, draw, focus, and close dedicated editor windows for nodes whose operators export `VIVID_EDITOR`. This phase proves the rendering and lifecycle model without yet making editor windows part of the public host UI.

The design in this phase intentionally minimizes risk: editor windows manage their own input locally and do not require a generic refactor of the main window callback path.

## Current Repo Facts

- `src/runtime/debug/output_window.cpp` already shows the correct pattern for a second GLFW window with its own `WGPUSurface`.
- Main-window input today is still centered around `WindowUserData` in `src/runtime/core/window_manager.{h,cpp}`.
- The main loop already tracks runtime rebuild boundaries through `RuntimeAPI::reload_serial()` in `src/runtime/core/main.cpp`.
- The inspector already adapts `UICommandSink` into a command API suitable for operator UI calls.

These facts support a local editor-window manager rather than a global input abstraction.

## Locked Decisions For This Phase

1. Add `src/runtime/core/editor_window_manager.{h,cpp}`.
2. Keep the main-window `WindowUserData` path intact.
3. Define editor-specific GLFW callbacks inside `editor_window_manager.cpp`.
4. Resolve the live node from `runtime.compiled_graph()->find_node(node_id)` whenever the editor is opened, focused, or drawn.
5. Treat `reload_serial()` changes as a hard close-all boundary.

## Responsibilities

The new manager owns:

- editor-window creation and destruction
- duplicate-open prevention
- per-window input accumulation
- per-frame editor rendering
- focus/refocus behavior
- teardown on node removal or runtime reload

It does **not** own:

- inspector button UI
- public keyboard shortcut registration
- double-click behavior
- editor geometry persistence policy beyond exposing enough hooks for Phase 3

## Editor Window Data Model

Use an internal structure similar to:

```cpp
struct EditorWindow {
    std::string node_id;
    GLFWwindow* glfw = nullptr;
    WGPUSurface surface = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
    std::unique_ptr<Renderer2D> renderer;

    std::vector<VividEditorEvent> pending_events;
    VividEditorMouse mouse{};
    VividEditorMetadata meta{};
    bool wants_keyboard = false;
};
```

Do not cache raw `OperatorLoader*` or `instance` pointers across reload boundaries. Resolve those from the current compiled graph whenever the window is used. That keeps stale-instance handling simple: reload closes all windows, and even before that, any missing node or missing editor capability cleanly tears a window down.

## Manager API Shape

The concrete class interface can evolve, but the docs should require these behaviors:

- `open(node_id)` opens or refocuses the existing editor window for that node.
- `is_open(node_id)` reports whether a node already has an editor window.
- `focus(node_id)` brings an already-open editor window to the foreground.
- `close(node_id)` closes a specific window.
- `close_all()` closes all editor windows.
- `tick(time)` renders every live editor window once per main-loop frame.

The constructor should take enough dependencies to avoid hidden globals:

- `GpuContext&`
- `RuntimeCore&` or equivalent compiled-graph access
- `UICommandSink&`
- a callback or helper that produces `VividInspectorTheme`

## Rendering Flow

For each open editor window on every frame:

1. If the GLFW window has requested close, mark it for destruction.
2. Resolve `node_id` against `runtime.compiled_graph()->find_node(node_id)`.
3. If the node is missing, has no loader, has no instance, or no longer reports `has_editor()`, mark it for destruction.
4. Refresh framebuffer size and reconfigure the surface if needed.
5. Acquire the surface texture.
6. Build a `VividEditorContext` using:
   - framebuffer width and height
   - content scale from GLFW
   - `populate_draw_api(...)`
   - a command adapter backed by `UICommandSink`
   - theme values consistent with inspector styling
   - current param, output, and string-param views from the live compiled node
   - local editor mouse snapshot
   - pending editor events
   - `time`
7. Call `loader->draw_editor(instance, &ctx)`.
8. Flush the `Renderer2D` pass and present.
9. If `ctx.request_close` is set, mark the window for destruction.
10. After iteration, destroy all marked windows and clear their per-window buffers.

## Input Routing

Keep input handling local to the editor manager.

Implementation requirements:

- Define editor-specific GLFW callbacks in `editor_window_manager.cpp`.
- Store `EditorWindow*` in `glfwSetWindowUserPointer`.
- Translate GLFW callbacks into `VividEditorEvent` entries with editor-local pixel coordinates.
- Maintain `VividEditorMouse` incrementally in the same callbacks.
- Clear one-frame click or release flags after each draw.

Do not introduce a new generic `WindowInputSink` layer in this phase. That is a broader architecture change than the feature needs.

## Main-Loop Integration

Integrate the manager into `src/runtime/core/main.cpp`.

Required integration points:

1. Construct the manager after GPU setup and after the UI command sink exists.
2. Tick the manager once per frame after the primary window render path.
3. Close all editor windows when the graph unloads.
4. Close all editor windows whenever `runtime_api.reload_serial()` changes.

The reload rule is explicit: this feature does not attempt to rebind editor windows across compiled-graph swaps in v1.

## Temporary Open Hook

Add a development-only entry point in this phase so rendering can be exercised before host UI wiring lands.

Recommended behavior:

- `Cmd+Shift+E` / `Ctrl+Shift+E`
- only active in development builds or behind a clearly temporary gate
- opens the selected node’s editor if that node exists and `has_editor()` is true

This hook is temporary and should be removed once Phase 3 lands the official public affordances.

## Files To Change

| Change | Path |
|---|---|
| New editor window manager | `src/runtime/core/editor_window_manager.{h,cpp}` |
| Main-loop construction, tick, and reload-close integration | `src/runtime/core/main.cpp` |
| Temporary debug shortcut in main callback path if needed | `src/runtime/core/window_manager.{h,cpp}` or the nearest real shortcut handler |
| Reference implementation pattern | `src/runtime/debug/output_window.{h,cpp}` |

Do not describe changes to nonexistent build files. New app sources should be wired through the actual main app build definition, not a fictional `src/runtime/core/CMakeLists.txt`.

## Tests

### Automated Coverage

Add unit coverage for manager behavior that does not require driving a second real window in CI:

1. Duplicate-open prevention for the same `node_id`.
2. `is_open(node_id)` bookkeeping.
3. Close behavior when the node disappears from compiled graph state.
4. Close-all behavior when a reload boundary is observed.
5. Guard behavior when a node no longer has a loader, instance, or editor capability.

Where direct GLFW/WGPU coverage is impractical, isolate bookkeeping into testable helpers and leave actual native-window rendering to manual QA.

### Manual QA

1. Use the temporary shortcut to open an editor window for a fixture operator.
2. Verify the window opens as a separate native OS window.
3. Verify resize and move behavior works without GPU validation errors.
4. Verify editor-originated `set_param` updates reach the live node and are reflected in the inspector.
5. Delete the node, reload the graph, and rebuild the package; confirm the editor closes cleanly in all three cases.

## Acceptance Criteria

1. A dev-only open path can open an editor window for a node with `has_editor() == true`.
2. The editor window renders through `Renderer2D` and `VividEditorContext`.
3. Editor input is delivered as local pixel-space events.
4. Mutations from the editor flow through `UICommandSink`.
5. Missing nodes or reload boundaries close windows instead of leaving stale pointers alive.
6. Main-window input and rendering remain unchanged.

## Non-Goals

- Public inspector button.
- Official keyboard shortcut.
- Geometry persistence.
- Changing node double-click behavior.
- Making editor windows survive compiled-graph reloads.
