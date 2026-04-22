# Editor Windows

Operators that opt into `VIVID_EDITOR` render into a dedicated
secondary GLFW window owned by `EditorWindowManager`
(`src/runtime/core/editor_window_manager.{h,cpp}`). This doc describes
the editor-window contract as of the editor-UI platform plan:
the three-layer API surface, the per-frame context assembly, and where
host services live.

For authoring (the operator side), see
[`docs/operators/editor-ui.md`](../operators/editor-ui.md).

## Three-layer API surface

```
operator::draw_editor(ctx)                   ← operator-owned
        │
        ▼
  editor_ui.h                                ← widgets + layout
        │     (src/operator_api/editor_ui.h)
        ▼
  draw_ui_helpers.h                          ← stateless render
        │     (src/operator_api/draw_ui_helpers.h)
        ▼
  VividDrawAPI fn ptrs                       ← paint primitives
```

- **Paint layer** — `VividDrawAPI` in `src/operator_api/types.h` holds
  function pointers for rect / rounded-rect / text / line / tri / arc /
  wrapped-text / clip-rect primitives. The host populates these via
  `ui::populate_draw_api(...)` on `Renderer2D`.
- **Render helpers** — `draw_ui_helpers.h` composes the paint layer
  into stateless panels / buttons / meters / sliders that render in
  Vivid's standard look without any input handling.
- **Interaction layer** — `editor_ui.h` is the widget + layout toolkit.
  Every widget takes a `VividEditorContext&`, reads `ctx.mouse` +
  `ctx.events` itself, and returns a small result struct
  (`ButtonResult`, `SliderResult`, `GridResult`, `DragHandleResult`,
  etc.). Cross-frame state lives in caller-owned structs (`SliderState`,
  `GridState`, `DragHandleState`, `ScrollState`) that operators store
  on their own core type.

Additions to any layer are **additive**: operators built against an
older header ignore new trailing struct fields, and the host
populates those fields unconditionally. `VIVID_OPERATOR_ABI_VERSION`
has not bumped for the editor-UI platform plan.

## Host services (`VividEditorContext::host`)

`VividEditorHostAPI` (defined in `src/operator_api/types.h`) is a
function-pointer surface the host populates each tick. It exposes:

- **Clipboard** — `get_clipboard_text`, `set_clipboard_text`.
- **Cursor shape** — `set_cursor(VividCursorKind)`; reset to default
  every frame. Kinds: `DEFAULT`, `ARROW`, `IBEAM`, `CROSSHAIR`,
  `HAND`, `RESIZE_H`, `RESIZE_V`, `RESIZE_NESW`, `RESIZE_NWSE`.
- **Pointer capture** — `capture_pointer`, `release_pointer`,
  `has_pointer_capture` (application-level flag; GLFW's implicit
  mouse grab during a button-held drag already delivers events
  outside the window, so capture mostly gates focus-loss behavior).
- **Focus** — `request_focus`, `has_focus`.
- **Transient chrome** — `set_status_text`, `show_tooltip`. Reset to
  empty every frame.

Every callback is optional; operators must guard on null before
invoking. Unset callbacks leave the editor in the "no-op" state.

The thunks live in `src/runtime/core/editor_window_host_api.{h,cpp}`.
Each editor window owns a `HostCtx` (cursor / status / tooltip scratch
strings + pointer-capture flag + clipboard cache). The manager binds
`make_host_api(&window.host_ctx)` into `ctx.host` per frame.

## Per-frame tick

`EditorWindowManager::tick(time)` runs after the primary window's end-
of-frame. For each live editor window:

1. Resolve the live `CompiledNode` by id; drop the window if missing,
   hot-reloaded, or missing an editor capability.
2. Refresh logical + framebuffer dimensions; reconfigure the WGPU
   surface on HiDPI changes.
3. Acquire the surface texture; build a `VividEditorContext`:
   - Surface metrics, draw API, command thunks (routed to
     `UICommandSink::set_param/set_string_param`), theme.
   - Operator state (param values, output values, file-param strings).
   - Mouse state + per-frame event queue (populated by the window's
     GLFW callbacks).
   - `ctx.host` populated from the window's `HostCtx`; per-frame
     fields (cursor / status / tooltip / focus-request) reset first.
4. Call `operator->draw_editor(ctx)`.
5. Apply the operator's requested cursor via `glfwSetCursor`.
6. Focus the window if the operator asked.
7. Render the status strip (bottom of surface) and tooltip (near
   cursor) if the operator set them.
8. Encode + submit + present.

Command routing stays through `UICommandSink` → `RuntimeCommandSink` so
`ctx.commands.set_param` lands in the undo history exactly like an
inspector edit.

## Cursor cache

`EditorWindowManager::Impl` owns a lazy `std::array<GLFWcursor*, 9>`
cursor cache — one slot per `VividCursorKind`. The first time an
operator asks for a kind, the manager calls
`glfwCreateStandardCursor` and stashes the result; subsequent frames
reuse it. All cursors are destroyed in `~Impl`.

On toolchains older than GLFW 3.4, the diagonal cursor kinds
(`RESIZE_NESW`, `RESIZE_NWSE`) fall back to axis-aligned resize
cursors via `#ifdef GLFW_RESIZE_NESW_CURSOR`.

