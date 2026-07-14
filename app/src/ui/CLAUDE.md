# `app/src/ui/` — 2D renderer + the DAW surface

- **`renderer_2d.{h,cpp}`** — the wgpu-backed 2D drawing library: text (stb_truetype
  atlas, rasterized at `dpi`), shapes, clip-rect stack, textured quads (node
  thumbnails). A CPU view transform powers graph pan/zoom. Two-pass flush: base +
  thumbnails, then floating overlays.
- **`layout.h`** — pure, header-only geometry + constants (session grid, device dock,
  `visuals_panel`/`preview_*`/splitter rects, `kChars`/`kMapSources`, `char_id_for`, `param_dest`). Shared
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
  Per ADR-0014 this graph **is** the visuals zone (it owns the whole right column), and
  **Tab is the only way to add a node** — the chooser is registry-driven, opens at the
  cursor, and spawns there. Don't reintroduce a hard-coded palette.
- **`chooser.{h,cpp}`** — the **Tab palette, shared by both graphs**: type to filter, Enter to
  spawn, at the cursor. Generic over its entries, so each surface supplies a catalog
  (`audio_catalog.h` for audio; the operator registry for visuals) and spawns what comes back.
  `text_match.h` is the ranked matcher (exact > prefix > substring > metadata).
- **`audio_catalog.h`** — the ONE audio add-catalog: native operators + every installed VST3 +
  every installed CLAP, grouped instrument/effect, badged by format. **Tab is the only way to add
  an audio node** — there is no plugin browser and no `+ Src`/`+ FX`; those covered disjoint halves
  of the catalog (registry-only vs bundles-only), so neither could add everything. A CLAP
  *note-effect* is listed but **disabled** until ADR-0015's note edges exist: spawned as an audio
  effect it would get zero notes and write silence over the chain.
- **`clip_editor.{h,cpp}`** — the dockable MIDI piano-roll / audio waveform editor.

Renderer/UI is **kept ours** (not lifted from vivid-classic) — see ADR-0011.
