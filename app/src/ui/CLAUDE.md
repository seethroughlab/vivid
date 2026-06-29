# `app/src/ui/` — 2D renderer + the DAW surface

- **`renderer_2d.{h,cpp}`** — the wgpu-backed 2D drawing library: text (stb_truetype
  atlas, rasterized at `dpi`), shapes, clip-rect stack, textured quads (node
  thumbnails). A CPU view transform powers graph pan/zoom. Two-pass flush: base +
  thumbnails, then floating overlays.
- **`layout.h`** — pure, header-only geometry + constants (session grid, device dock,
  viewer/splitter rects, `kChars`/`kMapSources`, `char_id_for`, `param_dest`). Shared
  by draw **and** input so hit-rects agree. Window-relative helpers are `Window`
  methods (in `app/window.h`); free helpers here take explicit dims.
- **`ui_style.h`** — the palette (`Style`) + widgets (`knob`, `section_header`,
  `draw_card`). Keep new UI on these, not ad-hoc colors.
- **`session_view.{h,cpp}`** — draws the Session view (transport, clip grid, mixer),
  the bottom device dock, clip previews, and the context menus. Reads a
  `const Window&`; reaches shared state via `win.app->`.
- **`mapping_overview.{h,cpp}`** — the P28 bridge overview (geometry inline in the
  header so the modal input handler agrees with the draw).
- **`node_graph.{h,cpp}`** — the visuals node editor: op chain, data nodes, the
  `MappingRegistry` (the bridge), the Tab operator chooser, live thumbnails.
- **`clip_editor.{h,cpp}`** — the dockable MIDI piano-roll / audio waveform editor.

Renderer/UI is **kept ours** (not lifted from vivid-classic) — see ADR-0011.