## Key files

| Path | Role |
|------|------|
| `src/operator_api/types.h` | `VividEditorContext`, `VividEditorHostAPI`, `VividDrawAPI`, `VividCursorKind`. |
| `src/operator_api/editor_ui.h` | Widget + layout toolkit. |
| `src/operator_api/draw_ui_helpers.h` | Stateless render helpers. |
| `src/runtime/core/editor_window_manager.{h,cpp}` | Secondary-window lifecycle + per-frame tick. |
| `src/runtime/core/editor_window_host_api.{h,cpp}` | `HostCtx` + thunks bound into `ctx.host`. |
| `src/runtime/core/editor_window_bookkeeping.h` | Open/close set-tracking + surface metric helper. |

## Tests

| Path | Scope |
|------|-------|
| `tests/ui/test_editor_ui_widgets.cpp` | `ui_button` / `ui_toggle` / `ui_radio` / `ui_slider_h` / `ui_slider_v` + `LayoutCursor` helpers. |
| `tests/ui/test_editor_ui_grid.cpp` | `ui_step_grid` click / shift-click / drag-paint / shift-extend; `ui_drag_handle`; `ui_scroll_region`. |
| `tests/ops/test_editor_window_manager.cpp` | `EditorWindowBookkeeping` + `make_editor_string_param_view` + `make_editor_window_surface_metrics`. |
| `tests/ops/test_editor_window_host_api.cpp` | `HostCtx` thunks: cursor / status / tooltip / pointer-capture / focus / clipboard degrade-without-GLFW. |

## Anchoring adopters

- **DrumSequencer** —
  `operators/control/drum_sequencer/drum_sequencer_editor.cpp` uses
  `ui_layout` + `ui_row`/`ui_split_h` for the side-panel column,
  `ui_toggle`/`ui_slider_h`/`ui_radio` for the side-panel widgets,
  `ui_step_grid` for the main grid, and `ctx.host.set_cursor` +
  `set_status_text` for cursor hints + selection span.
- **MSEG** —
  `operators/control/mseg/mseg_editor.cpp` uses `ui_layout` + `ui_row`
  for the top-bar / plot split, `ui_drag_handle_begin` +
  `ui_drag_handle_update` for point and curve handles (paired with
  the operator's `pick_point` / `pick_curve_handle` helpers), and
  `ctx.host.set_cursor` + `set_status_text` on handle hover.

## Automated testing of editor windows

The `test_ui_screenshot_smoke` harness drives editor windows end-to-end
via two seams added in the follow-up second-window test-coverage work:

- **Script routing** — `UITestAction::target_window` (string) tags any
  `mouse_move` / `mouse_button` / `key` / `char` / `screenshot` action
  with the node id of the editor it should hit. Empty → main graph UI
  (the legacy path). The new `open_editor` action type calls
  `EditorWindowManager::open(node_id)` directly. See
  `src/runtime/debug/ui_test_runner.h` + `.cpp`.
- **Injection API** — the manager exposes `inject_event`,
  `inject_mouse_move`, `inject_mouse_button`, `inject_key`,
  `inject_char` as the script runner's landing points; each pushes a
  synthetic `VividEditorEvent` into the target window's
  `pending_events` queue exactly as a GLFW callback would. See
  `src/runtime/core/editor_window_manager.h`.
- **Capture** — `EditorWindowManager::capture_surface_png(node_id)`
  drives one internal tick with a copy-texture-to-buffer encode
  alongside the render pass, then synchronously reads back the mapped
  buffer and returns a PNG. The spawned `vivid --editor-screenshot
  NODE=PATH` triggers this after the ui-test script completes and
  writes the PNG to disk.
- **Fixtures** — `tests/fixtures/ui_editor_drum_sequencer.json` and
  `tests/fixtures/ui_editor_mseg.json` are minimal single-node graphs
  with stable node ids (`drum_1`, `mseg_1`) so smoke cases can
  reference them directly.

### Subprocess close-gate

Secondary editor windows on macOS used to receive an immediate
`glfwWindowShouldClose` signal from the Cocoa app lifecycle when the
spawned `vivid` ran as a non-foreground child process (ctest / CI).
`EditorWindowManager` now gates the tick()'s close-check on an
`explicit_close_requested` sentinel that's set only by:

- The Cmd+W / Ctrl+W handler in `key_cb`.
- The native red-button close callback (`glfwSetWindowCloseCallback` →
  `close_cb`).

Any `glfwWindowShouldClose` that arrives without that sentinel is
treated as spurious, cleared in-place, and the window survives. The
fix lives in `src/runtime/core/editor_window_manager.cpp`.

With the gate in place, the two editor smoke cases
(`drum sequencer editor`, `mseg editor`) pass end-to-end under
`VIVID_UI_SMOKE_LANE=gui_smoke` with baseline fingerprint diffs.

## See also

- [`docs/operators/editor-ui.md`](../operators/editor-ui.md) — operator
  authoring guide for the widget + host-service surface.
- [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) — overall runtime
  architecture (execution model, lanes, operator contract).
