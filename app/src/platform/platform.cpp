#include "platform/platform.h"

#include <filesystem>
#include <cstdlib>
#include <cstdint>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace vivid::platform {

const char* plugin_suffix() {
#if defined(__APPLE__)
    return ".dylib";
#elif defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

std::string executable_path() {
    namespace fs = std::filesystem;
#if defined(__APPLE__)
    char buf[4096];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) == 0) {
        std::error_code ec;
        fs::path p = fs::weakly_canonical(fs::path(buf), ec);
        return ec ? std::string(buf) : p.string();
    }
    return {};
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(std::wstring(buf, n)).string();
    return {};
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf));
    if (n > 0) return std::string(buf, static_cast<size_t>(n));
    return {};
#endif
}

std::string user_data_dir() {
    namespace fs = std::filesystem;
    fs::path dir;
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    dir = (home ? fs::path(home) : fs::path(".")) / "Library" / "Application Support" / "Vivid";
#elif defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    dir = (appdata ? fs::path(appdata) : fs::path(".")) / "Vivid";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        dir = fs::path(xdg) / "vivid";
    else {
        const char* home = std::getenv("HOME");
        dir = (home ? fs::path(home) : fs::path(".")) / ".local" / "share" / "vivid";
    }
#endif
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

}  // namespace vivid::platform
