#pragma once

/**
 * @file panel_group.h
 * @brief Tabbed panel container
 *
 * A PanelGroup holds multiple panels as tabs. Only one panel is active at a time.
 * Users can click tabs to switch, drag tabs to reorder, or drag tabs out to detach.
 */

#include <vivid/devtools/layout_node.h>
#include <vivid/devtools/panel.h>
#include <memory>
#include <vector>

namespace vivid {

/**
 * @brief Container that holds multiple panels as tabs
 *
 * Features:
 * - Tab bar at top showing all panels
 * - Click tab to switch active panel
 * - Drag tab to reorder within group
 * - Drag tab out of group to detach (creates floating panel)
 * - Close button on tabs (optional)
 *
 * Usage:
 * @code
 * auto group = std::make_unique<PanelGroup>();
 * group->addPanel(terminalPanel);
 * group->addPanel(consolePanel);
 * group->setActiveTab(0);
 * @endcode
 */
class PanelGroup : public LayoutNode {
public:
    PanelGroup();
    ~PanelGroup() override;

    // LayoutNode interface
    LayoutNodeType type() const override { return LayoutNodeType::PanelGroup; }
    void updateLayout() override;
    void render(OverlayCanvas& canvas, const FrameInput& input, const UIStyle& style) override;
    bool handleInput(const FrameInput& input) override;
    void collectPanels(std::vector<Panel*>& outPanels) override;
    Panel* findPanel(const std::string& id) override;
    bool containsPanel(Panel* panel) const override;
    bool isHovered() const override;
    bool isInteracting() const override;

    // -------------------------------------------------------------------------
    /// @name Panel Management
    /// @{

    /**
     * @brief Add a panel to the group
     * @param panel Panel to add (ownership NOT transferred - panels owned by PanelManager)
     */
    void addPanel(Panel* panel);

    /**
     * @brief Remove a panel from the group
     * @param panel Panel to remove
     * @return true if panel was found and removed
     */
    bool removePanel(Panel* panel);

    /**
     * @brief Get number of panels in group
     */
    size_t panelCount() const { return m_panels.size(); }

    /**
     * @brief Get panel at index
     */
    Panel* panelAt(size_t index) const;

    /**
     * @brief Check if group is empty
     */
    bool isEmpty() const { return m_panels.empty(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Tab Control
    /// @{

    /**
     * @brief Get/set active tab index
     */
    int activeTabIndex() const { return m_activeTab; }
    void setActiveTab(int index);

    /**
     * @brief Get active panel
     */
    Panel* activePanel() const;

    /**
     * @brief Set active panel by pointer
     */
    void setActivePanel(Panel* panel);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Callbacks
    /// @{

    /**
     * @brief Set callback for when a tab is selected
     */
    void onTabSelect(TabSelectCallback callback) { m_onTabSelect = std::move(callback); }

    /**
     * @brief Set callback for when a tab is closed
     */
    void onTabClose(TabCloseCallback callback) { m_onTabClose = std::move(callback); }

    /**
     * @brief Set callback for when a tab is dragged out
     */
    void onTabDrag(TabDragCallback callback) { m_onTabDrag = std::move(callback); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Enable/disable close buttons on tabs
     */
    void setShowCloseButtons(bool show) { m_showCloseButtons = show; }
    bool showCloseButtons() const { return m_showCloseButtons; }

    /**
     * @brief Enable/disable tab reordering via drag
     */
    void setAllowReorder(bool allow) { m_allowReorder = allow; }
    bool allowReorder() const { return m_allowReorder; }

    /**
     * @brief Get/set group ID (for serialization)
     */
    const std::string& id() const { return m_id; }
    void setId(const std::string& id) { m_id = id; }

    /// @}

private:
    void renderTabBar(OverlayCanvas& canvas);
    int hitTestTab(const glm::vec2& pos) const;  // Returns tab index or -1

    std::string m_id;
    std::vector<Panel*> m_panels;  // Non-owning pointers
    int m_activeTab = 0;

    // Tab interaction state
    bool m_hovered = false;
    bool m_tabBarHovered = false;
    int m_hoveredTab = -1;
    int m_draggingTab = -1;
    glm::vec2 m_dragStartPos = {0, 0};
    bool m_dragStarted = false;  // True once drag threshold exceeded

    // Tab bar geometry (calculated during render)
    float m_tabBarHeight = 28.0f;
    std::vector<glm::vec4> m_tabRects;  // Tab bounds for hit testing

    // Configuration
    bool m_showCloseButtons = false;
    bool m_allowReorder = true;

    // Callbacks
    TabSelectCallback m_onTabSelect;
    TabCloseCallback m_onTabClose;
    TabDragCallback m_onTabDrag;

    // Input tracking
    bool m_lastMouseDown = false;
};

} // namespace vivid
