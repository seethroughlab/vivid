# Code Review Phase 4: UI + Interaction Surface Exploration

## Purpose

This note is the Phase 4 UI-and-interaction artifact for the Vivid code review process described in [CODE_REVIEW.md](/Users/jeff/Developer/vivid/docs/CODE_REVIEW.md).

The goal of this phase is to map the UI as its own subsystem rather than treating it as a thin layer over the runtime. This is still exploration rather than audit. It records:

- the high-level shape of the retained-mode UI in `src/ui`
- how the node graph editor, overlays, and renderer layers are divided
- what appears to be UI-owned state versus runtime-fed state
- where the UI mutates application state and where it only reflects it
- likely UI-centered review hotspots for later phases

This note does not judge UX quality, visual polish, or implementation correctness.

## UI Overview

At a high level, the current UI appears to be organized around one dominant retained controller, [NodeGraphUI](/Users/jeff/Developer/vivid/src/ui/node_graph.h), plus a set of supporting modules:

- low-level drawing primitives in [renderer_2d.h](/Users/jeff/Developer/vivid/src/ui/renderer_2d.h)
- thumbnail capture and presentation in [thumbnail_cache.h](/Users/jeff/Developer/vivid/src/ui/thumbnail_cache.h) and [thumbnail_renderer.h](/Users/jeff/Developer/vivid/src/ui/thumbnail_renderer.h)
- overlay layout helpers in [overlay_layouts.h](/Users/jeff/Developer/vivid/src/ui/overlay_layouts.h)
- theme/style support in [theme_loader.h](/Users/jeff/Developer/vivid/src/ui/theme_loader.h) and [ui_style.h](/Users/jeff/Developer/vivid/src/ui/ui_style.h)
- native platform integration in [file_dialog.h](/Users/jeff/Developer/vivid/src/ui/file_dialog.h)

The UI matches the retained-mode direction described in [INTERFACE.md](/Users/jeff/Developer/vivid/docs/INTERFACE.md): state is accumulated in a long-lived object, then split across per-frame update, draw, overlay draw, thumbnail draw, and input-routing methods.

The main architectural boundary visible in this phase is:

- read model from runtime: [GraphSnapshot](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h)
- mutation sink into runtime: [UICommandSink](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h)
- UI-owned interaction state: mostly inside [NodeGraphUI](/Users/jeff/Developer/vivid/src/ui/node_graph.h)

## UI Module Map

### 1. Central retained controller

**Primary files**
- [node_graph.h](/Users/jeff/Developer/vivid/src/ui/node_graph.h)
- [node_graph.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph.cpp)

[NodeGraphUI](/Users/jeff/Developer/vivid/src/ui/node_graph.h) appears to be the central retained UI object. It owns or coordinates:

- viewport pan/zoom state
- selection state and node rectangles
- inspector behavior and edit focus
- popup/modal visibility flags
- browser/search/filter state for examples and packages
- graph meta editor state
- style/theme selection hooks
- callback wiring for package actions, updates, about dialogs, and custom inspector hooks

This suggests the UI is centered around one broad controller object rather than many smaller screen- or panel-specific controllers.

### 2. Core graph layout and geometry

**Primary files**
- [node_graph.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph.cpp)
- [node_graph_constants.h](/Users/jeff/Developer/vivid/src/ui/node_graph_constants.h)
- [node_graph_util.h](/Users/jeff/Developer/vivid/src/ui/node_graph_util.h)
- [inspector_layout.h](/Users/jeff/Developer/vivid/src/ui/inspector_layout.h)

These files appear to own:

- node box sizing and port placement
- graph-space to screen-space transforms
- inspector layout helpers
- reusable constants for node dimensions, spacing, and overlay geometry

Layout logic appears separate from raw drawing calls, but it still lives under the `NodeGraphUI` umbrella rather than in a separate layout engine.

### 3. Graph drawing layer

**Primary files**
- [node_graph_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_draw.cpp)

This translation unit appears to handle the main graph-surface rendering:

- node bodies and labels
- wire rendering
- control previews, audio previews, and node status markers
- inspector-adjacent visual affordances
- integration with thumbnails and renderer primitives

The draw path appears meaningfully split from input handling and retained state, but it is still method-based rendering on the main `NodeGraphUI` object.

### 4. Graph input layer

**Primary files**
- [node_graph_input.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_input.cpp)

This file appears to own the core graph/editor interaction loop:

- mouse hit-testing and drag behavior
- pan/zoom and scroll behavior
- keyboard shortcuts and text entry routing
- inspector text-field editing
- clipboard paste and focused-field handling
- node/connection selection and manipulation

It suggests that input focus, modal state, and field-editing concerns are handled centrally inside the same retained controller that also owns viewport and overlay state.

### 5. Overlay and browser layer

