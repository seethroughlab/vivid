#include <vivid/gui/node_graph.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/ui_style.h>
#include <algorithm>
#include <cmath>
#include <iostream>

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

    // Clear links and pin mappings - they get rebuilt each frame
    // This ensures stale data doesn't persist after hot reloads
    m_links.clear();
    m_pinToNode.clear();

    // Reset node hover/dragging state (but keep node positions)
    for (auto& [id, node] : m_nodes) {
        node.hovered = false;
    }
}

void NodeGraph::endEditor() {
    if (!m_inEditor || !m_canvas) return;

    // Compute pin screen positions before hover detection
    computePinPositions();

    // Update hover states FIRST so handleInput knows what's under the mouse
    updateHover();

    // Handle input (zoom, pan, selection, drag)
    handleInput();

    // Render everything
    // NOTE: Nodes must render BEFORE links because pin.screenPos is computed during node rendering
    // Only render background/grid when showGrid is true (allows see-through when disabled)
    if (m_style.showGrid) {
        renderBackground();
        renderGrid();
    }
    renderNodes();  // Computes pin screen positions
    renderLinks();  // Uses pin positions (renders on top of nodes)

    // Render mini-map last (on top of everything)
    if (m_style.showMiniMap) {
        renderMiniMap();
    }

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
            contentAreaHeight = 128.0f;
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
    lnk.dashed = false;
    lnk.customColor = {0, 0, 0, 0};
    m_links[id] = lnk;
}

