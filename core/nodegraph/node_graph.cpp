#include "node_graph.h"
#include <vivid/overlay_canvas.h>
#include <algorithm>
#include <cmath>

namespace vivid {

// -------------------------------------------------------------------------
// Frame Lifecycle
// -------------------------------------------------------------------------

void NodeGraph::beginEditor(OverlayCanvas& canvas, float width, float height, const NodeGraphInput& input) {
    m_canvas = &canvas;
    m_width = width;
    m_height = height;
    m_input = input;
    m_inEditor = true;

    // Reset hover state
    m_hoveredNodeId = -1;
    m_hoveredLinkId = -1;
    m_hoveredPinId = -1;

    // Reset node hover/dragging state
    for (auto& [id, node] : m_nodes) {
        node.hovered = false;
    }
    for (auto& [id, link] : m_links) {
        link.hovered = false;
    }
}

void NodeGraph::endEditor() {
    if (!m_inEditor || !m_canvas) return;

    // Update hover states FIRST so handleInput knows what's under the mouse
    updateHover();

    // Handle input (zoom, pan, selection, drag)
    handleInput();

    // Render everything
    // NOTE: Nodes must render BEFORE links because pin.screenPos is computed during node rendering
    renderBackground();
    if (m_style.showGrid) {
        renderGrid();
    }
    renderNodes();  // Computes pin screen positions
    renderLinks();  // Uses pin positions (renders on top of nodes)

    m_inEditor = false;
    m_canvas = nullptr;
}

// -------------------------------------------------------------------------
// Node API
// -------------------------------------------------------------------------

void NodeGraph::beginNode(int id) {
    m_currentNodeId = id;

    // Create node if it doesn't exist
    if (m_nodes.find(id) == m_nodes.end()) {
        NodeState node;
        node.id = id;
        node.size = {m_style.nodeWidth, 80};  // Default size
        m_nodes[id] = node;
    }

    // Clear pins for rebuild
    m_nodes[id].inputs.clear();
    m_nodes[id].outputs.clear();
}

void NodeGraph::setNodeTitle(const std::string& title) {
    if (m_currentNodeId >= 0 && m_nodes.count(m_currentNodeId)) {
        m_nodes[m_currentNodeId].title = title;
    }
}

void NodeGraph::setNodeContent(std::function<void(OverlayCanvas&, float, float, float, float)> callback) {
    if (m_currentNodeId >= 0 && m_nodes.count(m_currentNodeId)) {
        m_nodes[m_currentNodeId].contentCallback = callback;
    }
}

void NodeGraph::endNode() {
    if (m_currentNodeId >= 0 && m_nodes.count(m_currentNodeId)) {
        auto& node = m_nodes[m_currentNodeId];

        // Calculate node height based on pins
        int maxPins = std::max(node.inputs.size(), node.outputs.size());
        float pinsHeight = std::max(1, maxPins) * m_style.pinSpacing + m_style.nodeContentPadding * 2;

        // Add space for content area (operator preview) if callback is set
        float contentAreaHeight = 0.0f;
        if (node.contentCallback) {
            // 16:9 aspect ratio thumbnail: width ~100px, height ~56px + padding
            contentAreaHeight = 64.0f;
        }

        node.size.y = m_style.nodeTitleHeight + contentAreaHeight + pinsHeight;
        node.size.x = m_style.nodeWidth;
    }
    m_currentNodeId = -1;
}

// -------------------------------------------------------------------------
// Pin API
// -------------------------------------------------------------------------

void NodeGraph::beginInputAttribute(int id) {
    m_currentPinId = id;
    m_currentPinIsOutput = false;

    if (m_currentNodeId >= 0 && m_nodes.count(m_currentNodeId)) {
        PinState pin;
        pin.id = id;
        m_nodes[m_currentNodeId].inputs.push_back(pin);
        m_pinToNode[id] = m_currentNodeId;
    }
}

void NodeGraph::beginOutputAttribute(int id) {
    m_currentPinId = id;
    m_currentPinIsOutput = true;

    if (m_currentNodeId >= 0 && m_nodes.count(m_currentNodeId)) {
        PinState pin;
        pin.id = id;
        m_nodes[m_currentNodeId].outputs.push_back(pin);
        m_pinToNode[id] = m_currentNodeId;
    }
}

void NodeGraph::pinLabel(const std::string& label) {
    if (m_currentNodeId < 0 || !m_nodes.count(m_currentNodeId)) return;

    auto& node = m_nodes[m_currentNodeId];
    std::vector<PinState>& pins = m_currentPinIsOutput ? node.outputs : node.inputs;

    if (!pins.empty()) {
        pins.back().label = label;
    }
}

void NodeGraph::endInputAttribute() {
    m_currentPinId = -1;
}

void NodeGraph::endOutputAttribute() {
    m_currentPinId = -1;
}

// -------------------------------------------------------------------------
// Links
// -------------------------------------------------------------------------

void NodeGraph::link(int id, int startPinId, int endPinId) {
    LinkState lnk;
    lnk.id = id;
    lnk.startPinId = startPinId;
    lnk.endPinId = endPinId;
    m_links[id] = lnk;
}

// -------------------------------------------------------------------------
// Node Positioning
// -------------------------------------------------------------------------

void NodeGraph::setNodePosition(int nodeId, glm::vec2 gridPos) {
    if (m_nodes.count(nodeId)) {
        m_nodes[nodeId].gridPos = gridPos;
    }
}

glm::vec2 NodeGraph::getNodePosition(int nodeId) const {
    auto it = m_nodes.find(nodeId);
    if (it != m_nodes.end()) {
        return it->second.gridPos;
    }
    return {0, 0};
}

void NodeGraph::autoLayout() {
    // TODO: Implement Sugiyama hierarchical layout
    // For now, just arrange nodes in a grid
    int col = 0;
    int row = 0;
    for (auto& [id, node] : m_nodes) {
        node.gridPos = {col * 250.0f + 50, row * 150.0f + 50};
        col++;
        if (col >= 4) {
            col = 0;
            row++;
        }
    }
}

// -------------------------------------------------------------------------
// Selection & Hover
// -------------------------------------------------------------------------

bool NodeGraph::isNodeHovered(int* outId) const {
    if (m_hoveredNodeId >= 0) {
        if (outId) *outId = m_hoveredNodeId;
        return true;
    }
    return false;
}

bool NodeGraph::isLinkHovered(int* outId) const {
    if (m_hoveredLinkId >= 0) {
        if (outId) *outId = m_hoveredLinkId;
        return true;
    }
    return false;
}

bool NodeGraph::isPinHovered(int* outId) const {
    if (m_hoveredPinId >= 0) {
        if (outId) *outId = m_hoveredPinId;
        return true;
    }
    return false;
}

void NodeGraph::selectNode(int id) {
    // Deselect previous
    if (m_selectedNodeId >= 0 && m_nodes.count(m_selectedNodeId)) {
        m_nodes[m_selectedNodeId].selected = false;
    }

    m_selectedNodeId = id;

    if (id >= 0 && m_nodes.count(id)) {
        m_nodes[id].selected = true;
    }
}

void NodeGraph::clearSelection() {
    if (m_selectedNodeId >= 0 && m_nodes.count(m_selectedNodeId)) {
        m_nodes[m_selectedNodeId].selected = false;
    }
    m_selectedNodeId = -1;
}

// -------------------------------------------------------------------------
// Zoom & Pan
// -------------------------------------------------------------------------

void NodeGraph::setZoom(float z) {
    m_zoom = std::clamp(z, MIN_ZOOM, MAX_ZOOM);
}

void NodeGraph::zoomToFit() {
    if (m_nodes.empty()) {
        m_zoom = 1.0f;
        m_pan = {0, 0};
        return;
    }

    // Find bounding box of all nodes
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& [id, node] : m_nodes) {
        minX = std::min(minX, node.gridPos.x);
        minY = std::min(minY, node.gridPos.y);
        maxX = std::max(maxX, node.gridPos.x + node.size.x);
        maxY = std::max(maxY, node.gridPos.y + node.size.y);
    }

