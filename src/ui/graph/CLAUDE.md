# Node Graph Editor

## Purpose

This directory implements the visual node graph editor — the primary interface for building and inspecting Vivid graphs. It handles rendering nodes, wires, and thumbnails; processing mouse/keyboard input for selection, dragging, and connection; and drawing the parameter inspector panel.

## Key Files

| File | Role |
|------|------|
| `node_graph.h/cpp` | `NodeGraphUI` — core class, owns all editor state (selection, layout, interaction mode) |
| `graph_snapshot.h` | `GraphSnapshot` — read-only view of the compiled graph consumed by the UI each frame |
| `node_graph_constants.h` | Layout constants: margins, spacing, port sizes, animation speeds |
| `node_graph_util.h` | Coordinate conversion and hit-testing utilities |

### Drawing Files

| File | Role |
|------|------|
| `node_graph_draw.cpp` | Top-level draw orchestration: background, nodes, wires, overlays |
| `node_graph_draw_elements.cpp` | Individual node rendering: body, header, ports, thumbnails, badges |
| `node_graph_draw_connections.cpp` | Wire rendering: bezier curves, type-colored, animated flow |
| `node_graph_draw_overlays.cpp` | Overlay rendering: selection box, connection preview, tooltips, context menus |
| `node_graph_draw_inspector.cpp` | Inspector panel top-level draw |
| `node_graph_draw_inspector_sections.cpp` | Inspector sections: node header, port list, preset selector |
| `node_graph_draw_inspector_params.cpp` | Parameter widgets: sliders, knobs, toggles, file pickers, enum dropdowns |

### Input Files

| File | Role |
|------|------|
| `node_graph_input.cpp` | Top-level input dispatch: routes mouse/key events to specific handlers |
| `node_graph_input_click.cpp` | Click handling on the graph canvas: node selection, port clicking, background click |
| `node_graph_input_click_widgets.cpp` | Click handling within inspector widgets and overlays |
| `node_graph_input_click_context.cpp` | Right-click context menu handling |

### Update Files

| File | Role |
|------|------|
| `node_graph_update.cpp` | Per-frame state updates: layout animation, snapshot sync, auto-scroll |
| `node_graph_update_hover.cpp` | Hover detection and tooltip state |
| `node_graph_update_drag.cpp` | Drag handling: node move, wire draw, selection box, panning |

## How It's Organized

The editor is decomposed by concern rather than by class — `NodeGraphUI` is a single large class, but its implementation is spread across files to keep each file focused. The naming convention (`_draw_*`, `_input_*`, `_update_*`) makes the split clear.

**State model:** `NodeGraphUI` maintains persistent state including: the current selection set, hover target, active drag operation (if any), connection-in-progress state, inspector scroll position, and animation timers. The `GraphSnapshot` provides a read-only view of the current compiled graph, rebuilt by `GraphSnapshotBuilder` each frame from `RuntimeCore` state.

**Input flow:** GLFW events arrive at `node_graph_input.cpp`, which determines what was clicked or dragged and updates interaction state. Drags are tracked in `node_graph_update_drag.cpp` with frame-by-frame updates. Mutations (add node, connect, set param) go through `RuntimeCommandSink` — the editor never modifies the graph directly.

**Render flow:** Each frame, `node_graph_draw.cpp` orchestrates the full draw pass using `Renderer2D`. It iterates over `NodeRect`s (layout-resolved node positions), drawing bodies, ports, wires, thumbnails, and the inspector panel. Thumbnails come from `ThumbnailCache`, which is fed by `ThumbnailRenderer` capturing operator GPU output.

## Relationships

- **Upstream:** `GraphSnapshot` (read-only compiled graph view), `Renderer2D` (2D GPU drawing), `ThumbnailCache`/`ThumbnailRenderer`
- **Downstream:** `RuntimeCommandSink` → `RuntimeAPI` for all graph mutations
- **Peer:** `DialogManager` handles modal dialogs; `BuildConsolePanel` handles build output display

## See Also

- `docs/INTERFACE.md` — UI architecture, visual style, session exploration surface
- `docs/runtime/custom_thumbnails.md` — operator thumbnail rendering contract
