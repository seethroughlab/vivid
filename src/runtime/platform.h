#pragma once
#include <string>

namespace vivid {

// Returns the platform config directory, creating it if needed.
// macOS:   ~/Library/Application Support/Vivid/
// Linux:   $XDG_CONFIG_HOME/vivid/ (fallback ~/.config/vivid/)
// Windows: %APPDATA%/Vivid/
std::string get_config_dir();

} // namespace vivid
