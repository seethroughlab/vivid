#pragma once

/**
 * @file dock_zone.h
 * @brief Docking zone definitions for panel drag-and-drop
 *
 * Defines the data structures used for the drag-to-dock system:
 * - DockPosition: Where a panel can be docked (center, edges, or float)
 * - DockZone: A droppable region with hit-test and preview bounds
 * - DragState: Tracks active drag operations
 */

#include <glm/glm.hpp>
#include <string>

namespace vivid {

class Panel;
class PanelGroup;
class LayoutNode;

/**
 * @brief Position where a panel can be docked
 */
enum class DockPosition {
    None,     ///< No dock position (invalid/cancelled)
    Center,   ///< Drop as tab in existing group
    Left,     ///< Split and dock to left
    Right,    ///< Split and dock to right
    Top,      ///< Split and dock to top
    Bottom,   ///< Split and dock to bottom
    Float     ///< Create floating panel
};

/**
 * @brief A droppable zone for docking operations
 *
 * Each dock zone represents a region where a panel can be dropped.
 * The zone has:
 * - Hit-test bounds for detecting when cursor enters
 * - Preview bounds for showing where the panel will appear
 * - Target node where the panel will be inserted
 */
struct DockZone {
    DockPosition position = DockPosition::None;
    glm::vec4 hitBounds = {0, 0, 0, 0};      ///< Hit-test area (x, y, w, h)
    glm::vec4 previewBounds = {0, 0, 0, 0};  ///< Preview highlight area
    LayoutNode* targetNode = nullptr;        ///< Where to dock (PanelGroup or split target)
    bool isActive = false;                   ///< Currently hovered
    bool isRootZone = false;                 ///< Zone is at window edges (vs. panel edges)
};

/**
 * @brief State tracking for drag operations
 *
 * Tracks what is being dragged (tab, panel header, floating panel)
 * and where it came from, for proper cleanup on drop or cancel.
 */
struct DragState {
    enum class Type {
        None,           ///< No drag in progress
        Tab,            ///< Dragging a tab from a PanelGroup
        PanelHeader,    ///< Dragging a panel by its header (from PanelLeaf)
        FloatingPanel   ///< Dragging a floating panel
    };

    Type type = Type::None;
    Panel* panel = nullptr;              ///< The panel being dragged
    PanelGroup* sourceGroup = nullptr;   ///< Source group (for Tab type)
    LayoutNode* sourceNode = nullptr;    ///< Source node in layout tree

    glm::vec2 startPos = {0, 0};         ///< Mouse position when drag started
    glm::vec2 currentPos = {0, 0};       ///< Current mouse position
    glm::vec2 panelOffset = {0, 0};      ///< Offset from mouse to panel corner
    glm::vec4 originalBounds = {0, 0, 0, 0}; ///< Original bounds before drag

    bool isDragging = false;             ///< True once drag threshold exceeded
    bool showPreview = false;            ///< Show dock preview

    /**
     * @brief Reset drag state
     */
    void reset() {
        type = Type::None;
        panel = nullptr;
        sourceGroup = nullptr;
        sourceNode = nullptr;
        startPos = {0, 0};
        currentPos = {0, 0};
        panelOffset = {0, 0};
        originalBounds = {0, 0, 0, 0};
        isDragging = false;
        showPreview = false;
    }

    /**
     * @brief Check if a drag is active
     */
    bool isActive() const { return type != Type::None && isDragging; }
};

/**
 * @brief Information about a panel's position in the layout tree
 *
 * Used to restore a panel to its previous position after hiding/showing.
 * Captures enough information to reconstruct the panel's location.
 *
 * Includes version tracking to detect when the layout has changed since
 * the slot was captured (making restoration unreliable).
 */
struct TreeSlot {
    enum class Type {
        None,       ///< Not in layout tree
        Group,      ///< Panel was a tab in a PanelGroup
        Leaf,       ///< Panel was in a PanelLeaf
        Root        ///< Panel was the root of the layout
    };

    Type type = Type::None;
    std::string groupId;          ///< ID of containing PanelGroup (for Group type)
    int tabIndex = -1;            ///< Tab position in group
    int captureVersion = 0;       ///< Layout version when captured (for validation)

    // For Leaf type: info to recreate split
    struct SplitInfo {
        int direction = 0;        ///< 0=horizontal, 1=vertical (maps to SplitDirection)
        float ratio = 0.5f;       ///< Split ratio
        bool wasFirst = true;     ///< Was this the first or second child?
        std::string siblingId;    ///< Panel ID of the sibling
    };
    SplitInfo splitInfo;

    /**
     * @brief Check if slot has valid restoration info
     */
    bool isValid() const { return type != Type::None; }

    /**
     * @brief Reset to invalid state
     */
    void reset() {
        type = Type::None;
        groupId.clear();
        tabIndex = -1;
        captureVersion = 0;
        splitInfo = SplitInfo{};
    }
};

/**
 * @brief Dock mode for panels
 *
 * Panels can be in one of three states:
 * - Docked: Part of the layout tree (in a PanelGroup or PanelLeaf)
 * - Floating: Independent window rendered on top
 * - Hidden: Not visible but position remembered for restoration
 */
enum class DockMode {
    Docked,     ///< In layout tree
    Floating,   ///< Independent floating window
    Hidden      ///< Not visible, position saved
};

} // namespace vivid
