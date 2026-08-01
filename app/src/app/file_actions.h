#pragma once

#include <string>

struct GLFWwindow;

namespace vivid {

struct App;
struct Window;

// The File-menu actions (native menu bar + any other trigger). Folder-aware via
// app/project_io; the Open/Save variants drive the native file dialogs. All run on the
// UI/main thread and refresh the Open Recent submenu after a successful load/save.
namespace file_actions {

void new_project(App& app, Window& win);                      // fresh slate (no dialog): blank session
void open(GLFWwindow* w, Window& win, App& app);              // Open… → dialog → load
void save(GLFWwindow* w, Window& win, App& app);             // Save (current path, else Save As)
void save_as(GLFWwindow* w, Window& win, App& app);          // Save As… → dialog → save
void open_recent(GLFWwindow* w, Window& win, App& app, const std::string& path);

}  // namespace file_actions
}  // namespace vivid
