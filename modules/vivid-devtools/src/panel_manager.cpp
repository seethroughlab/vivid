// PanelManager implementation
// Manages layout and focus for devtools panels

#include <vivid/devtools/panel_manager.h>
#include <vivid/context.h>
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

void PanelManager::render(OverlayCanvas& canvas, const FrameInput& input,
                           float screenWidth, float screenHeight) {
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Calculate layout for docked panels
    calculateLayout(screenWidth, screenHeight);

    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;

    // Track if any panel consumed input
    m_consumedInput = false;

    // Render panels in order (back to front)
    // Docked panels first, then floating panels, focused panel last
    for (auto& panel : m_panels) {
        if (!panel->isVisible()) continue;

        // Skip focused panel (render last)
        if (panel->config().id == m_focusedPanelId) continue;

        // Scale bounds to physical pixels
        glm::vec4 scaledBounds = panel->bounds() * scale;

        // Render panel
        panel->render(canvas, scaledBounds, input, scale);

        if (panel->consumedInput()) {
            m_consumedInput = true;
        }
    }

    // Render focused panel last (on top)
    if (!m_focusedPanelId.empty()) {
        Panel* focused = getPanel(m_focusedPanelId);
        if (focused && focused->isVisible()) {
            glm::vec4 scaledBounds = focused->bounds() * scale;
            focused->render(canvas, scaledBounds, input, scale);
            if (focused->consumedInput()) {
                m_consumedInput = true;
            }
        }
    }

    // Update focus based on clicks
    // If a panel was clicked and it's not the focused one, change focus
    for (auto& panel : m_panels) {
        if (panel->isVisible() && panel->isHovered() && input.mouseDown[0]) {
            if (panel->config().id != m_focusedPanelId) {
                setFocus(panel->config().id);
            }
            break;
        }
    }
}

void PanelManager::addPanel(std::unique_ptr<Panel> panel) {
    const std::string& id = panel->config().id;
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
        p->setVisible(true);
    }
}

void PanelManager::hidePanel(const std::string& id) {
    if (Panel* p = getPanel(id)) {
        p->setVisible(false);
    }
}

void PanelManager::togglePanel(const std::string& id) {
    if (Panel* p = getPanel(id)) {
        p->toggleVisible();
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

} // namespace vivid
