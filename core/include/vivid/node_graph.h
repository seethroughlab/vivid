#pragma once

/**
 * @file node_graph.h
 * @brief Custom node graph visualization with zoom/pan support
 *
 * Replaces imnodes with a purpose-built solution that supports:
 * - Zoom in/out (scroll wheel, pivot around mouse)
 * - Pan (Ctrl+drag or middle mouse)
 * - Hierarchical auto-layout
 * - Draggable nodes
 *
 * Renders using OverlayCanvas for direct screen drawing.
 */

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace vivid {

class OverlayCanvas;

// -------------------------------------------------------------------------
// Style Configuration
// -------------------------------------------------------------------------

/**
 * @brief Visual style configuration for node graph
 */
struct NodeGraphStyle {
    // Colors
    glm::vec4 backgroundColor = {0.1f, 0.1f, 0.1f, 0.9f};
    glm::vec4 gridColor = {0.2f, 0.2f, 0.2f, 0.5f};

    glm::vec4 nodeBackground = {0.2f, 0.2f, 0.25f, 1.0f};
    glm::vec4 nodeTitleBar = {0.3f, 0.3f, 0.4f, 1.0f};
    glm::vec4 nodeBorder = {0.4f, 0.4f, 0.5f, 1.0f};
    glm::vec4 nodeSelectedBorder = {0.8f, 0.6f, 0.2f, 1.0f};
    glm::vec4 nodeHoveredBorder = {0.6f, 0.6f, 0.7f, 1.0f};

    glm::vec4 pinInput = {0.3f, 0.6f, 0.3f, 1.0f};
    glm::vec4 pinOutput = {0.6f, 0.3f, 0.3f, 1.0f};
    glm::vec4 pinHovered = {0.8f, 0.8f, 0.3f, 1.0f};

    glm::vec4 linkColor = {0.6f, 0.6f, 0.6f, 0.8f};
    glm::vec4 linkHoveredColor = {0.8f, 0.8f, 0.3f, 1.0f};

    glm::vec4 textColor = {0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 textDimColor = {0.6f, 0.6f, 0.6f, 1.0f};

    // Sizes (in grid units, scale with zoom)
    float nodeWidth = 200.0f;
    float nodeTitleHeight = 48.0f;  // Larger for 40px title font
    float nodeContentPadding = 8.0f;
    float nodeCornerRadius = 8.0f;
    float pinRadius = 8.0f;
    float pinSpacing = 40.0f;  // Larger for 36px label font

    // Sizes (in screen pixels, don't scale with zoom)
    float nodeBorderWidth = 1.0f;
    float linkWidth = 2.0f;
    float selectionBorderWidth = 2.0f;

    // Grid
    float gridSpacing = 20.0f;
    bool showGrid = true;
};

// -------------------------------------------------------------------------
// Data Structures
// -------------------------------------------------------------------------

/**
 * @brief Pin (attribute) state
 */
struct PinState {
    int id = 0;
    std::string label;
    glm::vec2 screenPos = {0, 0};  // Computed during render
    bool hovered = false;
};

/**
 * @brief Node state
 */
struct NodeState {
    int id = 0;
    std::string title;
    glm::vec2 gridPos = {0, 0};         // Position in grid space
    glm::vec2 size = {180, 100};        // Computed size after content
    std::vector<PinState> inputs;
    std::vector<PinState> outputs;
    bool selected = false;
    bool hovered = false;
    bool dragging = false;

    // Content callback (called during render to draw custom content)
    std::function<void(OverlayCanvas&, float x, float y, float w, float h)> contentCallback;
};

/**
 * @brief Link between pins
 */
struct LinkState {
    int id = 0;
    int startPinId = 0;  // Output pin
    int endPinId = 0;    // Input pin
    bool hovered = false;
};

// -------------------------------------------------------------------------
// Input State
// -------------------------------------------------------------------------

/**
 * @brief Input state for the node graph
 */
struct NodeGraphInput {
    glm::vec2 mousePos = {0, 0};      // Screen position
    glm::vec2 mouseDelta = {0, 0};    // Movement since last frame
    glm::vec2 scroll = {0, 0};        // Scroll wheel
    bool mouseDown[3] = {false, false, false};
    bool mouseClicked[3] = {false, false, false};
    bool mouseReleased[3] = {false, false, false};
    bool keyCtrl = false;
    bool keyShift = false;
    bool keyAlt = false;
};

// -------------------------------------------------------------------------
// NodeGraph Class
// -------------------------------------------------------------------------

/**
 * @brief Node graph editor with zoom/pan support
 *
 * Usage:
 * @code
 * NodeGraph graph;
 *
 * // Each frame
 * graph.beginEditor(canvas, width, height, input);
 *
 * graph.beginNode(nodeId);
 * graph.setNodeTitle("My Node");
 * graph.beginInputAttribute(pinId);
 * graph.pinLabel("Input");
 * graph.endInputAttribute();
 * graph.endNode();
 *
 * graph.link(linkId, outputPinId, inputPinId);
 *
 * graph.endEditor();
 * @endcode
 */
class NodeGraph {
public:
    NodeGraph() = default;
    ~NodeGraph() = default;

    // -------------------------------------------------------------------------
    /// @name Frame Lifecycle
    /// @{

    /**
     * @brief Begin a new editor frame
     * @param canvas OverlayCanvas to draw to
     * @param width Editor width in pixels
     * @param height Editor height in pixels
     * @param input Input state for this frame
     */
    void beginEditor(OverlayCanvas& canvas, float width, float height, const NodeGraphInput& input);

    /**
     * @brief End the editor frame (renders everything)
     */
    void endEditor();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Node API
    /// @{

    /**
     * @brief Begin defining a node
     * @param id Unique node ID
     */
    void beginNode(int id);

    /**
     * @brief Set the node title (call after beginNode)
     */
    void setNodeTitle(const std::string& title);

    /**
     * @brief Set custom content callback
     */
    void setNodeContent(std::function<void(OverlayCanvas&, float x, float y, float w, float h)> callback);

    /**
     * @brief End node definition
     */
    void endNode();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Pin API
    /// @{

    /**
     * @brief Begin defining an input pin
     * @param id Unique pin ID
     */
    void beginInputAttribute(int id);

    /**
     * @brief Begin defining an output pin
     * @param id Unique pin ID
     */
    void beginOutputAttribute(int id);

    /**
     * @brief Set pin label
     */
    void pinLabel(const std::string& label);

    /**
     * @brief End pin definition
     */
    void endInputAttribute();
    void endOutputAttribute();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Links
    /// @{

    /**
     * @brief Define a link between pins
     * @param id Unique link ID
     * @param startPinId Output pin ID (source)
     * @param endPinId Input pin ID (destination)
     */
    void link(int id, int startPinId, int endPinId);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Node Positioning
    /// @{

    /**
     * @brief Set node position in grid space
     */
    void setNodePosition(int nodeId, glm::vec2 gridPos);

    /**
     * @brief Get node position in grid space
     */
    glm::vec2 getNodePosition(int nodeId) const;

    /**
     * @brief Run hierarchical auto-layout
     */
    void autoLayout();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Selection & Hover
    /// @{

    /**
     * @brief Check if a node is hovered
     * @param outId If non-null and returns true, set to hovered node ID
     */
    bool isNodeHovered(int* outId = nullptr) const;

    /**
     * @brief Check if a link is hovered
     * @param outId If non-null and returns true, set to hovered link ID
     */
    bool isLinkHovered(int* outId = nullptr) const;

    /**
     * @brief Check if a pin is hovered
     * @param outId If non-null and returns true, set to hovered pin ID
     */
    bool isPinHovered(int* outId = nullptr) const;

    /**
     * @brief Get selected node ID (-1 if none)
     */
    int getSelectedNode() const { return m_selectedNodeId; }

    /**
     * @brief Select a node
     */
    void selectNode(int id);

    /**
     * @brief Clear selection
     */
    void clearSelection();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Zoom & Pan
    /// @{

    /**
     * @brief Get current zoom level
     */
    float zoom() const { return m_zoom; }

    /**
     * @brief Set zoom level
     * @param z Zoom factor (0.1 to 4.0)
     */
    void setZoom(float z);

    /**
     * @brief Zoom to fit all nodes in view
     */
    void zoomToFit();

    /**
     * @brief Get current pan offset
     */
    glm::vec2 pan() const { return m_pan; }

    /**
     * @brief Set pan offset
     */
    void setPan(glm::vec2 p) { m_pan = p; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Coordinate Transforms
    /// @{

    /**
     * @brief Transform grid position to screen position
     */
    glm::vec2 gridToScreen(glm::vec2 gridPos) const;

    /**
     * @brief Transform screen position to grid position
     */
    glm::vec2 screenToGrid(glm::vec2 screenPos) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Style
    /// @{

    /**
     * @brief Get mutable style reference
     */
    NodeGraphStyle& style() { return m_style; }

    /**
     * @brief Get style (const)
     */
    const NodeGraphStyle& style() const { return m_style; }

    /// @}

private:
    // Rendering helpers
    void renderBackground();
    void renderGrid();
    void renderLinks();
    void renderNodes();
    void renderNode(NodeState& node);

    // Hit testing
    void updateHover();
    bool isPointInNode(glm::vec2 screenPos, const NodeState& node) const;
    bool isPointNearLink(glm::vec2 screenPos, const LinkState& link) const;
    int findPinAtPosition(glm::vec2 screenPos) const;

    // Input handling
    void handleInput();
    void handleZoom();
    void handlePan();
    void handleNodeDrag();
    void handleSelection();

    // Get pin screen position
    glm::vec2 getPinScreenPos(int pinId) const;

    // Style
    NodeGraphStyle m_style;

    // Zoom/pan state
    float m_zoom = 1.0f;
    glm::vec2 m_pan = {0, 0};
    static constexpr float MIN_ZOOM = 0.1f;
    static constexpr float MAX_ZOOM = 4.0f;

    // Editor state
    float m_width = 0;
    float m_height = 0;
    OverlayCanvas* m_canvas = nullptr;
    NodeGraphInput m_input;
    bool m_inEditor = false;

    // Node data
    std::unordered_map<int, NodeState> m_nodes;
    std::unordered_map<int, LinkState> m_links;
    std::unordered_map<int, int> m_pinToNode;  // Pin ID -> Node ID

    // Current build state
    int m_currentNodeId = -1;
    int m_currentPinId = -1;
    bool m_currentPinIsOutput = false;

    // Hover/selection state
    int m_hoveredNodeId = -1;
    int m_hoveredLinkId = -1;
    int m_hoveredPinId = -1;
    int m_selectedNodeId = -1;

    // Drag state
    bool m_isPanning = false;
    bool m_isDraggingNode = false;
    glm::vec2 m_dragStartPos = {0, 0};
    glm::vec2 m_dragNodeStartGridPos = {0, 0};
};

} // namespace vivid
