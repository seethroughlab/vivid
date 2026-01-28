// PanelManager implementation
// Manages layout and focus for devtools panels

#include <vivid/devtools/panel_manager.h>
#include <vivid/devtools/panel_group.h>
#include <vivid/devtools/split_container.h>
#include <vivid/devtools/panel_leaf.h>
#include <vivid/devtools/dock_manager.h>
#include <vivid/context.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace vivid {

PanelManager::PanelManager()
    : m_dockManager(std::make_unique<DockManager>(this)) {
}

PanelManager::~PanelManager() {
    shutdown();
}

bool PanelManager::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    if (m_initialized) return true;

    m_ctx = &ctx;
    m_surfaceFormat = surfaceFormat;

    // Initialize all registered panels
    for (auto& panel : m_panels) {
        if (!panel->init(ctx, surfaceFormat)) {
            std::cerr << "[PanelManager] Failed to initialize panel: "
                      << panel->config().id << "\n";
            return false;
        }
    }

    // Set up dock manager callbacks
    if (m_dockManager) {
        m_dockManager->onPanelFloated([this](Panel* panel, const glm::vec2& pos) {
            floatPanel(panel, pos);
            // Reset interaction state so panel can be dragged again
            panel->resetInteractionState();
        });
        m_dockManager->onPanelDocked([this](Panel* panel) {
            // Remove from floating order if it was floating
            removeFromFloatingOrder(panel->config().id);
            // Reset interaction state so isInteracting() returns false
            panel->resetInteractionState();
        });
    }

    m_initialized = true;
    return true;
}

void PanelManager::shutdown() {
    if (!m_initialized) return;

    // Shutdown all panels
    for (auto& panel : m_panels) {
        panel->shutdown();
    }

    m_panels.clear();
    m_panelMap.clear();
    m_focusedPanelId.clear();
    m_initialized = false;
}

void PanelManager::update() {
    for (auto& panel : m_panels) {
        if (panel->isVisible()) {
            panel->update();
        }
    }
}

void PanelManager::render(OverlayCanvas& canvas, const FrameInput& input,
                           float screenWidth, float screenHeight, const UIStyle& style) {
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;

    // Track if any panel consumed input
    m_consumedInput = false;

    if (m_layoutMode && m_layoutRoot) {
        renderLayoutMode(canvas, input, scale, style);
    } else {
        renderFlatMode(canvas, input, scale, style);
    }

    // Update focus based on interactions
    // If a panel is being interacted with (dragging/resizing), focus it and bring to front
    for (auto& panel : m_panels) {
        if (panel->isInteracting()) {
            const std::string& id = panel->config().id;
            if (id != m_focusedPanelId) {
                setFocus(id);  // setFocus already calls bringToFront for floating panels
            }
            return;  // Don't process further focus changes while interacting
        }
    }

    // If a panel was clicked and it's not the focused one, change focus
    // Check panels in z-order (front to back) to only process the topmost one
    // Use mouseClicked from FrameInput (computed once per frame in app.cpp)
    if (input.mouseClicked[0]) {
        // Find which panel to focus (don't modify z-order while searching)
        std::string panelToFocus;

        // Check floating panels first (in reverse z-order = front to back)
        // Make a copy to avoid iterator invalidation
        std::vector<std::string> floatingCopy = m_floatingZOrder;
        for (auto it = floatingCopy.rbegin(); it != floatingCopy.rend(); ++it) {
            Panel* panel = getPanel(*it);
            if (panel && panel->isVisible() && panel->isHovered()) {
                if (*it != m_focusedPanelId) {
                    panelToFocus = *it;
                }
                break;  // Found the topmost panel, don't check others
            }
        }

        // If no floating panel, check docked panels
        if (panelToFocus.empty()) {
            for (auto& panel : m_panels) {
                if (panel->config().dockSide == DockSide::None) continue;  // Skip floating
                if (panel->isVisible() && panel->isHovered()) {
                    const std::string& id = panel->config().id;
                    if (id != m_focusedPanelId) {
                        panelToFocus = id;
                    }
                    break;
                }
            }
        }

        // Now set focus (safe to modify z-order)
        if (!panelToFocus.empty()) {
            setFocus(panelToFocus);
        }
    }
}

