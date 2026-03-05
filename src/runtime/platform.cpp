#include "runtime/platform.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#if defined(__APPLE__) || defined(__linux__)
#include <spawn.h>
extern char** environ;
#endif

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

bool open_url(const std::string& url, std::string* error_out) {
    if (url.empty()) {
        if (error_out) *error_out = "URL is empty";
        return false;
    }

#if defined(__APPLE__)
    pid_t pid = 0;
    const char* argv[] = { "/usr/bin/open", url.c_str(), nullptr };
    int rc = posix_spawn(&pid, argv[0], nullptr, nullptr,
                         const_cast<char* const*>(argv), ::environ);
    if (rc != 0 && error_out)
        *error_out = "Failed to launch /usr/bin/open";
    return rc == 0;
#elif defined(__linux__)
    pid_t pid = 0;
    const char* argv[] = { "xdg-open", url.c_str(), nullptr };
    int rc = posix_spawn(&pid, argv[0], nullptr, nullptr,
                         const_cast<char* const*>(argv), ::environ);
    if (rc != 0 && error_out)
        *error_out = "Failed to launch xdg-open";
    return rc == 0;
#elif defined(_WIN32)
    std::string cmd = "start \"\" \"" + url + "\"";
    int rc = std::system(cmd.c_str());
    if (rc != 0 && error_out)
        *error_out = "Failed to launch default browser";
    return rc == 0;
#else
    if (error_out) *error_out = "Unsupported platform";
    return false;
#endif
}

} // namespace vivid
