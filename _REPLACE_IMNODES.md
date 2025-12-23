# Plan: Replace imnodes + Move ImGui to Addon

## Goal
1. **Extend existing `vivid::Canvas` API** for overlay rendering (no new Canvas needed)
2. **Build custom node graph** using Canvas (replaces imnodes)
3. **Move ImGui to `vivid-gui` addon** (users can still use it)
4. **Add zoom + better auto-layout** (missing from imnodes)

## Current State
- `core/imgui/chain_visualizer.cpp` (~1200 lines) uses imnodes + ImGui
- ImGui tightly integrated in cli/app.cpp (11 integration points)
- No ImGui in core effect operators (clean separation)
- **Existing 2D Canvas API** at `core/include/vivid/effects/canvas.h` with full feature set

---

## Phase 0: Extend Existing Canvas for Overlay Rendering

### Existing Canvas API
Vivid already has a comprehensive 2D Canvas API at:
- **Header**: `core/include/vivid/effects/canvas.h`
- **Implementation**: `core/src/effects/canvas.cpp`
- **Renderer**: `core/src/effects/canvas_renderer.h/.cpp`

**Already supports**:
- Full transform (translate, rotate, scale, setTransform with glm::mat3)
- Primitives (rect, circle, paths, bezier curves, arcs)
- Gradients (linear, radial, conic)
- TTF text rendering with FreeType
- Clipping with stencil buffer
- State save/restore stack
- Image/texture drawing

### What Needs to Change
The current Canvas is a `TextureOperator` that renders to an output texture.
For the node graph overlay, we need to render **directly to the screen**.

**Approach: OverlayCanvas class**
- New class that wraps `CanvasRenderer` for direct screen rendering
- Accepts `WGPURenderPassEncoder` instead of creating its own texture
- Reuses all existing primitives/transforms from CanvasRenderer

### New Files (Minimal)
```
core/include/vivid/overlay_canvas.h   # Thin wrapper for screen rendering
core/src/overlay_canvas.cpp           # Uses existing CanvasRenderer
```

### Implementation Notes
- Reuse `CanvasRenderer` internals (already batched, already has transforms)
- Just need different output target (render pass vs texture)
- Font atlas already exists, reuse it
- ~200 lines of new code (wrapper only)

---

## Phase 1: Custom Node Graph (Using Canvas)

### New Files
```
core/nodegraph/
├── node_graph.h          # Main API
├── node_graph.cpp        # Core implementation (uses Canvas)
├── node_graph_style.h    # Style configuration
└── node_graph_layout.cpp # Hierarchical layout algorithm
```

### Core API
```cpp
class NodeGraph {
public:
    // Frame lifecycle
    void beginEditor(Canvas& canvas, float width, float height);
    void endEditor();

    // Nodes
    void beginNode(int id);
    void endNode();
    void beginNodeTitleBar();
    void endNodeTitleBar();

    // Pins
    void beginInputAttribute(int id);
    void endInputAttribute();
    void beginOutputAttribute(int id);
    void endOutputAttribute();

    // Links
    void link(int id, int startAttr, int endAttr);

    // Positioning
    void setNodePosition(int nodeId, glm::vec2 gridPos);
    glm::vec2 getNodePosition(int nodeId) const;

    // Selection & Hover
    bool isNodeHovered(int* outId) const;
    bool isLinkHovered(int* outId) const;
    void selectNode(int id);
    void clearSelection();

    // ZOOM (new!)
    float zoom() const;
    void setZoom(float z);
    void zoomToFit();

    // Pan
    glm::vec2 pan() const;
    void setPan(glm::vec2 p);

    // Layout
    void autoLayout();  // Re-run hierarchical layout
};
```

### Zoom Implementation
- Transform matrix: `glm::mat3` with translation + scale
- `gridToScreen(glm::vec2)` / `screenToGrid(glm::vec2)` helpers
- Scroll wheel = zoom (pivot around mouse position)
- Ctrl+drag = pan
- Zoom range: 0.1x to 4.0x

### Rendering Approach
- All drawing via `Canvas` (not ImDrawList)
- Transform coordinates before drawing
- Nodes: rounded rects with title bars
- Pins: circles at node edges
- Links: bezier curves between pins

---

## Phase 2: Move ImGui to vivid-gui Addon

### New Addon Structure
```
addons/vivid-gui/
├── CMakeLists.txt           # Fetches ImGui, builds addon
├── include/vivid/gui/
│   ├── imgui_integration.h  # (moved from core)
│   └── inspector.h          # Parameter editing UI
└── src/
    ├── imgui_integration.cpp
    ├── inspector.cpp
    └── gui_addon.cpp        # Registration
```

### What Moves to Addon
- ImGui initialization/shutdown
- ImGui frame management
- Inspector panel (parameter editing)
- Any future ImGui-based UI