std::string PanelManager::determineInputTarget(const FrameInput& input) {
    // 1. Panel currently being dragged/resized owns input
    for (auto& panel : m_panels) {
        if (panel->isInteracting()) {
            return panel->config().id;
        }
    }

    // 2. Check floating panels front-to-back (reverse z-order)
    for (auto it = m_floatingZOrder.rbegin(); it != m_floatingZOrder.rend(); ++it) {
        Panel* panel = getPanel(*it);
        if (!panel || !panel->isVisible()) continue;

        glm::vec4 bounds = panel->bounds();
        float hitSize = 8.0f;
        bool hit = input.mousePos.x >= bounds.x - hitSize &&
                   input.mousePos.x <= bounds.x + bounds.z + hitSize &&
                   input.mousePos.y >= bounds.y - hitSize &&
                   input.mousePos.y <= bounds.y + bounds.w + hitSize;

        if (hit) {
            return *it;
        }
    }

    // 3. Check docked panels
    for (auto& panel : m_panels) {
        if (panel->config().dockSide == DockSide::None) continue;
        if (!panel->isVisible()) continue;

        glm::vec4 bounds = panel->bounds();
        if (input.mousePos.x >= bounds.x && input.mousePos.x <= bounds.x + bounds.z &&
            input.mousePos.y >= bounds.y && input.mousePos.y <= bounds.y + bounds.w) {
            return panel->config().id;
        }
    }

    return "";  // No panel at mouse position
}

void PanelManager::renderFlatMode(OverlayCanvas& canvas, const FrameInput& input, float scale, const UIStyle& style) {
    // Calculate layout for docked panels
    calculateLayout(m_screenWidth, m_screenHeight);

    // Pre-pass: determine which panel owns input this frame
    std::string inputTarget = determineInputTarget(input);

    // Set interaction and ownership flags on all panels
    for (auto& panel : m_panels) {
        const std::string& id = panel->config().id;
        bool ownsInput = (id == inputTarget);

        // Set input ownership for the new model
        panel->setInputOwnership(ownsInput);

        // Also set canStartInteraction for backwards compatibility
        bool canStart = ownsInput || panel->isInteracting();
        panel->setCanStartInteraction(canStart);
    }

    // Collect docked panels
    std::vector<Panel*> dockedPanels;
    for (auto& panel : m_panels) {
        if (!panel->isVisible()) continue;
        if (panel->config().dockSide != DockSide::None) {
            dockedPanels.push_back(panel.get());
        }
    }

    // Set content scale on canvas for HiDPI support
    canvas.setContentScale(scale);

    // Render docked panels at Panels layer
    // All coordinates are in logical pixels - canvas handles scaling
    canvas.setLayer(UILayer::Panels);
    for (auto* panel : dockedPanels) {
        panel->render(canvas, panel->bounds(), input, style);
        // Call handleInput for panels that own input or are interacting
        if (panel->ownsInput() || panel->isInteracting()) {
            if (panel->handleInput(input)) {
                m_consumedInput = true;
            }
        }
        if (panel->consumedInput()) {
            m_consumedInput = true;
        }
    }

    // Render floating panels in z-order (back to front)
    // Each panel gets its own layer to prevent content overlap
    int floatingLayer = UILayer::FloatingPanels;
    for (const std::string& id : m_floatingZOrder) {
        Panel* panel = getPanel(id);
        if (!panel) {
            std::cerr << "[PanelManager] Floating panel '" << id << "' not found!\n";
            continue;
        }
        if (!panel->isVisible()) {
            if (input.mouseClicked[0]) {
                std::cerr << "[PanelManager] Floating panel '" << id << "' is NOT visible\n";
            }
            continue;
        }

        // Floating panels always show their title bar
        panel->setShowTitleBar(true);

        canvas.setLayer(floatingLayer);
        panel->render(canvas, panel->bounds(), input, style);
        // Call handleInput for panels that own input or are interacting
        if (panel->ownsInput() || panel->isInteracting()) {
            if (panel->handleInput(input)) {
                m_consumedInput = true;
            }
        }
        if (panel->consumedInput()) {
            m_consumedInput = true;
        }
        floatingLayer += 5;  // Each panel gets its own layer
    }

    // Reset layer
    canvas.setLayer(0);
}

