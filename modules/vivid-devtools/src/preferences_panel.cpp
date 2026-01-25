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
                                      const FrameInput& input, float scale,
                                      const UIStyle& style) {
    float x = contentBounds.x;
    float y = contentBounds.y;
    float w = contentBounds.z;
    float h = contentBounds.w;

    // Render tabs
    float tabBarH = kTabHeight * scale;
    renderTabs(canvas, x, y, w, scale, style);

    // Content area below tabs
    glm::vec4 tabContent(x, y + tabBarH + 8 * scale, w, h - tabBarH - 8 * scale);

    // Render active tab content
    switch (m_activeTab) {
        case PreferenceTab::Appearance:
            renderAppearanceTab(canvas, tabContent, input, scale, style);
            break;
        case PreferenceTab::Shortcuts:
            renderShortcutsTab(canvas, tabContent, input, scale, style);
            break;
        case PreferenceTab::Layout:
            renderLayoutTab(canvas, tabContent, input, scale, style);
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

void PreferencesPanel::renderTabs(OverlayCanvas& canvas, float x, float y, float w, float scale,
                                   const UIStyle& style) {
    m_tabRects.clear();

    const char* tabNames[] = {"Appearance", "Shortcuts", "Layout"};
    float tabW = w / 3.0f;
    float tabH = kTabHeight * scale;

    for (int i = 0; i < 3; i++) {
        float tabX = x + i * tabW;
        bool isActive = (static_cast<int>(m_activeTab) == i);
        bool isHovered = (m_hoveredTab == i);

        // Store for hit testing (in logical pixels)
        m_tabRects.push_back({tabX / scale, y / scale, tabW / scale, tabH / scale});

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
        float textX = tabX + tabW / 2 - strlen(tabNames[i]) * 3.5f * scale;
        canvas.text(tabNames[i], textX, y + tabH - 10 * scale, textColor, 0);

        // Active indicator
        if (isActive) {
            canvas.fillRect(tabX, y + tabH - 2 * scale, tabW, 2 * scale, style.accent);
        }
    }

    // Separator line
    canvas.fillRect(x, y + tabH, w, 1 * scale, style.panelBorder);
}

void PreferencesPanel::renderAppearanceTab(OverlayCanvas& canvas, const glm::vec4& bounds,
                                            const FrameInput& input, float scale,
                                            const UIStyle& style) {
    float x = bounds.x;
    float y = bounds.y;
    float w = bounds.z;

    // Theme section
    canvas.text("Theme", x, y + 16 * scale, style.textTitle, 0);
    y += 28 * scale;

    // Theme buttons
    float buttonW = (w - 16 * scale) / 3.0f;
    float buttonH = kButtonHeight * scale;

    // Get current theme from preferences
    ThemePreset currentPreset = Preferences::instance().themePreset();

    if (renderButton(canvas, "Dark", x, y, buttonW - 4 * scale, buttonH, scale, style, input,
                     currentPreset == ThemePreset::Dark)) {
        Preferences::instance().setThemePreset(ThemePreset::Dark);
        if (m_onThemeChange) m_onThemeChange(ThemePreset::Dark);
    }

    if (renderButton(canvas, "Light", x + buttonW + 4 * scale, y, buttonW - 4 * scale, buttonH,
                     scale, style, input, currentPreset == ThemePreset::Light)) {
        Preferences::instance().setThemePreset(ThemePreset::Light);
        if (m_onThemeChange) m_onThemeChange(ThemePreset::Light);
    }

    if (renderButton(canvas, "High Contrast", x + 2 * buttonW + 8 * scale, y, buttonW - 4 * scale,
                     buttonH, scale, style, input, currentPreset == ThemePreset::HighContrast)) {
        Preferences::instance().setThemePreset(ThemePreset::HighContrast);
        if (m_onThemeChange) m_onThemeChange(ThemePreset::HighContrast);
    }

    y += buttonH + 20 * scale;

    // Color preview section
    canvas.text("Color Preview", x, y + 16 * scale, style.textTitle, 0);
    y += 28 * scale;

    // Show current colors
    float swatchSize = 24 * scale;
    float swatchSpacing = 8 * scale;

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
        renderColorSwatch(canvas, cp.color, colorX, y, swatchSize, scale, style);

        // Label below
        float labelW = strlen(cp.name) * 6.0f * scale;
        canvas.text(cp.name, colorX + swatchSize / 2 - labelW / 2,
                    y + swatchSize + 12 * scale, style.textDim, 0);

        colorX += swatchSize + swatchSpacing + 32 * scale;
        if (colorX + swatchSize > x + w) {
            colorX = x;
            y += swatchSize + 28 * scale;
        }
    }
}

void PreferencesPanel::renderShortcutsTab(OverlayCanvas& canvas, const glm::vec4& bounds,
                                           const FrameInput& input, float scale,
                                           const UIStyle& style) {
    float x = bounds.x;
    float y = bounds.y;
    float w = bounds.z;
    float h = bounds.w;

    if (!m_shortcuts) {
        canvas.text("No shortcuts available", x, y + 20 * scale, style.textDim, 0);
        return;
    }

    // Header
    canvas.text("Keyboard Shortcuts", x, y + 16 * scale, style.textTitle, 0);
    y += 28 * scale;

    // Column headers
    float labelColW = w * 0.6f;
    float shortcutColW = w * 0.4f;

    canvas.text("Action", x, y + 14 * scale, style.textDim, 0);
    canvas.text("Shortcut", x + labelColW, y + 14 * scale, style.textDim, 0);
    y += 20 * scale;

    // Separator
    canvas.fillRect(x, y, w, 1 * scale, style.panelBorder);
    y += 8 * scale;

    // List shortcuts
    const auto& shortcuts = m_shortcuts->shortcuts();
    float rowH = kRowHeight * scale;

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
                          mousePos.y >= y / scale && mousePos.y <= (y + rowH) / scale;

        if (rowHovered) {
            canvas.fillRect(x, y, w, rowH, style.buttonHover);
        }

        // Action label
        canvas.text(sc.label, x + 4 * scale, y + rowH - 6 * scale, style.textPrimary, 0);

        // Shortcut key
        std::string shortcutStr = ShortcutManager::formatShortcut(sc);
        canvas.text(shortcutStr, x + labelColW, y + rowH - 6 * scale, style.accent, 0);

        y += rowH;
    }
}

