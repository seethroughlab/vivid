// Panel base class implementation
// Provides common drag/resize functionality for all panels

#include <vivid/devtools/panel.h>
#include <vivid/gui/ui_style.h>
#include <algorithm>

namespace vivid {

Panel::Panel() {
    // Default configuration
    m_config.id = "";
    m_config.title = "";
    m_config.bounds = {20, 60, 400, 300};
    m_config.dockSide = DockSide::None;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 200.0f;
    m_config.minHeight = 150.0f;
}

Panel::~Panel() = default;

void Panel::handleDragAndResize(const FrameInput& input, float screenW, float screenH,
                                 float titleBarHeight, bool /*allowNewInteraction*/) {
    if (!m_config.visible) {
        m_hovered = false;
        m_consumedInput = false;
        return;
    }

    glm::vec2 mousePos = input.mousePos;

    float x = m_config.bounds.x;
    float y = m_config.bounds.y;
    float w = m_config.bounds.z;
    float h = m_config.bounds.w;
    float hitSize = 8.0f;

    // Check if mouse is in panel bounds (with resize margin)
    m_hovered = mousePos.x >= x - hitSize && mousePos.x <= x + w + hitSize &&
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
        if (m_canStartInteraction && overTitleBar && leftMouseClicked && !m_dragging && m_resizing == 0) {
            m_dragging = true;
            m_dragOffset = mousePos - glm::vec2(x, y);
        }

        // Continue existing drag regardless of m_canStartInteraction or inputBlocked
        if (m_dragging) {
            if (leftMouseDown) {
                m_config.bounds.x = mousePos.x - m_dragOffset.x;
                m_config.bounds.y = mousePos.y - m_dragOffset.y;
            } else {
                m_dragging = false;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Resizing (from edges)
    // -------------------------------------------------------------------------
    // Only start new resize if this panel can start interactions
    if (m_canStartInteraction && m_config.resizable && !m_dragging && m_resizing == 0 && leftMouseClicked) {
        int edge = 0;
        if (mousePos.x >= x - hitSize && mousePos.x <= x + hitSize) edge |= 1;  // left
        if (mousePos.x >= x + w - hitSize && mousePos.x <= x + w + hitSize) edge |= 2;  // right
        if (mousePos.y >= y - hitSize && mousePos.y <= y + hitSize) edge |= 4;  // top
        if (mousePos.y >= y + h - hitSize && mousePos.y <= y + h + hitSize) edge |= 8;  // bottom

        if (edge != 0) {
            m_resizing = edge;
            m_resizeStartBounds = m_config.bounds;
            m_resizeStartMouse = mousePos;
        }
    }

    if (m_resizing != 0) {
        if (leftMouseDown) {
            glm::vec2 delta = mousePos - m_resizeStartMouse;
            float newX = m_resizeStartBounds.x;
            float newY = m_resizeStartBounds.y;
            float newW = m_resizeStartBounds.z;
            float newH = m_resizeStartBounds.w;

            if (m_resizing & 1) { newX += delta.x; newW -= delta.x; }  // left
            if (m_resizing & 2) { newW += delta.x; }  // right
            if (m_resizing & 4) { newY += delta.y; newH -= delta.y; }  // top
            if (m_resizing & 8) { newH += delta.y; }  // bottom

            // Enforce minimum size
            if (newW < m_config.minWidth) {
                if (m_resizing & 1) newX = m_resizeStartBounds.x + m_resizeStartBounds.z - m_config.minWidth;
                newW = m_config.minWidth;
            }
            if (newH < m_config.minHeight) {
                if (m_resizing & 4) newY = m_resizeStartBounds.y + m_resizeStartBounds.w - m_config.minHeight;
                newH = m_config.minHeight;
            }

            m_config.bounds = glm::vec4(newX, newY, newW, newH);
        } else {
            m_resizing = 0;
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
    m_consumedInput = m_hovered || m_dragging || m_resizing != 0;
}

void Panel::renderChrome(OverlayCanvas& canvas, float x, float y, float w, float h,
                          const UIStyle& style, bool showTitleBar,
                          const FrameInput* input) {
    // Reset close button state
    m_closeButtonClicked = false;

    // All dimensions in logical pixels - canvas handles scaling
    float cornerRadius = style.panelCornerRadius();
    float titleBarHeight = style.titleBarHeight();

    // Use square corners when docked to edge (no rounded corners at top)
    float effectiveRadius = m_squareTopCorners ? 0.0f : cornerRadius;

    // Background - use fully opaque to prevent bleed-through
    glm::vec4 bgColor = style.panelBg;
    bgColor.a = 1.0f;  // Force opaque

    if (effectiveRadius > 0.0f) {
        canvas.fillRoundedRect(x, y, w, h, effectiveRadius, bgColor);
        canvas.strokeRoundedRect(x, y, w, h, effectiveRadius, 1.0f, style.panelBorder);
    } else {
        canvas.fillRect(x, y, w, h, bgColor);
        canvas.strokeRect(x, y, w, h, 1.0f, style.panelBorder);
    }

    // Title bar - use fully opaque
    if (showTitleBar) {
        glm::vec4 headerColor = style.headerBg;
        headerColor.a = 1.0f;  // Force opaque
        if (m_squareTopCorners) {
            // Square corners at top
            canvas.fillRect(x, y, w, titleBarHeight, headerColor);
        } else {
            canvas.fillRoundedRectTop(x, y, w, titleBarHeight, cornerRadius, headerColor);
        }

        // Title text
        canvas.text(m_config.title, x + 10, y + 18,
                    style.textPrimary, 0);

        // Close button (X) on right side
        float closeSize = 8.0f;
        float closePadding = 12.0f;
        float closeX = x + w - closePadding - closeSize;
        float closeY = y + titleBarHeight / 2;

        // Check hover and click
        m_closeButtonHovered = false;
        if (input) {
            float hitPadding = 4.0f;
            bool overClose = input->mousePos.x >= closeX - closeSize - hitPadding &&
                             input->mousePos.x <= closeX + closeSize + hitPadding &&
                             input->mousePos.y >= closeY - closeSize - hitPadding &&
                             input->mousePos.y <= closeY + closeSize + hitPadding;

            if (overClose) {
                m_closeButtonHovered = true;
                if (input->mouseClicked[0]) {
                    m_closeButtonClicked = true;
                    if (m_onClose) {
                        m_onClose(this);
                    }
                }
            }
        }

        // Draw X with hover effect
        glm::vec4 closeColor = m_closeButtonHovered
            ? glm::vec4(1.0f, 0.4f, 0.4f, 1.0f)  // Red on hover
            : style.textDim;
        float lineWidth = m_closeButtonHovered ? 2.0f : 1.5f;

        canvas.line(closeX - closeSize, closeY - closeSize,
                    closeX + closeSize, closeY + closeSize, lineWidth, closeColor);
        canvas.line(closeX + closeSize, closeY - closeSize,
                    closeX - closeSize, closeY + closeSize, lineWidth, closeColor);
    }
}

} // namespace vivid
