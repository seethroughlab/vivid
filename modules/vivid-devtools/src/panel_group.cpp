// PanelGroup implementation - tabbed panel container

#include <vivid/devtools/panel_group.h>
#include <vivid/gui/ui_style.h>
#include <algorithm>

namespace vivid {

PanelGroup::PanelGroup() = default;
PanelGroup::~PanelGroup() = default;

void PanelGroup::updateLayout() {
    // Update active panel bounds to fill space below tab bar
    if (Panel* panel = activePanel()) {
        glm::vec4 contentBounds = m_bounds;
        contentBounds.y += m_tabBarHeight;
        contentBounds.w -= m_tabBarHeight;
        panel->setBounds(contentBounds);
    }
}

void PanelGroup::render(OverlayCanvas& canvas, const FrameInput& input, const UIStyle& style) {
    if (m_panels.empty()) return;

    // All dimensions in logical pixels - canvas handles scaling
    m_tabBarHeight = 28.0f;

    float x = m_bounds.x;
    float y = m_bounds.y;
    float w = m_bounds.z;
    float h = m_bounds.w;
    float tabBarH = m_tabBarHeight;

    // Background
    canvas.fillRect(x, y, w, h, style.panelBg);

    // Tab bar background
    canvas.fillRect(x, y, w, tabBarH, style.headerBg);

    // Render tabs
    renderTabBar(canvas);

    // Render active panel content
    if (Panel* panel = activePanel()) {
        glm::vec4 contentBounds(x, y + tabBarH, w, h - tabBarH);
        panel->render(canvas, contentBounds, input, style);
    }

    // Border
    canvas.strokeRect(x, y, w, h, 1.0f, style.panelBorder);
}

void PanelGroup::renderTabBar(OverlayCanvas& canvas) {
    m_tabRects.clear();

    // All dimensions in logical pixels
    float x = m_bounds.x;
    float y = m_bounds.y;
    float tabBarH = m_tabBarHeight;

    float tabX = x + 4;
    float tabPadding = 12;
    float tabHeight = tabBarH - 4;
    float tabY = y + 2;

    for (size_t i = 0; i < m_panels.size(); i++) {
        Panel* panel = m_panels[i];
        const std::string& title = panel->config().title;

        // Estimate tab width based on title
        float textWidth = title.length() * 8.0f;  // Rough estimate
        float tabWidth = textWidth + tabPadding * 2;

        // Store tab rect for hit testing (in logical coordinates)
        m_tabRects.push_back({tabX, tabY, tabWidth, tabHeight});

        // Tab background
        bool isActive = (static_cast<int>(i) == m_activeTab);
        bool isHovered = (static_cast<int>(i) == m_hoveredTab);

        glm::vec4 tabBg;
        if (isActive) {
            tabBg = glm::vec4(0.2f, 0.2f, 0.25f, 1.0f);
        } else if (isHovered) {
            tabBg = glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
        } else {
            tabBg = glm::vec4(0.1f, 0.1f, 0.12f, 1.0f);
        }

        // Draw tab with rounded top corners
        canvas.fillRoundedRectTop(tabX, tabY, tabWidth, tabHeight, 4.0f, tabBg);

        // Active tab indicator (bottom line)
        if (isActive) {
            glm::vec4 accentColor(0.4f, 0.6f, 1.0f, 1.0f);
            canvas.fillRect(tabX, tabY + tabHeight - 2, tabWidth, 2, accentColor);
        }

        // Tab title
        glm::vec4 textColor = isActive ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)
                                        : glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
        canvas.text(title, tabX + tabPadding, tabY + tabHeight - 8, textColor, 0);

        // Close button (if enabled)
        if (m_showCloseButtons) {
            float closeX = tabX + tabWidth - 18;
            float closeY = tabY + tabHeight / 2;
            float closeSize = 5;
            glm::vec4 closeColor(0.5f, 0.5f, 0.5f, 1.0f);
            canvas.line(closeX - closeSize, closeY - closeSize,
                        closeX + closeSize, closeY + closeSize, 1.5f, closeColor);
            canvas.line(closeX + closeSize, closeY - closeSize,
                        closeX - closeSize, closeY + closeSize, 1.5f, closeColor);
        }

        tabX += tabWidth + 2;
    }
}