**Primary files**
- [node_graph_overlays.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_overlays.cpp)
- [node_graph_overlay_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_overlay_draw.cpp)
- [node_graph_overlay_input.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_overlay_input.cpp)
- [overlay_layouts.h](/Users/jeff/Developer/vivid/src/ui/overlay_layouts.h)
- [overlay_layouts.cpp](/Users/jeff/Developer/vivid/src/ui/overlay_layouts.cpp)

These files appear to make overlays into a semi-separate UI layer inside the same controller:

- example browser
- package browser
- create-operator modal
- graph meta editor
- about panel

The split is fairly readable:

- one file for overlay state/setup
- one for overlay drawing
- one for overlay input
- one shared set of geometry helpers for centering, list height, button placement, and visible rows

This is one of the clearer module boundaries inside `src/ui`.

### 6. Runtime-facing read model

**Primary file**
- [graph_snapshot.h](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h)

[GraphSnapshot](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h) appears to be the UI’s main runtime-fed read model. It holds:

- nodes and connections
- per-node param/output/string/file data
- cached operator metadata
- audio analysis snapshots
- MIDI mapping snapshots
- variation state
- operator catalog data for chooser flows
- recording and solo status

This snapshot looks intentionally UI-friendly:

- owned strings rather than borrowed C strings
- O(1) lookup tables for nodes and mappings
- packaged metadata for inspector and browser use

The UI therefore appears to consume a denormalized, presentation-ready graph snapshot rather than traversing live runtime structures directly.

### 7. Runtime-facing command boundary

**Primary file**
- [ui_command_sink.h](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h)

[UICommandSink](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h) appears to be the main write boundary from UI into runtime/application state.

It exposes commands for:

- param and string-param mutation
- node add/remove/connect/disconnect
- layout and resolution updates
- MIDI mapping changes
- variation and preset actions
- capture/recording actions
- operator creation/cloning/editor preferences
- undo/redo
- solo state

This suggests the UI generally does not mutate runtime structures directly. Instead, it issues high-level commands through an interface that can be implemented differently for app, test, or headless contexts.

### 8. Rendering support layer

**Primary files**
- [renderer_2d.h](/Users/jeff/Developer/vivid/src/ui/renderer_2d.h)
- [renderer_2d.cpp](/Users/jeff/Developer/vivid/src/ui/renderer_2d.cpp)
- [thumbnail_cache.h](/Users/jeff/Developer/vivid/src/ui/thumbnail_cache.h)
- [thumbnail_renderer.h](/Users/jeff/Developer/vivid/src/ui/thumbnail_renderer.h)

These files appear to provide lower-level drawing support beneath `NodeGraphUI`:

- batched 2D primitives, text, clip rects, and atlas-backed glyph rendering
- per-node thumbnail texture caching
- thumbnail compositing into the surface

This layer appears much closer to a reusable rendering service than the rest of `src/ui`, which is strongly editor-specific.

### 9. Theme and platform support

**Primary files**
- [theme_loader.h](/Users/jeff/Developer/vivid/src/ui/theme_loader.h)
- [ui_style.h](/Users/jeff/Developer/vivid/src/ui/ui_style.h)
- [file_dialog.h](/Users/jeff/Developer/vivid/src/ui/file_dialog.h)

These files appear to cover:

- style/theme discovery and loading
- serializable theme definitions
- built-in style fallbacks
- native file-dialog integration
- OS-level “open theme folder” behavior

This suggests styling is data-driven enough to be its own concern, but still relatively small compared with the graph-editor subsystem.

## Interaction Boundary Summary

The most important boundary visible in this phase is that the UI seems intentionally separated into three roles:

- **UI-owned retained state** inside [NodeGraphUI](/Users/jeff/Developer/vivid/src/ui/node_graph.h)
- **runtime-fed snapshot data** through [GraphSnapshot](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h)
- **runtime mutation commands** through [UICommandSink](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h)

This is a helpful architectural seam for later review because it gives three different questions to ask separately:

- what state does the UI own itself?
- what state is merely displayed?
- what actions does the UI request from the rest of the app?

From this phase, the UI appears to avoid treating the live runtime as its direct scene graph. Instead, it works from a presentation snapshot and emits coarse-grained commands back into the application.

## UI State vs Runtime State

### UI-owned state

The following categories appear to be owned primarily by [NodeGraphUI](/Users/jeff/Developer/vivid/src/ui/node_graph.h):

- viewport pan and zoom
- selection and hovered node/port state
- open/closed popup and overlay flags
- search text, filter tabs, scroll offsets, and browser selection state
- temporary text-entry buffers and edit focus
- transient node rectangle/cache geometry used for hit-testing and drawing
- inspector interaction state
- package/example browser affordances and callbacks

This is session-time interaction state rather than persisted graph data.

### Runtime-fed state

The following categories appear to be provided mainly through [GraphSnapshot](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h):

- current node/connection topology
- parameter values and lock flags
- output values, spreads, string outputs, and file-param values
- operator metadata and port metadata
- audio analysis snapshots and underrun state
- MIDI mappings and recent CC relay data
- preset, variation, solo, and recording status
- operator catalog and WGSL preset lists

