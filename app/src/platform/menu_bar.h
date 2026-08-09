#pragma once

#include <functional>
#include <string>
#include <vector>

// A real OS menu bar (macOS NSMenu) for the application's File menu, instead of an
// in-window dropdown. Actions fire on the main thread (the Cocoa run loop, same thread
// as the frame loop), so their callbacks may touch the session/graph directly.
namespace vivid::platform {

struct MenuActions {
    std::function<void()> new_project;      // File > New
    std::function<void()> open_project;     // File > Open…      (runs the open dialog)
    std::function<void()> save_project;     // File > Save       (current path, else Save As)
    std::function<void()> save_project_as;  // File > Save As…   (runs the save dialog)
    std::function<void(const std::string&)> open_recent;  // File > Open Recent > <path>
    std::function<void(const std::string&)> open_example; // File > Open Example > <path> (ADR-0021/P2)
    std::function<void()> undo;   // Edit > Undo (ADR-0017/G4)
    std::function<void()> redo;   // Edit > Redo
    std::function<void()> set_gemini_key;   // Eval > Set Gemini Key… (ADR-0026)
    std::function<void()> evaluate_output;  // Eval > Evaluate Output
    std::function<void()> export_video;     // File > Export Video (toggles start/stop a realtime AV export)
    std::function<void()> export_audio;     // File > Export Audio (offline master-mix bounce to .wav, ADR-0032)
    std::function<void()> export_av;        // File > Export Video (Deterministic) (offline AV render, ADR-0032 Phase C)
    std::function<void()> toggle_reduce_motion;  // View > Reduce Motion (UX Ph4 F1 accessibility toggle)
    std::function<void(const std::string&)> select_audio_device;  // View > Audio Output > <name> (ADR-0032 Phase A; "" = system default)
};

// A menu entry for the File > Open Example submenu: a display label + the project path to open.
// `group` is "" for a top-level item, else the name of a submenu to nest it under (e.g. "operators"
// -> an "Operators" submenu). Entries are expected pre-sorted by (group, label).
struct MenuItemEntry { std::string label; std::string path; std::string group; };

// Insert native "File" + "Edit" menus into the app's menu bar. macOS: File gets the standard
// ⌘N/⌘O/⌘S/⇧⌘S key equivalents; Edit's Undo/Redo are label-only (no ⌘Z key-equivalent, so AppKit
// doesn't steal ⌘Z from the clip editor's own GLFW note-undo — the keyboard is handled in input.cpp).
// Other platforms: a no-op. Call once after the window exists (so NSApp + its menu bar are up).
void install_menu_bar(const MenuActions& actions);

// (Re)populate the File > Open Recent submenu. Call after the recent list changes.
void set_recent_projects(const std::vector<std::string>& paths);

// (Re)populate the View > Audio Output submenu (device names + a "System Default" item) and check the
// one matching `active_name`. Call after install_menu_bar and after a device switch. (ADR-0032 Phase A)
void set_audio_devices(const std::vector<std::string>& names, const std::string& active_name);

// Populate the File > Open Example submenu (label -> path). Call once after discovery. (ADR-0021/P2)
void set_example_projects(const std::vector<MenuItemEntry>& examples);

// Update the Edit > Undo/Redo item titles ("Undo Delete Node") + enabled state. Call when the undo
// history changes (the frame loop watches EditGateway::revision()). (ADR-0017/G4)
void set_edit_labels(const std::string& undo_label, const std::string& redo_label,
                     bool can_undo, bool can_redo);

// Flip the File > Export Video item label between "Export Video…" and "Stop Export" as recording
// starts/stops (from any trigger). The frame loop calls this when the recorder state changes.
void set_export_video_recording(bool recording);

// Set the checkmark on View > Reduce Motion. Call after install_menu_bar with the persisted state, and
// again whenever the setting toggles, so the menu reflects the live value. (UX Ph4 F1)
void set_reduce_motion_checked(bool checked);

// ADR-0018: the macOS window "edited" dot (the close-button gets a dot; ⌘-title shows the proxy is
// modified) — set from the app-level dirty flag. No-op off macOS.
void set_document_edited(bool edited);

}  // namespace vivid::platform
