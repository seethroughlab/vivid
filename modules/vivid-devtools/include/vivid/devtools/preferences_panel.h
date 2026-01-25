#pragma once

/**
 * @file preferences_panel.h
 * @brief Preferences modal dialog for UI customization
 *
 * Provides tabs for:
 * - Appearance: Theme selection, color customization
 * - Shortcuts: View and rebind keyboard shortcuts
 * - Layout: Layout presets and reset options
 */

#include <vivid/devtools/modal_dialog.h>
#include <vivid/devtools/shortcut_manager.h>
#include <vivid/devtools/preferences.h>
#include <functional>
#include <vector>

namespace vivid {

class PanelManager;

/**
 * @brief Preference tabs
 */
enum class PreferenceTab {
    Appearance,
    Shortcuts,
    Layout
};

// ThemePreset is defined in preferences.h

/**
 * @brief Preferences modal dialog
 *
 * Features:
 * - Theme selection (Dark, Light, High Contrast)
 * - Live preview of color changes
 * - Keyboard shortcut viewer
 * - Layout preset selection
 *
 * Usage:
 * @code
 * PreferencesPanel prefs;
 * prefs.setStyle(&style);  // For live preview
 * prefs.setShortcuts(&shortcuts);  // To show shortcuts
 * prefs.show();
 * @endcode
 */
class PreferencesPanel : public ModalDialog {
public:
    PreferencesPanel();
    ~PreferencesPanel() override;

    /**
     * @brief Set the style to modify (for live preview)
     */
    void setStyle(UIStyle* style) { m_style = style; }

    /**
     * @brief Set the shortcut manager (for displaying shortcuts)
     */
    void setShortcuts(ShortcutManager* shortcuts) { m_shortcuts = shortcuts; }

    /**
     * @brief Set the panel manager (for layout presets)
     */
    void setPanelManager(PanelManager* manager) { m_panelManager = manager; }

    /**
     * @brief Set callback for when theme changes
     */
    void onThemeChange(std::function<void(ThemePreset)> callback) {
        m_onThemeChange = std::move(callback);
    }

    /**
     * @brief Set callback for when layout preset is selected
     */
    void onLayoutPreset(std::function<void(const std::string&)> callback) {
        m_onLayoutPreset = std::move(callback);
    }

    /**
     * @brief Handle character input (for shortcut rebinding)
     */
    void onChar(uint32_t codepoint) override;

protected:
    void renderContent(OverlayCanvas& canvas, const glm::vec4& contentBounds,
                       const FrameInput& input, float scale,
                       const UIStyle& style) override;

    bool handleContentInput(const FrameInput& input, const glm::vec4& contentBounds) override;

private:
    void renderTabs(OverlayCanvas& canvas, float x, float y, float w, float scale,
                    const UIStyle& style);
    void renderAppearanceTab(OverlayCanvas& canvas, const glm::vec4& bounds,
                              const FrameInput& input, float scale, const UIStyle& style);
    void renderShortcutsTab(OverlayCanvas& canvas, const glm::vec4& bounds,
                             const FrameInput& input, float scale, const UIStyle& style);
    void renderLayoutTab(OverlayCanvas& canvas, const glm::vec4& bounds,
                          const FrameInput& input, float scale, const UIStyle& style);

    // Helper to render a button
    bool renderButton(OverlayCanvas& canvas, const std::string& label,
                      float x, float y, float w, float h, float scale,
                      const UIStyle& style, const FrameInput& input, bool selected = false);

    // Helper to render a color swatch
    void renderColorSwatch(OverlayCanvas& canvas, const glm::vec4& color,
                           float x, float y, float size, float scale, const UIStyle& style);

    PreferenceTab m_activeTab = PreferenceTab::Appearance;
    ThemePreset m_currentTheme = ThemePreset::Dark;

    // External references (not owned)
    UIStyle* m_style = nullptr;
    ShortcutManager* m_shortcuts = nullptr;
    PanelManager* m_panelManager = nullptr;

    // Callbacks
    std::function<void(ThemePreset)> m_onThemeChange;
    std::function<void(const std::string&)> m_onLayoutPreset;

    // Tab geometry
    std::vector<glm::vec4> m_tabRects;
    int m_hoveredTab = -1;

    // Shortcut rebinding state
    int m_rebindingShortcut = -1;  // Index of shortcut being rebound, -1 if none

    // Scroll state for shortcuts list
    float m_shortcutsScrollY = 0.0f;

    // Input tracking
    bool m_lastMouseDown = false;

    static constexpr float kTabHeight = 32.0f;
    static constexpr float kButtonHeight = 28.0f;
    static constexpr float kRowHeight = 24.0f;
};

} // namespace vivid
