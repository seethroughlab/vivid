# Chain Visualizer Migration to VS Code

Move the chain visualizer from Vivid runtime to the vivid-vscode extension.

## Current State

### Vivid Runtime (chain_visualizer.cpp, ~2,500 lines)
- Node graph with auto-layout (Sugiyama algorithm)
- Inspector panel with sliders, color pickers, ADSR graphs
- Solo mode (double-click node → full-screen its output)
- Status bar, mini-map, video recording
- Built on custom `OverlayCanvas` + `Gui` + `NodeGraph` classes

### vivid-vscode Extension (~4,400 lines TypeScript)
- Already functional (v0.1.0-alpha.8)
- Operator Library sidebar (browsable, searchable)
- Pending Changes panel (shows slider changes, apply/discard)
- WebSocket to running Vivid (port 9876)
- Claude Code MCP configuration
- **No node graph visualization** (intentional gap)

### Communication Infrastructure
WebSocket already supports: `select_node`, `solo_node`, `set_param_immediate`, `request_chain_structure`

---

## Target Workflow

| Now | After |
|-----|-------|
| App: node graph + preview + sliders | App: **preview only** (selected node output) |
| VS Code: pending changes list | VS Code: **node graph** + parameter controls |

---

## Analysis

### Arguments FOR Moving to VS Code

1. **Unified editing experience** - Parameters live next to code
2. **Better code navigation** - Click node → jump to source line
3. **Familiar UI toolkit** - VS Code webviews, no custom GUI maintenance
4. **Simpler runtime** - App window just shows output
5. **Claude workflow** - Tighter integration with MCP tools
6. **Already halfway there** - Pending Changes panel exists, WebSocket works

### Arguments AGAINST Moving to VS Code

1. **Standalone use** - App works without IDE (demos, installations)
2. **Performance** - Webview can't match native rendering for complex graphs
3. **Significant rewrite** - Node graph is 700+ lines of C++, needs TypeScript rewrite
4. **Split attention** - User looks at two windows (app + VS Code)
5. **Operator visualizations** - Audio waveforms, ADSR graphs need OverlayCanvas

### Key Insight

The **solo mode preview** is the valuable part that must stay in the app. Everything else (node graph, inspector, status bar) is arguably better in VS Code.

---

## Design Decisions

- **Scope**: Full migration - eventually remove NodeGraph, Gui widgets, and inspector from Vivid runtime
- **Node display**: Appropriate icons per operator type (texture, audio, geometry, etc.) - no thumbnails

---

## What Stays in Vivid Runtime

- `OverlayCanvas` for operator visualizations (waveforms, envelopes)
- Solo mode - full-screen any operator's output
- Simple status overlay (FPS, compile status)
- Video recording
- `VizDrawList` - operators use this for custom visualizations

## What Moves to VS Code Extension

- **Chain Graph View** (new webview panel)
  - Use [Elk.js](https://github.com/kieler/elkjs) for hierarchical layout
  - Show operators as nodes with connections
  - Click node → solo it in app + highlight source line
  - Type-appropriate icons (texture/audio/geometry/generator)

- **Parameter Inspector** (extend or replace Pending Changes)
  - Real-time sliders call `set_param_immediate` via WebSocket
  - Group by selected operator
  - Bidirectional sync with app

---

## Implementation Plan

### Phase 1: VS Code Chain Graph

**Goal**: Add chain visualization to vivid-vscode extension

**New file**: `src/chainGraphPanel.ts`

1. Create new webview panel registered in package.json under `views.vivid`
2. Use [Elk.js](https://github.com/kieler/elkjs) for hierarchical layout (similar to current Sugiyama)
3. Request chain structure via existing WebSocket (`request_chain_structure`)
4. Render nodes with type-appropriate icons:
   - Texture operators: image icon (purple)
   - Audio operators: waveform icon (blue)
   - Geometry operators: cube icon (green)
   - Generator operators: source icon
5. Draw connections between nodes (bezier curves via SVG or Canvas)
6. Click node → send `solo_node` via WebSocket → app shows that output full-screen
7. Selected node highlighted, parameters shown in inspector

**WebSocket additions needed** (if not already present):
- Operator type/category in chain structure response
- Connection info (which outputs connect to which inputs)

### Phase 2: VS Code Parameter Inspector

**Goal**: Edit parameters in VS Code instead of app

**Extend or replace**: `src/pendingChangesPanel.ts`

1. When a node is selected in chain graph, show its parameters
2. Render sliders, color pickers, dropdowns based on parameter type
3. On slider change → call `set_param_immediate` via WebSocket
4. Real-time sync: if parameters change in app, update VS Code sliders

**Parameter types to support**:
- `float` → slider
- `Vec2/3/4` → multiple sliders or specialized input
- `color` → color picker
- `enum` → dropdown
- `int` → slider or number input

### Phase 3: Simplify Vivid Runtime

**Goal**: Remove redundant GUI code from Vivid

**Delete from vivid-core**:
- `src/chain_visualizer.cpp` → most of it (keep status overlay, solo mode)
- `include/vivid/gui/node_graph.h` and implementation
- `include/vivid/gui/gui.h` widget system (sliders, buttons, etc.)
- Inspector panel rendering code

**Keep in vivid-core**:
- `OverlayCanvas` - still needed for operator visualizations (waveforms, ADSR)
- Solo mode - show selected operator's output full-screen
- Simple status bar (FPS, compile status, recording indicator)
- `VizDrawList` - operators use this for custom visualizations

**Behavior changes**:
- `--show-ui` flag → just shows simple status overlay, no node graph
- App window is primarily a preview surface
- All interaction happens in VS Code

### Phase 4: Polish & Edge Cases

1. **Standalone mode**: Consider if app should work without VS Code
   - Minimal: just show final output, no interaction
   - Or: keep a simplified `--show-ui` for demos

2. **Keyboard shortcuts**: Port useful ones to VS Code
   - Escape: exit solo mode
   - Arrow keys: navigate nodes

3. **Multi-window**: If user has multiple monitors, VS Code on one, app on other

---

## Files to Modify

### vivid-vscode (add)
| File | Changes |
|------|---------|
| `package.json` | Add chainGraph view, new dependencies (elkjs) |
| `src/chainGraphPanel.ts` | New - chain visualization webview |
| `src/statusBar.ts` | Handle chain structure messages |
| `src/pendingChangesPanel.ts` | Extend with parameter sliders |

### vivid (modify/delete in Phase 3)
| File | Changes |
|------|---------|
| `modules/vivid-core/src/chain_visualizer.cpp` | Simplify to solo mode + status only |
| `modules/vivid-core/include/vivid/gui/node_graph.h` | Delete |
| `modules/vivid-core/include/vivid/gui/gui.h` | Delete (keep OverlayCanvas) |
| `src/cli/editor_bridge.cpp` | May need to enhance chain structure response |

---

## Verification

After each phase:
1. Run a Vivid project with `--show-ui`
2. Open VS Code with vivid-vscode extension
3. Verify chain graph shows all operators with correct connections
4. Click node → app shows its output full-screen
5. Edit parameter in VS Code → see change in app preview
6. Adjust slider in app (if still present) → VS Code reflects change
