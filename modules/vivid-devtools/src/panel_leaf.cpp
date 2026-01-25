// PanelLeaf implementation - wraps a single panel as a layout node

#include <vivid/devtools/panel_leaf.h>

namespace vivid {

PanelLeaf::PanelLeaf(Panel* panel)
    : m_panel(panel) {}

PanelLeaf::~PanelLeaf() = default;

void PanelLeaf::updateLayout() {
    if (m_panel) {
        m_panel->setBounds(m_bounds);
    }
}

void PanelLeaf::render(OverlayCanvas& canvas, const FrameInput& input, float scale, const UIStyle& style) {
    if (!m_panel) return;

    glm::vec4 scaledBounds = m_bounds * scale;
    m_panel->render(canvas, scaledBounds, input, scale, style);
}

bool PanelLeaf::handleInput(const FrameInput& input) {
    if (!m_panel) return false;
    return m_panel->handleInput(input);
}

void PanelLeaf::collectPanels(std::vector<Panel*>& outPanels) {
    if (m_panel) {
        outPanels.push_back(m_panel);
    }
}

Panel* PanelLeaf::findPanel(const std::string& id) {
    if (m_panel && m_panel->config().id == id) {
        return m_panel;
    }
    return nullptr;
}

bool PanelLeaf::containsPanel(Panel* panel) const {
    return m_panel == panel;
}

bool PanelLeaf::isHovered() const {
    return m_panel && m_panel->isHovered();
}

bool PanelLeaf::isInteracting() const {
    return m_panel && m_panel->isInteracting();
}

} // namespace vivid
