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
- **`shader_library_view.{h,cpp}`** — the ADR-0021 content browser: lists the shaders
  in the shader library (each row carries its compile `error` when it failed), for
  browsing/forking shipped shaders.
- **`graph_adapter.h`** — the **Layer-1 `GraphModelAdapter`** (ADR-0023 #2): the read surface a shared
  canvas enumerates a graph through, independent of the domain model. It carries the node-level shape both
  editors already compute for `card()` — `AdapterNode {id, draw-space rect, accent, selected, broken,
  title, error}` — via `collect_nodes()` + `selected_node_id()`. Both `NodeGraph` and `AudioNodeGraph`
  implement it AND consume it in their own card loops (so the contract has real consumers). What diverges
  stays a per-editor domain overlay outside the adapter: wires, ports, the preview-well contents (thumbnail
  / waveform / sparkline), the visuals bridge data-nodes, and the audio param strip. WRITE commands wait
  for the Layer-3 controller. `rect` is in the editor's own draw space (world for visual, screen for audio)
  — reconciling the two spaces is the separate ADR-0023 #3 problem.
- **`node_canvas.h` / `graph_canvas.h`** — the **shared graph-UI substrate** both node editors
  (visuals `node_graph`, per-track `audio_node_graph`) draw through (ADR-0023). `node_canvas.h` is
  the vocabulary: the `NodeView` world↔screen transform (+ `region_view`), `CardPorts` card geometry,
  and the marks (`node_grid`/`node_wire`/`node_port`/`node_card` [selection ring folded in] /
  `node_preview_panel`/`node_waveform` + the ADR-0019 error vocab). `graph_canvas.h` is the **Layer-2
  `GraphCanvas`** — a small class each editor owns as a member, providing the shared draw skeleton
  (`card()` chrome, `grid()`, `ghost_wire()`) **and owning that editor's pan/zoom camera** (ADR-0023 #1
  — the sole `NodeView`; each editor reaches it via `canvas_.view()` rather than keeping its own copy).
  What still diverges stays per-editor: coordinate space
  (visual draws WORLD-space via `set_transform`; audio bakes SCREEN coords in `layout()`), wire
  endpoints/color, port/label content, preview-well contents, and the audio param band.
- **`node_graph.{h,cpp}`** — the visuals node editor: op chain, data nodes, the
  `MappingRegistry` (the bridge), the Tab operator chooser, live thumbnails.
  Per ADR-0014 this graph **is** the visuals zone (it owns the whole right column), and
  **Tab is the only way to add a node** — the chooser is registry-driven, opens at the
  cursor, and spawns there. Don't reintroduce a hard-coded palette.
- **`chooser.{h,cpp}` / `graph_catalog.h`** — the **Tab palette, shared by both graphs**: type to
  filter, Enter to spawn, at the cursor. Generic over its entries, so each surface supplies a catalog
  (`audio_catalog.h` for audio; the operator registry for visuals) and spawns what comes back.
  `text_match.h` is the ranked matcher (exact > prefix > substring > metadata). `graph_catalog.h`
  (ADR-0023 step 5) is the **typed `CatalogSpawn` descriptor** every node-catalog entry carries —
  `{Domain, SpawnKind, type, format, char_id}` — replacing the old colliding `ChooserEntry.tag` ints
  and the `id`/`badge` overloading; each editor's spawn dispatcher switches on `spawn.kind`. (`tag`
  survives only for the non-catalog param pickers.)
- **`audio_catalog.h`** — the ONE audio add-catalog: native operators + every installed VST3 +
  every installed CLAP, grouped instrument/effect, badged by format. **Tab is the only way to add
  an audio node** — there is no plugin browser and no `+ Src`/`+ FX`; those covered disjoint halves
  of the catalog (registry-only vs bundles-only), so neither could add everything. A CLAP
  *note-effect* is listed but **disabled** until ADR-0015's note edges exist: spawned as an audio
  effect it would get zero notes and write silence over the chain.
- **`clip_editor.{h,cpp}`** — the dockable MIDI piano-roll / audio waveform editor.

Renderer/UI is **kept ours** (not lifted from vivid-classic) — see ADR-0011.
