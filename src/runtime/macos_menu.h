#pragma once
#ifdef __APPLE__

#include <functional>

namespace vivid {

// All callbacks are invoked on the main thread by Cocoa's NSMenu dispatch.
// Bound functions may freely access main-thread-only state (graph, scheduler, UI).
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

    // Edit menu
    std::function<void()> on_delete_selected;
    std::function<void()> on_edit_meta;

    // View menu
    std::function<void()> on_toggle_ui;
    std::function<void()> on_toggle_fullscreen;
    std::function<void()> on_toggle_bezier_wires;
    std::function<void()> on_toggle_show_param_wires;
    std::function<void()> on_toggle_session_grid;
    std::function<void()> on_toggle_midi_map;

    // Insert menu
    std::function<void()> on_add_node;

    // State queries for checkmarks / enable states
    std::function<bool()> is_ui_visible;
    std::function<bool()> is_fullscreen;
    std::function<bool()> is_bezier_wires;
    std::function<bool()> is_show_param_wires;
    std::function<bool()> is_session_grid_open;
    std::function<bool()> is_midi_map_mode;
    std::function<bool()> has_selection;
    std::function<bool()> can_edit_meta;
    std::function<bool()> has_graph_path;
    std::function<bool()> is_auto_check_updates;
};

void macos_setup_menu(const MenuCallbacks& callbacks);
void macos_set_presentation_fullscreen(bool enabled);
void macos_set_document_edited(bool edited);

}  // namespace vivid

#endif  // __APPLE__
