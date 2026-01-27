#pragma once

/**
 * @file dock_manager.h
 * @brief Manages panel docking operations
 *
 * The DockManager handles:
 * - Drag state tracking (tabs, panel headers, floating panels)
 * - Drop zone computation (where panels can be docked)
 * - Dock execution (inserting panels into the layout tree)
 * - Visual dock guides rendering
 *
 * It integrates with PanelManager and the layout tree system.
 */

#include <vivid/devtools/dock_zone.h>
#include <vivid/devtools/layout_node.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/frame_input.h>
#include <memory>
#include <vector>
#include <functional>

namespace vivid {

class PanelManager;
class PanelGroup;
class SplitContainer;
class PanelLeaf;
struct UIStyle;

/**
 * @brief Callback for when a panel is floated (drag-out from layout)
 */
using PanelFloatedCallback = std::function<void(Panel* panel, const glm::vec2& pos)>;

/**
 * @brief Callback for when a panel is docked
 */
using PanelDockedCallback = std::function<void(Panel* panel)>;

/**
 * @brief Manages drag-to-dock operations for panels
 *
 * Provides the logic for:
 * 1. Starting drags from tabs, panel headers, or floating panels
 * 2. Computing valid drop zones based on layout tree state
 * 3. Rendering visual dock guides during drag
 * 4. Executing dock operations (modifying the layout tree)
 * 5. Cleaning up empty containers after operations
 *
 * Usage:
 * @code
 * // In PanelManager initialization:
 * m_dockManager = std::make_unique<DockManager>(this);
 *
 * // Wire up tab drag callback:
 * group->onTabDrag([this](Panel* p, const glm::vec2& pos) {
 *     m_dockManager->beginTabDrag(p, group, pos);
 * });
 *
 * // Each frame in render:
 * m_dockManager->update(input);
 * m_dockManager->renderGuides(canvas, style);
 * @endcode
 */
class DockManager {
public:
    /**
     * @brief Construct a dock manager
     * @param panelManager Parent panel manager (not owned)
     */
    explicit DockManager(PanelManager* panelManager);
    ~DockManager();

    // Non-copyable
    DockManager(const DockManager&) = delete;
    DockManager& operator=(const DockManager&) = delete;

    // -------------------------------------------------------------------------
    /// @name Drag Operations
    /// @{

    /**
     * @brief Start dragging a tab from a PanelGroup
     *
     * Called by PanelGroup when user drags a tab out of the tab bar.
     * The panel is removed from the group and becomes a floating preview.
     *
     * @param panel Panel being dragged
     * @param sourceGroup Group the tab came from
     * @param mousePos Current mouse position
     */
    void beginTabDrag(Panel* panel, PanelGroup* sourceGroup, const glm::vec2& mousePos);

    /**
     * @brief Start dragging a panel from its header (PanelLeaf)
     *
     * @param panel Panel being dragged
     * @param sourceLeaf Leaf node containing the panel
     * @param mousePos Current mouse position
     */
    void beginLeafDrag(Panel* panel, PanelLeaf* sourceLeaf, const glm::vec2& mousePos);

    /**
     * @brief Start dragging a floating panel
     *
     * Called when user starts dragging a floating panel that might be docked.
     *
     * @param panel Floating panel being dragged
     * @param mousePos Current mouse position
     */
    void beginFloatingPanelDrag(Panel* panel, const glm::vec2& mousePos);

    /**
     * @brief Cancel current drag operation
     *
     * Restores panel to original position/state.
     */
    void cancelDrag();

    /**
     * @brief End drag operation at current position
     *
     * If hovering a drop zone, executes the dock. Otherwise floats the panel.
     */
    void endDrag();

    /**
     * @brief Check if a drag is in progress
     */
    bool isDragging() const { return m_dragState.isActive(); }

