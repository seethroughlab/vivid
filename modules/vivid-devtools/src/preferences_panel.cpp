// PreferencesPanel implementation - UI customization dialog

#include <vivid/devtools/preferences_panel.h>
#include <vivid/devtools/preferences.h>
#include <vivid/devtools/panel_manager.h>
#include <GLFW/glfw3.h>
#include <algorithm>

namespace vivid {

PreferencesPanel::PreferencesPanel()
    : ModalDialog("Preferences", 500, 400) {}

PreferencesPanel::~PreferencesPanel() = default;

void PreferencesPanel::onChar(uint32_t codepoint) {
    // Could be used for shortcut rebinding text input
}

void PreferencesPanel::renderContent(OverlayCanvas& canvas, const glm::vec4& contentBounds,
                                      const FrameInput& input, const UIStyle& style) {
    // All dimensions in logical pixels - canvas handles scaling
    float x = contentBounds.x;
    float y = contentBounds.y;
    float w = contentBounds.z;
    float h = contentBounds.w;

    // Render tabs
    float tabBarH = kTabHeight;
    renderTabs(canvas, x, y, w, style);

    // Content area below tabs
    glm::vec4 tabContent(x, y + tabBarH + 8, w, h - tabBarH - 8);

    // Render active tab content
    switch (m_activeTab) {
        case PreferenceTab::Appearance:
            renderAppearanceTab(canvas, tabContent, input, style);
            break;
        case PreferenceTab::Shortcuts:
            renderShortcutsTab(canvas, tabContent, input, style);
            break;
        case PreferenceTab::Layout:
            renderLayoutTab(canvas, tabContent, input, style);
            break;
    }
}

bool PreferencesPanel::handleContentInput(const FrameInput& input, const glm::vec4& contentBounds) {
    glm::vec2 mousePos = input.mousePos;
    bool leftMouseDown = input.mouseDown[0];
    bool leftMouseClicked = leftMouseDown && !m_lastMouseDown;
    m_lastMouseDown = leftMouseDown;

    // Handle tab clicks
    if (leftMouseClicked) {
        for (size_t i = 0; i < m_tabRects.size(); i++) {
            const glm::vec4& rect = m_tabRects[i];
            if (mousePos.x >= rect.x && mousePos.x <= rect.x + rect.z &&
                mousePos.y >= rect.y && mousePos.y <= rect.y + rect.w) {
                m_activeTab = static_cast<PreferenceTab>(i);
                return true;
            }
        }
    }

    // Update hovered tab
    m_hoveredTab = -1;
    for (size_t i = 0; i < m_tabRects.size(); i++) {
        const glm::vec4& rect = m_tabRects[i];
        if (mousePos.x >= rect.x && mousePos.x <= rect.x + rect.z &&
            mousePos.y >= rect.y && mousePos.y <= rect.y + rect.w) {
            m_hoveredTab = static_cast<int>(i);
            break;
        }
    }

    return false;
}

void PreferencesPanel::renderTabs(OverlayCanvas& canvas, float x, float y, float w,
                                   const UIStyle& style) {
    m_tabRects.clear();

    const char* tabNames[] = {"Appearance", "Shortcuts", "Layout"};
    float tabW = w / 3.0f;
    float tabH = kTabHeight;

    for (int i = 0; i < 3; i++) {
        float tabX = x + i * tabW;
        bool isActive = (static_cast<int>(m_activeTab) == i);
        bool isHovered = (m_hoveredTab == i);

        // Store for hit testing (in logical pixels)
        m_tabRects.push_back({tabX, y, tabW, tabH});

        // Tab background
        glm::vec4 tabBg;
        if (isActive) {
            tabBg = style.accent;
            tabBg.a = 0.3f;
        } else if (isHovered) {
            tabBg = style.buttonHover;
        } else {
            tabBg = glm::vec4(0, 0, 0, 0);  // Transparent
        }

        if (tabBg.a > 0) {
            canvas.fillRect(tabX, y, tabW, tabH, tabBg);
        }

        // Tab text
        glm::vec4 textColor = isActive ? style.accent : style.textPrimary;
        float textX = tabX + tabW / 2 - strlen(tabNames[i]) * 3.5f;
        canvas.text(tabNames[i], textX, y + tabH - 10, textColor, 0);

        // Active indicator
        if (isActive) {
            canvas.fillRect(tabX, y + tabH - 2, tabW, 2, style.accent);
        }
    }

    // Separator line
    canvas.fillRect(x, y + tabH, w, 1, style.panelBorder);
}

void PreferencesPanel::renderAppearanceTab(OverlayCanvas& canvas, const glm::vec4& bounds,
                                            const FrameInput& input, const UIStyle& style) {
    float x = bounds.x;
    float y = bounds.y;
    float w = bounds.z;

    // Theme section
    canvas.text("Theme", x, y + 16, style.textTitle, 0);
    y += 28;

    // Theme buttons
    float buttonW = (w - 16) / 3.0f;
    float buttonH = kButtonHeight;

    // Get current theme from preferences
    ThemePreset currentPreset = Preferences::instance().themePreset();

    if (renderButton(canvas, "Dark", x, y, buttonW - 4, buttonH, style, input,
                     currentPreset == ThemePreset::Dark)) {
        Preferences::instance().setThemePreset(ThemePreset::Dark);
        if (m_onThemeChange) m_onThemeChange(ThemePreset::Dark);
    }

    if (renderButton(canvas, "Light", x + buttonW + 4, y, buttonW - 4, buttonH,
                     style, input, currentPreset == ThemePreset::Light)) {
        Preferences::instance().setThemePreset(ThemePreset::Light);
        if (m_onThemeChange) m_onThemeChange(ThemePreset::Light);
    }

    if (renderButton(canvas, "High Contrast", x + 2 * buttonW + 8, y, buttonW - 4,
                     buttonH, style, input, currentPreset == ThemePreset::HighContrast)) {
        Preferences::instance().setThemePreset(ThemePreset::HighContrast);
        if (m_onThemeChange) m_onThemeChange(ThemePreset::HighContrast);
    }

    y += buttonH + 20;

    // Color preview section
    canvas.text("Color Preview", x, y + 16, style.textTitle, 0);
    y += 28;

    // Show current colors
    float swatchSize = 24;
    float swatchSpacing = 8;

    struct ColorPreview {
        const char* name;
        glm::vec4 color;
    };

    std::vector<ColorPreview> colors;
    if (m_style) {
        colors = {
            {"Panel", m_style->panelBg},
            {"Header", m_style->headerBg},
            {"Accent", m_style->accent},
            {"Text", m_style->textPrimary},
            {"Button", m_style->buttonBg},
            {"Slider", m_style->sliderFill},
            {"Success", m_style->success},
            {"Warning", m_style->warning},
            {"Error", m_style->error},
        };
    }

    float colorX = x;
    for (const auto& cp : colors) {
        renderColorSwatch(canvas, cp.color, colorX, y, swatchSize, style);

        // Label below
        float labelW = strlen(cp.name) * 6.0f;
        canvas.text(cp.name, colorX + swatchSize / 2 - labelW / 2,
                    y + swatchSize + 12, style.textDim, 0);

        colorX += swatchSize + swatchSpacing + 32;
        if (colorX + swatchSize > x + w) {
            colorX = x;
            y += swatchSize + 28;
        }
    }

    // Skip past color swatches
    y += swatchSize + 32;

    // Corner Radius section
    canvas.text("Corner Radius", x, y + 16, style.textTitle, 0);
    y += 28;

    auto& prefs = Preferences::instance();
    float sliderW = w - 60;  // Leave room for value label

    // Panel corner radius slider
    float panelRadius = prefs.panelCornerRadius();
    if (renderSlider(canvas, "Panels", x, y, sliderW, kSliderHeight, style, input,
                     &panelRadius, 0.0f, 12.0f)) {
        prefs.setPanelCornerRadius(panelRadius);
    }
    // Value label
    char valBuf[16];
    snprintf(valBuf, sizeof(valBuf), "%.0f", panelRadius);
    canvas.text(valBuf, x + sliderW + 8, y + kSliderHeight - 4, style.textDim, 0);
    y += kSliderHeight + 8;

    // Button corner radius slider
    float buttonRadius = prefs.buttonCornerRadius();
    if (renderSlider(canvas, "Buttons", x, y, sliderW, kSliderHeight, style, input,
                     &buttonRadius, 0.0f, 8.0f)) {
        prefs.setButtonCornerRadius(buttonRadius);
    }
    snprintf(valBuf, sizeof(valBuf), "%.0f", buttonRadius);
    canvas.text(valBuf, x + sliderW + 8, y + kSliderHeight - 4, style.textDim, 0);
    y += kSliderHeight + 8;

    // Slider corner radius slider
    float sliderRadius = prefs.sliderCornerRadius();
    if (renderSlider(canvas, "Sliders", x, y, sliderW, kSliderHeight, style, input,
                     &sliderRadius, 0.0f, 6.0f)) {
        prefs.setSliderCornerRadius(sliderRadius);
    }
    snprintf(valBuf, sizeof(valBuf), "%.0f", sliderRadius);
    canvas.text(valBuf, x + sliderW + 8, y + kSliderHeight - 4, style.textDim, 0);
}

void PreferencesPanel::renderShortcutsTab(OverlayCanvas& canvas, const glm::vec4& bounds,
                                           const FrameInput& input, const UIStyle& style) {
    float x = bounds.x;
    float y = bounds.y;
    float w = bounds.z;
    float h = bounds.w;

    if (!m_shortcuts) {
        canvas.text("No shortcuts available", x, y + 20, style.textDim, 0);
        return;
    }

    // Header
    canvas.text("Keyboard Shortcuts", x, y + 16, style.textTitle, 0);
    y += 28;

    // Column headers
    float labelColW = w * 0.6f;
    float shortcutColW = w * 0.4f;

    canvas.text("Action", x, y + 14, style.textDim, 0);
    canvas.text("Shortcut", x + labelColW, y + 14, style.textDim, 0);
    y += 20;

    // Separator
    canvas.fillRect(x, y, w, 1, style.panelBorder);
    y += 8;

    // List shortcuts
    const auto& shortcuts = m_shortcuts->shortcuts();
    float rowH = kRowHeight;

    for (size_t i = 0; i < shortcuts.size(); i++) {
        const Shortcut& sc = shortcuts[i];

        // Skip if off-screen (simple culling)
        if (y > bounds.y + h) break;
        if (y + rowH < bounds.y) {
            y += rowH;
            continue;
        }

        // Row background on hover
        glm::vec2 mousePos = input.mousePos;
        bool rowHovered = mousePos.x >= x && mousePos.x <= x + w &&
                          mousePos.y >= y && mousePos.y <= y + rowH;

        if (rowHovered) {
            canvas.fillRect(x, y, w, rowH, style.buttonHover);
        }

        // Action label
        canvas.text(sc.label, x + 4, y + rowH - 6, style.textPrimary, 0);

        // Shortcut key
        std::string shortcutStr = ShortcutManager::formatShortcut(sc);
        canvas.text(shortcutStr, x + labelColW, y + rowH - 6, style.accent, 0);

        y += rowH;
    }
}

void PreferencesPanel::renderLayoutTab(OverlayCanvas& canvas, const glm::vec4& bounds,
                                        const FrameInput& input, const UIStyle& style) {
    float x = bounds.x;
    float y = bounds.y;
    float w = bounds.z;

    // Layout presets section
    canvas.text("Layout Presets", x, y + 16, style.textTitle, 0);
    y += 28;

    float buttonW = (w - 8) / 2.0f;
    float buttonH = kButtonHeight;

    if (renderButton(canvas, "Default", x, y, buttonW, buttonH, style, input, false)) {
        if (m_onLayoutPreset) m_onLayoutPreset("default");
    }

    if (renderButton(canvas, "IDE Focus", x + buttonW + 8, y, buttonW, buttonH,
                     style, input, false)) {
        if (m_onLayoutPreset) m_onLayoutPreset("ide");
    }

    y += buttonH + 8;

    if (renderButton(canvas, "Visualizer Focus", x, y, buttonW, buttonH, style, input, false)) {
        if (m_onLayoutPreset) m_onLayoutPreset("visualizer");
    }

    if (renderButton(canvas, "Minimal", x + buttonW + 8, y, buttonW, buttonH,
                     style, input, false)) {
        if (m_onLayoutPreset) m_onLayoutPreset("minimal");
    }

    y += buttonH + 24;

    // Layout mode section
    canvas.text("Layout Mode", x, y + 16, style.textTitle, 0);
    y += 28;

    bool layoutMode = m_panelManager && m_panelManager->isLayoutMode();
    std::string modeLabel = layoutMode ? "Docking Mode (Experimental)" : "Classic Mode";
    canvas.text(modeLabel, x, y + 14, style.textPrimary, 0);
    y += 24;

    canvas.text("Press Cmd/Ctrl+L to toggle layout mode", x, y + 14, style.textDim, 0);
    y += 32;

    // Reset section
    canvas.text("Reset", x, y + 16, style.textTitle, 0);
    y += 28;

    if (renderButton(canvas, "Reset to Defaults", x, y, w, buttonH, style, input, false)) {
        // Reset theme via Preferences (will save automatically)
        Preferences::instance().setThemePreset(ThemePreset::Dark);
        if (m_onThemeChange) m_onThemeChange(ThemePreset::Dark);

        // Reset layout mode
        if (m_panelManager) {
            m_panelManager->setLayoutMode(false);
        }
    }
}

bool PreferencesPanel::renderButton(OverlayCanvas& canvas, const std::string& label,
                                     float x, float y, float w, float h,
                                     const UIStyle& style, const FrameInput& input,
                                     bool selected) {
    // Input is in logical pixels
    glm::vec2 mousePos = input.mousePos;
    bool hovered = mousePos.x >= x && mousePos.x <= x + w &&
                   mousePos.y >= y && mousePos.y <= y + h;

    bool clicked = hovered && input.mouseDown[0] && !m_lastMouseDown;

    // Background
    glm::vec4 bg;
    if (selected) {
        bg = style.accent;
        bg.a = 0.4f;
    } else if (hovered) {
        bg = style.buttonHover;
    } else {
        bg = style.buttonBg;
    }

    float radius = style.buttonCornerRadius();
    canvas.fillRoundedRect(x, y, w, h, radius, bg);

    // Border
    glm::vec4 border = selected ? style.accent : style.buttonBorder;
    canvas.strokeRoundedRect(x, y, w, h, radius, style.strokeWidth(), border);

    // Text (centered)
    float textW = label.length() * 7.0f;
    float textX = x + (w - textW) / 2;
    float textY = y + h - 8;
    glm::vec4 textColor = selected ? style.accent : style.textPrimary;
    canvas.text(label, textX, textY, textColor, 0);

    return clicked;
}

bool PreferencesPanel::renderSlider(OverlayCanvas& canvas, const std::string& label,
                                     float x, float y, float w, float h,
                                     const UIStyle& style, const FrameInput& input,
                                     float* value, float minVal, float maxVal) {
    bool changed = false;
    glm::vec2 mousePos = input.mousePos;

    // Label on the left
    float labelW = 60.0f;
    canvas.text(label, x, y + h - 6, style.textDim, 0);

    // Slider track
    float trackX = x + labelW;
    float trackW = w - labelW;
    float trackH = 6.0f;
    float trackY = y + (h - trackH) / 2;

    // Track background
    float radius = style.sliderCornerRadius();
    canvas.fillRoundedRect(trackX, trackY, trackW, trackH, radius, style.sliderBg);

    // Calculate fill width
    float normalizedVal = (*value - minVal) / (maxVal - minVal);
    normalizedVal = std::max(0.0f, std::min(1.0f, normalizedVal));
    float fillW = trackW * normalizedVal;

    // Track fill
    if (fillW > 0) {
        canvas.fillRoundedRect(trackX, trackY, fillW, trackH, radius, style.sliderFill);
    }

    // Handle
    float handleRadius = 8.0f;
    float handleX = trackX + fillW;
    float handleY = trackY + trackH / 2;

    bool hovered = mousePos.x >= trackX - handleRadius && mousePos.x <= trackX + trackW + handleRadius &&
                   mousePos.y >= y && mousePos.y <= y + h;

    // Check for drag start
    if (hovered && input.mouseDown[0] && !m_lastMouseDown && !m_draggingSlider) {
        m_draggingSlider = true;
        m_draggingSliderValue = value;
        m_draggingSliderMin = minVal;
        m_draggingSliderMax = maxVal;
        m_draggingSliderRect = {trackX, trackY, trackW, trackH};
    }

    // Handle ongoing drag
    if (m_draggingSlider && m_draggingSliderValue == value) {
        if (input.mouseDown[0]) {
            float newNorm = (mousePos.x - trackX) / trackW;
            newNorm = std::max(0.0f, std::min(1.0f, newNorm));
            float newVal = minVal + newNorm * (maxVal - minVal);
            // Round to integer for cleaner values
            newVal = std::round(newVal);
            if (newVal != *value) {
                *value = newVal;
                changed = true;
            }
        } else {
            m_draggingSlider = false;
            m_draggingSliderValue = nullptr;
        }
    }

    // Draw handle
    glm::vec4 handleColor = (m_draggingSlider && m_draggingSliderValue == value)
        ? style.sliderActive : (hovered ? style.accent : style.sliderFill);
    canvas.fillCircle(handleX, handleY, handleRadius, handleColor, 16);

    return changed;
}

void PreferencesPanel::renderColorSwatch(OverlayCanvas& canvas, const glm::vec4& color,
                                          float x, float y, float size, const UIStyle& style) {
    // Checkerboard background for alpha
    float checkSize = 4;
    glm::vec4 checkLight(0.4f, 0.4f, 0.4f, 1.0f);
    glm::vec4 checkDark(0.2f, 0.2f, 0.2f, 1.0f);

    for (float cy = y; cy < y + size; cy += checkSize) {
        for (float cx = x; cx < x + size; cx += checkSize) {
            int row = static_cast<int>((cy - y) / checkSize);
            int col = static_cast<int>((cx - x) / checkSize);
            glm::vec4 checkColor = ((row + col) % 2 == 0) ? checkLight : checkDark;
            canvas.fillRect(cx, cy, checkSize, checkSize, checkColor);
        }
    }

    // Color overlay
    canvas.fillRect(x, y, size, size, color);

    // Border
    canvas.strokeRect(x, y, size, size, 1.0f, style.panelBorder);
}

} // namespace vivid