    // Add generous padding for comfortable viewing
    float contentWidth = maxX - minX + 300;
    float contentHeight = maxY - minY + 200;

    // Calculate zoom to fit with extra margin (0.7x instead of 0.9x)
    float zoomX = m_width / contentWidth;
    float zoomY = m_height / contentHeight;
    m_zoom = std::clamp(std::min(zoomX, zoomY) * 0.7f, MIN_ZOOM, MAX_ZOOM);

    // Center content
    float centerX = (minX + maxX) * 0.5f;
    float centerY = (minY + maxY) * 0.5f;
    m_pan.x = m_width * 0.5f - centerX * m_zoom;
    m_pan.y = m_height * 0.5f - centerY * m_zoom;
}

// -------------------------------------------------------------------------
// Coordinate Transforms
// -------------------------------------------------------------------------

glm::vec2 NodeGraph::gridToScreen(glm::vec2 gridPos) const {
    return gridPos * m_zoom + m_pan;
}

glm::vec2 NodeGraph::screenToGrid(glm::vec2 screenPos) const {
    return (screenPos - m_pan) / m_zoom;
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

void NodeGraph::renderBackground() {
    m_canvas->fillRect(0, 0, m_width, m_height, m_style.backgroundColor);
}

void NodeGraph::renderGrid() {
    float gridSize = m_style.gridSpacing * m_zoom;
    if (gridSize < 5.0f) return;  // Don't draw grid if too dense

    // Offset for pan
    float offsetX = std::fmod(m_pan.x, gridSize);
    float offsetY = std::fmod(m_pan.y, gridSize);

    // Vertical lines
    for (float x = offsetX; x < m_width; x += gridSize) {
        m_canvas->line(x, 0, x, m_height, 1.0f, m_style.gridColor);
    }

    // Horizontal lines
    for (float y = offsetY; y < m_height; y += gridSize) {
        m_canvas->line(0, y, m_width, y, 1.0f, m_style.gridColor);
    }
}

void NodeGraph::renderLinks() {
    for (auto& [id, link] : m_links) {
        glm::vec2 start = getPinScreenPos(link.startPinId);
        glm::vec2 end = getPinScreenPos(link.endPinId);

        if (start.x == 0 && start.y == 0) continue;
        if (end.x == 0 && end.y == 0) continue;

        // Bezier control points
        float dx = std::abs(end.x - start.x) * 0.5f;
        float cx1 = start.x + dx;
        float cy1 = start.y;
        float cx2 = end.x - dx;
        float cy2 = end.y;

        glm::vec4 color = link.hovered ? m_style.linkHoveredColor : m_style.linkColor;
        m_canvas->bezierCurve(start.x, start.y, cx1, cy1, cx2, cy2, end.x, end.y,
                               m_style.linkWidth, color, 32);
    }
}

void NodeGraph::renderNodes() {
    // Render nodes (selected last so they're on top)
    std::vector<int> renderOrder;
    for (const auto& [id, node] : m_nodes) {
        if (!node.selected) {
            renderOrder.push_back(id);
        }
    }
    // Add selected nodes last
    for (const auto& [id, node] : m_nodes) {
        if (node.selected) {
            renderOrder.push_back(id);
        }
    }

    for (int id : renderOrder) {
        renderNode(m_nodes[id]);
    }
}

void NodeGraph::renderNode(NodeState& node) {
    glm::vec2 pos = gridToScreen(node.gridPos);
    float w = node.size.x * m_zoom;
    float h = node.size.y * m_zoom;

    float titleH = m_style.nodeTitleHeight * m_zoom;
    float cornerR = m_style.nodeCornerRadius * m_zoom;
    float pinR = m_style.pinRadius * m_zoom;

    // Text scales with zoom to maintain constant ratio to node size
    float textScale = m_zoom * 0.85f;  // Slightly smaller for better fit in title bar

    // Content area height (for operator preview)
    float contentAreaH = node.contentCallback ? 64.0f * m_zoom : 0.0f;

    // Node background
    m_canvas->fillRoundedRect(pos.x, pos.y, w, h, cornerR, m_style.nodeBackground);

    // Title bar
    m_canvas->fillRoundedRect(pos.x, pos.y, w, titleH, cornerR, m_style.nodeTitleBar);

    // Border
    glm::vec4 borderColor = m_style.nodeBorder;
    float borderWidth = m_style.nodeBorderWidth;
    if (node.selected) {
        borderColor = m_style.nodeSelectedBorder;
        borderWidth = m_style.selectionBorderWidth;
    } else if (node.hovered) {
        borderColor = m_style.nodeHoveredBorder;
    }
    m_canvas->strokeRoundedRect(pos.x, pos.y, w, h, cornerR, borderWidth, borderColor);

    // Title text (baseline positioned to vertically center in title bar)
    // Use font index 1 (Medium weight) for titles
    // For 40px font at textScale (m_zoom * 0.85), baseline should be at center + ascent/2
    if (!node.title.empty()) {
        float textX = pos.x + 12 * m_zoom;
        // Position baseline so text is vertically centered (ascent ~0.75 of font height)
        float scaledFontSize = 40.0f * textScale;  // 40px base font * scale
        float textY = pos.y + titleH * 0.5f + scaledFontSize * 0.35f;
        m_canvas->textScaled(node.title, textX, textY, m_style.textColor, textScale, 1);
    }

    // Content area (operator preview) - rendered between title and pins
    if (node.contentCallback) {
        float padding = m_style.nodeContentPadding * m_zoom;
        float contentX = pos.x + padding;
        float contentY = pos.y + titleH + padding * 0.5f;
        float contentW = w - padding * 2;
        float contentH = contentAreaH - padding;
        node.contentCallback(*m_canvas, contentX, contentY, contentW, contentH);
    }

    // Pins start after content area
    float pinStartY = pos.y + titleH + contentAreaH + m_style.nodeContentPadding * m_zoom;

    // Input pins
    for (size_t i = 0; i < node.inputs.size(); i++) {
        auto& pin = node.inputs[i];
        float pinY = pinStartY + i * m_style.pinSpacing * m_zoom + pinR;
        float pinX = pos.x;

        pin.screenPos = {pinX, pinY};

        glm::vec4 pinColor = (pin.hovered || m_hoveredPinId == pin.id)
                              ? m_style.pinHovered : m_style.pinInput;
        m_canvas->fillCircle(pinX, pinY, pinR, pinColor);

        // Pin label (vertically centered with pin)
        // For 36px font at textScale, position baseline at pin center + ~1/3 font height
        if (!pin.label.empty()) {
            float scaledFontSize = 36.0f * textScale;
            float labelY = pinY + scaledFontSize * 0.35f;
            m_canvas->textScaled(pin.label, pinX + pinR + 6 * m_zoom, labelY, m_style.textDimColor, textScale);
        }
    }

    // Output pins
    for (size_t i = 0; i < node.outputs.size(); i++) {
        auto& pin = node.outputs[i];
        float pinY = pinStartY + i * m_style.pinSpacing * m_zoom + pinR;
        float pinX = pos.x + w;

        pin.screenPos = {pinX, pinY};

        glm::vec4 pinColor = (pin.hovered || m_hoveredPinId == pin.id)
                              ? m_style.pinHovered : m_style.pinOutput;
        m_canvas->fillCircle(pinX, pinY, pinR, pinColor);

        // Pin label (right-aligned, vertically centered with pin)
        if (!pin.label.empty()) {
            float textW = m_canvas->measureTextScaled(pin.label, textScale);
            float scaledFontSize = 36.0f * textScale;
            float labelY = pinY + scaledFontSize * 0.35f;
            m_canvas->textScaled(pin.label, pinX - pinR - textW - 6 * m_zoom, labelY, m_style.textDimColor, textScale);
        }
    }
}

// -------------------------------------------------------------------------
// Hit Testing
// -------------------------------------------------------------------------

void NodeGraph::updateHover() {
    glm::vec2 mousePos = m_input.mousePos;

    // Check pins first (smaller targets)
    m_hoveredPinId = findPinAtPosition(mousePos);

    // Check nodes
    for (auto& [id, node] : m_nodes) {
        node.hovered = isPointInNode(mousePos, node);
        if (node.hovered) {
            m_hoveredNodeId = id;
        }
    }

    // Check links
    for (auto& [id, link] : m_links) {
        link.hovered = isPointNearLink(mousePos, link);
        if (link.hovered) {
            m_hoveredLinkId = id;
        }
    }
}

bool NodeGraph::isPointInNode(glm::vec2 screenPos, const NodeState& node) const {
    glm::vec2 nodePos = gridToScreen(node.gridPos);
    float w = node.size.x * m_zoom;
    float h = node.size.y * m_zoom;

    return screenPos.x >= nodePos.x && screenPos.x <= nodePos.x + w &&
           screenPos.y >= nodePos.y && screenPos.y <= nodePos.y + h;
}

bool NodeGraph::isPointNearLink(glm::vec2 screenPos, const LinkState& link) const {
    glm::vec2 start = getPinScreenPos(link.startPinId);
    glm::vec2 end = getPinScreenPos(link.endPinId);

    if (start.x == 0 && start.y == 0) return false;
    if (end.x == 0 && end.y == 0) return false;

    // Simple distance check to bezier curve (sample points along curve)
    float tolerance = 8.0f;  // Screen pixels
    for (int i = 0; i <= 16; i++) {
        float t = static_cast<float>(i) / 16.0f;
        float t2 = t * t;
        float t3 = t2 * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;

        float dx = std::abs(end.x - start.x) * 0.5f;
        float cx1 = start.x + dx;
        float cx2 = end.x - dx;

        float x = mt3 * start.x + 3 * mt2 * t * cx1 + 3 * mt * t2 * cx2 + t3 * end.x;
        float y = mt3 * start.y + 3 * mt2 * t * start.y + 3 * mt * t2 * end.y + t3 * end.y;

        float dist = glm::length(screenPos - glm::vec2(x, y));
        if (dist < tolerance) {
            return true;
        }
    }
    return false;
}

int NodeGraph::findPinAtPosition(glm::vec2 screenPos) const {
    float tolerance = m_style.pinRadius * m_zoom + 4.0f;

    for (const auto& [nodeId, node] : m_nodes) {
        for (const auto& pin : node.inputs) {
            float dist = glm::length(screenPos - pin.screenPos);
            if (dist < tolerance) {
                return pin.id;
            }
        }
        for (const auto& pin : node.outputs) {
            float dist = glm::length(screenPos - pin.screenPos);
            if (dist < tolerance) {
                return pin.id;
            }
        }
    }
    return -1;
}

glm::vec2 NodeGraph::getPinScreenPos(int pinId) const {
    auto nodeIt = m_pinToNode.find(pinId);
    if (nodeIt == m_pinToNode.end()) return {0, 0};

    auto it = m_nodes.find(nodeIt->second);
    if (it == m_nodes.end()) return {0, 0};

    const NodeState& node = it->second;

    for (const auto& pin : node.inputs) {
        if (pin.id == pinId) return pin.screenPos;
    }
    for (const auto& pin : node.outputs) {
        if (pin.id == pinId) return pin.screenPos;
    }

    return {0, 0};
}

// -------------------------------------------------------------------------
// Input Handling
// -------------------------------------------------------------------------

void NodeGraph::handleInput() {
    handleZoom();
    handlePan();
    handleNodeDrag();
    handleSelection();
}

void NodeGraph::handleZoom() {
    if (std::abs(m_input.scroll.y) > 0.01f) {
        float zoomDelta = m_input.scroll.y * 0.1f;
        float newZoom = std::clamp(m_zoom * (1.0f + zoomDelta), MIN_ZOOM, MAX_ZOOM);

        // Zoom toward mouse position
        glm::vec2 mouseGridPos = screenToGrid(m_input.mousePos);
        m_zoom = newZoom;
        glm::vec2 newScreenPos = gridToScreen(mouseGridPos);
        m_pan += m_input.mousePos - newScreenPos;
    }
}

void NodeGraph::handlePan() {
    // Pan with: left-click on empty space, OR middle mouse anywhere, OR Ctrl+left-click
    // (TouchDesigner style: click and drag empty space to pan)
    bool wantPan = m_input.mouseDown[2] ||  // Middle mouse always pans
                   (m_input.keyCtrl && m_input.mouseDown[0]);  // Ctrl+left also pans

    // Start panning on left-click in empty space (no node hovered)
    if (m_input.mouseClicked[0] && !m_input.keyCtrl && m_hoveredNodeId < 0 && m_hoveredPinId < 0) {
        m_isPanning = true;
        m_dragStartPos = m_input.mousePos;
    }

    // Also start panning for middle mouse or Ctrl+left
    if (wantPan && !m_isPanning && !m_isDraggingNode) {
        m_isPanning = true;
        m_dragStartPos = m_input.mousePos;
    }

    if (m_isPanning) {
        if (m_input.mouseDown[0] || m_input.mouseDown[2]) {
            m_pan += m_input.mouseDelta;
        } else {
            m_isPanning = false;
        }
    }
}

void NodeGraph::handleNodeDrag() {
    // Left click on node to drag (unless Ctrl is held for pan)
    if (m_input.mouseClicked[0] && !m_input.keyCtrl && m_hoveredNodeId >= 0) {
        m_isDraggingNode = true;
        m_selectedNodeId = m_hoveredNodeId;
        if (m_nodes.count(m_hoveredNodeId)) {
            m_nodes[m_hoveredNodeId].selected = true;
            m_nodes[m_hoveredNodeId].dragging = true;
            m_dragNodeStartGridPos = m_nodes[m_hoveredNodeId].gridPos;
            m_dragStartPos = m_input.mousePos;
        }
    }

    if (m_isDraggingNode && m_selectedNodeId >= 0) {
        if (m_input.mouseDown[0]) {
            glm::vec2 delta = (m_input.mousePos - m_dragStartPos) / m_zoom;
            m_nodes[m_selectedNodeId].gridPos = m_dragNodeStartGridPos + delta;
        } else {
            m_isDraggingNode = false;
            if (m_nodes.count(m_selectedNodeId)) {
                m_nodes[m_selectedNodeId].dragging = false;
            }
        }
    }
}

void NodeGraph::handleSelection() {
    // Click on empty space deselects - but NOT if we're panning
    // (panning starts on click in empty space, so we only deselect on release without drag)
    static bool wasPanning = false;
    if (m_input.mouseReleased[0] && !wasPanning && m_hoveredNodeId < 0 && !m_isDraggingNode) {
        // Only deselect if we didn't move much (wasn't a pan gesture)
        clearSelection();
    }
    wasPanning = m_isPanning;
}

} // namespace vivid
