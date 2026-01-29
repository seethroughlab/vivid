#pragma once

/**
 * @file preferences.h
 * @brief User preferences storage and persistence
 *
 * Handles saving/loading user preferences to disk.
 * Stored in ~/.vivid/preferences.json
 */

#include <vivid/gui/ui_style.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <functional>

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

    bool terminalVisible() const { return m_terminalVisible; }
    void setTerminalVisible(bool v) { m_terminalVisible = v; }

    bool editorVisible() const { return m_editorVisible; }
    void setEditorVisible(bool v) { m_editorVisible = v; }

    bool consoleVisible() const { return m_consoleVisible; }
    void setConsoleVisible(bool v) { m_consoleVisible = v; }

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
    /// @name Editor Session
    /// @{

    /**
     * @brief Get list of open file paths
     */
    const std::vector<std::string>& openFiles() const { return m_openFiles; }

    /**
     * @brief Set list of open file paths
     */
    void setOpenFiles(const std::vector<std::string>& files);

    /**
     * @brief Get active file path
     */
    const std::string& activeFile() const { return m_activeFile; }

    /**
     * @brief Set active file path
     */
    void setActiveFile(const std::string& path);

    /// @}
    // -------------------------------------------------------------------------
    /// @name File Browser Session
    /// @{

    /**
     * @brief Get expanded folders
     */
    const std::set<std::string>& expandedFolders() const { return m_expandedFolders; }

    /**
     * @brief Set expanded folders
     */
    void setExpandedFolders(const std::set<std::string>& folders);

    /**
     * @brief Check if file browser is visible
     */
    bool fileBrowserVisible() const { return m_fileBrowserVisible; }

    /**
     * @brief Set file browser visibility
     */
    void setFileBrowserVisible(bool v) { m_fileBrowserVisible = v; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Layout Presets
    /// @{

    /**
     * @brief Get a layout preset by name
     * @param name Preset name
     * @return Layout JSON string, or empty string if not found
     */
    std::string getLayoutPreset(const std::string& name) const;

    /**
     * @brief Set a layout preset
     * @param name Preset name
     * @param layoutJson Layout JSON string
     */
    void setLayoutPreset(const std::string& name, const std::string& layoutJson);

    /**
     * @brief Delete a layout preset
     * @param name Preset name
     */
    void deleteLayoutPreset(const std::string& name);

    /**
     * @brief Get all layout preset names
     * @return Vector of preset names
     */
    std::vector<std::string> getLayoutPresetNames() const;

    /**
     * @brief Get the active preset name
     */
    const std::string& activePreset() const { return m_activePreset; }

    /**
     * @brief Set the active preset name
     */
    void setActivePreset(const std::string& name);

    /**
     * @brief Get the last used preset name (for startup restoration)
     */
    const std::string& lastUsedPreset() const { return m_lastUsedPreset; }

    /**
     * @brief Set the last used preset name
     */
    void setLastUsedPreset(const std::string& name);

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
    bool m_terminalVisible = false;
    bool m_editorVisible = false;
    bool m_consoleVisible = false;
    bool m_visualizerVisible = true;
    float m_gridOpacity = 0.0f;

    // Corner radii (default to UIStyle defaults)
    float m_panelCornerRadius = 6.0f;
    float m_buttonCornerRadius = 4.0f;
    float m_sliderCornerRadius = 3.0f;

    // Editor session
    std::vector<std::string> m_openFiles;
    std::string m_activeFile;

    // File browser session
    std::set<std::string> m_expandedFolders;
    bool m_fileBrowserVisible = false;

    // Layout presets (name -> JSON string)
    std::map<std::string, std::string> m_layoutPresets;
    std::string m_activePreset = "Default";
    std::string m_lastUsedPreset = "Default";

    // Callbacks
    StyleChangeCallback m_styleChangeCallback;
};

// Theme creation functions are defined in ui_style.h:
// - createDarkTheme()
// - createLightTheme()
// - createHighContrastTheme()

} // namespace vivid
