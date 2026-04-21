# Phase 3: Host Integration

## Goal

Replace the debug open-hook from Phase 2 with real host UI: an **Open Editor** button in the inspector (and double-click-to-open on nodes that declare an editor), plus per-operator-type persistence of window size/position.

## Context

After Phase 2 the machinery to open, draw, and close editor windows is in place and proven via a debug shortcut. Phase 3 turns it into a feature users can discover.

Relevant existing surfaces:

- `InspectorController` (`src/ui/inspector/inspector_controller.h`) already tracks button rects and hover/click state for the inspector panel.
- `draw_inspector(...)` is orchestrated from `src/ui/graph/node_graph_draw_inspector.cpp` and section layout from `node_graph_draw_inspector_sections.cpp`. Custom-inspector invocation already passes a `VividInspectorContext` — the "Open Editor" button slots into the same surface at the top of the panel.
- Node double-click is handled in `src/ui/graph/node_graph_input.cpp` and is currently a no-op for most nodes (or minor selection behavior). Check at implementation time.
- `Settings` (`src/runtime/core/settings.h`) already persists the main window size/position; editor windows extend that pattern keyed by operator type.

## Scope

- Render an **Open Editor** button at a consistent location (top of the inspector panel) when the selected node's loader reports `has_editor() == true`.
- Optional: double-click on a node in the graph opens its editor if it has one.
- Persist per-operator-type editor window geometry across sessions.
- Keyboard shortcut (Cmd+E / Ctrl+E with a node selected) opens the editor — kept from Phase 2 debug hook but now official.
- Remove / disable the Phase 2 debug-only entry point.

## Design

### "Open Editor" button

Add a small button at the top of the inspector panel (above the parameter groups) when `loader.has_editor()` is true. Use existing `draw_ui_helpers.h` button primitives for visual consistency. The button:

- Is disabled / omitted entirely for operators without an editor (keeps v1 operators visually identical).
- On click, calls `editor_windows.open(node_id, loader, instance)`.
- Shows a subtle active indicator when an editor is already open for that node (e.g. inverted colors or "Editor Open ▸").
- Clicking while open refocuses the existing window (`glfwFocusWindow`) rather than opening a second.

The button rect goes through the existing `InspectorController` rect-tracking machinery so it participates in normal hit-testing.

### Double-click to open

In `src/ui/graph/node_graph_input.cpp`, when a node is double-clicked and its loader `has_editor()`, route to `editor_windows.open(...)`. Do not override other double-click behavior (renaming, etc.) — if there is existing conflicting behavior, keep it and prefer the editor only where the slot is currently empty, or introduce a modifier-click variant. Confirm at implementation time by reading the current handler.

### Settings persistence

Extend `Settings` (`src/runtime/core/settings.h` / its JSON load/save):

```cpp
struct EditorWindowGeometry {
    int x = -1, y = -1;       // -1 = let OS pick
    int width = 0, height = 0; // 0 = use VividEditorMetadata default
};

std::unordered_map<std::string, EditorWindowGeometry> editor_window_geometry_by_type;
```

Keyed by operator type id (e.g. `"drum_sequencer"`), not node id — the user expects "the DrumSequencer editor" to remember its size the next time any DrumSequencer's editor is opened. `EditorWindowManager::open(...)` consults this map before calling `glfwCreateWindow`; `EditorWindow` writes back on close (and on size/move callbacks, debounced).

### Graph-delete / unload hooks

Already wired in Phase 2. In this phase, just make sure the inspector's "Open Editor" state refreshes when the selected node changes or is deleted (the button's open/closed visual state should reflect reality).

### Removing Phase 2's debug entry point

If Phase 2 introduced a `VIVID_EDITOR_DEBUG` compile guard or an undocumented shortcut, delete it here. The Cmd+E keyboard shortcut stays as an official entry point.

## Files

| Change | Path |
|---|---|
| "Open Editor" button rendering + click | `src/ui/inspector/inspector_controller.{h,cpp}` |
| Wire button into draw path | `src/ui/graph/node_graph_draw_inspector_sections.cpp`, `src/ui/graph/node_graph_draw_inspector.cpp` |
| Double-click to open | `src/ui/graph/node_graph_input.cpp` |
| Cmd+E shortcut | `src/runtime/core/window_manager.{h,cpp}` (key callback) or appropriate keymap file |
| Per-type geometry persistence | `src/runtime/core/settings.h`, settings JSON load/save |
| Geometry read/write from manager | `src/runtime/core/editor_window_manager.{h,cpp}` (from Phase 2) |

## Acceptance Criteria

1. Selecting a DrumSequencer node (once it has `VIVID_EDITOR` in Phase 4) shows an **Open Editor** button at the top of the inspector. Selecting a basic LFO does not.
2. Clicking the button opens the editor window. Clicking again with the editor already open refocuses rather than duplicates.
3. Double-clicking a node with an editor opens it without interfering with any existing double-click behavior.
4. Cmd+E (Ctrl+E on non-mac) opens the editor for the currently selected node if available; otherwise no-op.
5. Resizing / moving the editor window and quitting, then relaunching and opening the editor again, restores the previous size/position.
6. Opening the editor for DrumSequencer A, closing it, then opening for DrumSequencer B reuses the same remembered geometry.
7. Deleting the node while its editor is open closes the editor and clears the button's "open" indicator on the next inspector frame.
8. No regressions in inspector rendering for operators without an editor.
9. `ctest` passes.

## Dependencies

- **Phase 1** — ABI with `has_editor()` flag.
- **Phase 2** — working `EditorWindowManager` including lifecycle hooks.

## Out of Scope for This Phase

- Any operator actually using `VIVID_EDITOR` in its real implementation (Phase 4).
- Docking, tabbed editors, preset-browser sidebar inside the editor window — additive future work.