void PreferencesPanel::renderLayoutTab(OverlayCanvas& canvas, const glm::vec4& bounds,
                                        const FrameInput& input, float scale,
                                        const UIStyle& style) {
    float x = bounds.x;
    float y = bounds.y;
    float w = bounds.z;

    // Layout presets section
    canvas.text("Layout Presets", x, y + 16 * scale, style.textTitle, 0);
    y += 28 * scale;

    float buttonW = (w - 8 * scale) / 2.0f;
    float buttonH = kButtonHeight * scale;

    if (renderButton(canvas, "Default", x, y, buttonW, buttonH, scale, style, input, false)) {
        if (m_onLayoutPreset) m_onLayoutPreset("default");
    }

    if (renderButton(canvas, "IDE Focus", x + buttonW + 8 * scale, y, buttonW, buttonH,
                     scale, style, input, false)) {
        if (m_onLayoutPreset) m_onLayoutPreset("ide");
    }

    y += buttonH + 8 * scale;

    if (renderButton(canvas, "Visualizer Focus", x, y, buttonW, buttonH, scale, style, input, false)) {
        if (m_onLayoutPreset) m_onLayoutPreset("visualizer");
    }

    if (renderButton(canvas, "Minimal", x + buttonW + 8 * scale, y, buttonW, buttonH,
                     scale, style, input, false)) {
        if (m_onLayoutPreset) m_onLayoutPreset("minimal");
    }

    y += buttonH + 24 * scale;

    // Layout mode section
    canvas.text("Layout Mode", x, y + 16 * scale, style.textTitle, 0);
    y += 28 * scale;

    bool layoutMode = m_panelManager && m_panelManager->isLayoutMode();
    std::string modeLabel = layoutMode ? "Docking Mode (Experimental)" : "Classic Mode";
    canvas.text(modeLabel, x, y + 14 * scale, style.textPrimary, 0);
    y += 24 * scale;

    canvas.text("Press Cmd/Ctrl+L to toggle layout mode", x, y + 14 * scale, style.textDim, 0);
    y += 32 * scale;

    // Reset section
    canvas.text("Reset", x, y + 16 * scale, style.textTitle, 0);
    y += 28 * scale;

    if (renderButton(canvas, "Reset to Defaults", x, y, w, buttonH, scale, style, input, false)) {
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
                                     float x, float y, float w, float h, float scale,
                                     const UIStyle& style, const FrameInput& input,
                                     bool selected) {
    glm::vec2 mousePos = input.mousePos * scale;
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

    float radius = 4.0f * scale;
    canvas.fillRoundedRect(x, y, w, h, radius, bg);

    // Border
    glm::vec4 border = selected ? style.accent : style.buttonBorder;
    canvas.strokeRoundedRect(x, y, w, h, radius, 1.0f * scale, border);

    // Text (centered)
    float textW = label.length() * 7.0f * scale;
    float textX = x + (w - textW) / 2;
    float textY = y + h - 8 * scale;
    glm::vec4 textColor = selected ? style.accent : style.textPrimary;
    canvas.text(label, textX, textY, textColor, 0);

    return clicked;
}

void PreferencesPanel::renderColorSwatch(OverlayCanvas& canvas, const glm::vec4& color,
                                          float x, float y, float size, float scale,
                                          const UIStyle& style) {
    // Checkerboard background for alpha
    float checkSize = 4 * scale;
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
    canvas.strokeRect(x, y, size, size, 1.0f * scale, style.panelBorder);
}

} // namespace vivid
