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
};

// Insert a native "File" menu into the app's menu bar. macOS: builds an NSMenu with the
// standard ⌘N/⌘O/⌘S/⇧⌘S key equivalents. Other platforms: a no-op. Call once after the
// window exists (so NSApp + its menu bar are up).
void install_menu_bar(const MenuActions& actions);

// (Re)populate the File > Open Recent submenu. Call after the recent list changes.
void set_recent_projects(const std::vector<std::string>& paths);

}  // namespace vivid::platform
