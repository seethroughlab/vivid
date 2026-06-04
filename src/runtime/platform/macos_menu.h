#pragma once
#ifdef __APPLE__

#include <functional>
#include <string>
#include <vector>

namespace vivid {

// All callbacks are invoked on the main thread by Cocoa's NSMenu dispatch.
// Bound functions may freely access main-thread-only state (graph, runtime, UI).
struct MenuCallbacks {
    std::function<void()> on_about;
    std::function<void()> on_new;
    std::function<void()> on_new_project;
    std::function<void()> on_open;
    std::function<void()> on_open_example;
    std::function<void()> on_open_graph_folder;
    std::function<void()> on_save;
    std::function<void()> on_save_as;
    std::function<void()> on_preferences;
    std::function<void()> on_export;
    std::function<void()> on_browse_packages;
    std::function<void()> on_open_package_catalog_website;
    std::function<void()> on_check_for_updates;
    std::function<void()> on_toggle_auto_check_updates;
    std::function<void()> on_report_issue;
    std::function<void()> on_check_system_requirements;

    // Edit menu
    std::function<void()> on_undo;
    std::function<void()> on_redo;
    std::function<void()> on_delete_selected;
    std::function<void()> on_edit_meta;

    // View menu
    std::function<void()> on_toggle_ui;
    std::function<void()> on_toggle_fullscreen;
    std::function<void()> on_toggle_bezier_wires;
    std::function<void()> on_toggle_show_param_wires;
    std::function<void()> on_toggle_analysis;
    std::function<void()> on_toggle_session_grid;
    std::function<void()> on_toggle_build_console;
    std::function<void()> on_toggle_midi_map;

    // Insert menu
    std::function<void()> on_add_node;

    // State queries for checkmarks / enable states
    std::function<bool()> is_ui_visible;
    std::function<bool()> is_fullscreen;
    std::function<bool()> is_bezier_wires;
    std::function<bool()> is_show_param_wires;
    std::function<bool()> is_analysis_enabled;
    std::function<bool()> is_session_grid_open;
    std::function<bool()> is_build_console_open;
    std::function<bool()> is_midi_map_mode;
    std::function<bool()> has_selection;
    std::function<bool()> can_edit_meta;
    std::function<bool()> has_graph_path;
    std::function<bool()> is_auto_check_updates;
    std::function<bool()> can_undo;
    std::function<bool()> can_redo;
    std::function<std::string()> undo_label;  // e.g. "Clear pattern" ("" if none)
    std::function<std::string()> redo_label;

    // Recent files
    std::function<void(const std::string&)> on_open_recent;
    std::function<void()> on_clear_recent;
};

void macos_setup_menu(const MenuCallbacks& callbacks);
void macos_update_recent_files_menu(const std::vector<std::string>& paths);
void macos_set_presentation_fullscreen(bool enabled);
void macos_set_document_edited(bool edited);

}  // namespace vivid

#endif  // __APPLE__
