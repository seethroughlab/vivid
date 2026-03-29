#pragma once
#include <cstdint>
#include <string>

namespace vivid {

struct Settings {
    int window_x      = -1;   // -1 means "no saved position, center it"
    int window_y      = -1;
    int window_width  = 1280;
    int window_height = 800;
    bool bezier_wires = false;
    bool show_param_wires = false;
    bool show_analysis = true;   // GPU frame analysis + audio RMS/peak

    std::string editor;          // app name for `open -a`, empty = system default
    std::string editor_command;  // custom command template with {file} placeholder
    std::string style_id;        // "dark_steel", "midnight", "slate"

    bool core_update_auto_check = true;
    std::string core_update_last_checked_at;  // unix epoch seconds as string
    std::string core_update_skipped_version;
    std::string workspace_root;               // user-editable workspace root (default: ~/Documents/Vivid)
    std::string workspace_seeded_version;     // last app version that seeded workspace assets

    // Project-local operator destination policy.
    // project_default: prefer project package/root first, fallback to core.
    // core_explicit: default to core unless caller explicitly requests project.
    std::string operator_clone_destination_mode = "project_default";
    std::string project_operator_root;         // absolute path (optional)
    std::string project_package_name;          // package target hint (optional)
    std::string pan_gesture = "left";          // "middle", "left", or "right"
};

Settings load_settings();
void save_settings(const Settings& s);

// Open a file in the user's preferred editor
void open_in_editor(const std::string& file_path, const Settings& settings);

} // namespace vivid
