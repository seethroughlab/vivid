# `app/src/` — source map

Entry point is `main.cpp` (init + wiring + teardown only). Everything else is in
cohesive modules. Full design in [`../ARCHITECTURE.md`](../ARCHITECTURE.md); RT rules
in [`../docs/thread-safety.md`](../docs/thread-safety.md).

- **`app/`** — `App` (shared engine/document), `Window` (per-view state), `input`
  (GLFW callbacks), `frame` (the run loop), the `edit_gateway` + `undo_manager` command
  sink (ADR-0017 undo/redo), and `examples` / `node_presets` (ADR-0021). The App/Window
  seam is the multi-window extension point.
- **`audio/`** — VST3 host + multi-track session; `vst3_host.h` is the session C API.
  The RT `audio_callback` lives here. **Touching this? read the thread-safety guide.**
- **`gpu/`** — wgpu context, the visuals `VisualGraph`, shader/effect ops, render
  targets, video, texture source, `file_drop_registry` (ADR-0021).
- **`ui/`** — `Renderer2D`, `layout` (pure geometry/constants), the node-graph editor,
  clip editor, `session_view` + `mapping_overview` draw code, `ui_style`,
  `shader_library_view` (the ADR-0021 content browser).
- **`cli/`** — the loopback HTTP `control_server` (MCP backend) + `control_errors`
  (stable codes) + `control_parse` (pure, unit-tested) + `control_handlers_edit`
  (`undo`/`redo`).
- **`midi/`** — `MidiClip` types.  **`platform/`** — macOS CFRunLoop frame timer +
  the native `menu_bar` (File + the ADR-0017 Edit menu).
- Root-level `persist.*` (session JSON) + `persist_undo.*` (undo's canonical-document
  projection + tiered restore) + `mapping.*` (the bridge).

Conventions: classes `CamelCase`, functions `snake_case`, members `snake_case_`,
constants `kCamelCase`. UI geometry/constants live in `ui/layout.h` (shared so draw +
hit-test agree). All session/graph **edits** happen on the UI/main thread.
