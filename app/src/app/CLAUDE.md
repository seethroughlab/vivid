# `app/src/app/` — application shell

The model/view seam (see [`../../ARCHITECTURE.md`](../../ARCHITECTURE.md) §2).

- **`app.h` / `app.cpp`** — `struct App`: the **shared** engine/document (one per
  process) — audio `Session`, `Transport`, visuals `VisualGraph` + `NodeGraph`,
  `ControlServer`, `GpuContext`, source texture, video, **and the audio-thread DSP
  state**. The `ma_device` user pointer is an `App*`; the audio thread never sees a
  `Window`.
- **`window.h`** — `struct Window`: **per-view** state (surface metrics, splitter/dock
  layout, selection/drag/menu/plugin-editor-handle state, frame-side smoothing, its
  `Renderer2D` + `ClipEditor`) + an `App* app` + window-relative geometry methods.
  `CtxMenu` lives here. The GLFW user pointer is a `Window*`.
- **`input.cpp`** — the GLFW key/char/scroll/mouse handlers; `install_input_callbacks`
  wires them. Handlers fetch the `Window*` and reach shared state via `win->app->`.
- **`frame.{h,cpp}`** — `run_frame_loop(App&, Window&)`: the per-frame tick (drain MCP,
  publish characteristics, apply drags + mappings, render). Blocks until the window
  closes.

To add a second (editor) window: build another `Window` with its own renderer +
surface, point `.app` at the same `App`, call `install_input_callbacks`, render it in
the loop. No `App` changes required.
