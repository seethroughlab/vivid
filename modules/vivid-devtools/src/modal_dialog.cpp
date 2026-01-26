// ModalDialog implementation - base class for modal overlays

#include <vivid/devtools/modal_dialog.h>
#include <vivid/gui/ui_style.h>
#include <GLFW/glfw3.h>

namespace vivid {

ModalDialog::ModalDialog(const std::string& title, float width, float height)
    : m_title(title)
    , m_width(width)
    , m_height(height) {}

ModalDialog::~ModalDialog() = default;

void ModalDialog::show() {
    m_visible = true;
}

void ModalDialog::hide() {
    m_visible = false;
    if (m_onClose) {
        m_onClose();
    }
}

void ModalDialog::render(OverlayCanvas& canvas, const FrameInput& input,
                          float screenWidth, float screenHeight,
                          const UIStyle& style) {
    if (!m_visible) return;

    // All dimensions in logical pixels - canvas handles scaling
    float dialogW = m_width;
    float dialogH = m_height;
    float dialogX = (screenWidth - dialogW) / 2.0f;
    float dialogY = (screenHeight - dialogH) / 2.0f;

    // Store for input handling
    m_dialogBounds = {dialogX, dialogY, m_width, m_height};

    // Set layer for modal overlay (blocks content behind it)
    canvas.setLayer(UILayer::ModalOverlay);

    // Darkened background overlay - use solid color to fully block content behind
    glm::vec4 overlayColor(0.0f, 0.0f, 0.0f, 0.85f);
    canvas.fillRect(0, 0, screenWidth, screenHeight, overlayColor);

    // Set layer for modal dialog content
    canvas.setLayer(UILayer::ModalDialog);

    // Dialog background with shadow
    float shadowOffset = 4.0f;
    glm::vec4 shadowColor(0.0f, 0.0f, 0.0f, 0.3f);
    canvas.fillRoundedRect(dialogX + shadowOffset, dialogY + shadowOffset,
                           dialogW, dialogH, kCornerRadius, shadowColor);

    // Dialog background - force opaque
    glm::vec4 bgColor = style.panelBg;
    bgColor.a = 1.0f;
    canvas.fillRoundedRect(dialogX, dialogY, dialogW, dialogH,
                           kCornerRadius, bgColor);

    // Title bar - force opaque
    float titleBarH = kTitleBarHeight;
    glm::vec4 headerColor = style.headerBg;
    headerColor.a = 1.0f;
    canvas.fillRoundedRectTop(dialogX, dialogY, dialogW, titleBarH,
                               kCornerRadius, headerColor);

    // Title text
    canvas.text(m_title, dialogX + 12, dialogY + titleBarH - 10,
                style.textPrimary, 1);  // Font index 1 = medium

    // Close button (X)
    float closeX = dialogX + dialogW - 28;
    float closeY = dialogY + titleBarH / 2;
    float closeSize = 6;

    // Check if mouse is over close button (input is in logical pixels)
    glm::vec2 mousePos = input.mousePos;
    bool closeHovered = mousePos.x >= closeX - 12 && mousePos.x <= closeX + 12 &&
                        mousePos.y >= closeY - 12 && mousePos.y <= closeY + 12;

    glm::vec4 closeColor = closeHovered ? style.error : style.textDim;
    canvas.line(closeX - closeSize, closeY - closeSize,
                closeX + closeSize, closeY + closeSize, 2.0f, closeColor);
    canvas.line(closeX + closeSize, closeY - closeSize,
                closeX - closeSize, closeY + closeSize, 2.0f, closeColor);

    // Handle close button click
    bool leftMouseDown = input.mouseDown[0];
    bool leftMouseClicked = leftMouseDown && !m_lastMouseDown;

    if (closeHovered && leftMouseClicked) {
        hide();
        m_lastMouseDown = leftMouseDown;
        return;
    }

    // Handle click outside to close
    if (m_clickOutsideToClose && leftMouseClicked) {
        bool insideDialog = mousePos.x >= dialogX && mousePos.x <= dialogX + dialogW &&
                            mousePos.y >= dialogY && mousePos.y <= dialogY + dialogH;
        if (!insideDialog) {
            hide();
            m_lastMouseDown = leftMouseDown;
            return;
        }
    }

    m_lastMouseDown = leftMouseDown;

    // Border
    canvas.strokeRoundedRect(dialogX, dialogY, dialogW, dialogH,
                              kCornerRadius, 1.0f, style.panelBorder);

    // Content area (in logical pixels)
    glm::vec4 contentBounds(
        dialogX + 12,
        dialogY + titleBarH + 8,
        dialogW - 24,
        dialogH - titleBarH - 20
    );

    // Clip content to dialog bounds
    canvas.beginClipRect(contentBounds.x, contentBounds.y, contentBounds.z, contentBounds.w);

    // Render subclass content
    renderContent(canvas, contentBounds, input, style);

    // Handle content input
    handleContentInput(input, contentBounds);

    // End content clipping
    canvas.endClipRect();

    // Reset layer
    canvas.setLayer(0);
}

bool ModalDialog::onKeyDown(int key, int mods) {
    if (!m_visible) return false;

    // Escape to close
    if (key == GLFW_KEY_ESCAPE) {
        hide();
        return true;
    }

    return true;  // Consume all input when visible
}

} // namespace vivid
