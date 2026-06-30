#pragma once

#include <string>

// Cross-platform seam for the handful of OS-specific assumptions the app makes:
// the operator plugin suffix, the executable's path, and a per-user data directory.
// macOS is the only backend exercised today; the Linux/Windows branches make the tree
// cross-platform-ready (see app/src/platform/CLAUDE.md).
namespace vivid::platform {

// The dynamic-library suffix for operator plugins on this OS: ".dylib"/".so"/".dll".
const char* plugin_suffix();

// Absolute path to the running executable (empty on failure).
std::string executable_path();

// Per-user writable data directory for Vivid, created if missing. macOS: Application
// Support; Linux: $XDG_DATA_HOME or ~/.local/share; Windows: %APPDATA%. Empty on failure.
std::string user_data_dir();

}  // namespace vivid::platform
