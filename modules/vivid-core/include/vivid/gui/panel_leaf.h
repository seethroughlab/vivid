#pragma once

/**
 * @file panel_leaf.h
 * @brief Leaf node that wraps a single panel
 *
 * PanelLeaf is a simple wrapper that allows a single Panel to be used
 * in the layout tree without needing a full PanelGroup.
 */

#include <vivid/gui/layout_node.h>
#include <vivid/gui/panel.h>

namespace vivid {

/**
 * @brief Leaf node wrapping a single Panel
 *
 * Use this when you want a single panel in the layout tree
 * without the overhead of a PanelGroup (no tab bar).
 */
class PanelLeaf : public LayoutNode {
public:
    /**
     * @brief Construct a leaf node
     * @param panel Panel to wrap (ownership NOT transferred)
     */
    explicit PanelLeaf(Panel* panel = nullptr);
    ~PanelLeaf() override;

    // LayoutNode interface
    LayoutNodeType type() const override { return LayoutNodeType::PanelLeaf; }
    void updateLayout() override;
    void render(OverlayCanvas& canvas, const gui::InputState& input, const UIStyle& style) override;
    bool handleInput(const gui::InputState& input) override;
    void collectPanels(std::vector<Panel*>& outPanels) override;
    Panel* findPanel(const std::string& id) override;
    bool containsPanel(Panel* panel) const override;
    bool isHovered() const override;
    bool isInteracting() const override;

    /**
     * @brief Get/set the wrapped panel
     */
    Panel* panel() const { return m_panel; }
    void setPanel(Panel* panel) { m_panel = panel; }

private:
    Panel* m_panel = nullptr;  // Non-owning pointer
};

} // namespace vivid
