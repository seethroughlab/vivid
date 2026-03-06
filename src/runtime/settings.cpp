#include "runtime/settings.h"
#include "runtime/platform.h"
#include <yyjson.h>
#include <filesystem>
#include <cstdio>
#include <spawn.h>
#include <string>

extern "C" char** environ;

namespace vivid {

static std::string settings_path() {
    return get_config_dir() + "/settings.json";
}

Settings load_settings() {
    Settings s;
    std::string path = settings_path();

    if (!std::filesystem::exists(path)) return s;

    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_file(path.c_str(), 0, nullptr, &err);
    if (!doc) {
        std::fprintf(stderr, "[vivid] Failed to read settings: %s\n", err.msg);
        return s;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return s;
    }

    yyjson_val* v;
    if ((v = yyjson_obj_get(root, "window_x")) && yyjson_is_int(v))
        s.window_x = (int)yyjson_get_int(v);
    if ((v = yyjson_obj_get(root, "window_y")) && yyjson_is_int(v))
        s.window_y = (int)yyjson_get_int(v);
    if ((v = yyjson_obj_get(root, "window_width")) && yyjson_is_int(v))
        s.window_width = (int)yyjson_get_int(v);
    if ((v = yyjson_obj_get(root, "window_height")) && yyjson_is_int(v))
        s.window_height = (int)yyjson_get_int(v);
    if ((v = yyjson_obj_get(root, "bezier_wires")) && yyjson_is_bool(v))
        s.bezier_wires = yyjson_get_bool(v);
    if ((v = yyjson_obj_get(root, "editor")) && yyjson_is_str(v))
        s.editor = yyjson_get_str(v);
    if ((v = yyjson_obj_get(root, "editor_command")) && yyjson_is_str(v))
        s.editor_command = yyjson_get_str(v);
    if ((v = yyjson_obj_get(root, "style_id")) && yyjson_is_str(v))
        s.style_id = yyjson_get_str(v);
    if ((v = yyjson_obj_get(root, "core_update_auto_check")) && yyjson_is_bool(v))
        s.core_update_auto_check = yyjson_get_bool(v);
    if ((v = yyjson_obj_get(root, "core_update_last_checked_at")) && yyjson_is_str(v))
        s.core_update_last_checked_at = yyjson_get_str(v);
    if ((v = yyjson_obj_get(root, "core_update_skipped_version")) && yyjson_is_str(v))
        s.core_update_skipped_version = yyjson_get_str(v);

    // Sanity: clamp size to something reasonable
    if (s.window_width < 320) s.window_width = 320;
    if (s.window_height < 240) s.window_height = 240;

    yyjson_doc_free(doc);
    return s;
}

void save_settings(const Settings& s) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_int(doc, root, "window_x", s.window_x);
    yyjson_mut_obj_add_int(doc, root, "window_y", s.window_y);
    yyjson_mut_obj_add_int(doc, root, "window_width", s.window_width);
    yyjson_mut_obj_add_int(doc, root, "window_height", s.window_height);
    yyjson_mut_obj_add_bool(doc, root, "bezier_wires", s.bezier_wires);
    if (!s.editor.empty())
        yyjson_mut_obj_add_str(doc, root, "editor", s.editor.c_str());
    if (!s.editor_command.empty())
        yyjson_mut_obj_add_str(doc, root, "editor_command", s.editor_command.c_str());
    if (!s.style_id.empty())
        yyjson_mut_obj_add_str(doc, root, "style_id", s.style_id.c_str());
    yyjson_mut_obj_add_bool(doc, root, "core_update_auto_check", s.core_update_auto_check);
    if (!s.core_update_last_checked_at.empty())
        yyjson_mut_obj_add_str(doc, root, "core_update_last_checked_at",
                               s.core_update_last_checked_at.c_str());
    if (!s.core_update_skipped_version.empty())
        yyjson_mut_obj_add_str(doc, root, "core_update_skipped_version",
                               s.core_update_skipped_version.c_str());

    std::string path = settings_path();
    yyjson_write_err werr;
    bool ok = yyjson_mut_write_file(path.c_str(), doc,
                                     YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END,
                                     nullptr, &werr);
    if (!ok) {
        std::fprintf(stderr, "[vivid] Failed to write settings: %s\n", werr.msg);
    }

    yyjson_mut_doc_free(doc);
}

// Fire-and-forget process launch via posix_spawn (no shell interpolation).
static void spawn_detached(const char* const argv[]) {
    pid_t pid;
    posix_spawn(&pid, argv[0], nullptr, nullptr,
                const_cast<char* const*>(argv), environ);
    // Fire-and-forget: don't waitpid — child is short-lived (open/sh).
}

void open_in_editor(const std::string& file_path, const Settings& settings) {
    if (settings.editor == "custom" && !settings.editor_command.empty()) {
        // User-provided shell command template — must use shell for expansion.
        std::string cmd = settings.editor_command;
        std::string placeholder = "{file}";
        size_t pos = cmd.find(placeholder);
        if (pos != std::string::npos) {
            cmd.replace(pos, placeholder.size(), file_path);
        } else {
            cmd += " " + file_path;
        }
        const char* argv[] = { "/bin/sh", "-c", cmd.c_str(), nullptr };
        spawn_detached(argv);
    } else if (!settings.editor.empty()) {
        const char* argv[] = { "/usr/bin/open", "-a", settings.editor.c_str(),
                               file_path.c_str(), nullptr };
        spawn_detached(argv);
    } else {
        const char* argv[] = { "/usr/bin/open", "-t", file_path.c_str(), nullptr };
        spawn_detached(argv);
    }
}

} // namespace vivid
