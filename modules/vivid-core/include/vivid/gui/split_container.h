#pragma once

/**
 * @file split_container.h
 * @brief Container that splits space between two children
 *
 * A SplitContainer divides its space between two child LayoutNodes
 * with a draggable divider. Supports horizontal (left/right) and
 * vertical (top/bottom) splits.
 */

#include <vivid/gui/layout_node.h>
#include <memory>

namespace vivid {

/**
 * @brief Container that splits space between two children with a draggable divider
 *
 * Features:
 * - Horizontal split (left/right children)
 * - Vertical split (top/bottom children)
 * - Draggable divider to adjust split ratio
 * - Minimum size enforcement for children
 * - Can be nested for complex layouts
 *
 * Usage:
 * @code
 * auto split = std::make_unique<SplitContainer>(SplitDirection::Horizontal);
 * split->setFirst(nodeGraphGroup);   // Left side (70%)
 * split->setSecond(rightSplit);      // Right side (30%)
 * split->setSplitRatio(0.7f);
 * @endcode
 */
class SplitContainer : public LayoutNode {
public:
    /**
     * @brief Construct a split container
     * @param direction Split direction (Horizontal or Vertical)
     */
    explicit SplitContainer(SplitDirection direction = SplitDirection::Horizontal);
    ~SplitContainer() override;

    // LayoutNode interface
    LayoutNodeType type() const override { return LayoutNodeType::SplitContainer; }
    void updateLayout() override;
    void render(OverlayCanvas& canvas, const gui::InputState& input, const UIStyle& style) override;
    bool handleInput(const gui::InputState& input) override;
    void collectPanels(std::vector<Panel*>& outPanels) override;
    Panel* findPanel(const std::string& id) override;
    bool containsPanel(Panel* panel) const override;
    bool isHovered() const override;
    bool isInteracting() const override;

    // -------------------------------------------------------------------------
    /// @name Children
    /// @{

    /**
     * @brief Set the first child (left or top)
     * @param node Child node (ownership transferred)
     */
    void setFirst(std::unique_ptr<LayoutNode> node);

    /**
     * @brief Set the second child (right or bottom)
     * @param node Child node (ownership transferred)
     */
    void setSecond(std::unique_ptr<LayoutNode> node);

    /**
     * @brief Get the first child
     */
    LayoutNode* first() const { return m_first.get(); }

    /**
     * @brief Get the second child
     */
    LayoutNode* second() const { return m_second.get(); }

    /**
     * @brief Release ownership of first child
     */
    std::unique_ptr<LayoutNode> releaseFirst() { return std::move(m_first); }

    /**
     * @brief Release ownership of second child
     */
    std::unique_ptr<LayoutNode> releaseSecond() { return std::move(m_second); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Get/set split direction
     */
    SplitDirection direction() const { return m_direction; }
    void setDirection(SplitDirection dir) { m_direction = dir; }

    /**
     * @brief Get/set split ratio (0.0 to 1.0)
     *
     * For horizontal: ratio is width of first child / total width
     * For vertical: ratio is height of first child / total height
     */
    float splitRatio() const { return m_splitRatio; }
    void setSplitRatio(float ratio);

    /**
     * @brief Get/set minimum size for children
     */
    float minChildSize() const { return m_minChildSize; }
    void setMinChildSize(float size) { m_minChildSize = size; }

    /**
     * @brief Get/set divider thickness
     */
    float dividerSize() const { return m_dividerSize; }
    void setDividerSize(float size) { m_dividerSize = size; }

    /**
     * @brief Get/set container ID (for serialization)
     */
    const std::string& id() const { return m_id; }
    void setId(const std::string& id) { m_id = id; }

    /**
     * @brief Get/set whether the divider is resizable
     *
     * When false, the divider is not rendered and cannot be dragged.
     * The split ratio remains fixed. Useful for status bars and fixed UI elements.
     */
    bool isResizable() const { return m_resizable; }
    void setResizable(bool resizable) { m_resizable = resizable; }

    /// @}

private:
    void calculateChildBounds();
    bool hitTestDivider(const glm::vec2& pos) const;

    std::string m_id;
    SplitDirection m_direction;
    float m_splitRatio = 0.5f;
    float m_minChildSize = 150.0f;
    float m_dividerSize = 6.0f;
    bool m_resizable = true;

    std::unique_ptr<LayoutNode> m_first;
    std::unique_ptr<LayoutNode> m_second;

    // Divider interaction state
    bool m_dividerHovered = false;
    bool m_dividerDragging = false;
    float m_dragStartRatio = 0.0f;
    float m_dragStartPos = 0.0f;

    // Calculated bounds
    glm::vec4 m_firstBounds = {0, 0, 0, 0};
    glm::vec4 m_secondBounds = {0, 0, 0, 0};
    glm::vec4 m_dividerBounds = {0, 0, 0, 0};

    // Input tracking
    bool m_lastMouseDown = false;
};

} // namespace vivid
