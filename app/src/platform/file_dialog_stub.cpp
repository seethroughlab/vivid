#ifndef __APPLE__
#include "platform/file_dialog.h"

// No native file chooser off macOS — return "" so callers fall back (e.g. MCP passes an
// explicit path). A GTK/Win32 dialog is a future platform step.
namespace vivid::platform {

std::string open_project_dialog() { return {}; }
std::string save_project_dialog(const std::string&) { return {}; }

}  // namespace vivid::platform

#endif  // !__APPLE__