### What Stays in Core
- `vivid::Canvas` (new, no ImGui)
- `NodeGraph` (uses Canvas, no ImGui)
- `ChainVisualizer` (rewritten to use Canvas + NodeGraph)
- `Display` class (existing text renderer)

### CLI Changes (app.cpp)
- Remove direct ImGui calls
- Use `ChainVisualizer` which uses `Canvas` internally
- If vivid-gui addon loaded: ImGui available for user chains
- If not loaded: core visualization still works

### Build System
```cmake
option(VIVID_BUILD_GUI "Build ImGui addon" ON)

if(VIVID_BUILD_GUI)
    add_subdirectory(addons/vivid-gui)
    target_link_libraries(vivid PRIVATE vivid-gui)
endif()
```

---

## Phase 3: Enhanced Layout

### Hierarchical Layout (Sugiyama Algorithm)
1. Layer assignment (longest path from sources)
2. Crossing reduction (barycenter heuristic)
3. Coordinate assignment (Brandes-Köpf or simple)

### Features
- Auto-relayout on chain change
- Animated transitions between layouts
- Manual position override (drag nodes)
- Snap to grid option

---

## Phase 4: Additional Features

### Mini-map
- Corner overview showing all nodes
- Click to navigate
- Current viewport indicator

### Search/Filter
- Text search for operator names
- Filter by type (Texture, Audio, etc.)

### Keyboard Navigation
- Arrow keys: move selection
- F: fit all in view
- 1: zoom to 100%
- Enter: solo mode
- B: bypass

---

## Header/Status Bar Info (Must Preserve)

The current chain visualizer header shows critical info that must be preserved:

### Performance Stats (Always Visible)
- **FPS**: `%.1f FPS` - Frames per second
- **Frame Time**: `%.2fms` - Milliseconds per frame
- **Resolution**: `%dx%d` - Current framebuffer size
- **Operator Count**: `%zu ops` - Total operators in chain

### Memory Usage
- **Format**: `MEM: X.X MB` or `MEM: X.XX GB`
- **Color-coded**: Green (<500MB), Yellow (500MB-2GB), Red (>=2GB)

### Audio Stats (When Audio Active)
- **DSP Load**: `DSP: %.0f%%` with peak tracking
- **Dropped Events**: `⚠ %llu dropped` (red, if any)
- Both clickable to reset counters

### Recording Controls
- **Not Recording**: "Record" dropdown (H.264, H.265, ProRes), "Snapshot" button
- **Recording**: Red `● REC`, frame counter `%d frames (%.1fs)`, "Stop" button

### Controls Menu
- Dropdown showing keyboard shortcuts:
  - Tab: Toggle UI
  - F: Fullscreen
  - Ctrl+Drag: Pan graph
  - S: Solo node
  - B: Bypass node

### Solo Mode Overlay
- Top-left: `SOLO: <name>` (yellow) + "Exit Solo" button + "(or press ESC)"

### Debug Values Panel
- Bottom-left corner, only when debug values present
- Each value: name + sparkline (120x20) + current value

### Node Hover Tooltip
- Operator type name (blue)
- Output type (Texture, Geometry, Audio, etc.)
- Resource info (texture size, memory, channels)
- Bypass status if active (orange)

---

## Design Decisions
- **Separate branch**: Breaking changes OK for cleaner refactor
- **Node positioning**: Draggable + auto-layout
- **Phase 4 priority**: Mini-map first, then search/filter, then keyboard nav
- **Canvas API**: Native Vivid drawing, no ImGui dependency in core

---

## Implementation Order

### Week 1: OverlayCanvas + Node Graph
- [ ] OverlayCanvas wrapper (extends existing CanvasRenderer for screen output)
- [ ] NodeGraph class skeleton
- [ ] Node rendering (background, title, content area)
- [ ] Pin rendering and tracking
- [ ] Link rendering (bezier curves)
- [ ] Hit testing with transforms
- [ ] Selection and hover
- [ ] Zoom (scroll wheel) + Pan (Ctrl+drag)

### Week 2: Chain Visualizer Migration
- [ ] Rewrite chain_visualizer to use NodeGraph + OverlayCanvas
- [ ] Port header/status bar to OverlayCanvas
- [ ] Port debug panel to OverlayCanvas
- [ ] Port hover tooltips to OverlayCanvas
- [ ] Remove imnodes dependency

### Week 3: ImGui Addon + Polish
- [ ] Create vivid-gui addon structure
- [ ] Move ImGui to addon
- [ ] Update cli/app.cpp for optional ImGui
- [ ] Hierarchical layout algorithm
- [ ] Mini-map

---

## Files Summary

### New Files (Core)
- `core/include/vivid/overlay_canvas.h` - Thin wrapper for screen rendering
- `core/src/overlay_canvas.cpp` - Uses existing CanvasRenderer
- `core/nodegraph/node_graph.h` - Main node graph API
- `core/nodegraph/node_graph.cpp` - Node graph implementation
- `core/nodegraph/node_graph_style.h` - Style configuration
- `core/nodegraph/node_graph_layout.cpp` - Hierarchical layout algorithm

