// DockManager implementation
// Handles panel drag-to-dock operations

#include <vivid/gui/dock_manager.h>
#include <vivid/gui/panel_manager.h>
#include <vivid/gui/panel_group.h>
#include <vivid/gui/split_container.h>
#include <vivid/gui/panel_leaf.h>
#include <vivid/gui/panel.h>
#include <vivid/gui/ui_style.h>
#include <vivid/gui/gui_debug.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>

namespace vivid {

DockManager::DockManager(PanelManager* panelManager)
    : m_panelManager(panelManager) {
}

DockManager::~DockManager() = default;

// -----------------------------------------------------------------------------
// Drag Operations
// -----------------------------------------------------------------------------

void DockManager::beginTabDrag(Panel* panel, PanelGroup* sourceGroup, const glm::vec2& mousePos) {
    if (!panel || !sourceGroup || !m_panelManager) return;

    gui::logTransition("DockManager", "idle", "tab-drag", panel->config().id.c_str());

    // Auto-enable layout mode if not already enabled
    if (!m_panelManager->isLayoutMode()) {
        if (!m_panelManager->layoutRoot()) {
            m_panelManager->buildDefaultLayout();
        }
        m_panelManager->setLayoutMode(true);
        gui::logDebug("DockManager", "Auto-enabled layout mode for tab drag");
    }

    m_dragState.reset();
    m_dragState.type = DragState::Type::Tab;
    m_dragState.panel = panel;
    m_dragState.sourceGroup = sourceGroup;
    m_dragState.startPos = mousePos;
    m_dragState.currentPos = mousePos;
    m_dragState.originalBounds = panel->bounds();
    m_dragState.isDragging = true;
    m_dragState.showPreview = true;

    // Calculate offset from mouse to panel top-left (for preview positioning)
    m_dragState.panelOffset = glm::vec2(100, 20); // Default offset from cursor

    // Remove panel from source group
    sourceGroup->removePanel(panel);
    gui::logDebug("DockManager", "Removed panel from source group", panel->config().id);

    // Compute drop zones
    computeDropZones();

    std::cerr << "[DockManager] Begin tab drag: " << panel->config().id << "\n";
}

void DockManager::beginLeafDrag(Panel* panel, PanelLeaf* sourceLeaf, const glm::vec2& mousePos) {
    if (!panel || !sourceLeaf) return;

    m_dragState.reset();
    m_dragState.type = DragState::Type::PanelHeader;
    m_dragState.panel = panel;
    m_dragState.sourceNode = sourceLeaf;
    m_dragState.startPos = mousePos;
    m_dragState.currentPos = mousePos;
    m_dragState.originalBounds = panel->bounds();
    m_dragState.isDragging = true;
    m_dragState.showPreview = true;

    m_dragState.panelOffset = glm::vec2(mousePos.x - panel->bounds().x,
                                         mousePos.y - panel->bounds().y);

    // Compute drop zones
    computeDropZones();

    std::cerr << "[DockManager] Begin leaf drag: " << panel->config().id << "\n";
}

void DockManager::beginFloatingPanelDrag(Panel* panel, const glm::vec2& mousePos) {
    if (!panel) return;

    gui::logTransition("DockManager", "idle", "floating-drag", panel->config().id.c_str());

    m_dragState.reset();
    m_dragState.type = DragState::Type::FloatingPanel;
    m_dragState.panel = panel;
    m_dragState.startPos = mousePos;
    m_dragState.currentPos = mousePos;
    m_dragState.originalBounds = panel->bounds();
    m_dragState.isDragging = true;
    m_dragState.showPreview = true;

    m_dragState.panelOffset = glm::vec2(mousePos.x - panel->bounds().x,
                                         mousePos.y - panel->bounds().y);

    // Compute drop zones
    computeDropZones();

    std::cerr << "[DockManager] Begin floating panel drag: " << panel->config().id << "\n";
}

void DockManager::cancelDrag() {
    if (!m_dragState.isActive()) return;

    Panel* panel = m_dragState.panel;
    gui::logTransition("DockManager", "dragging", "cancelled",
                      panel ? panel->config().id.c_str() : "null");

    // Restore panel to original location based on drag type
    if (m_dragState.type == DragState::Type::Tab && m_dragState.sourceGroup) {
        // Re-add to source group
        m_dragState.sourceGroup->addPanel(panel);
        gui::logDebug("DockManager", "Restored panel to source group", panel->config().id);
    } else if (m_dragState.type == DragState::Type::PanelHeader) {
        // Panel is still in its leaf, just restore bounds
        panel->setBounds(m_dragState.originalBounds);
    } else if (m_dragState.type == DragState::Type::FloatingPanel) {
        // Restore floating panel bounds
        panel->setBounds(m_dragState.originalBounds);
    }

    m_dragState.reset();
    m_dropZones.clear();
    m_activeZoneIndex = -1;
}

void DockManager::endDrag() {
    if (!m_dragState.isActive()) return;

    Panel* panel = m_dragState.panel;
    const DockZone* zone = activeZone();
    const char* result = zone && zone->position != DockPosition::None ? "dock" : "float";
    gui::logTransition("DockManager", "dragging", result,
                      panel ? panel->config().id.c_str() : "null");

    // Check if we have an active zone
    if (zone && zone->position != DockPosition::None) {
        executeDock(*zone);
    } else {
        // No drop zone - float the panel
        executeFloat();
    }

    m_dragState.reset();
    m_dropZones.clear();
    m_activeZoneIndex = -1;
}

// -----------------------------------------------------------------------------
// Frame Update
// -----------------------------------------------------------------------------

void DockManager::update(const gui::InputState& input) {
    // Store screen size for zone calculations (use logical pixels to match mousePos)
    m_screenWidth = static_cast<float>(input.logicalWidth());
    m_screenHeight = static_cast<float>(input.logicalHeight());

    if (!m_dragState.isActive()) return;

    // Update drag position
    m_dragState.currentPos = input.mousePos;

    // Hit-test drop zones
    hitTestZones(input.mousePos);

    // Check for ESC key to cancel
    if (input.keyPressed[256]) { // GLFW_KEY_ESCAPE = 256
        cancelDrag();
        return;
    }

    // Check for mouse release to end drag
    if (!input.mouseDown[0]) {
        endDrag();
    }
}

// -----------------------------------------------------------------------------
// Drop Zone Computation
// -----------------------------------------------------------------------------

void DockManager::computeDropZones() {
    m_dropZones.clear();
    m_activeZoneIndex = -1;

    if (!m_panelManager || !m_panelManager->layoutRoot()) return;

    // Add root edge zones first (lowest priority in hit-testing)
    addRootZones();

    // Traverse layout tree and add zones for each node
    traverseLayout(m_panelManager->layoutRoot(), [this](LayoutNode* node) {
        switch (node->type()) {
            case LayoutNodeType::PanelGroup:
                addGroupZones(static_cast<PanelGroup*>(node));
                break;
            case LayoutNodeType::SplitContainer:
                addSplitZones(static_cast<SplitContainer*>(node));
                break;
            case LayoutNodeType::PanelLeaf:
                addLeafZones(static_cast<PanelLeaf*>(node));
                break;
        }
    });
}

void DockManager::addGroupZones(PanelGroup* group) {
    if (!group) return;

    glm::vec4 bounds = group->bounds();
    float cx = bounds.x + bounds.z / 2;
    float cy = bounds.y + bounds.w / 2;

    // Icon layout: icons are arranged in a cross pattern at the center
    // The hit areas are positioned around the icons, not along panel edges
    float iconSize = kGuideIconSize;
    float spacing = kGuideSpacing;
    float offset = iconSize + spacing;

    // Calculate status bar height for preview bounds
    float statusBarHeight = 0.0f;
    if (m_panelManager && m_panelManager->layoutRoot() &&
        m_panelManager->layoutRoot()->type() == LayoutNodeType::SplitContainer) {
        auto* rootSplit = static_cast<SplitContainer*>(m_panelManager->layoutRoot());
        if (rootSplit->direction() == SplitDirection::Vertical && !rootSplit->isResizable()) {
            statusBarHeight = m_screenHeight * rootSplit->splitRatio();
        }
    }
    float contentHeight = m_screenHeight - statusBarHeight;

    // Center zone (drop as tab) - icon at center
    // This is the only node-relative zone
    DockZone centerZone;
    centerZone.position = DockPosition::Center;
    centerZone.hitBounds = {cx - iconSize/2, cy - iconSize/2, iconSize, iconSize};
    centerZone.previewBounds = bounds;
    centerZone.targetNode = group;
    m_dropZones.push_back(centerZone);

    // Directional zones all dock into the content area (below status bar)
    // Preview bounds reflect the actual dock result

    // Left zone - content area height on left side
    DockZone leftZone;
    leftZone.position = DockPosition::Left;
    leftZone.hitBounds = {cx - offset - iconSize/2, cy - iconSize/2, iconSize, iconSize};
    leftZone.previewBounds = {0, statusBarHeight, m_screenWidth * 0.25f, contentHeight};
    leftZone.targetNode = group;
    m_dropZones.push_back(leftZone);

    // Right zone - content area height on right side
    DockZone rightZone;
    rightZone.position = DockPosition::Right;
    rightZone.hitBounds = {cx + offset - iconSize/2, cy - iconSize/2, iconSize, iconSize};
    rightZone.previewBounds = {m_screenWidth * 0.75f, statusBarHeight, m_screenWidth * 0.25f, contentHeight};
    rightZone.targetNode = group;
    m_dropZones.push_back(rightZone);

    // Top zone - full width below status bar
    DockZone topZone;
    topZone.position = DockPosition::Top;
    topZone.hitBounds = {cx - iconSize/2, cy - offset - iconSize/2, iconSize, iconSize};
    topZone.previewBounds = {0, statusBarHeight, m_screenWidth, contentHeight * 0.25f};
    topZone.targetNode = group;
    m_dropZones.push_back(topZone);

    // Bottom zone - full width at bottom
    DockZone bottomZone;
    bottomZone.position = DockPosition::Bottom;
    bottomZone.hitBounds = {cx - iconSize/2, cy + offset - iconSize/2, iconSize, iconSize};
    bottomZone.previewBounds = {0, m_screenHeight - contentHeight * 0.25f, m_screenWidth, contentHeight * 0.25f};
    bottomZone.targetNode = group;
    m_dropZones.push_back(bottomZone);
}

void DockManager::addSplitZones(SplitContainer* split) {
    // SplitContainers themselves don't have drop zones
    // Their children (groups/leaves) have the zones
}

void DockManager::addLeafZones(PanelLeaf* leaf) {
    if (!leaf || !leaf->panel()) return;

    // Skip if this leaf contains the panel being dragged
    if (m_dragState.panel && leaf->panel() == m_dragState.panel) return;

    glm::vec4 bounds = leaf->bounds();
    float cx = bounds.x + bounds.z / 2;
    float cy = bounds.y + bounds.w / 2;

    // Icon layout matches renderGuideIcons - icons in cross pattern at center
    float iconSize = kGuideIconSize;
    float spacing = kGuideSpacing;
    float offset = iconSize + spacing;

    // Calculate status bar height for preview bounds
    float statusBarHeight = 0.0f;
    if (m_panelManager && m_panelManager->layoutRoot() &&
        m_panelManager->layoutRoot()->type() == LayoutNodeType::SplitContainer) {
        auto* rootSplit = static_cast<SplitContainer*>(m_panelManager->layoutRoot());
        if (rootSplit->direction() == SplitDirection::Vertical && !rootSplit->isResizable()) {
            statusBarHeight = m_screenHeight * rootSplit->splitRatio();
        }
    }
    float contentHeight = m_screenHeight - statusBarHeight;

    // Center zone (replace leaf with group containing both panels)
    // This is the only node-relative zone
    DockZone centerZone;
    centerZone.position = DockPosition::Center;
    centerZone.hitBounds = {cx - iconSize/2, cy - iconSize/2, iconSize, iconSize};
    centerZone.previewBounds = bounds;
    centerZone.targetNode = leaf;
    m_dropZones.push_back(centerZone);

    // Directional zones all dock into the content area (below status bar)

    // Left zone - content area height on left side
    DockZone leftZone;
    leftZone.position = DockPosition::Left;
    leftZone.hitBounds = {cx - offset - iconSize/2, cy - iconSize/2, iconSize, iconSize};
    leftZone.previewBounds = {0, statusBarHeight, m_screenWidth * 0.25f, contentHeight};
    leftZone.targetNode = leaf;
    m_dropZones.push_back(leftZone);

    // Right zone - content area height on right side
    DockZone rightZone;
    rightZone.position = DockPosition::Right;
    rightZone.hitBounds = {cx + offset - iconSize/2, cy - iconSize/2, iconSize, iconSize};
    rightZone.previewBounds = {m_screenWidth * 0.75f, statusBarHeight, m_screenWidth * 0.25f, contentHeight};
    rightZone.targetNode = leaf;
    m_dropZones.push_back(rightZone);

    // Top zone - full width below status bar
    DockZone topZone;
    topZone.position = DockPosition::Top;
    topZone.hitBounds = {cx - iconSize/2, cy - offset - iconSize/2, iconSize, iconSize};
    topZone.previewBounds = {0, statusBarHeight, m_screenWidth, contentHeight * 0.25f};
    topZone.targetNode = leaf;
    m_dropZones.push_back(topZone);

    // Bottom zone - full width at bottom
    DockZone bottomZone;
    bottomZone.position = DockPosition::Bottom;
    bottomZone.hitBounds = {cx - iconSize/2, cy + offset - iconSize/2, iconSize, iconSize};
    bottomZone.previewBounds = {0, m_screenHeight - contentHeight * 0.25f, m_screenWidth, contentHeight * 0.25f};
    bottomZone.targetNode = leaf;
    m_dropZones.push_back(bottomZone);
}

void DockManager::addRootZones() {
    float edgeSize = kEdgeZoneSize;

    // Calculate status bar height
    float statusBarHeight = 0.0f;
    if (m_panelManager && m_panelManager->layoutRoot() &&
        m_panelManager->layoutRoot()->type() == LayoutNodeType::SplitContainer) {
        auto* rootSplit = static_cast<SplitContainer*>(m_panelManager->layoutRoot());
        if (rootSplit->direction() == SplitDirection::Vertical && !rootSplit->isResizable()) {
            statusBarHeight = m_screenHeight * rootSplit->splitRatio();
        }
    }
    float contentHeight = m_screenHeight - statusBarHeight;

    // Left edge - content area only (below status bar)
    DockZone leftZone;
    leftZone.position = DockPosition::Left;
    leftZone.hitBounds = {0, statusBarHeight, edgeSize, contentHeight};
    leftZone.previewBounds = {0, statusBarHeight, m_screenWidth * 0.25f, contentHeight};
    leftZone.targetNode = nullptr;
    leftZone.isRootZone = true;
    m_dropZones.push_back(leftZone);

    // Right edge - content area only (below status bar)
    DockZone rightZone;
    rightZone.position = DockPosition::Right;
    rightZone.hitBounds = {m_screenWidth - edgeSize, statusBarHeight, edgeSize, contentHeight};
    rightZone.previewBounds = {m_screenWidth * 0.75f, statusBarHeight, m_screenWidth * 0.25f, contentHeight};
    rightZone.targetNode = nullptr;
    rightZone.isRootZone = true;
    m_dropZones.push_back(rightZone);

    // Top edge - below status bar
    DockZone topZone;
    topZone.position = DockPosition::Top;
    topZone.hitBounds = {0, statusBarHeight, m_screenWidth, edgeSize};
    topZone.previewBounds = {0, statusBarHeight, m_screenWidth, contentHeight * 0.25f};
    topZone.targetNode = nullptr;
    topZone.isRootZone = true;
    m_dropZones.push_back(topZone);

    // Bottom edge
    DockZone bottomZone;
    bottomZone.position = DockPosition::Bottom;
    bottomZone.hitBounds = {0, m_screenHeight - edgeSize, m_screenWidth, edgeSize};
    bottomZone.previewBounds = {0, m_screenHeight - contentHeight * 0.25f, m_screenWidth, contentHeight * 0.25f};
    bottomZone.targetNode = nullptr;
    bottomZone.isRootZone = true;
    m_dropZones.push_back(bottomZone);
}

void DockManager::hitTestZones(const glm::vec2& pos) {
    m_activeZoneIndex = -1;

    // Reset all zones
    for (auto& zone : m_dropZones) {
        zone.isActive = false;
    }

    // Check if mouse is near screen edges - if so, prioritize root zones
    // This ensures full-height/width docking when user drags to screen edge
    bool nearLeftEdge = pos.x < kEdgeZoneSize;
    bool nearRightEdge = pos.x > m_screenWidth - kEdgeZoneSize;
    bool nearTopEdge = pos.y < kEdgeZoneSize;
    bool nearBottomEdge = pos.y > m_screenHeight - kEdgeZoneSize;
    bool nearEdge = nearLeftEdge || nearRightEdge || nearTopEdge || nearBottomEdge;

    // First pass: if near edge, check root zones first
    if (nearEdge) {
        for (int i = 0; i < static_cast<int>(m_dropZones.size()); ++i) {
            DockZone& zone = m_dropZones[i];
            if (!zone.isRootZone) continue;

            if (pos.x >= zone.hitBounds.x && pos.x <= zone.hitBounds.x + zone.hitBounds.z &&
                pos.y >= zone.hitBounds.y && pos.y <= zone.hitBounds.y + zone.hitBounds.w) {
                zone.isActive = true;
                m_activeZoneIndex = i;
                return;  // Root zone found, use it
            }
        }
    }

    // Second pass: hit-test in reverse order (later zones have priority)
    // This means node center zones take priority over root zones when not near edge
    for (int i = static_cast<int>(m_dropZones.size()) - 1; i >= 0; --i) {
        DockZone& zone = m_dropZones[i];
        if (pos.x >= zone.hitBounds.x && pos.x <= zone.hitBounds.x + zone.hitBounds.z &&
            pos.y >= zone.hitBounds.y && pos.y <= zone.hitBounds.y + zone.hitBounds.w) {
            zone.isActive = true;
            m_activeZoneIndex = i;
            break;
        }
    }
}

const DockZone* DockManager::activeZone() const {
    if (m_activeZoneIndex >= 0 && m_activeZoneIndex < static_cast<int>(m_dropZones.size())) {
        return &m_dropZones[m_activeZoneIndex];
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Dock Execution
// -----------------------------------------------------------------------------

void DockManager::executeDock(const DockZone& zone) {
    Panel* panel = m_dragState.panel;
    if (!panel) return;

    std::cerr << "[DockManager] Execute dock: position=" << static_cast<int>(zone.position) << "\n";

    // Clean up empty nodes BEFORE creating new splits
    // This removes empty space from where the panel was dragged FROM,
    // but won't affect the intentionally empty space we're about to create
    cleanupEmptyNodes();

    if (zone.position == DockPosition::Center) {
        executeDockCenter(zone);
    } else {
        executeDockEdge(zone);
    }

    // Increment layout version since tree structure changed
    if (m_panelManager) {
        m_panelManager->incrementLayoutVersion();
    }

    // Notify callback
    if (m_onPanelDocked) {
        m_onPanelDocked(panel);
    }
}

std::unique_ptr<PanelGroup> DockManager::createDockedGroup(Panel* panel) {
    auto group = std::make_unique<PanelGroup>();
    group->setShowCloseButtons(true);  // Enable close buttons on tab bar
    group->addPanel(panel);

    // Wire up tab drag callback so panels can be re-dragged
    PanelGroup* groupPtr = group.get();
    group->onTabDrag([this, groupPtr](Panel* p, const glm::vec2& pos) {
        beginTabDrag(p, groupPtr, pos);
    });

    return group;
}

void DockManager::executeDockCenter(const DockZone& zone) {
    Panel* panel = m_dragState.panel;
    if (!panel) return;

    if (!zone.targetNode) {
        // Root level - shouldn't happen for center
        executeFloat();
        return;
    }

    // Validate that the target node is still in the layout tree
    // This guards against stale pointers if the tree was modified during drag
    bool nodeStillValid = false;
    if (m_panelManager && m_panelManager->layoutRoot()) {
        traverseLayout(m_panelManager->layoutRoot(), [&](LayoutNode* node) {
            if (node == zone.targetNode) {
                nodeStillValid = true;
            }
        });
    }

    if (!nodeStillValid) {
        std::cerr << "[DockManager] Warning: target node no longer in tree, floating panel instead\n";
        executeFloat();
        return;
    }

    if (zone.targetNode->type() == LayoutNodeType::PanelGroup) {
        // Add as tab to existing group
        auto* group = static_cast<PanelGroup*>(zone.targetNode);
        group->addPanel(panel);
        group->setActivePanel(panel);
        std::cerr << "[DockManager] Added panel as tab to group\n";
    } else if (zone.targetNode->type() == LayoutNodeType::PanelLeaf) {
        // Replace leaf with group containing both panels
        auto* leaf = static_cast<PanelLeaf*>(zone.targetNode);
        Panel* existingPanel = leaf->panel();

        auto group = createDockedGroup(existingPanel);
        group->addPanel(panel);
        group->setActivePanel(panel);

        // Replace the leaf with the group
        replaceInParent(leaf, std::move(group));
        std::cerr << "[DockManager] Replaced leaf with group\n";
    }
}

void DockManager::executeDockEdge(const DockZone& zone) {
    Panel* panel = m_dragState.panel;
    if (!panel || !m_panelManager) return;

    // Create a PanelGroup for the dragged panel (so it can be re-dragged via tab)
    auto newGroup = createDockedGroup(panel);

    LayoutNode* root = m_panelManager->layoutRoot();
    // Determine split direction
    SplitDirection dir = (zone.position == DockPosition::Left || zone.position == DockPosition::Right)
                         ? SplitDirection::Horizontal
                         : SplitDirection::Vertical;

    // Determine if panel goes first or second
    bool panelFirst = (zone.position == DockPosition::Left || zone.position == DockPosition::Top);

    std::cerr << "[DockManager] executeDockEdge: position=" << static_cast<int>(zone.position)
              << " dir=" << (dir == SplitDirection::Horizontal ? "H" : "V")
              << " panelFirst=" << panelFirst << "\n";

    // Create split container
    auto split = std::make_unique<SplitContainer>(dir);
    split->setSplitRatio(panelFirst ? 0.25f : 0.75f);

    std::cerr << "[DockManager] Created split with ratio=" << split->splitRatio() << "\n";

    // All directional docking goes into the content area (below status bar if present)
    // This ensures panels don't overlap the status bar

    std::cerr << "[DockManager] root=" << root
              << " type=" << (root ? static_cast<int>(root->type()) : -1) << "\n";

    // Check if root is a status bar split (non-resizable vertical split with small ratio)
    // We check both !isResizable() and a small ratio to handle older saved layouts
    if (root && root->type() == LayoutNodeType::SplitContainer) {
        auto* rootSplit = static_cast<SplitContainer*>(root);
        std::cerr << "[DockManager] rootSplit dir=" << static_cast<int>(rootSplit->direction())
                  << " resizable=" << rootSplit->isResizable()
                  << " ratio=" << rootSplit->splitRatio() << "\n";

        // Detect status bar split: vertical split that either:
        // 1. Is marked non-resizable (new layouts), OR
        // 2. Has a very small ratio (<0.1) indicating a status bar (older layouts)
        bool isStatusBarSplit = rootSplit->direction() == SplitDirection::Vertical &&
                                (!rootSplit->isResizable() || rootSplit->splitRatio() < 0.1f);

        if (isStatusBarSplit) {
            // Status bar split found - dock into the content area (second child)
            auto contentArea = rootSplit->releaseSecond();
            std::cerr << "[DockManager] Released contentArea=" << contentArea.get() << "\n";

            // If contentArea is null, create an empty group as placeholder
            if (!contentArea) {
                std::cerr << "[DockManager] WARNING: contentArea was null, creating empty group\n";
                contentArea = std::make_unique<PanelGroup>();
            }

            if (panelFirst) {
                split->setFirst(std::move(newGroup));
                split->setSecond(std::move(contentArea));
            } else {
                split->setFirst(std::move(contentArea));
                split->setSecond(std::move(newGroup));
            }

            SplitContainer* newSplit = split.get();  // Keep pointer before move
            rootSplit->setSecond(std::move(split));

            // Force the root split to update layout now that we've modified it
            if (m_screenWidth > 0 && m_screenHeight > 0) {
                rootSplit->setBounds({0, 0, m_screenWidth, m_screenHeight});
                rootSplit->updateLayout();
                if (newSplit->first() && newSplit->second()) {
                    std::cerr << "[DockManager] Updated layout, new split bounds: first=("
                              << newSplit->first()->bounds().x << "," << newSplit->first()->bounds().y << ","
                              << newSplit->first()->bounds().z << "," << newSplit->first()->bounds().w << ") second=("
                              << newSplit->second()->bounds().x << "," << newSplit->second()->bounds().y << ","
                              << newSplit->second()->bounds().z << "," << newSplit->second()->bounds().w << ")\n";
                }
            }

            std::cerr << "[DockManager] Docked into content area below status bar\n";
            return;
        }
    }

    // No status bar - dock at true root level
    auto existingRoot = m_panelManager->releaseLayoutRoot();
    std::cerr << "[DockManager] No status bar split found, wrapping entire root\n";

    if (panelFirst) {
        split->setFirst(std::move(newGroup));
        split->setSecond(std::move(existingRoot));
    } else {
        split->setFirst(std::move(existingRoot));
        split->setSecond(std::move(newGroup));
    }

    m_panelManager->setLayoutRoot(std::move(split));
    std::cerr << "[DockManager] Docked to root (no status bar)\n";
}

void DockManager::executeFloat() {
    Panel* panel = m_dragState.panel;
    if (!panel || !m_panelManager) return;

    // For floating panel drags, the panel's handleDragAndResize already moved it
    // For tab drags, we need to position the panel
    if (m_dragState.type == DragState::Type::FloatingPanel) {
        // Panel is already at correct position, just ensure it's in floating list
        std::cerr << "[DockManager] Floating panel drag ended without dock\n";
        return;
    }

    // Calculate floating position from current drag position
    glm::vec4 bounds = m_dragState.originalBounds;
    bounds.x = m_dragState.currentPos.x - m_dragState.panelOffset.x;
    bounds.y = m_dragState.currentPos.y - m_dragState.panelOffset.y;

    // Ensure minimum size
    bounds.z = std::max(bounds.z, 200.0f);
    bounds.w = std::max(bounds.w, 150.0f);

    // Soft clamp - keep at least 50px visible for grabbing
    constexpr float kMinVisiblePx = 50.0f;
    bounds.x = std::max(-bounds.z + kMinVisiblePx, std::min(bounds.x, m_screenWidth - kMinVisiblePx));
    bounds.y = std::max(-bounds.w + kMinVisiblePx, std::min(bounds.y, m_screenHeight - kMinVisiblePx));

    panel->setBounds(bounds);

    // Clean up empty nodes left behind when panel was removed from layout
    cleanupEmptyNodes();

    // Notify callback
    if (m_onPanelFloated) {
        m_onPanelFloated(panel, glm::vec2(bounds.x, bounds.y));
    }

    std::cerr << "[DockManager] Floated panel at " << bounds.x << ", " << bounds.y << "\n";
}

// -----------------------------------------------------------------------------
// Layout Tree Operations
// -----------------------------------------------------------------------------

bool DockManager::removePanelFromLayout(Panel* panel) {
    if (!panel || !m_panelManager || !m_panelManager->layoutRoot()) return false;

    bool removed = false;

    // Search through layout tree
    traverseLayout(m_panelManager->layoutRoot(), [&](LayoutNode* node) {
        if (removed) return;

        if (node->type() == LayoutNodeType::PanelGroup) {
            auto* group = static_cast<PanelGroup*>(node);
            if (group->containsPanel(panel)) {
                group->removePanel(panel);
                removed = true;
            }
        } else if (node->type() == LayoutNodeType::PanelLeaf) {
            auto* leaf = static_cast<PanelLeaf*>(node);
            if (leaf->panel() == panel) {
                leaf->setPanel(nullptr);
                removed = true;
            }
        }
    });

    return removed;
}

void DockManager::cleanupEmptyNodes() {
    if (!m_panelManager || !m_panelManager->layoutRoot()) return;

    // This is tricky because we can't modify the tree while traversing
    // We'll do multiple passes until no changes
    bool changed = true;
    int maxIterations = 10;
    int totalRemoved = 0;

    while (changed && maxIterations-- > 0) {
        changed = false;

        // Check for empty groups
        std::vector<PanelGroup*> emptyGroups;
        traverseLayout(m_panelManager->layoutRoot(), [&](LayoutNode* node) {
            if (node->type() == LayoutNodeType::PanelGroup) {
                auto* group = static_cast<PanelGroup*>(node);
                if (group->isEmpty()) {
                    emptyGroups.push_back(group);
                }
            }
        });

        // Remove empty groups
        for (auto* group : emptyGroups) {
            bool isFirst;
            SplitContainer* parent = findParent(group, isFirst);
            if (parent) {
                // Don't collapse non-resizable splits (e.g., status bar split)
                // These are structural and should remain even when one side is empty
                if (!parent->isResizable()) {
                    continue;
                }

                // Replace parent with the other child
                auto otherChild = isFirst ? parent->releaseSecond() : parent->releaseFirst();
                replaceInParent(parent, std::move(otherChild));
                changed = true;
                ++totalRemoved;
            }
        }

        // Check for empty leaves
        std::vector<PanelLeaf*> emptyLeaves;
        traverseLayout(m_panelManager->layoutRoot(), [&](LayoutNode* node) {
            if (node->type() == LayoutNodeType::PanelLeaf) {
                auto* leaf = static_cast<PanelLeaf*>(node);
                if (!leaf->panel()) {
                    emptyLeaves.push_back(leaf);
                }
            }
        });

        // Remove empty leaves
        for (auto* leaf : emptyLeaves) {
            bool isFirst;
            SplitContainer* parent = findParent(leaf, isFirst);
            if (parent) {
                // Don't collapse non-resizable splits (e.g., status bar split)
                if (!parent->isResizable()) {
                    continue;
                }
                auto otherChild = isFirst ? parent->releaseSecond() : parent->releaseFirst();
                replaceInParent(parent, std::move(otherChild));
                changed = true;
                ++totalRemoved;
            }
        }
    }

    if (totalRemoved > 0) {
        gui::logDebug("DockManager", "cleaned up empty nodes",
                     std::to_string(totalRemoved) + " removed");
        // Increment layout version since tree structure changed
        if (m_panelManager) {
            m_panelManager->incrementLayoutVersion();
        }
    }
}

TreeSlot DockManager::captureTreeSlot(Panel* panel) {
    TreeSlot slot;
    if (!panel || !m_panelManager || !m_panelManager->layoutRoot()) return slot;

    // Search for panel in layout tree
    traverseLayout(m_panelManager->layoutRoot(), [&](LayoutNode* node) {
        if (slot.isValid()) return; // Already found

        if (node->type() == LayoutNodeType::PanelGroup) {
            auto* group = static_cast<PanelGroup*>(node);
            for (size_t i = 0; i < group->panelCount(); ++i) {
                if (group->panelAt(i) == panel) {
                    slot.type = TreeSlot::Type::Group;
                    slot.groupId = group->id();
                    slot.tabIndex = static_cast<int>(i);
                    return;
                }
            }
        } else if (node->type() == LayoutNodeType::PanelLeaf) {
            auto* leaf = static_cast<PanelLeaf*>(node);
            if (leaf->panel() == panel) {
                slot.type = TreeSlot::Type::Leaf;

                // Find parent to get split info
                bool isFirst;
                SplitContainer* parent = findParent(leaf, isFirst);
                if (parent) {
                    slot.splitInfo.direction = static_cast<int>(parent->direction());
                    slot.splitInfo.ratio = parent->splitRatio();
                    slot.splitInfo.wasFirst = isFirst;

                    // Get sibling panel ID
                    LayoutNode* sibling = isFirst ? parent->second() : parent->first();
                    if (sibling) {
                        std::vector<Panel*> siblingPanels;
                        sibling->collectPanels(siblingPanels);
                        if (!siblingPanels.empty()) {
                            slot.splitInfo.siblingId = siblingPanels[0]->config().id;
                        }
                    }
                }
            }
        }
    });

    if (slot.isValid()) {
        // Capture layout version for later validation
        slot.captureVersion = m_panelManager->layoutVersion();
        gui::logDebug("TreeSlot", "captured slot",
                     panel->config().id + " type=" +
                     (slot.type == TreeSlot::Type::Group ? "Group" : "Leaf") +
                     " version=" + std::to_string(slot.captureVersion));
    } else {
        gui::logDebug("TreeSlot", "no slot found for", panel->config().id);
    }

    return slot;
}

bool DockManager::restoreFromTreeSlot(Panel* panel, const TreeSlot& slot) {
    if (!panel || !slot.isValid() || !m_panelManager) return false;

    gui::logDebug("TreeSlot", "attempting restore",
                 panel->config().id + " to " + slot.groupId);

    // Check if layout version has changed since slot was captured
    int currentVersion = m_panelManager->layoutVersion();
    if (slot.captureVersion != currentVersion) {
        gui::logTransition("TreeSlot", "restore", "version-mismatch",
                          (panel->config().id + " captured=" + std::to_string(slot.captureVersion) +
                           " current=" + std::to_string(currentVersion)).c_str());
        // Version mismatch - the layout may have changed, try restoration anyway but log warning
        // The group validation below will catch if the target no longer exists
    }

    if (slot.type == TreeSlot::Type::Group) {
        // Find group by ID and insert panel at original tab index
        PanelGroup* group = findGroupById(slot.groupId);
        if (!group) {
            gui::logTransition("TreeSlot", "restore", "group-not-found", slot.groupId.c_str());
            return false;  // Caller floats panel as fallback
        }

        if (group->isEmpty()) {
            gui::logDebug("TreeSlot", "group is empty, skipping restore", slot.groupId);
            return false;  // Don't restore to empty groups
        }

        // Only restore to groups that have other panels (active tab groups)
        // Don't restore to empty groups that were preserved structurally -
        // those panels should float instead so they have title bars and can be dragged
        group->addPanel(panel);
        gui::logTransition("TreeSlot", "hidden", "restored", panel->config().id.c_str());
        // Try to restore tab position
        // (Note: addPanel adds to end, we'd need insertPanel at index for exact restoration)
        return true;
    } else if (slot.type == TreeSlot::Type::Leaf) {
        // Find sibling and recreate split
        Panel* siblingPanel = m_panelManager->getPanel(slot.splitInfo.siblingId);
        if (siblingPanel) {
            // Find sibling in tree and create split next to it
            // For now, just add as floating - full restoration would be complex
            gui::logDebug("TreeSlot", "Leaf restoration not fully implemented", panel->config().id);
        }
    }

    gui::logTransition("TreeSlot", "restore", "failed", panel->config().id.c_str());
    return false;
}

void DockManager::dockPanelProgrammatically(Panel* panel, DockPosition position) {
    if (!panel || !m_panelManager) return;

    const std::string& panelId = panel->config().id;
    std::cerr << "[DockManager] dockPanelProgrammatically: panel=" << panelId
              << " position=" << static_cast<int>(position) << "\n";

    // Handle float request
    if (position == DockPosition::Float) {
        // Remove from layout if present
        removePanelFromLayout(panel);
        cleanupEmptyNodes();

        // Set as floating
        glm::vec4 bounds = panel->bounds();
        if (bounds.z < 100 || bounds.w < 100) {
            // Set reasonable default size
            bounds.z = 400;
            bounds.w = 300;
        }
        panel->setBounds(bounds);
        m_panelManager->addToFloatingOrder(panelId);
        return;
    }

    // Remove panel from its current position first
    removePanelFromLayout(panel);
    m_panelManager->removeFromFloatingOrder(panelId);
    cleanupEmptyNodes();

    // Set up temporary drag state so we can reuse executeDock methods
    m_dragState.reset();
    m_dragState.panel = panel;
    m_dragState.type = DragState::Type::Tab;
    m_dragState.isDragging = true;

    if (position == DockPosition::Center) {
        // For center, we need a target node. Find the first PanelGroup in the layout
        LayoutNode* targetGroup = nullptr;
        traverseLayout(m_panelManager->layoutRoot(), [&](LayoutNode* node) {
            if (!targetGroup && node->type() == LayoutNodeType::PanelGroup) {
                targetGroup = node;
            }
        });

        if (targetGroup) {
            DockZone zone;
            zone.position = DockPosition::Center;
            zone.targetNode = targetGroup;
            executeDockCenter(zone);
        } else {
            // No existing group - create one as root content
            auto group = createDockedGroup(panel);

            // Check for status bar split
            LayoutNode* root = m_panelManager->layoutRoot();
            if (root && root->type() == LayoutNodeType::SplitContainer) {
                auto* rootSplit = static_cast<SplitContainer*>(root);
                if (rootSplit->direction() == SplitDirection::Vertical && !rootSplit->isResizable()) {
                    auto contentArea = rootSplit->releaseSecond();
                    // We'd need to wrap with content, but for now just use the group
                    rootSplit->setSecond(std::move(group));
                    std::cerr << "[DockManager] Center dock: replaced content with group\n";
                } else {
                    m_panelManager->setLayoutRoot(std::move(group));
                }
            } else {
                m_panelManager->setLayoutRoot(std::move(group));
            }
        }
    } else {
        // Edge docking (Left, Right, Top, Bottom)
        DockZone zone;
        zone.position = position;
        zone.isRootZone = true;  // Always dock at root level for programmatic docking
        zone.targetNode = nullptr;  // executeDockEdge handles root-level docking
        executeDockEdge(zone);
    }

    // Reset drag state
    m_dragState.reset();

    // Force immediate layout update to get correct bounds
    if (m_panelManager && m_panelManager->layoutRoot()) {
        // Use screen dimensions if available
        if (m_screenWidth > 0 && m_screenHeight > 0) {
            m_panelManager->layoutRoot()->setBounds({0, 0, m_screenWidth, m_screenHeight});
            m_panelManager->layoutRoot()->updateLayout();
        }
    }

    // Notify callback
    if (m_onPanelDocked) {
        m_onPanelDocked(panel);
    }
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

void DockManager::renderGuides(OverlayCanvas& canvas, const UIStyle& style) {
    if (!m_dragState.isActive()) return;

    canvas.setLayer(UILayer::Tooltips + 100); // Above everything

    // Render preview highlight for active zone
    renderPreview(canvas, style);

    // Render root edge indicators (for full-height/width docking)
    renderRootEdgeIndicators(canvas, style);

    // Render dock guide icons for hovered panel
    renderGuideIcons(canvas, style);

    // Render dragged panel outline
    renderDraggedPanel(canvas, style);
}

void DockManager::renderGuideIcons(OverlayCanvas& canvas, const UIStyle& style) {
    // Find which node is under the cursor and render dock icons for it
    LayoutNode* hoveredNode = nullptr;
    glm::vec2 mousePos = m_dragState.currentPos;

    // Find the most specific (smallest) node containing the cursor
    if (m_panelManager && m_panelManager->layoutRoot()) {
        LayoutNode* candidate = nullptr;
        float candidateArea = std::numeric_limits<float>::max();

        traverseLayout(m_panelManager->layoutRoot(), [&](LayoutNode* node) {
            // Skip SplitContainers - we want PanelGroups or PanelLeafs
            if (node->type() == LayoutNodeType::SplitContainer) return;

            // Skip the panel being dragged
            if (node->type() == LayoutNodeType::PanelLeaf) {
                auto* leaf = static_cast<PanelLeaf*>(node);
                if (leaf->panel() == m_dragState.panel) return;
            }
            if (node->type() == LayoutNodeType::PanelGroup) {
                auto* group = static_cast<PanelGroup*>(node);
                if (group->containsPanel(m_dragState.panel)) return;
            }

            glm::vec4 bounds = node->bounds();
            if (mousePos.x >= bounds.x && mousePos.x <= bounds.x + bounds.z &&
                mousePos.y >= bounds.y && mousePos.y <= bounds.y + bounds.w) {
                float area = bounds.z * bounds.w;
                if (area < candidateArea) {
                    candidate = node;
                    candidateArea = area;
                }
            }
        });

        hoveredNode = candidate;
    }

    if (!hoveredNode) return;

    glm::vec4 bounds = hoveredNode->bounds();
    float cx = bounds.x + bounds.z / 2;
    float cy = bounds.y + bounds.w / 2;

    // Get active zone for highlighting
    const DockZone* zone = activeZone();
    DockPosition activePos = zone ? zone->position : DockPosition::None;
    LayoutNode* activeTarget = zone ? zone->targetNode : nullptr;

    // Icon colors from style
    glm::vec4 iconBg = style.dockGuideIcon;
    glm::vec4 iconBgActive = style.dockGuideIconActive;
    glm::vec4 iconFg = style.dockGuideIconFg;

    float iconSize = kGuideIconSize;
    float spacing = kGuideSpacing;
    float offset = iconSize + spacing;

    // Helper to draw an icon
    auto drawIcon = [&](float x, float y, DockPosition pos, const char* symbol) {
        // Only highlight if this is the active zone AND the target matches
        bool active = (activePos == pos && activeTarget == hoveredNode);
        glm::vec4 bg = active ? iconBgActive : iconBg;
        canvas.fillRoundedRect(x - iconSize/2, y - iconSize/2, iconSize, iconSize, 4.0f, bg);
        canvas.text(symbol, x - 4, y + 5, iconFg, 0);
    };

    // Center icon (tab)
    drawIcon(cx, cy, DockPosition::Center, "+");

    // Directional icons
    drawIcon(cx - offset, cy, DockPosition::Left, "<");
    drawIcon(cx + offset, cy, DockPosition::Right, ">");
    drawIcon(cx, cy - offset, DockPosition::Top, "^");
    drawIcon(cx, cy + offset, DockPosition::Bottom, "v");
}

void DockManager::renderRootEdgeIndicators(OverlayCanvas& canvas, const UIStyle& style) {
    // Render edge zone indicators at screen borders
    // These show where to drop for full-height/width root-level docking

    const DockZone* zone = activeZone();
    DockPosition activePos = zone && zone->isRootZone ? zone->position : DockPosition::None;

    glm::vec4 iconBg = style.dockGuideIcon;
    glm::vec4 iconBgActive = style.dockGuideIconActive;
    glm::vec4 iconFg = style.dockGuideIconFg;

    float iconSize = kGuideIconSize;
    float edgePadding = 16.0f;  // Enough padding to keep icons fully visible

    // Calculate status bar height for top edge positioning
    float statusBarHeight = 0.0f;
    if (m_panelManager && m_panelManager->layoutRoot() &&
        m_panelManager->layoutRoot()->type() == LayoutNodeType::SplitContainer) {
        auto* rootSplit = static_cast<SplitContainer*>(m_panelManager->layoutRoot());
        if (rootSplit->direction() == SplitDirection::Vertical && !rootSplit->isResizable()) {
            statusBarHeight = m_screenHeight * rootSplit->splitRatio();
        }
    }

    auto drawEdgeIcon = [&](float x, float y, DockPosition pos, const char* symbol) {
        bool active = (activePos == pos);
        glm::vec4 bg = active ? iconBgActive : iconBg;
        bg.a *= 0.8f;  // Slightly more transparent for edge icons
        canvas.fillRoundedRect(x - iconSize/2, y - iconSize/2, iconSize, iconSize, 4.0f, bg);
        canvas.text(symbol, x - 4, y + 5, iconFg, 0);
    };

    // Left edge - centered vertically in content area
    drawEdgeIcon(edgePadding + iconSize/2, statusBarHeight + (m_screenHeight - statusBarHeight) / 2, DockPosition::Left, "<");

    // Right edge - centered vertically in content area
    drawEdgeIcon(m_screenWidth - edgePadding - iconSize/2, statusBarHeight + (m_screenHeight - statusBarHeight) / 2, DockPosition::Right, ">");

    // Top edge - below status bar, centered horizontally
    drawEdgeIcon(m_screenWidth / 2, statusBarHeight + edgePadding + iconSize/2, DockPosition::Top, "^");

    // Bottom edge - centered horizontally
    drawEdgeIcon(m_screenWidth / 2, m_screenHeight - edgePadding - iconSize/2, DockPosition::Bottom, "v");
}

void DockManager::renderPreview(OverlayCanvas& canvas, const UIStyle& style) {
    const DockZone* zone = activeZone();
    if (!zone) return;

    // Preview colors from style
    glm::vec4 previewColor = style.dockPreview;
    previewColor.a = kPreviewAlpha;  // Apply standard alpha
    glm::vec4 previewBorder = style.dockPreviewBorder;

    canvas.fillRect(zone->previewBounds.x, zone->previewBounds.y,
                   zone->previewBounds.z, zone->previewBounds.w, previewColor);
    canvas.strokeRect(zone->previewBounds.x, zone->previewBounds.y,
                     zone->previewBounds.z, zone->previewBounds.w, 2.0f, previewBorder);
}

void DockManager::renderDraggedPanel(OverlayCanvas& canvas, const UIStyle& style) {
    if (!m_dragState.panel) return;

    // For floating panel drags, the actual panel is rendered and moved by PanelManager
    // Only show outline for tab drags where the panel is detached
    if (m_dragState.type == DragState::Type::FloatingPanel) {
        return;
    }

    // Draw outline at current drag position
    glm::vec4 bounds = m_dragState.originalBounds;
    bounds.x = m_dragState.currentPos.x - m_dragState.panelOffset.x;
    bounds.y = m_dragState.currentPos.y - m_dragState.panelOffset.y;

    glm::vec4 outlineColor(1.0f, 1.0f, 1.0f, 0.5f);
    canvas.strokeRect(bounds.x, bounds.y, bounds.z, bounds.w, 2.0f, outlineColor);

    // Draw panel title
    const std::string& title = m_dragState.panel->config().title;
    glm::vec4 titleBg(0.15f, 0.15f, 0.2f, 0.9f);
    glm::vec4 titleFg(1.0f, 1.0f, 1.0f, 1.0f);

    canvas.fillRect(bounds.x, bounds.y, bounds.z, 28.0f, titleBg);
    canvas.text(title, bounds.x + 8, bounds.y + 20, titleFg, 0);
}

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------

SplitContainer* DockManager::findParent(LayoutNode* node, bool& outIsFirst) {
    if (!m_panelManager || !m_panelManager->layoutRoot()) return nullptr;

    SplitContainer* result = nullptr;

    traverseLayout(m_panelManager->layoutRoot(), [&](LayoutNode* current) {
        if (result) return;

        if (current->type() == LayoutNodeType::SplitContainer) {
            auto* split = static_cast<SplitContainer*>(current);
            if (split->first() == node) {
                result = split;
                outIsFirst = true;
            } else if (split->second() == node) {
                result = split;
                outIsFirst = false;
            }
        }
    });

    return result;
}

void DockManager::replaceInParent(LayoutNode* oldNode, std::unique_ptr<LayoutNode> newNode) {
    if (!m_panelManager) return;

    // Check if oldNode is root
    if (m_panelManager->layoutRoot() == oldNode) {
        m_panelManager->setLayoutRoot(std::move(newNode));
        return;
    }

    // Find parent
    bool isFirst;
    SplitContainer* parent = findParent(oldNode, isFirst);
    if (parent) {
        if (isFirst) {
            parent->releaseFirst(); // Release old
            parent->setFirst(std::move(newNode));
        } else {
            parent->releaseSecond();
            parent->setSecond(std::move(newNode));
        }
    }
}

void DockManager::traverseLayout(LayoutNode* node, std::function<void(LayoutNode*)> visitor) {
    if (!node) return;

    visitor(node);

    if (node->type() == LayoutNodeType::SplitContainer) {
        auto* split = static_cast<SplitContainer*>(node);
        traverseLayout(split->first(), visitor);
        traverseLayout(split->second(), visitor);
    }
}

PanelGroup* DockManager::findGroupById(const std::string& id) {
    if (!m_panelManager || !m_panelManager->layoutRoot()) return nullptr;

    PanelGroup* result = nullptr;

    traverseLayout(m_panelManager->layoutRoot(), [&](LayoutNode* node) {
        if (result) return;

        if (node->type() == LayoutNodeType::PanelGroup) {
            auto* group = static_cast<PanelGroup*>(node);
            if (group->id() == id) {
                result = group;
            }
        }
    });

    return result;
}

} // namespace vivid