void PanelManager::renderLayoutMode(OverlayCanvas& canvas, const FrameInput& input, float scale, const UIStyle& style) {
    // Set content scale on canvas for HiDPI support
    canvas.setContentScale(scale);

    // Ensure floating panels have showTitleBar=true BEFORE input determination
    // This is important because determineInputTarget() checks panel->bounds() which
    // only gets updated correctly when showTitleBar is true
    for (const std::string& id : m_floatingZOrder) {
        if (Panel* panel = getPanel(id)) {
            panel->setShowTitleBar(true);
        }
    }

    // Check if a floating panel started dragging (for dock-on-drop)
    // Only start dock drag if mouse is still down - panel->isInteracting() may be stale
    // from the previous frame if mouse was just released
    if (m_dockManager && !m_dockManager->isDragging() && input.mouseDown[0]) {
        for (const std::string& id : m_floatingZOrder) {
            Panel* panel = getPanel(id);
            if (panel && panel->isVisible() && panel->isInteracting()) {
                // Panel just started dragging - begin floating panel dock drag
                m_dockManager->beginFloatingPanelDrag(panel, input.mousePos);
                break;
            }
        }
    }

    // Update dock manager (must happen before layout to handle drag state)
    if (m_dockManager) {
        m_dockManager->update(input);
    }

    // Update layout tree bounds (logical pixels)
    m_layoutRoot->setBounds({0, 0, m_screenWidth, m_screenHeight});
    m_layoutRoot->updateLayout();

    // Set interaction flags on all panels in layout mode
    // In layout mode, the layout nodes handle determining which panel owns input
    // Skip if dock manager is handling a drag
    bool dockDragging = m_dockManager && m_dockManager->isDragging();

    std::string inputTarget = dockDragging ? "" : determineInputTarget(input);
    for (auto& panel : m_panels) {
        const std::string& id = panel->config().id;
        bool ownsInput = (id == inputTarget);
        panel->setInputOwnership(ownsInput);
        bool canStart = ownsInput || panel->isInteracting();
        panel->setCanStartInteraction(canStart);
    }

    // Handle input through layout tree (skip if dock dragging)
    if (!dockDragging && m_layoutRoot->handleInput(input)) {
        m_consumedInput = true;
    }

    // Render layout tree (all coordinates in logical pixels)
    m_layoutRoot->render(canvas, input, style);

    // Render floating panels in z-order (back to front)
    // These are panels that have been dragged out of the layout tree
    int floatingLayer = UILayer::FloatingPanels;
    for (const std::string& id : m_floatingZOrder) {
        Panel* panel = getPanel(id);
        if (!panel || !panel->isVisible()) continue;

        // For tab drags, the panel is rendered as an outline by dock manager
        // For floating panel drags, we still render the actual panel (it moves with the mouse)
        if (dockDragging && m_dockManager->dragState().panel == panel &&
            m_dockManager->dragState().type != DragState::Type::FloatingPanel) continue;

        // Floating panels always show their title bar (may have been turned off when docked)
        panel->setShowTitleBar(true);

        canvas.setLayer(floatingLayer);
        panel->render(canvas, panel->bounds(), input, style);

        // Handle input for floating panels
        if (panel->ownsInput() || panel->isInteracting()) {
            if (panel->handleInput(input)) {
                m_consumedInput = true;
            }
        }
        if (panel->consumedInput()) {
            m_consumedInput = true;
        }
        floatingLayer += 5;
    }

    // Render dock guides on top of everything
    if (m_dockManager) {
        m_dockManager->renderGuides(canvas, style);
    }

    // Reset layer
    canvas.setLayer(0);

    if (m_layoutRoot->isInteracting()) {
        m_consumedInput = true;
    }
    if (dockDragging) {
        m_consumedInput = true;
    }
}

