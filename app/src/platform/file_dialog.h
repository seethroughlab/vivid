#pragma once
#include <string>

// Native file chooser (macOS NSOpenPanel/NSSavePanel; a no-op stub returns "" elsewhere).
// A Vivid project is a folder (or a legacy .json); the open panel allows either. Returns the
// chosen absolute path, or "" if the user cancelled / no native dialog is available. These
// run a modal panel — UI/main thread only.
namespace vivid::platform {

std::string open_project_dialog();
std::string save_project_dialog(const std::string& suggested_name);

}  // namespace vivid::platform
