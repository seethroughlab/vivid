# vivid-devtools

Developer tools for Vivid: panels and chain visualization.

## Installation

This module is included with Vivid by default. No additional installation required.

## Quick Start

```bash
# Run with devtools UI
./build/bin/vivid projects/getting-started/02-hello-noise --show-ui
```

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Cmd+1` | Toggle Performance |
| `Cmd+G` | Toggle Background Grid |
| `Cmd+,` | Open Preferences |
| `Escape` | Exit Solo Mode |

## Panels

| Panel | ID | Description |
|-------|-----|-------------|
| NodeGraph | `nodegraph` | Visual representation of the operator chain |
| Inspector | `inspector` | Parameter sliders for selected operator |
| Performance | `performance` | Real-time FPS, frame time, memory graphs |
| StatusBar | `statusbar` | Top bar with panel toggles, stats, and actions |

---

## Glossary of Terms

### Panel System

| Term | Description |
|------|-------------|
| **Panel** | A UI container that can be shown/hidden. Base class for all devtools windows. |
| **Floating Panel** | A panel that can be dragged anywhere on screen (Inspector, Performance). Uses `PanelRole::Floating`. |
| **Background Panel** | A panel that fills all remaining space behind everything (NodeGraph). Uses `PanelRole::Background`. |
| **Panel Chrome** | The decorative frame around a panel: background, border, and title bar. |
| **Title Bar** | The draggable header area at the top of floating panels. Contains the panel title. |
| **Bounds** | A panel's position and size as `glm::vec4(x, y, width, height)` in logical pixels. |
| **Focus** | The active panel that receives keyboard input. Only one panel is focused at a time. |
| **Z-Order** | The layering order of floating panels. Higher z-order renders on top. |

### Status Bar Elements

| Term | Description |
|------|-------------|
| **Panel Toggle Buttons** | Buttons that show/hide panels. |
| **Active Toggle** | A toggle button with filled accent background (panel is visible). |
| **Inactive Toggle** | A toggle button with transparent background (panel is hidden). |
| **Stats Section** | The middle area showing FPS, frame time, resolution, memory. |
| **Snapshot Button** | Button to capture current frame as PNG. |
| **Record Button** | Button to start/stop video recording. |
| **Pending Changes Badge** | Orange indicator showing unsaved parameter changes from Claude workflow. |

### Node Graph (Chain Visualizer)

| Term | Description |
|------|-------------|
| **Node** | A box representing an operator in the chain. Contains title, pins, and preview. |
| **Node Title** | The header bar of a node showing the operator name. |
| **Pin** | A connection point on a node. Input pins are on the left, output pins on the right. |
| **Input Pin** | Green circle on left side of node. Receives data from another operator. |
| **Output Pin** | Red circle on right side of node. Sends data to other operators. |
| **Link** | A curved line connecting an output pin to an input pin. |
| **Dashed Link** | An orange dashed line representing a value binding (not texture flow). |
| **Selected Node** | Node with gold border. Shown in Inspector. |
| **Hovered Node** | Node with lighter border when mouse is over it. |
| **Focused Node** | Node with enlarged preview (3x size). Set via hover callback. |
| **Minimap** | Small overview in bottom-right corner showing all nodes and current viewport. |
| **Viewport** | The orange rectangle in the minimap showing the visible area. |
| **Grid** | The background dots/lines in the node graph area. |
| **Pan** | Moving the view by Ctrl+drag or middle-mouse drag. |
| **Zoom** | Scaling the view via scroll wheel. Range: 0.1x to 4.0x. |
| **Solo Mode** | Viewing a single operator's output fullscreen. Enter with double-click or Enter key. |

### Inspector Panel

| Term | Description |
|------|-------------|
| **Parameter Slider** | Horizontal slider for adjusting a float parameter. |
| **Slider Track** | The background bar of a slider. |
| **Slider Fill** | The filled portion showing current value. |
| **Parameter Label** | The name shown to the left of a slider. |
| **Value Label** | The numeric value shown to the right of a slider. |
| **Operator Section** | Group of parameters for one operator. |

### Background Grid

| Term | Description |
|------|-------------|
| **Background Grid** | Full-screen dimmed grid behind all panels. Independent of node graph. |
| **Minor Grid Lines** | Thin lines at regular intervals (40px). |
| **Major Grid Lines** | Thicker lines every 5th minor line (200px). |

### Input System

| Term | Description |
|------|-------------|
| **Input Ownership** | Each frame, one panel "owns" input based on z-order and interaction state. |
| **Consumed Input** | When a panel handles a click/drag, preventing it from reaching lower panels. |
| **Hit Testing** | Determining which UI element is under the mouse cursor. |
| **Hit Rect** | A rectangular region used for click detection (e.g., `ButtonRect`). |
| **Interaction** | Active drag or resize operation. Maintains input ownership until released. |

### Styling

| Term | Description |
|------|-------------|
| **UIStyle** | Struct containing all colors and scaled layout values. |
| **Scale Factor** | HiDPI multiplier (1.0 = standard, 2.0 = Retina). |
| **Logical Pixels** | Screen-independent units. Actual pixels = logical * scale. |
| **Physical Pixels** | Actual screen pixels after HiDPI scaling. |
| **Accent Color** | Primary highlight color (blue by default). |
| **Theme** | A preset collection of style colors (Dark, Light, High Contrast). |

### UI Layers (Z-Order)

| Layer | Value | Contents |
|-------|-------|----------|
| `Background` | 0 | Grid, background elements |
| `Nodes` | 100 | Node boxes, connections |
| `NodeContent` | 200 | Thumbnails, operator previews |
| `Panels` | 300 | Layout-managed panels (StatusBar, NodeGraph) |
| `FloatingPanels` | 350+ | Floating panels (Inspector, Performance) |
| `Menus` | 400 | Dropdown menus, context menus |
| `ModalOverlay` | 450 | Modal dialog darkened background |
| `ModalDialog` | 460 | Modal dialog content |
| `Tooltips` | 500 | Tooltips (highest priority) |

---

## File Structure

```
modules/vivid-devtools/
├── include/vivid/devtools/
│   ├── devtools.h           # Main orchestrator
│   ├── panel.h              # Panel base class
│   ├── panel_manager.h      # Panel lifecycle and z-order
│   ├── shortcut_manager.h   # Keyboard shortcuts
│   ├── preferences.h        # User preferences
│   ├── system_info.h        # Memory usage utilities
│   └── panels/
│       ├── node_graph_panel.h
│       ├── inspector_panel.h
│       ├── performance_panel.h
│       └── status_bar_panel.h
├── src/
│   ├── devtools.cpp
│   ├── panel.cpp
│   ├── panel_manager.cpp
│   └── panels/
│       ├── node_graph_panel.cpp
│       ├── inspector_panel.cpp
│       ├── performance_panel.cpp
│       └── status_bar_panel.cpp
└── assets/
    └── fonts/
        └── JetBrainsMono-Regular.ttf
```

## Dependencies

- vivid-core (OverlayCanvas, NodeGraph, UIStyle)
- GLFW (window/input)

## License

MIT