void PanelManager::addPanel(std::unique_ptr<Panel> panel) {
    const std::string& id = panel->config().id;

    // Track floating panels in z-order
    if (panel->config().dockSide == DockSide::None) {
        m_floatingZOrder.push_back(id);
    }

    // Set up close button callback to hide the panel
    panel->setOnClose([this](Panel* p) {
        hidePanel(p->config().id);
    });

    m_panelMap[id] = panel.get();
    m_panels.push_back(std::move(panel));

    // Initialize if we're already initialized
    if (m_initialized && m_ctx) {
        Panel* p = m_panels.back().get();
        if (!p->init(*m_ctx, m_surfaceFormat)) {
            std::cerr << "[PanelManager] Failed to initialize panel: " << id << "\n";
        }
    }
}

void PanelManager::bringToFront(const std::string& id) {
    // Remove from current position and add to end (front)
    auto it = std::find(m_floatingZOrder.begin(), m_floatingZOrder.end(), id);
    if (it != m_floatingZOrder.end()) {
        m_floatingZOrder.erase(it);
        m_floatingZOrder.push_back(id);
    }
}

Panel* PanelManager::getPanel(const std::string& id) {
    auto it = m_panelMap.find(id);
    return it != m_panelMap.end() ? it->second : nullptr;
}

const Panel* PanelManager::getPanel(const std::string& id) const {
    auto it = m_panelMap.find(id);
    return it != m_panelMap.end() ? it->second : nullptr;
}

void PanelManager::showPanel(const std::string& id) {
    if (Panel* p = getPanel(id)) {
        showPanelFromSlot(id);
    }
}

void PanelManager::hidePanel(const std::string& id) {
    if (Panel* p = getPanel(id)) {
        hidePanelWithSlot(id, true);
    }
}

void PanelManager::togglePanel(const std::string& id) {
    if (Panel* p = getPanel(id)) {
        if (p->isVisible()) {
            // When hiding, use hidePanelWithSlot to properly remove from layout
            hidePanelWithSlot(id, true);
        } else {
            // When showing, use showPanelFromSlot to restore position
            showPanelFromSlot(id);
        }
    }
}

bool PanelManager::isPanelVisible(const std::string& id) const {
    if (const Panel* p = getPanel(id)) {
        return p->isVisible();
    }
    return false;
}

void PanelManager::setFocus(const std::string& id) {
    // Clear focus from previous panel
    if (!m_focusedPanelId.empty()) {
        if (Panel* prev = getPanel(m_focusedPanelId)) {
            prev->setFocused(false);
        }
    }

    // Set focus to new panel
    m_focusedPanelId = id;
    if (!id.empty()) {
        if (Panel* p = getPanel(id)) {
            p->setFocused(true);
            // Bring floating panels to front when focused
            if (p->config().dockSide == DockSide::None) {
                bringToFront(id);
            }
        }
    }
}

Panel* PanelManager::focusedPanel() {
    return getPanel(m_focusedPanelId);
}

void PanelManager::onChar(uint32_t codepoint) {
    if (Panel* p = focusedPanel()) {
        p->onChar(codepoint);
    }
}

void PanelManager::onKeyDown(int key, int mods) {
    if (Panel* p = focusedPanel()) {
        p->onKeyDown(key, mods);
    }
}

bool PanelManager::isInteracting() const {
    for (const auto& panel : m_panels) {
        if (panel->isInteracting()) {
            return true;
        }
    }
    return false;
}

