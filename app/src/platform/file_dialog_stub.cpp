#ifndef __APPLE__
#include "platform/file_dialog.h"

// No native file chooser off macOS — return "" so callers fall back (e.g. MCP passes an
// explicit path). A GTK/Win32 dialog is a future platform step.
namespace vivid::platform {

std::string open_project_dialog() { return {}; }
std::string save_project_dialog(const std::string&) { return {}; }
std::string open_file_dialog(const std::string&, const std::vector<std::string>&) { return {}; }
// No native modal off macOS — proceed (Discard) rather than block a headless build.
DiscardChoice confirm_discard_changes() { return DiscardChoice::Discard; }
bool confirm_recover_autosave(const std::string&) { return false; }

}  // namespace vivid::platform

#endif  // !__APPLE__
