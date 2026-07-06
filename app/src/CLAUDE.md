# `app/src/` — source map

Entry point is `main.cpp` (init + wiring + teardown only). Everything else is in
cohesive modules. Full design in [`../ARCHITECTURE.md`](../ARCHITECTURE.md); RT rules
in [`../docs/thread-safety.md`](../docs/thread-safety.md).

- **`app/`** — `App` (shared engine/document), `Window` (per-view state), `input`
  (GLFW callbacks), `frame` (the run loop). The App/Window seam is the multi-window
  extension point.
- **`audio/`** — VST3 host + multi-track session; `vst3_host.h` is the session C API.
  The RT `audio_callback` lives here. **Touching this? read the thread-safety guide.**
- **`gpu/`** — wgpu context, the visuals `VisualGraph`, shader/effect ops, render
  targets, video, texture source.
- **`ui/`** — `Renderer2D`, `layout` (pure geometry/constants), the node-graph editor,
  clip editor, `session_view` + `mapping_overview` draw code, `ui_style`.
- **`cli/`** — the loopback HTTP `control_server` (MCP backend) + `control_errors`
  (stable codes) + `control_parse` (pure, unit-tested).
- **`midi/`** — `MidiClip` types.  **`platform/`** — macOS CFRunLoop frame timer.

Conventions: classes `CamelCase`, functions `snake_case`, members `snake_case_`,
constants `kCamelCase`. UI geometry/constants live in `ui/layout.h` (shared so draw +
hit-test agree). All session/graph **edits** happen on the UI/main thread.