void PanelManager::calculateLayout(float screenWidth, float screenHeight) {
    // Calculate available space after docked panels
    float leftEdge = 0;
    float rightEdge = screenWidth;
    float topEdge = 0;
    float bottomEdge = screenHeight;

    // First pass: calculate space taken by docked panels
    for (auto& panel : m_panels) {
        if (!panel->isVisible()) continue;

        switch (panel->config().dockSide) {
            case DockSide::Left:
                leftEdge = std::max(leftEdge, panel->bounds().z);
                break;
            case DockSide::Right:
                rightEdge -= panel->bounds().z;
                break;
            case DockSide::Top:
                topEdge = std::max(topEdge, panel->bounds().w);
                break;
            case DockSide::Bottom:
                bottomEdge -= panel->bounds().w;
                break;
            default:
                break;
        }
    }

    // Second pass: update panel bounds based on dock position
    for (auto& panel : m_panels) {
        if (!panel->isVisible()) continue;

        glm::vec4 bounds = panel->bounds();

        switch (panel->config().dockSide) {
            case DockSide::Left:
                bounds.x = 0;
                bounds.y = topEdge;
                bounds.w = bottomEdge - topEdge;
                break;
            case DockSide::Right:
                bounds.x = rightEdge;
                bounds.y = topEdge;
                bounds.w = bottomEdge - topEdge;
                break;
            case DockSide::Top:
                bounds.x = leftEdge;
                bounds.y = 0;
                bounds.z = rightEdge - leftEdge;
                break;
            case DockSide::Bottom:
                bounds.x = leftEdge;
                bounds.y = bottomEdge;
                bounds.z = rightEdge - leftEdge;
                break;
            case DockSide::Fill:
                bounds.x = leftEdge;
                bounds.y = topEdge;
                bounds.z = rightEdge - leftEdge;
                bounds.w = bottomEdge - topEdge;
                break;
            default:
                // Floating - no layout changes
                break;
        }

        panel->setBounds(bounds);
    }
}

void PanelManager::setLayoutRoot(std::unique_ptr<LayoutNode> root) {
    m_layoutRoot = std::move(root);
    // Set bounds immediately so updateLayout() works correctly
    if (m_layoutRoot && m_screenWidth > 0 && m_screenHeight > 0) {
        m_layoutRoot->setBounds({0, 0, m_screenWidth, m_screenHeight});
        m_layoutRoot->updateLayout();
    }
}

void PanelManager::buildDefaultLayout() {
    // Find panels by dock position
    std::vector<Panel*> fillPanels;
    std::vector<Panel*> leftPanels;
    std::vector<Panel*> rightPanels;
    std::vector<Panel*> topPanels;
    std::vector<Panel*> bottomPanels;
    std::vector<Panel*> floatingPanels;

    for (auto& panel : m_panels) {
        switch (panel->config().dockSide) {
            case DockSide::Fill: fillPanels.push_back(panel.get()); break;
            case DockSide::Left: leftPanels.push_back(panel.get()); break;
            case DockSide::Right: rightPanels.push_back(panel.get()); break;
            case DockSide::Top: topPanels.push_back(panel.get()); break;
            case DockSide::Bottom: bottomPanels.push_back(panel.get()); break;
            default: floatingPanels.push_back(panel.get()); break;
        }
    }

    // Helper to create a group or leaf from a panel list
    auto makeGroup = [this](const std::vector<Panel*>& panels) -> std::unique_ptr<LayoutNode> {
        if (panels.empty()) return nullptr;
        if (panels.size() == 1) {
            return std::make_unique<PanelLeaf>(panels[0]);
        }
        auto group = std::make_unique<PanelGroup>();
        for (Panel* p : panels) {
            group->addPanel(p);
        }
        // Wire up dock callbacks
        wireGroupDockCallbacks(group.get());
        return group;
    };

    // Build center content (fill panels)
    std::unique_ptr<LayoutNode> center = makeGroup(fillPanels);
    if (!center) {
        // No fill panels, use empty group
        center = std::make_unique<PanelGroup>();
    }

    // Add right panels in a vertical split
    if (!rightPanels.empty()) {
        auto rightGroup = makeGroup(rightPanels);
        auto split = std::make_unique<SplitContainer>(SplitDirection::Horizontal);
        split->setSplitRatio(0.75f);
        split->setFirst(std::move(center));
        split->setSecond(std::move(rightGroup));
        center = std::move(split);
    }

    // Add left panels
    if (!leftPanels.empty()) {
        auto leftGroup = makeGroup(leftPanels);
        auto split = std::make_unique<SplitContainer>(SplitDirection::Horizontal);
        split->setSplitRatio(0.25f);
        split->setFirst(std::move(leftGroup));
        split->setSecond(std::move(center));
        center = std::move(split);
    }

    // Add bottom panels
    if (!bottomPanels.empty()) {
        auto bottomGroup = makeGroup(bottomPanels);
        auto split = std::make_unique<SplitContainer>(SplitDirection::Vertical);
        split->setSplitRatio(0.7f);
        split->setFirst(std::move(center));
        split->setSecond(std::move(bottomGroup));
        center = std::move(split);
    }

    // Add top panels (including status bar)
    if (!topPanels.empty()) {
        auto topGroup = makeGroup(topPanels);
        auto split = std::make_unique<SplitContainer>(SplitDirection::Vertical);
        split->setSplitRatio(0.044f);  // Match status bar height (~32px at 720 height)
        split->setResizable(false);   // Status bar split should not be resizable
        split->setFirst(std::move(topGroup));
        split->setSecond(std::move(center));
        center = std::move(split);
    }

    // Clear floating z-order for panels that are now in the layout tree
    // Only truly floating panels (DockSide::None that weren't added to layout) remain
    m_floatingZOrder.clear();
    for (Panel* p : floatingPanels) {
        m_floatingZOrder.push_back(p->config().id);
    }

    m_layoutRoot = std::move(center);
    m_layoutMode = true;
}

