#pragma once
#include <string>

// App-level (not per-project) settings, persisted to
// ~/Library/Application Support/Vivid/settings.json. These are viewer/accessibility preferences that
// should follow the person, not the document — unlike output identity (an Output-node param) or window
// geometry (window.json). Kept deliberately tiny; grow it only for genuinely app-global toggles.
namespace vivid {

struct AppSettings {
    // UX Ph4 F1 — reduce motion / flash limit. When on, the visual output is temporally low-passed so
    // rapid full-frame luminance swings are damped (photosensitivity accommodation). Off by default.
    bool reduce_motion = false;
};

// Absolute path to the settings file (user_data_dir()/settings.json); empty if no data dir.
std::string app_settings_path();
// Read settings from `path`; a missing/invalid file yields defaults.
AppSettings load_app_settings(const std::string& path);
// Write settings to `path`; returns false on I/O error.
bool save_app_settings(const AppSettings& s, const std::string& path);

}  // namespace vivid
