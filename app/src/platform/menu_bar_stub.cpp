#ifndef __APPLE__
#include "platform/menu_bar.h"

// Non-macOS: no native menu bar yet (the app still has keyboard shortcuts + MCP).
namespace vivid::platform {
void install_menu_bar(const MenuActions&) {}
void set_recent_projects(const std::vector<std::string>&) {}
void set_edit_labels(const std::string&, const std::string&, bool, bool) {}
}  // namespace vivid::platform
#endif
