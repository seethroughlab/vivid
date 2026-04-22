# Phase 3: Host Integration

## Goal

Expose dedicated editor windows as a supported user-facing feature through the inspector and a keyboard shortcut, while preserving existing node double-click behavior.

This phase converts the Phase 2 plumbing into a discoverable feature and adds the persistence policy for editor-window geometry.

## Current Repo Facts

- Inspector rendering is centered in `src/ui/graph/node_graph_draw_inspector.cpp`.
- Custom inspector contexts are built in `src/ui/graph/node_graph_draw_inspector_sections.cpp`.
- Inspector interaction state lives in `src/ui/inspector/inspector_controller.{h,cpp}`.
- Current node double-click behavior is implemented in `src/ui/graph/node_graph_input_click.cpp` and already triggers source or clone flows.
- Settings persistence lives in `src/runtime/core/settings.{h,cpp}`.

## Locked Decisions For This Phase

1. Show an **Open Editor** control only when `sel_node->op_info->has_editor` is true.
2. Preserve existing node double-click semantics.
3. Add an official `Cmd+E` / `Ctrl+E` shortcut for the selected node.
4. Persist geometry by operator type slug, not by node id.
5. Reflect open-state in the inspector button and refocus instead of duplicating windows.

## Inspector Integration

### Button Visibility

The button should be rendered only when:

- exactly one node is selected
- the selected node has `op_info`
- `op_info->has_editor` is true

Operators without editors must remain visually unchanged.

### Button Placement

Render the button near the top of the inspector, after the node header and before the main parameter sections. It should be part of the existing inspector layout and hit-testing system rather than a bespoke overlay.

The plan should require wiring through:

- `src/ui/inspector/inspector_controller.{h,cpp}`
- `src/ui/graph/node_graph_draw_inspector.cpp`
- `src/ui/graph/node_graph_draw_inspector_sections.cpp`

### Button Behavior

On click:

- if the editor is closed, open it
- if the editor is already open, refocus it

The button should visually indicate the open state. Exact styling can follow the existing inspector button language, but the behavior must be explicit in the docs.

## Keyboard Shortcut

Add an official shortcut in the main window callback path:

- macOS: `Cmd+E`
- other platforms: `Ctrl+E`

Behavior:

- if a single selected node has an editor, open or refocus it
- otherwise do nothing

This shortcut replaces the Phase 2 temporary debug entry point.

## Explicit Non-Goal: Double-Click Behavior

Do not repurpose node double-click for editor opening in v1.

Current behavior in `src/ui/graph/node_graph_input_click.cpp` already opens shader or module source or begins the clone flow. Phase 3 must preserve that behavior exactly. The plan should call this out explicitly so implementation does not reintroduce ambiguity.

## Geometry Persistence

Extend `Settings` to remember editor geometry by operator type:

```cpp
struct EditorWindowGeometry {
    int x = -1;
    int y = -1;
    int width = 0;
    int height = 0;
};

std::unordered_map<std::string, EditorWindowGeometry> editor_window_geometry_by_type;
```

Requirements:

- key by operator type slug, not node id
- `0` width or height means use metadata defaults
- `-1` position means let the OS choose placement
- serialize in `src/runtime/core/settings.{h,cpp}`
- read during editor open and write back on close

Phase 3 should explicitly avoid inventing per-node geometry because that would make the UX inconsistent across instances of the same operator.

## Editor Manager Integration

Phase 3 depends on the manager exposing:

- `open(node_id)`
- `focus(node_id)`
- `is_open(node_id)`

If those methods are not already present after Phase 2, add them as part of this phase before touching the inspector UI.

The docs should also require the inspector open-state indicator to update correctly when:

- selection changes
- the editor closes
- the node is deleted
- a runtime reload closes all editors

## Files To Change

| Change | Path |
|---|---|
| Inspector button state and hit rects | `src/ui/inspector/inspector_controller.{h,cpp}` |
| Inspector drawing integration | `src/ui/graph/node_graph_draw_inspector.cpp` |
| Custom inspector bridge and layout helpers | `src/ui/graph/node_graph_draw_inspector_sections.cpp` |
| Preserve existing double-click behavior by leaving current handler semantics intact | `src/ui/graph/node_graph_input_click.cpp` |
| Add official keyboard shortcut | `src/runtime/core/window_manager.{h,cpp}` or the nearest real shortcut handler |
| Persist geometry | `src/runtime/core/settings.{h,cpp}` |
| Read/write geometry and focus state | `src/runtime/core/editor_window_manager.{h,cpp}` |

## Tests

### Automated Coverage

Add coverage where the existing single-window harness can support it:

1. `OperatorInfo::has_editor` drives button visibility.
2. Editor-open shortcut dispatch only triggers for editor-capable selected nodes.
3. Settings serialization round-trips editor geometry data.
4. Inspector open-state logic reflects manager state correctly.

Do not promise automated second-window click-driving in this phase unless the harness is extended for that specific purpose.

### Manual QA

1. Select an editor-capable node and verify the button appears.
2. Select a non-editor-capable node and verify the button does not appear.
3. Click the button to open the editor, then click again to refocus without duplication.
4. Use `Cmd+E` / `Ctrl+E` to open or refocus the editor.
5. Resize and move the editor, quit the app, relaunch, and verify geometry is reused for the same operator type.
6. Delete the node and confirm the button state and window state both clear correctly.

## Acceptance Criteria

1. The inspector shows **Open Editor** only for editor-capable nodes.
2. Clicking the button opens or refocuses the editor window.
3. `Cmd+E` / `Ctrl+E` opens or refocuses the selected node’s editor when available.
4. Node double-click behavior is unchanged from current source or clone flows.
5. Geometry is persisted by operator type and reused across sessions.
6. Operators without editors remain visually and behaviorally unchanged.

## Non-Goals

- Changing node double-click behavior.
- Adding editor tabs, docking, or pane layouts.
- Driving second-window UI through the existing UI script runner.