### Reused Files (Core) - No Changes Needed
- `core/include/vivid/effects/canvas.h` - Existing Canvas API
- `core/src/effects/canvas.cpp` - Existing Canvas implementation
- `core/src/effects/canvas_renderer.h/.cpp` - Batched WebGPU renderer

### New Files (Addon)
- `addons/vivid-gui/CMakeLists.txt`
- `addons/vivid-gui/include/vivid/gui/imgui_integration.h`
- `addons/vivid-gui/src/imgui_integration.cpp`
- `addons/vivid-gui/src/gui_addon.cpp`

### Modified Files
- `core/CMakeLists.txt` - Remove imnodes, add overlay_canvas/nodegraph
- `core/imgui/chain_visualizer.cpp` - Full rewrite using NodeGraph + OverlayCanvas
- `cli/app.cpp` - Remove direct ImGui calls
- `cli/CMakeLists.txt` - Optional vivid-gui linking

---

## Technical Notes

### Coordinate Spaces
- **Screen space**: Pixel coordinates (Canvas draw commands)
- **Grid space**: Logical node positions (stored, used for layout)
- Transform: `screenPos = gridPos * zoom + pan`

### Node Data Structure
```cpp
struct NodeState {
    int id;
    glm::vec2 gridPos;            // Position in grid space
    glm::vec2 size;               // Computed size after content
    std::vector<PinState> inputs;
    std::vector<PinState> outputs;
    bool selected = false;
    bool hovered = false;
};
```

### Why Not Just Fork imnodes?
- imnodes has deep architectural assumptions against zoom
- Coordinates are stored as-is, not transformed
- Hit testing doesn't account for scale
- Cleaner to build purpose-fit solution

---

## Zoom Quality Considerations

The existing Canvas API uses **vector rendering** for shapes but **bitmap rendering** for text.
This has implications for zoom quality:

### What Scales Well (Vector)
| Element | Behavior | Notes |
|---------|----------|-------|
| Node boxes | Crisp at all zooms | Tessellated at draw time with transform applied |
| Bezier links | Crisp at all zooms | Curves generated with current transform |
| Pin circles | Crisp at all zooms | Geometry regenerated each frame |
| Rounded rects | Crisp at all zooms | Arc segments generated per-frame |

### Potential Issues at High Zoom

#### 1. Text (Bitmap Font Atlas)
**Problem**: FontAtlas is rasterized at a fixed size. At 4x zoom, text pixelates.

**Solution - Multiple Font Sizes**:
```cpp
// Load 3 sizes at startup
m_fontSmall = loadFont(ctx, "font.ttf", 10);   // For zoom < 0.5x
m_fontMedium = loadFont(ctx, "font.ttf", 14);  // For zoom 0.5x - 2x
m_fontLarge = loadFont(ctx, "font.ttf", 28);   // For zoom > 2x

// Select based on current zoom
FontAtlas* getFont(float zoom) {
    if (zoom < 0.5f) return m_fontSmall;
    if (zoom > 2.0f) return m_fontLarge;
    return m_fontMedium;
}
```

**Alternative - SDF Fonts** (future enhancement):
- Signed Distance Field fonts render crisp at any zoom
- Requires shader modification and SDF font generation
- Could be Phase 5 enhancement if needed

#### 2. Circle/Arc Segment Count
**Problem**: Circles use fixed segment count (default 32). At 10x zoom, polygon edges visible.

**Solution - Zoom-Aware Segments**:
```cpp
int getCircleSegments(float radius, float zoom) {
    float screenRadius = radius * zoom;
    // ~4 segments per 10 screen pixels of circumference
    return std::clamp(static_cast<int>(screenRadius * 0.6f), 8, 128);
}
```

#### 3. Line Width Interpretation
**Problem**: Should a 2px line stay 2 screen pixels, or scale with zoom?

**Decision**: Use **screen-space line widths** for node graph:
- Node borders: Always 1-2 screen pixels (crisp edges)
- Links: Always 2-3 screen pixels (visible but not chunky)
- Selection highlight: Always 3 screen pixels

This matches how design tools (Figma, Sketch) handle zoom.

#### 4. Hit Testing Tolerance
**Problem**: At high zoom, a 5px hit tolerance feels too small. At low zoom, it's too large.

**Solution - Zoom-Adjusted Hit Testing**:
```cpp
float getHitTolerance(float zoom) {
    // Always ~8 screen pixels regardless of zoom
    return 8.0f / zoom;
}
```

### Zoom Range
Recommend limiting zoom to **0.1x - 4.0x** for this phase:
- 0.1x: See entire chain overview
- 1.0x: Normal editing
- 4.0x: Detail inspection

This range is manageable with the multi-size font approach. If users need higher zoom,
SDF fonts can be added in a future phase.
