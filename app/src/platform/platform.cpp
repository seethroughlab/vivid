#include "platform/platform.h"

#include <filesystem>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
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

bool open_in_editor(const std::string& path) {
    if (path.empty()) return false;
#if defined(__APPLE__)
    const char* ed = std::getenv("VIVID_EDITOR");   // an app name/bundle id, or unset for the default app
    std::string a = "-a", edstr = ed ? ed : "", open = "/usr/bin/open", p = path;
    std::vector<char*> argv;
    argv.push_back(open.data());
    if (!edstr.empty()) { argv.push_back(a.data()); argv.push_back(edstr.data()); }
    argv.push_back(p.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, argv.data(), environ) != 0) return false;
    int status = 0; waitpid(pid, &status, 0);   // `open` returns immediately after launching
    return true;
#elif defined(_WIN32)
    std::string cmd = "start \"\" \"" + path + "\"";
    return std::system(cmd.c_str()) == 0;
#else
    std::string cmd = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
#endif
}

}  // namespace vivid::platform
