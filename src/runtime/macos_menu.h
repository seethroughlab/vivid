#pragma once
#ifdef __APPLE__

#include <functional>

namespace vivid {

struct MenuCallbacks {
    std::function<void()> on_open;
    std::function<void()> on_save;
    std::function<void()> on_preferences;
    std::function<void()> on_export;
    std::function<void()> on_browse_packages;
};

void macos_setup_menu(const MenuCallbacks& callbacks);

}  // namespace vivid

#endif  // __APPLE__
