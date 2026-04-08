#include "runtime/core/settings.h"
#include "runtime/platform/platform.h"
#include "runtime/platform/process_runner.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <string>

namespace vivid {

bool is_supported_audio_buffer_size(uint32_t value) {
    switch (value) {
        case 128:
        case 256:
        case 512:
        case 1024:
            return true;
        default:
            return false;
    }
}

uint32_t sanitize_audio_buffer_size(uint32_t value) {
    return is_supported_audio_buffer_size(value) ? value : kDefaultAudioBufferSize;
}

static std::string settings_path() {
    return get_config_dir() + "/settings.json";
}

// Helper: read a typed value from a JSON object if it exists and has the right type.
template<typename T>
static void json_read(const nlohmann::json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) {
        try { out = it->get<T>(); } catch (...) {}
    }
}

Settings load_settings() {
    Settings s;
    std::string path = settings_path();

    if (!std::filesystem::exists(path)) return s;

    nlohmann::json j;
    try {
        std::ifstream ifs(path);
        j = nlohmann::json::parse(ifs);
    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[vivid] Failed to read settings: %s\n", e.what());
        return s;
    }

    if (!j.is_object()) return s;

    json_read(j, "window_x", s.window_x);
    json_read(j, "window_y", s.window_y);
    json_read(j, "window_width", s.window_width);
    json_read(j, "window_height", s.window_height);
    json_read(j, "bezier_wires", s.bezier_wires);
    json_read(j, "show_param_wires", s.show_param_wires);
    json_read(j, "show_analysis", s.show_analysis);
    json_read(j, "audio_buffer_size", s.audio_buffer_size);
    json_read(j, "editor", s.editor);
    json_read(j, "editor_command", s.editor_command);
    json_read(j, "style_id", s.style_id);
    json_read(j, "core_update_auto_check", s.core_update_auto_check);
    json_read(j, "core_update_last_checked_at", s.core_update_last_checked_at);
    json_read(j, "core_update_skipped_version", s.core_update_skipped_version);
    json_read(j, "workspace_root", s.workspace_root);
    json_read(j, "workspace_seeded_version", s.workspace_seeded_version);
    json_read(j, "operator_clone_destination_mode", s.operator_clone_destination_mode);
    json_read(j, "project_operator_root", s.project_operator_root);
    json_read(j, "project_package_name", s.project_package_name);
    json_read(j, "pan_gesture", s.pan_gesture);
    json_read(j, "recent_files", s.recent_files);

    if (s.pan_gesture != "middle" && s.pan_gesture != "left" && s.pan_gesture != "right")
        s.pan_gesture = "left";

    if (s.operator_clone_destination_mode != "project_default" &&
        s.operator_clone_destination_mode != "core_explicit") {
        s.operator_clone_destination_mode = "project_default";
    }

    // Sanity: clamp size to something reasonable
    if (s.window_width < 320) s.window_width = 320;
    if (s.window_height < 240) s.window_height = 240;
    s.audio_buffer_size = sanitize_audio_buffer_size(s.audio_buffer_size);

    return s;
}

void save_settings(const Settings& s) {
    nlohmann::json j;

    j["window_x"] = s.window_x;
    j["window_y"] = s.window_y;
    j["window_width"] = s.window_width;
    j["window_height"] = s.window_height;
    j["bezier_wires"] = s.bezier_wires;
    j["show_param_wires"] = s.show_param_wires;
    j["show_analysis"] = s.show_analysis;
    j["audio_buffer_size"] = sanitize_audio_buffer_size(s.audio_buffer_size);
    if (!s.editor.empty()) j["editor"] = s.editor;
    if (!s.editor_command.empty()) j["editor_command"] = s.editor_command;
    if (!s.style_id.empty()) j["style_id"] = s.style_id;
    j["core_update_auto_check"] = s.core_update_auto_check;
    if (!s.core_update_last_checked_at.empty())
        j["core_update_last_checked_at"] = s.core_update_last_checked_at;
    if (!s.core_update_skipped_version.empty())
        j["core_update_skipped_version"] = s.core_update_skipped_version;
    if (!s.workspace_root.empty()) j["workspace_root"] = s.workspace_root;
    if (!s.workspace_seeded_version.empty())
        j["workspace_seeded_version"] = s.workspace_seeded_version;
    j["operator_clone_destination_mode"] = s.operator_clone_destination_mode;
    if (!s.project_operator_root.empty())
        j["project_operator_root"] = s.project_operator_root;
    if (!s.project_package_name.empty())
        j["project_package_name"] = s.project_package_name;
    j["pan_gesture"] = s.pan_gesture;
    if (!s.recent_files.empty()) j["recent_files"] = s.recent_files;

    std::string path = settings_path();
    std::ofstream ofs(path);
    if (!ofs) {
        std::fprintf(stderr, "[vivid] Failed to write settings: could not open %s\n", path.c_str());
        return;
    }
    ofs << j.dump(4) << '\n';
}

void add_recent_file(Settings& s, const std::string& path) {
    s.recent_files.erase(
        std::remove(s.recent_files.begin(), s.recent_files.end(), path),
        s.recent_files.end());
    s.recent_files.insert(s.recent_files.begin(), path);
    if (s.recent_files.size() > 10)
        s.recent_files.resize(10);
}

// Shell-quote a path with single quotes, escaping embedded single quotes as '\''.
static std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

void open_in_editor(const std::string& file_path, const Settings& settings) {
    if (settings.editor == "custom" && !settings.editor_command.empty()) {
        // User-provided shell command template — must use shell for expansion.
        // Shell-quote the file path to handle spaces and metacharacters.
        std::string cmd = settings.editor_command;
        std::string placeholder = "{file}";
        size_t pos = cmd.find(placeholder);
        if (pos != std::string::npos) {
            cmd.replace(pos, placeholder.size(), shell_quote(file_path));
        } else {
            cmd += " " + shell_quote(file_path);
        }
        std::string err;
        if (!spawn_detached({"/bin/sh", "-c", cmd}, &err) && !err.empty())
            std::fprintf(stderr, "[vivid] spawn_detached: %s\n", err.c_str());
    } else if (!settings.editor.empty()) {
        std::string err;
        if (!spawn_detached({"/usr/bin/open", "-a", settings.editor, file_path}, &err) && !err.empty())
            std::fprintf(stderr, "[vivid] spawn_detached: %s\n", err.c_str());
    } else {
        std::string err;
        if (!spawn_detached({"/usr/bin/open", "-t", file_path}, &err) && !err.empty())
            std::fprintf(stderr, "[vivid] spawn_detached: %s\n", err.c_str());
    }
}

} // namespace vivid
