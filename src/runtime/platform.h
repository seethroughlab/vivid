#pragma once
#include <string>

namespace vivid {

// Platform-specific shared library suffix
#if defined(__APPLE__)
inline constexpr const char* kPluginSuffix = ".dylib";
#elif defined(_WIN32)
inline constexpr const char* kPluginSuffix = ".dll";
#else
inline constexpr const char* kPluginSuffix = ".so";
#endif

// Returns the platform config directory, creating it if needed.
// macOS:   ~/Library/Application Support/Vivid/
// Linux:   $XDG_CONFIG_HOME/vivid/ (fallback ~/.config/vivid/)
// Windows: %APPDATA%/Vivid/
std::string get_config_dir();

} // namespace vivid
