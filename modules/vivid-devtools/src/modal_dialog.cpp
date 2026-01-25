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
                          float screenWidth, float screenHeight, float scale,
                          const UIStyle& style) {
    if (!m_visible) return;

    // Calculate dialog position (centered)
    float dialogW = m_width * scale;
    float dialogH = m_height * scale;
    float dialogX = (screenWidth * scale - dialogW) / 2.0f;
    float dialogY = (screenHeight * scale - dialogH) / 2.0f;

    // Store for input handling
    m_dialogBounds = {dialogX / scale, dialogY / scale, m_width, m_height};

    // Set layer for modal overlay (blocks content behind it)
    canvas.setLayer(UILayer::ModalOverlay);

    // Darkened background overlay - use solid color to fully block content behind
    glm::vec4 overlayColor(0.0f, 0.0f, 0.0f, 0.85f);
    canvas.fillRect(0, 0, screenWidth * scale, screenHeight * scale, overlayColor);

    // Set layer for modal dialog content
    canvas.setLayer(UILayer::ModalDialog);

    // Dialog background with shadow
    float shadowOffset = 4.0f * scale;
    glm::vec4 shadowColor(0.0f, 0.0f, 0.0f, 0.3f);
    canvas.fillRoundedRect(dialogX + shadowOffset, dialogY + shadowOffset,
                           dialogW, dialogH, kCornerRadius * scale, shadowColor);

    // Dialog background - force opaque
    glm::vec4 bgColor = style.panelBg;
    bgColor.a = 1.0f;
    canvas.fillRoundedRect(dialogX, dialogY, dialogW, dialogH,
                           kCornerRadius * scale, bgColor);

    // Title bar - force opaque
    float titleBarH = kTitleBarHeight * scale;
    glm::vec4 headerColor = style.headerBg;
    headerColor.a = 1.0f;
    canvas.fillRoundedRectTop(dialogX, dialogY, dialogW, titleBarH,
                               kCornerRadius * scale, headerColor);

    // Title text
    canvas.text(m_title, dialogX + 12 * scale, dialogY + titleBarH - 10 * scale,
                style.textPrimary, 1);  // Font index 1 = medium

    // Close button (X)
    float closeX = dialogX + dialogW - 28 * scale;
    float closeY = dialogY + titleBarH / 2;
    float closeSize = 6 * scale;

    // Check if mouse is over close button
    glm::vec2 mousePos = input.mousePos * scale;
    bool closeHovered = mousePos.x >= closeX - 12 * scale && mousePos.x <= closeX + 12 * scale &&
                        mousePos.y >= closeY - 12 * scale && mousePos.y <= closeY + 12 * scale;

    glm::vec4 closeColor = closeHovered ? style.error : style.textDim;
    canvas.line(closeX - closeSize, closeY - closeSize,
                closeX + closeSize, closeY + closeSize, 2.0f * scale, closeColor);
    canvas.line(closeX + closeSize, closeY - closeSize,
                closeX - closeSize, closeY + closeSize, 2.0f * scale, closeColor);

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
                              kCornerRadius * scale, 1.0f * scale, style.panelBorder);

    // Content area
    glm::vec4 contentBounds(
        dialogX + 12 * scale,
        dialogY + titleBarH + 8 * scale,
        dialogW - 24 * scale,
        dialogH - titleBarH - 20 * scale
    );

    // Render subclass content
    renderContent(canvas, contentBounds, input, scale, style);

    // Handle content input
    handleContentInput(input, {
        (dialogX + 12 * scale) / scale,
        (dialogY + titleBarH + 8 * scale) / scale,
        (dialogW - 24 * scale) / scale,
        (dialogH - titleBarH - 20 * scale) / scale
    });

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