bool PanelGroup::handleInput(const FrameInput& input) {
    if (m_panels.empty()) return false;

    glm::vec2 mousePos = input.mousePos;
    bool leftMouseDown = input.mouseDown[0];
    bool leftMouseClicked = leftMouseDown && !m_lastMouseDown;
    m_lastMouseDown = leftMouseDown;

    // Check if mouse is in bounds
    m_hovered = mousePos.x >= m_bounds.x && mousePos.x <= m_bounds.x + m_bounds.z &&
                mousePos.y >= m_bounds.y && mousePos.y <= m_bounds.y + m_bounds.w;

    // Check tab bar hover
    m_tabBarHovered = mousePos.y >= m_bounds.y && mousePos.y <= m_bounds.y + m_tabBarHeight;

    // Hit test tabs
    m_hoveredTab = -1;
    if (m_tabBarHovered) {
        m_hoveredTab = hitTestTab(mousePos);
    }

    // Handle tab click
    if (leftMouseClicked && m_hoveredTab >= 0) {
        setActiveTab(m_hoveredTab);
        m_draggingTab = m_hoveredTab;
        m_dragStartPos = mousePos;
        m_dragStarted = false;
        return true;
    }

    // Handle tab dragging
    if (m_draggingTab >= 0) {
        if (leftMouseDown) {
            glm::vec2 delta = mousePos - m_dragStartPos;
            float dragThreshold = 10.0f;

            if (!m_dragStarted && (std::abs(delta.x) > dragThreshold || std::abs(delta.y) > dragThreshold)) {
                m_dragStarted = true;
            }

            if (m_dragStarted) {
                // Check if dragging out of tab bar (to detach)
                if (mousePos.y > m_bounds.y + m_tabBarHeight + 20.0f ||
                    mousePos.y < m_bounds.y - 20.0f) {
                    if (m_onTabDrag && m_draggingTab < static_cast<int>(m_panels.size())) {
                        m_onTabDrag(m_panels[m_draggingTab], mousePos);
                    }
                }
                // Check for reorder within tab bar
                else if (m_allowReorder) {
                    int newIndex = hitTestTab(mousePos);
                    if (newIndex >= 0 && newIndex != m_draggingTab) {
                        // Swap tabs
                        std::swap(m_panels[m_draggingTab], m_panels[newIndex]);
                        m_draggingTab = newIndex;
                        m_activeTab = newIndex;
                    }
                }
            }
            return true;
        } else {
            m_draggingTab = -1;
            m_dragStarted = false;
        }
    }

    // Forward input to active panel
    if (Panel* panel = activePanel()) {
        return panel->handleInput(input);
    }

    return m_hovered;
}

int PanelGroup::hitTestTab(const glm::vec2& pos) const {
    for (size_t i = 0; i < m_tabRects.size(); i++) {
        const glm::vec4& rect = m_tabRects[i];
        if (pos.x >= rect.x && pos.x <= rect.x + rect.z &&
            pos.y >= rect.y && pos.y <= rect.y + rect.w) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void PanelGroup::collectPanels(std::vector<Panel*>& outPanels) {
    for (Panel* panel : m_panels) {
        outPanels.push_back(panel);
    }
}

Panel* PanelGroup::findPanel(const std::string& id) {
    for (Panel* panel : m_panels) {
        if (panel->config().id == id) {
            return panel;
        }
    }
    return nullptr;
}

bool PanelGroup::containsPanel(Panel* panel) const {
    return std::find(m_panels.begin(), m_panels.end(), panel) != m_panels.end();
}

bool PanelGroup::isHovered() const {
    return m_hovered;
}

bool PanelGroup::isInteracting() const {
    if (m_draggingTab >= 0) return true;
    if (Panel* panel = activePanel()) {
        return panel->isInteracting();
    }
    return false;
}

void PanelGroup::addPanel(Panel* panel) {
    if (!panel) return;
    if (containsPanel(panel)) return;

    m_panels.push_back(panel);

    // If this is the first panel, make it active
    if (m_panels.size() == 1) {
        m_activeTab = 0;
    }
}

bool PanelGroup::removePanel(Panel* panel) {
    auto it = std::find(m_panels.begin(), m_panels.end(), panel);
    if (it == m_panels.end()) return false;

    int index = static_cast<int>(it - m_panels.begin());
    m_panels.erase(it);

    // Adjust active tab if needed
    if (m_activeTab >= static_cast<int>(m_panels.size())) {
        m_activeTab = static_cast<int>(m_panels.size()) - 1;
    }
    if (m_activeTab < 0) {
        m_activeTab = 0;
    }

    return true;
}

Panel* PanelGroup::panelAt(size_t index) const {
    if (index >= m_panels.size()) return nullptr;
    return m_panels[index];
}

void PanelGroup::setActiveTab(int index) {
    if (index >= 0 && index < static_cast<int>(m_panels.size())) {
        m_activeTab = index;
        if (m_onTabSelect) {
            m_onTabSelect(m_panels[index]);
        }
    }
}

Panel* PanelGroup::activePanel() const {
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_panels.size())) {
        return m_panels[m_activeTab];
    }
    return nullptr;
}

void PanelGroup::setActivePanel(Panel* panel) {
    for (size_t i = 0; i < m_panels.size(); i++) {
        if (m_panels[i] == panel) {
            setActiveTab(static_cast<int>(i));
            return;
        }
    }
}

} // namespace vivid
