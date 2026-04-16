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

// Returns {config_dir}/crashes, creating it if needed.  Used by
// CrashRecoveryManager for marker + snapshot + history files.
std::string get_crash_dir();

// Opens a URL in the platform default browser.
// Returns true when launch command is dispatched successfully.
bool open_url(const std::string& url, std::string* error_out = nullptr);

} // namespace vivid
