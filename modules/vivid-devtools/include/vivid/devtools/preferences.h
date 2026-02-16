#pragma once

/**
 * @file preferences.h
 * @brief User preferences storage and persistence
 *
 * Handles saving/loading user preferences to disk.
 * Stored in ~/.vivid/preferences.json
 */

#include <vivid/gui/ui_style.h>
#include <glm/glm.hpp>
#include <string>
#include <functional>
#include <unordered_map>

namespace vivid {

/**
 * @brief Theme preset names
 */
enum class ThemePreset {
    Dark,
    Light,
    HighContrast,
    Custom
};

/**
 * @brief User preferences storage
 */
class Preferences {
public:
    /**
     * @brief Get the singleton instance
     */
    static Preferences& instance();

    /**
     * @brief Load preferences from disk
     * @return true if loaded successfully (or file doesn't exist yet)
     */
    bool load();

    /**
     * @brief Save preferences to disk
     * @return true if saved successfully
     */
    bool save();

    /**
     * @brief Get the preferences file path
     */
    static std::string getPrefsPath();

    // -------------------------------------------------------------------------
    /// @name Theme
    /// @{

    /**
     * @brief Get the current theme preset
     */
    ThemePreset themePreset() const { return m_themePreset; }

    /**
     * @brief Set the theme preset and update style
     */
    void setThemePreset(ThemePreset preset);

    /**
     * @brief Get the current UI style
     */
    const UIStyle& style() const { return m_style; }

    /**
     * @brief Get mutable style (for custom modifications)
     */
    UIStyle& style() { return m_style; }

    /**
     * @brief Apply current theme preset to style
     */
    void applyThemeToStyle();

    /**
     * @brief Set callback for when style changes
     */
    using StyleChangeCallback = std::function<void(const UIStyle&)>;
    void onStyleChange(StyleChangeCallback callback) { m_styleChangeCallback = callback; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Panel Visibility (remembered across sessions)
    /// @{

    bool visualizerVisible() const { return m_visualizerVisible; }
    void setVisualizerVisible(bool v) { m_visualizerVisible = v; }

    float gridOpacity() const { return m_gridOpacity; }
    void setGridOpacity(float opacity);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Corner Radius Settings
    /// @{

    /**
     * @brief Get panel corner radius (0 = square corners)
     */
    float panelCornerRadius() const { return m_panelCornerRadius; }

    /**
     * @brief Set panel corner radius and update style
     */
    void setPanelCornerRadius(float radius);

    /**
     * @brief Get button corner radius
     */
    float buttonCornerRadius() const { return m_buttonCornerRadius; }

    /**
     * @brief Set button corner radius and update style
     */
    void setButtonCornerRadius(float radius);

    /**
     * @brief Get slider corner radius
     */
    float sliderCornerRadius() const { return m_sliderCornerRadius; }

    /**
     * @brief Set slider corner radius and update style
     */
    void setSliderCornerRadius(float radius);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Panel Bounds (remembered across sessions)
    /// @{

    /**
     * @brief Get saved panel bounds
     * @return true if bounds were found for this panel
     */
    bool getPanelBounds(const std::string& id, glm::vec4& out) const;

    /**
     * @brief Save panel bounds
     */
    void setPanelBounds(const std::string& id, const glm::vec4& bounds);

    /// @}

private:
    Preferences() = default;
    ~Preferences() = default;

    // Non-copyable
    Preferences(const Preferences&) = delete;
    Preferences& operator=(const Preferences&) = delete;

    // Theme
    ThemePreset m_themePreset = ThemePreset::Dark;
    UIStyle m_style;

    // Panel visibility
    bool m_visualizerVisible = true;
    float m_gridOpacity = 0.0f;

    // Panel bounds (position/size persistence)
    std::unordered_map<std::string, glm::vec4> m_panelBounds;

    // Corner radii (default to UIStyle defaults)
    float m_panelCornerRadius = 6.0f;
    float m_buttonCornerRadius = 4.0f;
    float m_sliderCornerRadius = 3.0f;

    // Callbacks
    StyleChangeCallback m_styleChangeCallback;
};

// Theme creation functions are defined in ui_style.h:
// - createDarkTheme()
// - createLightTheme()
// - createHighContrastTheme()

} // namespace vivid