void NodeGraph::linkDashed(int id, int startPinId, int endPinId, const glm::vec4& color) {
    LinkState lnk;
    lnk.id = id;
    lnk.startPinId = startPinId;
    lnk.endPinId = endPinId;
    lnk.dashed = true;
    lnk.customColor = color;
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

glm::vec2 NodeGraph::getNodeSize(int nodeId) const {
    auto it = m_nodes.find(nodeId);
    if (it != m_nodes.end()) {
        return it->second.size;
    }
    return {200, 100};  // Default size
}

void NodeGraph::autoLayout() {
    if (m_nodes.empty()) return;

    // =========================================================================
    // Sugiyama Hierarchical Layout Algorithm
    // =========================================================================
    //
    // 1. Build adjacency (which nodes connect to which)
    // 2. Assign layers (depth from sources)
    // 3. Reduce crossings (barycenter heuristic)
    // 4. Assign coordinates (position within layers)

    // Step 1: Build adjacency lists from links
    // For each link: startPin (output) -> endPin (input)
    // Direction: source node -> destination node
    std::unordered_map<int, std::vector<int>> outgoing;  // nodeId -> [connected nodeIds]
    std::unordered_map<int, std::vector<int>> incoming;  // nodeId -> [source nodeIds]
    std::unordered_map<int, int> inDegree;
    // Also track destination pin IDs for input slot ordering
    std::unordered_map<int, std::vector<std::pair<int, int>>> outgoingWithPin;  // srcNode -> [(dstNode, endPinId)]

    // Initialize all nodes
    for (const auto& [id, node] : m_nodes) {
        outgoing[id] = {};
        incoming[id] = {};
        inDegree[id] = 0;
        outgoingWithPin[id] = {};
    }

    // Build adjacency from links
    for (const auto& [linkId, link] : m_links) {
        // Find nodes for each pin
        auto startIt = m_pinToNode.find(link.startPinId);
        auto endIt = m_pinToNode.find(link.endPinId);
        if (startIt == m_pinToNode.end() || endIt == m_pinToNode.end()) continue;

        int srcNode = startIt->second;
        int dstNode = endIt->second;
        if (srcNode == dstNode) continue;  // Skip self-loops

        outgoing[srcNode].push_back(dstNode);
        incoming[dstNode].push_back(srcNode);
        inDegree[dstNode]++;
        outgoingWithPin[srcNode].push_back({dstNode, link.endPinId});
    }

    // Step 2: Layer assignment using longest path from sources
    // Find sources (nodes with no incoming edges)
    std::vector<int> sources;
    for (const auto& [id, degree] : inDegree) {
        if (degree == 0) {
            sources.push_back(id);
        }
    }

    // If no sources found (cycle?), pick arbitrary starting nodes
    if (sources.empty()) {
        for (const auto& [id, node] : m_nodes) {
            sources.push_back(id);
            break;
        }
    }

    // BFS to assign layers (longest path gives better spread)
    std::unordered_map<int, int> nodeLayer;
    for (const auto& [id, node] : m_nodes) {
        nodeLayer[id] = -1;  // Unassigned
    }

    // Use longest path: each node's layer = max(predecessor layers) + 1
    // Process in topological order
    std::vector<int> topoOrder;
    std::unordered_map<int, int> tempInDegree = inDegree;
    std::vector<int> queue = sources;

    while (!queue.empty()) {
        int curr = queue.back();
        queue.pop_back();
        topoOrder.push_back(curr);

        for (int next : outgoing[curr]) {
            tempInDegree[next]--;
            if (tempInDegree[next] == 0) {
                queue.push_back(next);
            }
        }
    }

    // Handle nodes not in topological order (cycles)
    for (const auto& [id, node] : m_nodes) {
        bool found = false;
        for (int n : topoOrder) {
            if (n == id) { found = true; break; }
        }
        if (!found) {
            topoOrder.push_back(id);
        }
    }

    // Assign layers based on longest incoming path
    for (int id : topoOrder) {
        int maxPredLayer = -1;
        for (int pred : incoming[id]) {
            if (nodeLayer[pred] > maxPredLayer) {
                maxPredLayer = nodeLayer[pred];
            }
        }
        nodeLayer[id] = maxPredLayer + 1;
    }

    // Group nodes by layer
    int maxLayer = 0;
    for (const auto& [id, layer] : nodeLayer) {
        maxLayer = std::max(maxLayer, layer);
    }

    std::vector<std::vector<int>> layers(maxLayer + 1);
    for (const auto& [id, layer] : nodeLayer) {
        if (layer >= 0) {
            layers[layer].push_back(id);
        }
    }

    // Step 3: Crossing reduction using barycenter heuristic
    // For each layer (except first), sort nodes by average position of predecessors
    for (int iter = 0; iter < 4; iter++) {  // Multiple passes improve quality
        // Forward pass
        for (size_t layerIdx = 1; layerIdx < layers.size(); layerIdx++) {
            std::vector<std::pair<float, int>> positions;
            for (int nodeId : layers[layerIdx]) {
                float sum = 0;
                int count = 0;
                // Find position of predecessors in previous layer
                for (int pred : incoming[nodeId]) {
                    auto& prevLayer = layers[layerIdx - 1];
                    for (size_t i = 0; i < prevLayer.size(); i++) {
                        if (prevLayer[i] == pred) {
                            sum += static_cast<float>(i);
                            count++;
                            break;
                        }
                    }
                }
                float barycenter = (count > 0) ? sum / count : static_cast<float>(positions.size());
                positions.push_back({barycenter, nodeId});
            }
            // Sort by barycenter
            std::sort(positions.begin(), positions.end());
            layers[layerIdx].clear();
            for (const auto& [pos, id] : positions) {
                layers[layerIdx].push_back(id);
            }
        }

        // Backward pass - look at ALL successors across all later layers
        // Weight by layer distance and use input pin slot for sub-ordering
        for (int layerIdx = static_cast<int>(layers.size()) - 2; layerIdx >= 0; layerIdx--) {
            std::vector<std::pair<float, int>> positions;
            for (int nodeId : layers[layerIdx]) {
                float sum = 0;
                float totalWeight = 0;
                // Find position of successors in ANY later layer (not just adjacent)
                // Use pin information to order by input slot when connecting to same node
                for (const auto& [succ, endPinId] : outgoingWithPin[nodeId]) {
                    int succLayer = nodeLayer[succ];
                    if (succLayer <= layerIdx) continue;  // Only look forward

                    auto& targetLayer = layers[succLayer];
                    for (size_t i = 0; i < targetLayer.size(); i++) {
                        if (targetLayer[i] == succ) {
                            // Weight by inverse layer distance (closer = more weight)
                            float weight = 1.0f / static_cast<float>(succLayer - layerIdx);
                            // Use input pin slot to sub-order within the same destination node
                            // Pin ID format: nodeId * 100 + slot (slot 1-49 for inputs, 50 for values)
                            int pinSlot = endPinId % 100;
                            // Add small offset based on pin slot (0.01 per slot)
                            // This ensures nodes connecting to earlier slots are positioned higher
                            float slotOffset = static_cast<float>(pinSlot) * 0.01f;
                            sum += (static_cast<float>(i) + slotOffset) * weight;
                            totalWeight += weight;
                            break;
                        }
                    }
                }
                float barycenter = (totalWeight > 0) ? sum / totalWeight : static_cast<float>(positions.size());
                positions.push_back({barycenter, nodeId});
            }
            std::sort(positions.begin(), positions.end());
            layers[layerIdx].clear();
            for (const auto& [pos, id] : positions) {
                layers[layerIdx].push_back(id);
            }
        }
    }

    // Step 4: Assign coordinates
    const float xSpacing = 280.0f;   // Horizontal space between layers
    const float yPadding = 30.0f;    // Vertical padding between nodes
    const float startX = 50.0f;
    const float startY = 50.0f;

    for (size_t layerIdx = 0; layerIdx < layers.size(); layerIdx++) {
        float x = startX + layerIdx * xSpacing;
        float y = startY;

        for (int nodeId : layers[layerIdx]) {
            if (m_nodes.count(nodeId)) {
                m_nodes[nodeId].gridPos = {x, y};
                y += m_nodes[nodeId].size.y + yPadding;
            }
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
    m_zoom = std::clamp(z, m_style.minZoom, m_style.maxZoom);
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
    m_zoom = std::clamp(std::min(zoomX, zoomY) * 0.7f, m_style.minZoom, m_style.maxZoom);

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
    // Background renders on Background layer
    m_canvas->setLayer(UILayer::Background);
    m_canvas->fillRect(0, 0, m_width, m_height, m_style.backgroundColor);
}

void NodeGraph::renderGrid() {
    float gridSize = m_style.gridSpacing * m_zoom;
    if (gridSize < 5.0f) return;  // Don't draw grid if too dense

    // Grid renders on Background layer
    m_canvas->setLayer(UILayer::Background);

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
    // Links render on Nodes layer
    m_canvas->setLayer(UILayer::Nodes);
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

        // Determine link color
        glm::vec4 color;
        if (link.hovered) {
            color = m_style.linkHoveredColor;
        } else if (link.customColor.a > 0) {
            color = link.customColor;
        } else {
            color = m_style.linkColor;
        }

        if (link.dashed) {
            // Draw dashed bezier: sample points and draw alternating segments
            // Scale dash pattern with zoom, but enforce minimum size for visibility
            float dashLength = std::max(4.0f, 8.0f * m_zoom);   // At least 4 pixels
            float gapLength = std::max(3.0f, 6.0f * m_zoom);    // At least 3 pixels

            // Estimate curve length using control polygon (more accurate than chord)
            // Control polygon: start -> cp1 -> cp2 -> end
            glm::vec2 cp1 = {cx1, cy1};
            glm::vec2 cp2 = {cx2, cy2};
            float polyLength = glm::length(cp1 - start) + glm::length(cp2 - cp1) + glm::length(end - cp2);
            // Sample at least every 3 pixels for smooth dashes (smaller than gap)
            int numSamples = std::max(16, static_cast<int>(polyLength / 3.0f));
            numSamples = std::min(numSamples, 512);  // Cap for performance

            // Sample points along the bezier curve
            std::vector<glm::vec2> points(numSamples + 1);
            for (int i = 0; i <= numSamples; ++i) {
                float t = static_cast<float>(i) / numSamples;
                float t2 = t * t;
                float t3 = t2 * t;
                float mt = 1.0f - t;
                float mt2 = mt * mt;
                float mt3 = mt2 * mt;

                float x = mt3 * start.x + 3 * mt2 * t * cx1 + 3 * mt * t2 * cx2 + t3 * end.x;
                float y = mt3 * start.y + 3 * mt2 * t * cy1 + 3 * mt * t2 * cy2 + t3 * end.y;
                points[i] = {x, y};
            }

            // Calculate cumulative arc length for proper dash spacing
            std::vector<float> arcLengths(numSamples + 1);
            arcLengths[0] = 0.0f;
            for (int i = 1; i <= numSamples; ++i) {
                arcLengths[i] = arcLengths[i - 1] + glm::length(points[i] - points[i - 1]);
            }
            float totalLength = arcLengths[numSamples];

            // Draw dashes
            float dashCycle = dashLength + gapLength;
            float pos = 0.0f;
            int segmentStart = 0;

            while (pos < totalLength) {
                // Start of dash
                float dashStart = pos;
                float dashEnd = std::min(pos + dashLength, totalLength);

                // Find segment indices for dash start/end
                int i1 = 0, i2 = 0;
                for (int i = segmentStart; i <= numSamples; ++i) {
                    if (arcLengths[i] >= dashStart) {
                        i1 = std::max(0, i - 1);
                        break;
                    }
                }
                for (int i = i1; i <= numSamples; ++i) {
                    if (arcLengths[i] >= dashEnd) {
                        i2 = i;
                        break;
                    }
                    i2 = i;
                }

                // Draw dash as connected line segments
                for (int i = i1; i < i2; ++i) {
                    m_canvas->line(points[i].x, points[i].y,
                                  points[i + 1].x, points[i + 1].y,
                                  m_style.linkWidth, color);
                }

                segmentStart = i2;
                pos += dashCycle;
            }
        } else {
            // Solid bezier curve
            m_canvas->bezierCurve(start.x, start.y, cx1, cy1, cx2, cy2, end.x, end.y,
                                   m_style.linkWidth, color, 32);
        }
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

void NodeGraph::computePinPositions() {
    // Compute screen positions for all pins (for hover detection)
    // Must be called before updateHover() and uses same math as renderNode()
    for (auto& [id, node] : m_nodes) {
        glm::vec2 pos = gridToScreen(node.gridPos);
        float w = node.size.x * m_zoom;
        float titleH = m_style.nodeTitleHeight * m_zoom;
        float pinR = m_style.pinRadius * m_zoom;
        float contentAreaH = node.contentCallback ? 128.0f * m_zoom : 0.0f;
        float pinStartY = pos.y + titleH + contentAreaH + m_style.nodeContentPadding * m_zoom;

        for (size_t i = 0; i < node.inputs.size(); i++) {
            float pinY = pinStartY + i * m_style.pinSpacing * m_zoom + pinR;
            float pinX = pos.x;
            node.inputs[i].screenPos = {pinX, pinY};
        }

        for (size_t i = 0; i < node.outputs.size(); i++) {
            float pinY = pinStartY + i * m_style.pinSpacing * m_zoom + pinR;
            float pinX = pos.x + w;
            node.outputs[i].screenPos = {pinX, pinY};
        }
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
    float contentAreaH = node.contentCallback ? 128.0f * m_zoom : 0.0f;

    // Render node structure on Nodes layer
    m_canvas->setLayer(UILayer::Nodes);

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
    if (!node.title.empty()) {
        float textX = pos.x + 12 * m_zoom;
        // Position baseline so text is vertically centered using proper font metrics
        float ascent = m_canvas->fontAscent(1) * textScale;
        float descent = std::abs(m_canvas->fontDescent(1)) * textScale;
        float textY = pos.y + titleH * 0.5f + (ascent - descent) * 0.5f;
        m_canvas->textScaled(node.title, textX, textY, m_style.textColor, textScale, 1);
    }

    // Content area (operator preview) - rendered between title and pins
    // Use NodeContent layer so thumbnails render above node boxes but below panels
    if (node.contentCallback) {
        m_canvas->setLayer(UILayer::NodeContent);
        float padding = m_style.nodeContentPadding * m_zoom;
        float contentX = pos.x + padding;
        float contentY = pos.y + titleH + padding * 0.5f;
        float contentW = w - padding * 2;
        float contentH = contentAreaH - padding;
        // Set content scale so VizDrawList can scale text appropriately
        m_canvas->setContentScale(m_zoom);
        node.contentCallback(*m_canvas, contentX, contentY, contentW, contentH);
        m_canvas->setContentScale(1.0f);  // Reset after callback
        m_canvas->setLayer(UILayer::Nodes);  // Switch back for pins
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
        if (!pin.label.empty()) {
            float pinAscent = m_canvas->fontAscent(0) * textScale;
            float pinDescent = std::abs(m_canvas->fontDescent(0)) * textScale;
            float labelY = pinY + (pinAscent - pinDescent) * 0.5f;
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
            float outAscent = m_canvas->fontAscent(0) * textScale;
            float outDescent = std::abs(m_canvas->fontDescent(0)) * textScale;
            float labelY = pinY + (outAscent - outDescent) * 0.5f;
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

    // Check if hovering an output pin and invoke callback
    if (m_hoveredPinId >= 0 && m_outputPinHoverCallback) {
        // Find which node this pin belongs to and if it's an output
        auto nodeIt = m_pinToNode.find(m_hoveredPinId);
        if (nodeIt != m_pinToNode.end()) {
            int nodeId = nodeIt->second;
            auto it = m_nodes.find(nodeId);
            if (it != m_nodes.end()) {
                const NodeState& node = it->second;
                // Check if it's an output pin
                for (size_t i = 0; i < node.outputs.size(); ++i) {
                    if (node.outputs[i].id == m_hoveredPinId) {
                        m_outputPinHoverCallback(nodeId, static_cast<int>(i));
                        break;
                    }
                }
            }
        }
    } else if (m_outputPinHoverCallback) {
        // Not hovering any output pin
        m_outputPinHoverCallback(-1, -1);
    }

    // Check nodes
    int hoveredCount = 0;
    for (auto& [id, node] : m_nodes) {
        node.hovered = isPointInNode(mousePos, node);
        if (node.hovered) {
            hoveredCount++;
            if (m_hoveredNodeId < 0) {  // Only set first hovered node
                m_hoveredNodeId = id;
            }
        }
    }
    (void)hoveredCount;  // Silence unused warning

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
    // Reset consumed flag at start of frame
    m_consumedInput = false;

    // If input is blocked by a higher-z panel, don't process any input
    if (m_input.inputBlocked) {
        return;
    }

    // Handle mini-map input first (takes priority)
    handleMiniMapInput();

    // Skip other input handling if dragging in mini-map
    if (m_isDraggingMiniMap) {
        m_consumedInput = true;
        return;
    }

    handleKeyboard();
    handleZoom();
    handlePan();
    handleNodeDrag();
    handleSelection();

    // Mark input as consumed if we're doing any interaction
    if (m_isPanning || m_isDraggingNode || m_hoveredNodeId >= 0 ||
        std::abs(m_input.scroll.y) > 0.01f) {
        m_consumedInput = true;
    }
}

void NodeGraph::handleZoom() {
    if (std::abs(m_input.scroll.y) > 0.01f) {
        float zoomDelta = m_input.scroll.y * 0.1f;
        float newZoom = std::clamp(m_zoom * (1.0f + zoomDelta), m_style.minZoom, m_style.maxZoom);

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
        // Check for double-click
        float timeSinceLastClick = m_input.time - m_lastClickTime;
        if (m_lastClickedNodeId == m_hoveredNodeId && timeSinceLastClick < DOUBLE_CLICK_TIME) {
            // Double-click detected!
            if (m_doubleClickCallback) {
                m_doubleClickCallback(m_hoveredNodeId);
            }
            // Reset double-click state
            m_lastClickedNodeId = -1;
            m_lastClickTime = 0.0f;
            return;  // Don't start a drag on double-click
        }

        // Record this click for double-click detection
        m_lastClickTime = m_input.time;
        m_lastClickedNodeId = m_hoveredNodeId;

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

void NodeGraph::handleKeyboard() {
    // F: Fit all nodes in view
    if (m_input.keyF) {
        zoomToFit();
    }

    // 1: Zoom to 100%
    if (m_input.key1) {
        // Center on selected node if any, otherwise center on content
        if (m_selectedNodeId >= 0 && m_nodes.count(m_selectedNodeId)) {
            const auto& node = m_nodes[m_selectedNodeId];
            glm::vec2 nodeCenter = node.gridPos + node.size * 0.5f;
            m_zoom = 1.0f;
            m_pan.x = m_width * 0.5f - nodeCenter.x * m_zoom;
            m_pan.y = m_height * 0.5f - nodeCenter.y * m_zoom;
        } else {
            // Center on content center
            float minX, minY, maxX, maxY;
            getContentBounds(minX, minY, maxX, maxY);
            glm::vec2 center = {(minX + maxX) * 0.5f, (minY + maxY) * 0.5f};
            m_zoom = 1.0f;
            m_pan.x = m_width * 0.5f - center.x * m_zoom;
            m_pan.y = m_height * 0.5f - center.y * m_zoom;
        }
    }

    // Arrow keys: Navigate selection
    if (m_input.keyUp || m_input.keyDown || m_input.keyLeft || m_input.keyRight) {
        if (m_nodes.empty()) return;

        // If nothing selected, select first node
        if (m_selectedNodeId < 0) {
            auto it = m_nodes.begin();
            selectNode(it->first);
            return;
        }

        // Find nearest node in direction
        if (!m_nodes.count(m_selectedNodeId)) return;
        const auto& current = m_nodes[m_selectedNodeId];
        glm::vec2 currentCenter = current.gridPos + current.size * 0.5f;

        int bestNode = -1;
        float bestScore = 1e9f;

        for (const auto& [id, node] : m_nodes) {
            if (id == m_selectedNodeId) continue;

            glm::vec2 nodeCenter = node.gridPos + node.size * 0.5f;
            glm::vec2 delta = nodeCenter - currentCenter;

            // Direction filter
            bool validDirection = false;
            if (m_input.keyUp && delta.y < -20) validDirection = true;
            if (m_input.keyDown && delta.y > 20) validDirection = true;
            if (m_input.keyLeft && delta.x < -20) validDirection = true;
            if (m_input.keyRight && delta.x > 20) validDirection = true;

            if (!validDirection) continue;

            // Score: prefer nodes in the direction, penalize perpendicular distance
            float primaryDist = 0, perpDist = 0;
            if (m_input.keyUp || m_input.keyDown) {
                primaryDist = std::abs(delta.y);
                perpDist = std::abs(delta.x);
            } else {
                primaryDist = std::abs(delta.x);
                perpDist = std::abs(delta.y);
            }

            float score = primaryDist + perpDist * 2.0f;  // Penalize perpendicular distance
            if (score < bestScore) {
                bestScore = score;
                bestNode = id;
            }
        }

        if (bestNode >= 0) {
            selectNode(bestNode);

            // Pan to keep selected node visible
            const auto& selectedNode = m_nodes[bestNode];
            glm::vec2 screenPos = gridToScreen(selectedNode.gridPos);
            glm::vec2 screenEnd = gridToScreen(selectedNode.gridPos + selectedNode.size);

            float margin = 50.0f;
            if (screenPos.x < margin) {
                m_pan.x += margin - screenPos.x;
            } else if (screenEnd.x > m_width - margin) {
                m_pan.x -= screenEnd.x - (m_width - margin);
            }
            if (screenPos.y < margin) {
                m_pan.y += margin - screenPos.y;
            } else if (screenEnd.y > m_height - margin) {
                m_pan.y -= screenEnd.y - (m_height - margin);
            }
        }
    }

    // Enter: Solo mode callback
    if (m_input.keyEnter && m_enterCallback) {
        m_enterCallback(m_selectedNodeId);
    }

    // B: Bypass callback
    if (m_input.keyB && m_bypassCallback) {
        m_bypassCallback(m_selectedNodeId);
    }

    // Escape: Exit/deselect callback
    if (m_input.keyEscape) {
        if (m_escapeCallback) {
            m_escapeCallback();
        }
        clearSelection();
    }
}

// -------------------------------------------------------------------------
// Mini-map
// -------------------------------------------------------------------------

void NodeGraph::getContentBounds(float& minX, float& minY, float& maxX, float& maxY) const {
    if (m_nodes.empty()) {
        minX = minY = 0;
        maxX = maxY = 100;
        return;
    }

    minX = minY = 1e9f;
    maxX = maxY = -1e9f;

    for (const auto& [id, node] : m_nodes) {
        minX = std::min(minX, node.gridPos.x);
        minY = std::min(minY, node.gridPos.y);
        maxX = std::max(maxX, node.gridPos.x + node.size.x);
        maxY = std::max(maxY, node.gridPos.y + node.size.y);
    }

    // Add padding
    float padX = (maxX - minX) * 0.1f + 50.0f;
    float padY = (maxY - minY) * 0.1f + 50.0f;
    minX -= padX;
    minY -= padY;
    maxX += padX;
    maxY += padY;
}

bool NodeGraph::isPointInMiniMap(glm::vec2 screenPos) const {
    // Mini-map is in bottom-right corner
    float mmX = m_width - m_style.miniMapWidth - m_style.miniMapMargin;
    float mmY = m_height - m_style.miniMapHeight - m_style.miniMapMargin;

    return screenPos.x >= mmX && screenPos.x <= mmX + m_style.miniMapWidth &&
           screenPos.y >= mmY && screenPos.y <= mmY + m_style.miniMapHeight;
}

void NodeGraph::handleMiniMapInput() {
    if (!m_style.showMiniMap) return;

    // Check if clicking in mini-map
    if (m_input.mouseClicked[0] && isPointInMiniMap(m_input.mousePos)) {
        m_isDraggingMiniMap = true;
    }

    if (m_isDraggingMiniMap) {
        if (m_input.mouseDown[0]) {
            // Get mini-map screen position
            float mmX = m_width - m_style.miniMapWidth - m_style.miniMapMargin;
            float mmY = m_height - m_style.miniMapHeight - m_style.miniMapMargin;

            // Get content bounds
            float contentMinX, contentMinY, contentMaxX, contentMaxY;
            getContentBounds(contentMinX, contentMinY, contentMaxX, contentMaxY);

            float contentW = contentMaxX - contentMinX;
            float contentH = contentMaxY - contentMinY;

            // Scale to fit mini-map while preserving aspect ratio
            float scaleX = m_style.miniMapWidth / contentW;
            float scaleY = m_style.miniMapHeight / contentH;
            float scale = std::min(scaleX, scaleY);

            // Calculate offset to center content in mini-map
            float scaledW = contentW * scale;
            float scaledH = contentH * scale;
            float offsetX = (m_style.miniMapWidth - scaledW) * 0.5f;
            float offsetY = (m_style.miniMapHeight - scaledH) * 0.5f;

            // Convert mouse position in mini-map to grid position
            float relX = (m_input.mousePos.x - mmX - offsetX) / scale + contentMinX;
            float relY = (m_input.mousePos.y - mmY - offsetY) / scale + contentMinY;

            // Set pan so this grid position is at center of screen
            m_pan.x = m_width * 0.5f - relX * m_zoom;
            m_pan.y = m_height * 0.5f - relY * m_zoom;
        } else {
            m_isDraggingMiniMap = false;
        }
    }
}

void NodeGraph::renderMiniMap() {
    if (m_nodes.empty()) return;

    // Mini-map renders on Panels layer (above nodes)
    m_canvas->setLayer(UILayer::Panels);

    // Mini-map position (bottom-right corner)
    float mmX = m_width - m_style.miniMapWidth - m_style.miniMapMargin;
    float mmY = m_height - m_style.miniMapHeight - m_style.miniMapMargin;
    float mmW = m_style.miniMapWidth;
    float mmH = m_style.miniMapHeight;

    // Background
    m_canvas->fillRoundedRect(mmX, mmY, mmW, mmH, 4.0f, m_style.miniMapBackground);
    m_canvas->strokeRoundedRect(mmX, mmY, mmW, mmH, 4.0f, 1.0f, m_style.miniMapBorder);

    // Get content bounds
    float contentMinX, contentMinY, contentMaxX, contentMaxY;
    getContentBounds(contentMinX, contentMinY, contentMaxX, contentMaxY);

    float contentW = contentMaxX - contentMinX;
    float contentH = contentMaxY - contentMinY;

    // Scale to fit mini-map while preserving aspect ratio
    float scaleX = mmW / contentW;
    float scaleY = mmH / contentH;
    float scale = std::min(scaleX, scaleY);

    // Calculate offset to center content in mini-map
    float scaledW = contentW * scale;
    float scaledH = contentH * scale;
    float offsetX = (mmW - scaledW) * 0.5f;
    float offsetY = (mmH - scaledH) * 0.5f;

    // Lambda to convert grid pos to mini-map screen pos
    auto gridToMiniMap = [&](glm::vec2 gridPos) -> glm::vec2 {
        float x = mmX + offsetX + (gridPos.x - contentMinX) * scale;
        float y = mmY + offsetY + (gridPos.y - contentMinY) * scale;
        return {x, y};
    };

    // Draw links (simplified as lines)
    for (const auto& [id, link] : m_links) {
        // Find source and dest nodes
        auto srcNodeIt = m_pinToNode.find(link.startPinId);
        auto dstNodeIt = m_pinToNode.find(link.endPinId);
        if (srcNodeIt == m_pinToNode.end() || dstNodeIt == m_pinToNode.end()) continue;

        auto srcIt = m_nodes.find(srcNodeIt->second);
        auto dstIt = m_nodes.find(dstNodeIt->second);
        if (srcIt == m_nodes.end() || dstIt == m_nodes.end()) continue;

        const NodeState& srcNode = srcIt->second;
        const NodeState& dstNode = dstIt->second;

        glm::vec2 srcCenter = srcNode.gridPos + srcNode.size * 0.5f;
        glm::vec2 dstCenter = dstNode.gridPos + dstNode.size * 0.5f;

        glm::vec2 srcMM = gridToMiniMap(srcCenter);
        glm::vec2 dstMM = gridToMiniMap(dstCenter);

        m_canvas->line(srcMM.x, srcMM.y, dstMM.x, dstMM.y, 1.0f, m_style.linkColor * glm::vec4(1, 1, 1, 0.5f));
    }

    // Draw nodes as small rectangles
    for (const auto& [id, node] : m_nodes) {
        glm::vec2 topLeft = gridToMiniMap(node.gridPos);
        float nodeW = node.size.x * scale;
        float nodeH = node.size.y * scale;

        // Minimum size for visibility
        nodeW = std::max(nodeW, 4.0f);
        nodeH = std::max(nodeH, 3.0f);

        glm::vec4 nodeColor = m_style.miniMapNodeColor;
        if (node.selected) {
            nodeColor = m_style.nodeSelectedBorder;
        }

        m_canvas->fillRect(topLeft.x, topLeft.y, nodeW, nodeH, nodeColor);
    }

    // Draw viewport rectangle
    // Current viewport in grid space:
    // top-left: screenToGrid({0, 0})
    // bottom-right: screenToGrid({m_width, m_height})
    glm::vec2 vpTopLeft = screenToGrid({0, 0});
    glm::vec2 vpBottomRight = screenToGrid({m_width, m_height});

    glm::vec2 vpMM1 = gridToMiniMap(vpTopLeft);
    glm::vec2 vpMM2 = gridToMiniMap(vpBottomRight);

    float vpW = vpMM2.x - vpMM1.x;
    float vpH = vpMM2.y - vpMM1.y;

    // Clamp viewport rect to mini-map bounds
    float clampedX = std::max(mmX, std::min(vpMM1.x, mmX + mmW));
    float clampedY = std::max(mmY, std::min(vpMM1.y, mmY + mmH));
    float clampedW = std::min(vpW, mmX + mmW - clampedX);
    float clampedH = std::min(vpH, mmY + mmH - clampedY);

    if (clampedW > 0 && clampedH > 0) {
        // Semi-transparent fill
        m_canvas->fillRect(clampedX, clampedY, clampedW, clampedH,
                           m_style.miniMapViewportColor * glm::vec4(1, 1, 1, 0.3f));
        // Border
        m_canvas->strokeRect(clampedX, clampedY, clampedW, clampedH, 1.5f,
                             m_style.miniMapViewportColor);
    }
}

} // namespace vivid
