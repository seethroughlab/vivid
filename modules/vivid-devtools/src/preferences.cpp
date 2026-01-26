// Preferences implementation
// Handles loading/saving user preferences to ~/.vivid/preferences.json

#include <vivid/devtools/preferences.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

namespace vivid {

// -------------------------------------------------------------------------
// Preferences Implementation
// -------------------------------------------------------------------------

Preferences& Preferences::instance() {
    static Preferences s_instance;
    return s_instance;
}

std::string Preferences::getPrefsPath() {
    std::string homeDir;

#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        homeDir = path;
        homeDir += "\\Vivid";
    } else {
        homeDir = ".";
    }
#else
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    homeDir = home ? home : ".";
    homeDir += "/.vivid";
#endif

    // Create directory if it doesn't exist
    struct stat st;
    if (stat(homeDir.c_str(), &st) != 0) {
#ifdef _WIN32
        CreateDirectoryA(homeDir.c_str(), NULL);
#else
        mkdir(homeDir.c_str(), 0755);
#endif
    }

    return homeDir + "/preferences.json";
}

bool Preferences::load() {
    std::string path = getPrefsPath();
    std::ifstream file(path);
    if (!file.is_open()) {
        // No prefs file yet - use defaults
        m_style = createDarkTheme();
        return true;
    }

    try {
        nlohmann::json j;
        file >> j;

        // Theme preset
        if (j.contains("theme")) {
            std::string theme = j["theme"].get<std::string>();
            if (theme == "dark") m_themePreset = ThemePreset::Dark;
            else if (theme == "light") m_themePreset = ThemePreset::Light;
            else if (theme == "high_contrast") m_themePreset = ThemePreset::HighContrast;
            else if (theme == "custom") m_themePreset = ThemePreset::Custom;
        }

        // Corner radii (load before applying theme)
        if (j.contains("cornerRadius")) {
            auto& cr = j["cornerRadius"];
            if (cr.contains("panel")) m_panelCornerRadius = cr["panel"].get<float>();
            if (cr.contains("button")) m_buttonCornerRadius = cr["button"].get<float>();
            if (cr.contains("slider")) m_sliderCornerRadius = cr["slider"].get<float>();
        }

        // Apply theme (and custom corner radii)
        applyThemeToStyle();

        // Panel visibility
        if (j.contains("panels")) {
            auto& panels = j["panels"];
            if (panels.contains("terminal")) m_terminalVisible = panels["terminal"].get<bool>();
            if (panels.contains("editor")) m_editorVisible = panels["editor"].get<bool>();
            if (panels.contains("console")) m_consoleVisible = panels["console"].get<bool>();
            if (panels.contains("visualizer")) m_visualizerVisible = panels["visualizer"].get<bool>();
        }

        std::cerr << "[Preferences] Loaded from " << path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[Preferences] Failed to parse " << path << ": " << e.what() << std::endl;
        m_style = createDarkTheme();
        return false;
    }
}

bool Preferences::save() {
    std::string path = getPrefsPath();

    try {
        nlohmann::json j;

        // Theme preset
        switch (m_themePreset) {
            case ThemePreset::Dark: j["theme"] = "dark"; break;
            case ThemePreset::Light: j["theme"] = "light"; break;
            case ThemePreset::HighContrast: j["theme"] = "high_contrast"; break;
            case ThemePreset::Custom: j["theme"] = "custom"; break;
        }

        // Panel visibility
        j["panels"] = {
            {"terminal", m_terminalVisible},
            {"editor", m_editorVisible},
            {"console", m_consoleVisible},
            {"visualizer", m_visualizerVisible}
        };

        // Corner radii
        j["cornerRadius"] = {
            {"panel", m_panelCornerRadius},
            {"button", m_buttonCornerRadius},
            {"slider", m_sliderCornerRadius}
        };

        std::ofstream file(path);
        if (!file.is_open()) {
            std::cerr << "[Preferences] Failed to open " << path << " for writing" << std::endl;
            return false;
        }

        file << j.dump(2);
        std::cerr << "[Preferences] Saved to " << path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[Preferences] Failed to save: " << e.what() << std::endl;
        return false;
    }
}

void Preferences::setThemePreset(ThemePreset preset) {
    m_themePreset = preset;
    applyThemeToStyle();

    if (m_styleChangeCallback) {
        m_styleChangeCallback(m_style);
    }

    save();
}

void Preferences::applyThemeToStyle() {
    switch (m_themePreset) {
        case ThemePreset::Dark:
            m_style = createDarkTheme();
            break;
        case ThemePreset::Light:
            m_style = createLightTheme();
            break;
        case ThemePreset::HighContrast:
            m_style = createHighContrastTheme();
            break;
        case ThemePreset::Custom:
            // Keep current style (don't overwrite custom colors)
            break;
    }

    // Apply custom corner radii (override theme defaults)
    m_style.panelCornerRadiusBase = m_panelCornerRadius;
    m_style.buttonCornerRadiusBase = m_buttonCornerRadius;
    m_style.sliderCornerRadiusBase = m_sliderCornerRadius;
}

void Preferences::setPanelCornerRadius(float radius) {
    m_panelCornerRadius = radius;
    m_style.panelCornerRadiusBase = radius;

    if (m_styleChangeCallback) {
        m_styleChangeCallback(m_style);
    }

    save();
}

void Preferences::setButtonCornerRadius(float radius) {
    m_buttonCornerRadius = radius;
    m_style.buttonCornerRadiusBase = radius;

    if (m_styleChangeCallback) {
        m_styleChangeCallback(m_style);
    }

    save();
}

void Preferences::setSliderCornerRadius(float radius) {
    m_sliderCornerRadius = radius;
    m_style.sliderCornerRadiusBase = radius;

    if (m_styleChangeCallback) {
        m_styleChangeCallback(m_style);
    }

    save();
}

} // namespace vivid
