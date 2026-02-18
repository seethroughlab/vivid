// Panel base class implementation
// Provides common drag/resize functionality for all panels

#include <vivid/gui/panel.h>
#include <vivid/gui/ui_style.h>
#include <algorithm>
#include <cmath>

namespace vivid {

Panel::Panel() {
    // Default configuration
    m_config.id = "";
    m_config.title = "";
    m_config.bounds = {20, 60, 400, 300};
    m_config.role = PanelRole::Floating;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 200.0f;
    m_config.minHeight = 150.0f;
}

Panel::~Panel() = default;

void Panel::handleDragAndResize(const gui::InputState& input, float screenW, float screenH,
                                 float titleBarHeight) {
    if (!m_config.visible) {
        m_focus.hovered = false;
        m_inputRouting.consumedInput = false;
        return;
    }

    glm::vec2 mousePos = input.mousePos;

    float x = m_config.bounds.x;
    float y = m_config.bounds.y;
    float w = m_config.bounds.z;
    float h = m_config.bounds.w;
    float hitSize = 8.0f;

    // Check if mouse is in panel bounds (with resize margin)
    m_focus.hovered = mousePos.x >= x - hitSize && mousePos.x <= x + w + hitSize &&
                      mousePos.y >= y - hitSize && mousePos.y <= y + h + hitSize;

    bool leftMouseDown = input.mouseDown[0];
    bool leftMouseClicked = input.mouseClicked[0];

    // -------------------------------------------------------------------------
    // Dragging (from title bar)
    // -------------------------------------------------------------------------
    if (m_config.draggable) {
        bool overTitleBar = mousePos.x >= x && mousePos.x <= x + w &&
                            mousePos.y >= y && mousePos.y <= y + titleBarHeight;

        // Only start new drag if this panel can start interactions
        if (m_inputRouting.canStartInteraction && overTitleBar && leftMouseClicked &&
            !m_dragResize.dragging && m_dragResize.resizing == 0) {
            m_dragResize.dragging = true;
            m_dragResize.dragOffset = mousePos - glm::vec2(x, y);
        }

        // Continue existing drag regardless of canStartInteraction or inputBlocked
        if (m_dragResize.dragging) {
            if (leftMouseDown) {
                m_config.bounds.x = mousePos.x - m_dragResize.dragOffset.x;
                m_config.bounds.y = mousePos.y - m_dragResize.dragOffset.y;
            } else {
                m_dragResize.dragging = false;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Resizing (from edges)
    // -------------------------------------------------------------------------
    // Only start new resize if this panel can start interactions
    if (m_inputRouting.canStartInteraction && m_config.resizable &&
        !m_dragResize.dragging && m_dragResize.resizing == 0 && leftMouseClicked) {
        int edge = 0;
        if (mousePos.x >= x - hitSize && mousePos.x <= x + hitSize) edge |= 1;  // left
        if (mousePos.x >= x + w - hitSize && mousePos.x <= x + w + hitSize) edge |= 2;  // right
        if (mousePos.y >= y - hitSize && mousePos.y <= y + hitSize) edge |= 4;  // top
        if (mousePos.y >= y + h - hitSize && mousePos.y <= y + h + hitSize) edge |= 8;  // bottom

        if (edge != 0) {
            m_dragResize.resizing = edge;
            m_dragResize.startBounds = m_config.bounds;
            m_dragResize.startMouse = mousePos;
        }
    }

    if (m_dragResize.resizing != 0) {
        if (leftMouseDown) {
            glm::vec2 delta = mousePos - m_dragResize.startMouse;
            float newX = m_dragResize.startBounds.x;
            float newY = m_dragResize.startBounds.y;
            float newW = m_dragResize.startBounds.z;
            float newH = m_dragResize.startBounds.w;

            if (m_dragResize.resizing & 1) { newX += delta.x; newW -= delta.x; }  // left
            if (m_dragResize.resizing & 2) { newW += delta.x; }  // right
            if (m_dragResize.resizing & 4) { newY += delta.y; newH -= delta.y; }  // top
            if (m_dragResize.resizing & 8) { newH += delta.y; }  // bottom

            // Enforce minimum size
            if (newW < m_config.minWidth) {
                if (m_dragResize.resizing & 1) newX = m_dragResize.startBounds.x + m_dragResize.startBounds.z - m_config.minWidth;
                newW = m_config.minWidth;
            }
            if (newH < m_config.minHeight) {
                if (m_dragResize.resizing & 4) newY = m_dragResize.startBounds.y + m_dragResize.startBounds.w - m_config.minHeight;
                newH = m_config.minHeight;
            }

            m_config.bounds = glm::vec4(newX, newY, newW, newH);
        } else {
            m_dragResize.resizing = 0;
        }
    }

    // -------------------------------------------------------------------------
    // Soft clamp to screen bounds - allow panels to go partially off-screen
    // but keep at least 50px visible so user can grab them
    // -------------------------------------------------------------------------
    constexpr float kMinVisiblePx = 50.0f;
    m_config.bounds.x = std::max(-m_config.bounds.z + kMinVisiblePx, std::min(m_config.bounds.x, screenW - kMinVisiblePx));
    m_config.bounds.y = std::max(-m_config.bounds.w + kMinVisiblePx, std::min(m_config.bounds.y, screenH - kMinVisiblePx));
    m_config.bounds.z = std::max(m_config.minWidth, m_config.bounds.z);
    m_config.bounds.w = std::max(m_config.minHeight, m_config.bounds.w);

    // Update consumed input state
    m_inputRouting.consumedInput = m_focus.hovered || m_dragResize.isActive();
}

glm::vec4 Panel::beginRender(const gui::InputState& input, const glm::vec4& bounds) {
    if (m_display.showTitleBar) {
        // Floating panel - handle drag/resize and use own bounds (logical coordinates)
        float screenW = static_cast<float>(input.logicalWidth());
        float screenH = static_cast<float>(input.logicalHeight());
        handleDragAndResize(input, screenW, screenH);
        return m_config.bounds;
    } else {
        // Layout-managed panel - use bounds from parent container
        // Set hover state (normally done in handleDragAndResize)
        m_focus.hovered = input.mousePos.x >= bounds.x && input.mousePos.x <= bounds.x + bounds.z &&
                          input.mousePos.y >= bounds.y && input.mousePos.y <= bounds.y + bounds.w;
        return bounds;
    }
}

void Panel::renderChrome(OverlayCanvas& canvas, float x, float y, float w, float h,
                          const UIStyle& style, bool showTitleBar) {
    // All dimensions in logical pixels - canvas handles scaling
    float cornerRadius = style.panelCornerRadius();

    // Background - use style alpha (default 0.95 from UIStyle)
    glm::vec4 bgColor = style.panelBg;

    if (cornerRadius > 0.0f) {
        canvas.fillRoundedRect(x, y, w, h, cornerRadius, bgColor);
        canvas.strokeRoundedRect(x, y, w, h, cornerRadius, 1.0f, style.panelBorder);
    } else {
        canvas.fillRect(x, y, w, h, bgColor);
        canvas.strokeRect(x, y, w, h, 1.0f, style.panelBorder);
    }

    // Title bar
    if (showTitleBar) {
        glm::vec4 headerColor = style.headerBg;
        canvas.fillRoundedRectTop(x, y, w, style.titleBarHeight(), cornerRadius, headerColor);

        // Title text - vertically centered using font metrics
        float ascent = canvas.fontAscent(0);
        float descent = std::abs(canvas.fontDescent(0));
        float titleBaseline = y + style.titleBarHeight() * 0.5f + (ascent - descent) * 0.5f;
        canvas.text(m_config.title, x + 10, titleBaseline,
                    style.textPrimary, 0);
    }
}

} // namespace vivid
