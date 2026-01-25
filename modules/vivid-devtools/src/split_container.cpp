// SplitContainer implementation - splits space between two children

#include <vivid/devtools/split_container.h>
#include <vivid/gui/ui_style.h>
#include <algorithm>

namespace vivid {

SplitContainer::SplitContainer(SplitDirection direction)
    : m_direction(direction) {}

SplitContainer::~SplitContainer() = default;

void SplitContainer::updateLayout() {
    calculateChildBounds();

    if (m_first) {
        m_first->setBounds(m_firstBounds);
        m_first->updateLayout();
    }
    if (m_second) {
        m_second->setBounds(m_secondBounds);
        m_second->updateLayout();
    }
}

void SplitContainer::calculateChildBounds() {
    float x = m_bounds.x;
    float y = m_bounds.y;
    float w = m_bounds.z;
    float h = m_bounds.w;

    if (m_direction == SplitDirection::Horizontal) {
        // Left-right split
        float splitPos = w * m_splitRatio;
        float dividerHalf = m_dividerSize / 2.0f;

        m_firstBounds = {x, y, splitPos - dividerHalf, h};
        m_dividerBounds = {x + splitPos - dividerHalf, y, m_dividerSize, h};
        m_secondBounds = {x + splitPos + dividerHalf, y, w - splitPos - dividerHalf, h};
    } else {
        // Top-bottom split
        float splitPos = h * m_splitRatio;
        float dividerHalf = m_dividerSize / 2.0f;

        m_firstBounds = {x, y, w, splitPos - dividerHalf};
        m_dividerBounds = {x, y + splitPos - dividerHalf, w, m_dividerSize};
        m_secondBounds = {x, y + splitPos + dividerHalf, w, h - splitPos - dividerHalf};
    }
}

void SplitContainer::render(OverlayCanvas& canvas, const FrameInput& input, const UIStyle& style) {
    // Render children
    if (m_first) {
        m_first->render(canvas, input, style);
    }
    if (m_second) {
        m_second->render(canvas, input, style);
    }

    // Render divider (all dimensions in logical pixels)
    float dx = m_dividerBounds.x;
    float dy = m_dividerBounds.y;
    float dw = m_dividerBounds.z;
    float dh = m_dividerBounds.w;

    // Divider background
    glm::vec4 dividerColor = m_dividerHovered || m_dividerDragging
        ? glm::vec4(0.3f, 0.3f, 0.35f, 1.0f)
        : glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
    canvas.fillRect(dx, dy, dw, dh, dividerColor);

    // Divider grip dots
    glm::vec4 gripColor(0.4f, 0.4f, 0.45f, 1.0f);
    float dotSize = 2.0f;
    float dotSpacing = 6.0f;

    if (m_direction == SplitDirection::Horizontal) {
        float cx = dx + dw / 2;
        float cy = dy + dh / 2;
        for (int i = -2; i <= 2; i++) {
            canvas.fillCircle(cx, cy + i * dotSpacing, dotSize, gripColor, 6);
        }
    } else {
        float cx = dx + dw / 2;
        float cy = dy + dh / 2;
        for (int i = -2; i <= 2; i++) {
            canvas.fillCircle(cx + i * dotSpacing, cy, dotSize, gripColor, 6);
        }
    }
}

bool SplitContainer::handleInput(const FrameInput& input) {
    glm::vec2 mousePos = input.mousePos;
    bool leftMouseDown = input.mouseDown[0];
    bool leftMouseClicked = leftMouseDown && !m_lastMouseDown;
    m_lastMouseDown = leftMouseDown;

    // Check divider hover
    m_dividerHovered = hitTestDivider(mousePos);

    // Handle divider drag start
    if (leftMouseClicked && m_dividerHovered) {
        m_dividerDragging = true;
        m_dragStartRatio = m_splitRatio;
        if (m_direction == SplitDirection::Horizontal) {
            m_dragStartPos = mousePos.x;
        } else {
            m_dragStartPos = mousePos.y;
        }
        return true;
    }

    // Handle divider dragging
    if (m_dividerDragging) {
        if (leftMouseDown) {
            float totalSize = (m_direction == SplitDirection::Horizontal)
                ? m_bounds.z : m_bounds.w;
            float currentPos = (m_direction == SplitDirection::Horizontal)
                ? mousePos.x : mousePos.y;
            float startOffset = (m_direction == SplitDirection::Horizontal)
                ? m_bounds.x : m_bounds.y;

            float delta = currentPos - m_dragStartPos;
            float newRatio = m_dragStartRatio + delta / totalSize;

            // Enforce minimum sizes
            float minRatio = m_minChildSize / totalSize;
            float maxRatio = 1.0f - minRatio;
            newRatio = std::max(minRatio, std::min(maxRatio, newRatio));

            setSplitRatio(newRatio);
            updateLayout();
            return true;
        } else {
            m_dividerDragging = false;
        }
    }

    // Forward input to children (back to front)
    bool consumed = false;
    if (m_second && m_second->handleInput(input)) {
        consumed = true;
    }
    if (!consumed && m_first && m_first->handleInput(input)) {
        consumed = true;
    }

    return consumed || m_dividerHovered;
}

bool SplitContainer::hitTestDivider(const glm::vec2& pos) const {
    // Expand hit area slightly for easier grabbing
    float margin = 4.0f;
    return pos.x >= m_dividerBounds.x - margin &&
           pos.x <= m_dividerBounds.x + m_dividerBounds.z + margin &&
           pos.y >= m_dividerBounds.y - margin &&
           pos.y <= m_dividerBounds.y + m_dividerBounds.w + margin;
}

void SplitContainer::collectPanels(std::vector<Panel*>& outPanels) {
    if (m_first) m_first->collectPanels(outPanels);
    if (m_second) m_second->collectPanels(outPanels);
}

Panel* SplitContainer::findPanel(const std::string& id) {
    if (m_first) {
        if (Panel* panel = m_first->findPanel(id)) {
            return panel;
        }
    }
    if (m_second) {
        if (Panel* panel = m_second->findPanel(id)) {
            return panel;
        }
    }
    return nullptr;
}

bool SplitContainer::containsPanel(Panel* panel) const {
    if (m_first && m_first->containsPanel(panel)) return true;
    if (m_second && m_second->containsPanel(panel)) return true;
    return false;
}

bool SplitContainer::isHovered() const {
    if (m_dividerHovered) return true;
    if (m_first && m_first->isHovered()) return true;
    if (m_second && m_second->isHovered()) return true;
    return false;
}

bool SplitContainer::isInteracting() const {
    if (m_dividerDragging) return true;
    if (m_first && m_first->isInteracting()) return true;
    if (m_second && m_second->isInteracting()) return true;
    return false;
}

void SplitContainer::setFirst(std::unique_ptr<LayoutNode> node) {
    m_first = std::move(node);
    if (m_first) {
        m_first->setBounds(m_firstBounds);
    }
}

void SplitContainer::setSecond(std::unique_ptr<LayoutNode> node) {
    m_second = std::move(node);
    if (m_second) {
        m_second->setBounds(m_secondBounds);
    }
}

void SplitContainer::setSplitRatio(float ratio) {
    m_splitRatio = std::max(0.1f, std::min(0.9f, ratio));
}

} // namespace vivid