bool PanelManager::saveLayout(const std::string& path) const {
    if (!m_layoutRoot) return false;

    // Recursive function to serialize a layout node
    std::function<nlohmann::json(const LayoutNode*)> serialize;
    serialize = [&serialize, this](const LayoutNode* node) -> nlohmann::json {
        nlohmann::json j;

        switch (node->type()) {
            case LayoutNodeType::PanelLeaf: {
                auto* leaf = static_cast<const PanelLeaf*>(node);
                j["type"] = "leaf";
                if (leaf->panel()) {
                    j["panelId"] = leaf->panel()->config().id;
                }
                break;
            }
            case LayoutNodeType::PanelGroup: {
                auto* group = static_cast<const PanelGroup*>(node);
                j["type"] = "group";
                j["id"] = group->id();
                j["activeTab"] = group->activeTabIndex();
                nlohmann::json panels = nlohmann::json::array();
                for (size_t i = 0; i < group->panelCount(); i++) {
                    if (Panel* p = group->panelAt(i)) {
                        panels.push_back(p->config().id);
                    }
                }
                j["panels"] = panels;
                break;
            }
            case LayoutNodeType::SplitContainer: {
                auto* split = static_cast<const SplitContainer*>(node);
                j["type"] = "split";
                j["id"] = split->id();
                j["direction"] = split->direction() == SplitDirection::Horizontal ? "horizontal" : "vertical";
                j["ratio"] = split->splitRatio();
                j["resizable"] = split->isResizable();
                if (split->first()) {
                    j["first"] = serialize(split->first());
                }
                if (split->second()) {
                    j["second"] = serialize(split->second());
                }
                break;
            }
        }

        return j;
    };

    nlohmann::json root;
    root["version"] = 2;  // Bumped version for TreeSlot support
    root["layout"] = serialize(m_layoutRoot.get());

    // Serialize saved TreeSlots for hidden panels
    if (!m_savedTreeSlots.empty()) {
        nlohmann::json slots = nlohmann::json::object();
        for (const auto& [panelId, slot] : m_savedTreeSlots) {
            nlohmann::json slotJson;
            slotJson["type"] = static_cast<int>(slot.type);
            slotJson["groupId"] = slot.groupId;
            slotJson["tabIndex"] = slot.tabIndex;
            slotJson["splitDirection"] = slot.splitInfo.direction;
            slotJson["splitRatio"] = slot.splitInfo.ratio;
            slotJson["splitWasFirst"] = slot.splitInfo.wasFirst;
            slotJson["splitSiblingId"] = slot.splitInfo.siblingId;
            slots[panelId] = slotJson;
        }
        root["savedTreeSlots"] = slots;
    }

    // Serialize saved floating bounds for hidden panels
    if (!m_savedFloatBounds.empty()) {
        nlohmann::json bounds = nlohmann::json::object();
        for (const auto& [panelId, b] : m_savedFloatBounds) {
            bounds[panelId] = {b.x, b.y, b.z, b.w};
        }
        root["savedFloatBounds"] = bounds;
    }

    try {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << root.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool PanelManager::loadLayout(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        nlohmann::json root;
        file >> root;

        if (!root.contains("layout")) return false;

        // Recursive function to deserialize a layout node
        std::function<std::unique_ptr<LayoutNode>(const nlohmann::json&)> deserialize;
        deserialize = [&deserialize, this](const nlohmann::json& j) -> std::unique_ptr<LayoutNode> {
            std::string type = j.value("type", "");

            if (type == "leaf") {
                std::string panelId = j.value("panelId", "");
                Panel* panel = getPanel(panelId);
                return std::make_unique<PanelLeaf>(panel);
            }
            else if (type == "group") {
                auto group = std::make_unique<PanelGroup>();
                group->setId(j.value("id", ""));

                if (j.contains("panels") && j["panels"].is_array()) {
                    for (const auto& pid : j["panels"]) {
                        if (pid.is_string()) {
                            Panel* panel = getPanel(pid.get<std::string>());
                            if (panel) {
                                group->addPanel(panel);
                            }
                        }
                    }
                }

                int activeTab = j.value("activeTab", 0);
                group->setActiveTab(activeTab);

                // Wire up dock callbacks
                wireGroupDockCallbacks(group.get());

                return group;
            }
            else if (type == "split") {
                std::string dir = j.value("direction", "horizontal");
                SplitDirection direction = (dir == "vertical")
                    ? SplitDirection::Vertical
                    : SplitDirection::Horizontal;

                auto split = std::make_unique<SplitContainer>(direction);
                split->setId(j.value("id", ""));
                split->setSplitRatio(j.value("ratio", 0.5f));
                split->setResizable(j.value("resizable", true));

                if (j.contains("first")) {
                    split->setFirst(deserialize(j["first"]));
                }
                if (j.contains("second")) {
                    split->setSecond(deserialize(j["second"]));
                }
                return split;
            }

            return nullptr;
        };

        m_layoutRoot = deserialize(root["layout"]);
        m_layoutMode = (m_layoutRoot != nullptr);

        // Restore saved TreeSlots for hidden panels
        if (root.contains("savedTreeSlots") && root["savedTreeSlots"].is_object()) {
            m_savedTreeSlots.clear();
            for (auto& [panelId, slotJson] : root["savedTreeSlots"].items()) {
                TreeSlot slot;
                slot.type = static_cast<TreeSlot::Type>(slotJson.value("type", 0));
                slot.groupId = slotJson.value("groupId", "");
                slot.tabIndex = slotJson.value("tabIndex", -1);
                slot.splitInfo.direction = slotJson.value("splitDirection", 0);
                slot.splitInfo.ratio = slotJson.value("splitRatio", 0.5f);
                slot.splitInfo.wasFirst = slotJson.value("splitWasFirst", true);
                slot.splitInfo.siblingId = slotJson.value("splitSiblingId", "");
                m_savedTreeSlots[panelId] = slot;
            }
        }

        // Restore saved floating bounds for hidden panels
        if (root.contains("savedFloatBounds") && root["savedFloatBounds"].is_object()) {
            m_savedFloatBounds.clear();
            for (auto& [panelId, boundsJson] : root["savedFloatBounds"].items()) {
                if (boundsJson.is_array() && boundsJson.size() == 4) {
                    m_savedFloatBounds[panelId] = {
                        boundsJson[0].get<float>(),
                        boundsJson[1].get<float>(),
                        boundsJson[2].get<float>(),
                        boundsJson[3].get<float>()
                    };
                }
            }
        }

        return m_layoutMode;
    } catch (...) {
        return false;
    }
}

// -----------------------------------------------------------------------------
// Docking System
// -----------------------------------------------------------------------------

void PanelManager::floatPanel(Panel* panel, const glm::vec2& pos) {
    if (!panel) return;

    const std::string& id = panel->config().id;

    // Set panel bounds at position
    glm::vec4 bounds = panel->bounds();
    bounds.x = pos.x;
    bounds.y = pos.y;
    bounds.z = std::max(bounds.z, 200.0f);
    bounds.w = std::max(bounds.w, 150.0f);
    panel->setBounds(bounds);

    // Add to floating z-order if not already there
    addToFloatingOrder(id);

    std::cerr << "[PanelManager] Floated panel: " << id << " at " << pos.x << ", " << pos.y << "\n";
}

void PanelManager::addToFloatingOrder(const std::string& id) {
    // Check if already in list
    auto it = std::find(m_floatingZOrder.begin(), m_floatingZOrder.end(), id);
    if (it == m_floatingZOrder.end()) {
        m_floatingZOrder.push_back(id);
    }
}

void PanelManager::removeFromFloatingOrder(const std::string& id) {
    auto it = std::find(m_floatingZOrder.begin(), m_floatingZOrder.end(), id);
    if (it != m_floatingZOrder.end()) {
        m_floatingZOrder.erase(it);
    }
}

void PanelManager::hidePanelWithSlot(const std::string& id, bool saveSlot) {
    Panel* panel = getPanel(id);
    if (!panel) return;

    if (saveSlot && m_dockManager && m_layoutMode) {
        // Save tree slot for restoration
        TreeSlot slot = m_dockManager->captureTreeSlot(panel);
        if (slot.isValid()) {
            m_savedTreeSlots[id] = slot;
        }

        // Save floating bounds if panel is floating
        auto floatIt = std::find(m_floatingZOrder.begin(), m_floatingZOrder.end(), id);
        if (floatIt != m_floatingZOrder.end()) {
            m_savedFloatBounds[id] = panel->bounds();
        }

        // Remove from layout tree
        m_dockManager->removePanelFromLayout(panel);
        m_dockManager->cleanupEmptyNodes();
    }

    // Remove from floating order
    removeFromFloatingOrder(id);

    // Hide the panel
    panel->setVisible(false);

    std::cerr << "[PanelManager] Hidden panel with slot: " << id << "\n";
}

void PanelManager::showPanelFromSlot(const std::string& id) {
    Panel* panel = getPanel(id);
    if (!panel) return;

    // Make visible first
    panel->setVisible(true);

    // Try to restore from tree slot
    auto slotIt = m_savedTreeSlots.find(id);
    if (slotIt != m_savedTreeSlots.end() && m_dockManager) {
        TreeSlot& slot = slotIt->second;
        if (m_dockManager->restoreFromTreeSlot(panel, slot)) {
            m_savedTreeSlots.erase(slotIt);
            std::cerr << "[PanelManager] Restored panel from tree slot: " << id << "\n";
            setFocus(id);
            return;
        }
    }

    // Fall back to floating at saved position or default
    auto boundsIt = m_savedFloatBounds.find(id);
    if (boundsIt != m_savedFloatBounds.end()) {
        panel->setBounds(boundsIt->second);
        m_savedFloatBounds.erase(boundsIt);
    } else {
        // Default floating position
        glm::vec4 bounds = panel->bounds();
        bounds.x = 100;
        bounds.y = 100;
        bounds.z = std::max(bounds.z, 300.0f);
        bounds.w = std::max(bounds.w, 200.0f);
        panel->setBounds(bounds);
    }

    addToFloatingOrder(id);
    setFocus(id);

    std::cerr << "[PanelManager] Showed panel as floating: " << id << "\n";
}

void PanelManager::wireGroupDockCallbacks(PanelGroup* group) {
    if (!group || !m_dockManager) return;

    // Set up tab drag callback to initiate docking
    group->onTabDrag([this, group](Panel* panel, const glm::vec2& pos) {
        if (m_dockManager && panel) {
            m_dockManager->beginTabDrag(panel, group, pos);
        }
    });
}

} // namespace vivid
