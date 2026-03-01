#include "runtime/platform.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace vivid {

std::string get_config_dir() {
    namespace fs = std::filesystem;
    fs::path dir;

#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) {
        dir = fs::path(home) / "Library" / "Application Support" / "Vivid";
    }
#elif defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        dir = fs::path(appdata) / "Vivid";
    }
#else
    // Linux / other Unix
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        dir = fs::path(xdg) / "vivid";
    } else {
        const char* home = std::getenv("HOME");
        if (home) {
            dir = fs::path(home) / ".config" / "vivid";
        }
    }
#endif

    if (dir.empty()) {
        dir = fs::path(".vivid");
    }

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::fprintf(stderr, "[vivid] Warning: failed to create config directory '%s': %s\n",
                     dir.string().c_str(), ec.message().c_str());
    }
    return dir.string();
}

} // namespace vivid
