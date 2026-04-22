#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

// Persisted editor-window geometry keyed by operator type slug.
// width == 0 / height == 0 → use VividEditorMetadata defaults at open time.
// x == -1 / y == -1       → let the OS pick the placement.
struct EditorWindowGeometry {
    int x = -1;
    int y = -1;
    int width = 0;
    int height = 0;
};

inline constexpr uint32_t kDefaultAudioBufferSize = 256;

bool is_supported_audio_buffer_size(uint32_t value);
uint32_t sanitize_audio_buffer_size(uint32_t value);

struct Settings {
    int window_x      = -1;   // -1 means "no saved position, center it"
    int window_y      = -1;
    int window_width  = 1280;
    int window_height = 800;
    bool bezier_wires = false;
    bool show_param_wires = false;
    bool show_analysis = true;   // GPU frame analysis + audio RMS/peak
    uint32_t audio_buffer_size = kDefaultAudioBufferSize;

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

    std::vector<std::string> recent_files;     // most-recent first, capped at 10

    // Per-operator-type editor window geometry (Phase 3 host integration).
    // Keyed by CompiledNode::type_name / NodeSnapshot::type_name.
    std::unordered_map<std::string, EditorWindowGeometry> editor_window_geometry_by_type;

    // Project lockfile load mode.
    // studio (default): verify runs, status reported, nothing disabled.
    // strict: critical findings disable affected nodes (locked_unavailable).
    // recovery: currently identical to studio; reserved for later behavior.
    std::string lockfile_load_mode = "studio";
};

Settings load_settings();
void save_settings(const Settings& s);

// Insert path at front of recent_files, deduplicate, cap at 10.
void add_recent_file(Settings& s, const std::string& path);

// Open a file in the user's preferred editor
void open_in_editor(const std::string& file_path, const Settings& settings);

} // namespace vivid
