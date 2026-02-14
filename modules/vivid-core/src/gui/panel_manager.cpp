// PanelManager implementation
// Manages layout and focus for devtools panels

#include <vivid/gui/panel_manager.h>
#include <vivid/gui/gui_debug.h>
#include <vivid/context.h>
#include <algorithm>
#include <iostream>

namespace vivid {

PanelManager::PanelManager() = default;

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

void PanelManager::render(OverlayCanvas& canvas, const gui::InputState& input,
                           float screenWidth, float screenHeight, const UIStyle& style) {
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;

    // Track if any panel consumed input
    m_consumedInput = false;

    renderFlatMode(canvas, input, scale, style);

    // Update focus based on interactions
    // If a panel is being interacted with (dragging/resizing), focus it and bring to front
    for (auto& panel : m_panels) {
        if (panel->isInteracting()) {
            const std::string& id = panel->config().id;
            if (id != m_focusedPanelId) {
                setFocus(id);
            }
            return;  // Don't process further focus changes while interacting
        }
    }

    // If a panel was clicked and it's not the focused one, change focus
    if (input.mouseClicked[0]) {
        std::string panelToFocus;

        // Check floating panels first (in reverse z-order = front to back)
        std::vector<std::string> floatingCopy = m_floatingZOrder;
        for (auto it = floatingCopy.rbegin(); it != floatingCopy.rend(); ++it) {
            Panel* panel = getPanel(*it);
            if (panel && panel->isVisible() && panel->isHovered()) {
                if (*it != m_focusedPanelId) {
                    panelToFocus = *it;
                }
                break;
            }
        }

        // If no floating panel, check layout-managed panels
        if (panelToFocus.empty()) {
            for (auto& panel : m_panels) {
                if (panel->config().role == PanelRole::Floating) continue;
                if (panel->isVisible() && panel->isHovered()) {
                    const std::string& id = panel->config().id;
                    if (id != m_focusedPanelId) {
                        panelToFocus = id;
                    }
                    break;
                }
            }
        }

        if (!panelToFocus.empty()) {
            setFocus(panelToFocus);
        }
    }

#ifndef NDEBUG
    if (gui::isDebugEnabled()) {
        gui::validateState(*this);
    }
#endif
}

std::string PanelManager::determineInputTarget(const gui::InputState& input) {
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

    // 3. Check layout-managed panels
    for (auto& panel : m_panels) {
        if (panel->config().role == PanelRole::Floating) continue;
        if (!panel->isVisible()) continue;

        glm::vec4 bounds = panel->bounds();
        if (input.mousePos.x >= bounds.x && input.mousePos.x <= bounds.x + bounds.z &&
            input.mousePos.y >= bounds.y && input.mousePos.y <= bounds.y + bounds.w) {
            return panel->config().id;
        }
    }

    return "";
}

void PanelManager::renderFlatMode(OverlayCanvas& canvas, const gui::InputState& input, float scale, const UIStyle& style) {
    // Calculate layout for layout-managed panels
    calculateLayout(m_screenWidth, m_screenHeight);

    // Pre-pass: determine which panel owns input this frame
    std::string inputTarget = determineInputTarget(input);

    // Set interaction and ownership flags on all panels
    for (auto& panel : m_panels) {
        const std::string& id = panel->config().id;
        bool ownsInput = (id == inputTarget);
        panel->setInputOwnership(ownsInput);
        bool canStart = ownsInput || panel->isInteracting();
        panel->setCanStartInteraction(canStart);
    }

    // Collect layout-managed panels
    std::vector<Panel*> layoutPanels;
    for (auto& panel : m_panels) {
        if (!panel->isVisible()) continue;
        if (panel->config().role != PanelRole::Floating) {
            layoutPanels.push_back(panel.get());
        }
    }

    // Set content scale on canvas for HiDPI support
    canvas.setContentScale(scale);

    // Render layout-managed panels at Panels layer
    canvas.setLayer(UILayer::Panels);
    for (auto* panel : layoutPanels) {
        panel->render(canvas, panel->bounds(), input, style);
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
    int floatingLayer = UILayer::FloatingPanels;
    for (const std::string& id : m_floatingZOrder) {
        Panel* panel = getPanel(id);
        if (!panel || !panel->isVisible()) continue;

        panel->setShowTitleBar(true);

        canvas.setLayer(floatingLayer);
        panel->render(canvas, panel->bounds(), input, style);
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

    // Reset layer
    canvas.setLayer(0);
}

void PanelManager::addPanel(std::unique_ptr<Panel> panel) {
    const std::string& id = panel->config().id;

    // Track floating panels in z-order
    if (panel->config().role == PanelRole::Floating) {
        m_floatingZOrder.push_back(id);
    }

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
        gui::logTransition("PanelManager", "hidden", "visible", id.c_str());
        p->setVisible(true);
        if (p->config().role == PanelRole::Floating) {
            addToFloatingOrder(id);  // Re-add if removed by hidePanel
        }
        setFocus(id);
    }
}

void PanelManager::hidePanel(const std::string& id) {
    if (Panel* p = getPanel(id)) {
        gui::logTransition("PanelManager", "visible", "hidden", id.c_str());
        p->setVisible(false);
        removeFromFloatingOrder(id);
    }
}

void PanelManager::togglePanel(const std::string& id) {
    if (Panel* p = getPanel(id)) {
        if (p->isVisible()) {
            hidePanel(id);
        } else {
            showPanel(id);
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
    if (!m_focusedPanelId.empty()) {
        if (Panel* prev = getPanel(m_focusedPanelId)) {
            prev->setFocused(false);
        }
    }

    if (id != m_focusedPanelId) {
        gui::logTransition("PanelManager", "focus",
                          id.empty() ? "none" : id.c_str(),
                          m_focusedPanelId.empty() ? "none" : m_focusedPanelId.c_str());
    }

    m_focusedPanelId = id;
    if (!id.empty()) {
        if (Panel* p = getPanel(id)) {
            p->setFocused(true);
            if (p->config().role == PanelRole::Floating) {
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
    float leftEdge = 0;
    float rightEdge = screenWidth;
    float topEdge = 0;
    float bottomEdge = screenHeight;

    // First pass: calculate space taken by status bar
    for (auto& panel : m_panels) {
        if (!panel->isVisible()) continue;

        if (panel->config().role == PanelRole::StatusBar) {
            topEdge = std::max(topEdge, panel->bounds().w);
        }
    }

    // Second pass: update panel bounds based on role
    for (auto& panel : m_panels) {
        if (!panel->isVisible()) continue;

        glm::vec4 bounds = panel->bounds();

        switch (panel->config().role) {
            case PanelRole::StatusBar:
                bounds.x = leftEdge;
                bounds.y = 0;
                bounds.z = rightEdge - leftEdge;
                break;
            case PanelRole::Background:
                bounds.x = leftEdge;
                bounds.y = topEdge;
                bounds.z = rightEdge - leftEdge;
                bounds.w = bottomEdge - topEdge;
                break;
            default:
                break;
        }

        panel->setBounds(bounds);
    }
}

void PanelManager::addToFloatingOrder(const std::string& id) {
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

} // namespace vivid
