#pragma once

/**
 * @file layout_node.h
 * @brief Layout tree nodes for panel docking system
 *
 * The layout system uses a tree structure where:
 * - LayoutNode is the abstract base class
 * - PanelGroup holds multiple panels as tabs
 * - SplitContainer splits space between two children (horizontal or vertical)
 * - Leaf nodes reference actual Panel instances
 *
 * Example layout tree:
 *   SplitContainer (horizontal)
 *   ├── PanelGroup [NodeGraph] (70%)
 *   └── SplitContainer (vertical)
 *       ├── PanelGroup [Terminal, Console] (50%)
 *       └── PanelGroup [Inspector] (50%)
 */

#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/ui_style.h>
#include <vivid/gui/input_state.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace vivid {

class Panel;
class Context;

/**
 * @brief Type of layout node
 */
enum class LayoutNodeType {
    PanelGroup,      ///< Container with tabbed panels
    SplitContainer,  ///< Container that splits space between two children
    PanelLeaf        ///< Single panel (leaf node)
};

/**
 * @brief Split direction for SplitContainer
 */
enum class SplitDirection {
    Horizontal,  ///< Left-right split
    Vertical     ///< Top-bottom split
};

/**
 * @brief Abstract base class for layout tree nodes
 *
 * Layout nodes form a tree structure that defines how panels are arranged.
 * Each node knows its bounds and can render itself and handle input.
 */
class LayoutNode {
public:
    LayoutNode() = default;
    virtual ~LayoutNode() = default;

    // Non-copyable
    LayoutNode(const LayoutNode&) = delete;
    LayoutNode& operator=(const LayoutNode&) = delete;

    /**
     * @brief Get the type of this node
     */
    virtual LayoutNodeType type() const = 0;

    /**
     * @brief Get/set bounds in logical pixels
     */
    const glm::vec4& bounds() const { return m_bounds; }
    void setBounds(const glm::vec4& bounds) { m_bounds = bounds; }

    /**
     * @brief Recursively update layout of children based on current bounds
     */
    virtual void updateLayout() = 0;

    /**
     * @brief Render this node and its children
     * @param canvas Canvas for drawing
     * @param input Input state
     * @param style UI style for colors and layout
     *
     * All coordinates are in logical pixels. The canvas handles scaling internally.
     */
    virtual void render(OverlayCanvas& canvas, const gui::InputState& input, const UIStyle& style) = 0;

    /**
     * @brief Handle input for this node
     * @param input Input state
     * @return true if input was consumed
     */
    virtual bool handleInput(const gui::InputState& input) = 0;

    /**
     * @brief Get all panels contained in this node (recursively)
     * @param outPanels Vector to append panels to
     */
    virtual void collectPanels(std::vector<Panel*>& outPanels) = 0;

    /**
     * @brief Find a panel by ID
     * @param id Panel ID
     * @return Panel pointer or nullptr
     */
    virtual Panel* findPanel(const std::string& id) = 0;

    /**
     * @brief Check if this node contains a specific panel
     */
    virtual bool containsPanel(Panel* panel) const = 0;

    /**
     * @brief Check if any panel in this node is hovered
     */
    virtual bool isHovered() const = 0;

    /**
     * @brief Check if any panel in this node is being interacted with
     */
    virtual bool isInteracting() const = 0;

protected:
    glm::vec4 m_bounds = {0, 0, 0, 0};  ///< Position and size (x, y, w, h)
};

/**
 * @brief Callback for panel tab events
 */
using TabSelectCallback = std::function<void(Panel* panel)>;
using TabCloseCallback = std::function<void(Panel* panel)>;
using TabDragCallback = std::function<void(Panel* panel, const glm::vec2& pos)>;

} // namespace vivid