This looks like the read-only model the UI renders and inspects.

### Runtime-mutated state

Anything that changes the graph or application model appears to be routed through [UICommandSink](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h), including:

- graph topology edits
- param edits
- layout writes
- create/clone operations
- package actions
- capture actions
- undo/redo

That separation should be a useful audit seam later, especially when checking whether UI events and persisted/runtime state remain aligned.

## Major UI Flows

### 1. Graph editing flow

The main graph-editing flow appears to combine:

- runtime-fed node/connection data from [GraphSnapshot](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h)
- layout and hit-test calculations in [node_graph.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph.cpp)
- visual rendering in [node_graph_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_draw.cpp)
- interaction handling in [node_graph_input.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_input.cpp)
- mutation requests through [UICommandSink](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h)

This appears to be the central user workflow the rest of the UI grows around.

### 2. Inspector and field-editing flow

Inspector editing appears to be distributed across:

- parameter metadata from the snapshot/operator metadata
- layout helpers from [inspector_layout.h](/Users/jeff/Developer/vivid/src/ui/inspector_layout.h)
- field/input handling in [node_graph_input.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_input.cpp)
- draw behavior in [node_graph_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_draw.cpp)

This suggests the inspector is not a wholly separate subsystem, but a tightly integrated facet of the main graph UI.

### 3. Example/package browser flow

The browser flows appear to be the strongest example of a semi-modular sub-UI:

- state toggles and data refresh in [node_graph_overlays.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_overlays.cpp)
- layout in [overlay_layouts.cpp](/Users/jeff/Developer/vivid/src/ui/overlay_layouts.cpp)
- rendering in [node_graph_overlay_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_overlay_draw.cpp)
- interaction in [node_graph_overlay_input.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_overlay_input.cpp)

These flows still live under `NodeGraphUI`, but they appear more compartmentalized than some of the graph and inspector behavior.

### 4. Thumbnail visualization flow

Thumbnail behavior appears to span:

- thumbnail texture lifetime in [thumbnail_cache.h](/Users/jeff/Developer/vivid/src/ui/thumbnail_cache.h)
- surface compositing in [thumbnail_renderer.h](/Users/jeff/Developer/vivid/src/ui/thumbnail_renderer.h)
- node draw integration in [node_graph_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_draw.cpp)

This is a cross-cutting UI flow because it touches rendering primitives, GPU resources, and node presentation together.

## Architectural Choke Points

These are not findings. They are the UI areas later review is most likely to hinge on.

### 1. `NodeGraphUI` as the dominant controller

[NodeGraphUI](/Users/jeff/Developer/vivid/src/ui/node_graph.h) appears to own a very large amount of retained interaction state across graph editing, overlays, browsers, dialogs, inspector interactions, and viewport state. Later review should treat it as the main UI coupling hotspot.

### 2. Snapshot-to-command boundary

The `GraphSnapshot` and `UICommandSink` pair appears to be the core architectural seam between UI and runtime. Later review should check how consistently the UI stays on one side of that boundary rather than reaching around it.

### 3. Overlay complexity inside the same retained object

The overlay flows are split into separate files, but they still appear to share one broad controller object and much of the same input/focus state. Later review should look at whether overlays truly behave like isolated subflows or are strongly coupled to graph/editor state.

### 4. Distributed draw/input logic over one state object

The current file split is clear, but many behaviors appear to require coordination across:

- layout
n- draw
- input
- overlay draw
- overlay input

Later review should pay attention to how easily state transitions can be understood when behavior crosses those translation-unit boundaries.

### 5. Thumbnail and renderer integration

The thumbnail pipeline and renderer stack appear to be structurally separate from the graph controller, but tightly integrated at render time. Later review should examine this area when looking at GPU resource lifetime, visual caching, and editor responsiveness.

## Open Questions for Phase 5+

These are exploration questions, not findings.

- How is the `GraphSnapshot` produced and refreshed, and how tightly is it coupled to scheduler/runtime structures?
- How many `UICommandSink` implementations exist, and how different are test/headless/application paths?
- How much UI state in `NodeGraphUI` is effectively editor-only versus required for headless/control-driven operation?
- Are there additional UI-specific contracts outside `src/ui`, especially in `main.cpp` or runtime command surfaces, that materially shape interaction behavior?
- Where do package-browser data, example-browser data, and update-notice data actually originate, and how much transformation happens before they reach the UI?

## Phase Boundary

This phase establishes a broad model of the UI and interaction surface:

- one dominant retained controller in `NodeGraphUI`
- one runtime-fed presentation snapshot in `GraphSnapshot`
- one runtime mutation boundary in `UICommandSink`
- one supporting rendering layer for 2D primitives, thumbnails, themes, and native dialogs

The next exploration phase should move outward into cross-cutting systems that intersect with the UI but are not part of `src/ui` itself, especially package flows, control-server/MCP surfaces, export, hot reload, and release/update behavior.
