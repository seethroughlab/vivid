# `app/src/app/` — application shell

The model/view seam (see [`../../ARCHITECTURE.md`](../../ARCHITECTURE.md) §2).

- **`app.h` / `app.cpp`** — `struct App`: the **shared** engine/document (one per
  process) — audio `Session`, `Transport`, visuals `VisualGraph` + `NodeGraph`,
  `ControlServer`, `GpuContext`, source texture, video, the **`EditGateway`** (undo
  sink), **and the audio-thread DSP state**. The `ma_device` user pointer is an `App*`;
  the audio thread never sees a `Window`.
- **`edit_gateway.{h,cpp}`** — the ADR-0017 **command sink**: every document edit (UI +
  MCP) calls `note_edit(label, key)`; captures a labeled canonical-document snapshot
  (deferred to end-of-frame via `commit_frame()`); `undo()`/`redo()` restore through the
  tiered path in `persist_undo.*`. `VIVID_UNDO_AUDIT` builds assert no edit escapes it.
- **`undo_manager.{h,cpp}`** — pure cursor-over-history of JSON snapshots (push/undo/redo,
  depth-capped); no `App`, no GPU. Headless-tested.
- **`examples.{h,cpp}`**, **`node_presets*.cpp`** — ADR-0021 content: the Open Example
  discovery and per-operator named param-snapshot presets.
- **`window.h`** — `struct Window`: **per-view** state (surface metrics, splitter/dock
  layout, selection/drag/menu/plugin-editor-handle state, frame-side smoothing, its
  `Renderer2D` + `ClipEditor`) + an `App* app` + window-relative geometry methods.
  `CtxMenu` lives here. The GLFW user pointer is a `Window*`. Cohesive interaction groups
  are being extracted into their own persistent view owners (ADR-0025 pressure-point #2).
- **`output_preview.h`** — `struct OutputPreview`: the floating output-preview panel
  (ADR-0014) — position/size/drag state + its pure geometry (`panel`/`viewer`/`header`/
  `close`/`grip`) and `clamp` (keeps it inside the visuals column). Extracted from `Window`
  (ADR-0025); renderer-free, so the clamp geometry is headlessly tested
  (`tests/test_output_preview.cpp`). `Window` holds one as `win.preview`.
- **`input.cpp`** — the GLFW key/char/scroll/mouse handlers; `install_input_callbacks`
  wires them. Handlers fetch the `Window*` and reach shared state via `win->app->`.
  Cmd+Z/Cmd+Shift+Z route to the gateway; left-press/release bracket edit gestures.
- **`frame.{h,cpp}`** — `run_frame_loop(App&, Window&)`: the per-frame tick (drain MCP,
  publish characteristics, apply drags + mappings, render, `commit_frame()` the undo
  snapshot). Blocks until the window closes.

To add a second (editor) window: build another `Window` with its own renderer +
surface, point `.app` at the same `App`, call `install_input_callbacks`, render it in
the loop. No `App` changes required.