    /**
     * @brief Get current drag state (for external rendering)
     */
    const DragState& dragState() const { return m_dragState; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Frame Update
    /// @{

    /**
     * @brief Update dock manager state each frame
     *
     * Updates drag position, computes drop zones, hit tests zones.
     *
     * @param input Frame input state
     */
    void update(const FrameInput& input);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Rendering
    /// @{

    /**
     * @brief Render dock guides and preview during drag
     *
     * Renders:
     * - Dock guide icons (center, directional arrows)
     * - Preview highlight showing where panel will dock
     * - Dragged panel outline
     *
     * @param canvas Canvas for drawing
     * @param style UI style
     */
    void renderGuides(OverlayCanvas& canvas, const UIStyle& style);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Drop Zones
    /// @{

    /**
     * @brief Get computed drop zones
     */
    const std::vector<DockZone>& dropZones() const { return m_dropZones; }

    /**
     * @brief Get currently active (hovered) zone
     * @return Pointer to active zone, or nullptr if none
     */
    const DockZone* activeZone() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Callbacks
    /// @{

    /**
     * @brief Set callback for when a panel is floated
     */
    void onPanelFloated(PanelFloatedCallback callback) {
        m_onPanelFloated = std::move(callback);
    }

    /**
     * @brief Set callback for when a panel is docked
     */
    void onPanelDocked(PanelDockedCallback callback) {
        m_onPanelDocked = std::move(callback);
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Layout Tree Operations
    /// @{

    /**
     * @brief Remove a panel from its current location in the layout tree
     *
     * Handles removal from PanelGroup (tab removal) or PanelLeaf (node removal).
     * Does NOT modify floating panel list.
     *
     * @param panel Panel to remove
     * @return True if panel was found and removed
     */
    bool removePanelFromLayout(Panel* panel);

    /**
     * @brief Clean up empty containers after panel removal
     *
     * Removes empty PanelGroups and collapses SplitContainers with only one child.
     */
    void cleanupEmptyNodes();

    /**
     * @brief Capture a panel's current position in the layout tree
     *
     * Used before hiding a panel to remember where to restore it.
     *
     * @param panel Panel to capture position for
     * @return TreeSlot with position info, or invalid slot if not in tree
     */
    TreeSlot captureTreeSlot(Panel* panel);

    /**
     * @brief Restore a panel to a previously captured tree slot
     *
     * @param panel Panel to restore
     * @param slot TreeSlot with position info
     * @return True if restoration succeeded
     */
    bool restoreFromTreeSlot(Panel* panel, const TreeSlot& slot);

    /**
     * @brief Dock a panel to a specific position programmatically
     *
     * Used for MCP/API control of panel layout. Removes the panel from its
     * current position and docks it to the specified edge or center.
     *
     * @param panel Panel to dock
     * @param position Target dock position
     */
    void dockPanelProgrammatically(Panel* panel, DockPosition position);

    /// @}

private:
    /**
     * @brief Compute drop zones based on current layout
     */
    void computeDropZones();

    /**
     * @brief Add drop zones for a PanelGroup
     */
    void addGroupZones(PanelGroup* group);

    /**
     * @brief Add drop zones for a SplitContainer
     */
    void addSplitZones(SplitContainer* split);

    /**
     * @brief Add drop zones for a PanelLeaf
     */
    void addLeafZones(PanelLeaf* leaf);

    /**
     * @brief Add root edge zones (dock to window edges)
     */
    void addRootZones();

    /**
     * @brief Hit-test drop zones to find active zone
     * @param pos Mouse position
     */
    void hitTestZones(const glm::vec2& pos);

    /**
     * @brief Execute a dock operation
     * @param zone Zone to dock to
     */
    void executeDock(const DockZone& zone);

    /**
     * @brief Execute docking to center (add as tab)
     */
    void executeDockCenter(const DockZone& zone);

    /**
     * @brief Execute docking to edge (create split)
     */
    void executeDockEdge(const DockZone& zone);

    /**
     * @brief Create a floating panel from current drag
     */
    void executeFloat();

    /**
     * @brief Render dock guide icons for hovered panel
     */
    void renderGuideIcons(OverlayCanvas& canvas, const UIStyle& style);

    /**
     * @brief Render root edge dock indicators at screen borders
     */
    void renderRootEdgeIndicators(OverlayCanvas& canvas, const UIStyle& style);

    /**
     * @brief Render preview highlight
     */
    void renderPreview(OverlayCanvas& canvas, const UIStyle& style);

    /**
     * @brief Render dragged panel outline
     */
    void renderDraggedPanel(OverlayCanvas& canvas, const UIStyle& style);

    /**
     * @brief Create a PanelGroup with dock callbacks wired up
     * @param panel Initial panel for the group
     * @return New PanelGroup with proper callbacks
     */
    std::unique_ptr<PanelGroup> createDockedGroup(Panel* panel);

    /**
     * @brief Find the parent of a layout node
     * @param node Node to find parent of
     * @param outIsFirst Set to true if node is first child
     * @return Parent SplitContainer, or nullptr if node is root
     */
    SplitContainer* findParent(LayoutNode* node, bool& outIsFirst);

    /**
     * @brief Replace a node in its parent with another node
     */
    void replaceInParent(LayoutNode* oldNode, std::unique_ptr<LayoutNode> newNode);

    /**
     * @brief Recursively traverse layout tree
     */
    void traverseLayout(LayoutNode* node, std::function<void(LayoutNode*)> visitor);

    /**
     * @brief Find a PanelGroup by ID
     */
    PanelGroup* findGroupById(const std::string& id);

    PanelManager* m_panelManager;
    DragState m_dragState;
    std::vector<DockZone> m_dropZones;
    int m_activeZoneIndex = -1;

    // Screen bounds for zone calculation
    float m_screenWidth = 0;
    float m_screenHeight = 0;

    // Callbacks
    PanelFloatedCallback m_onPanelFloated;
    PanelDockedCallback m_onPanelDocked;

    // Constants
    static constexpr float kGuideIconSize = 32.0f;
    static constexpr float kGuideSpacing = 8.0f;
    static constexpr float kEdgeZoneSize = 80.0f;
    static constexpr float kCenterZoneSize = 60.0f;
    static constexpr float kPreviewAlpha = 0.3f;
};

} // namespace vivid
