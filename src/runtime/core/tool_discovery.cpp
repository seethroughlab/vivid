#include "runtime/core/tool_discovery.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vivid {

#ifdef _WIN32
static constexpr char kPathSep = ';';
static constexpr char kDirSep = '\\';
#else
static constexpr char kPathSep = ':';
static constexpr char kDirSep = '/';
#endif

static bool tool_forced_missing(const std::string& tool) {
    const char* env = std::getenv("VIVID_MOCK_MISSING_TOOL");
    if (!env || !*env) return false;
    std::string s(env);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t next = s.find(',', pos);
        std::string item = s.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (item == tool) return true;
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return false;
}

static const char* env_var_for_tool(const char* tool) {
    if (std::string(tool) == "clang++") return "VIVID_CXX";
    if (std::string(tool) == "cmake")   return "VIVID_CMAKE";
    if (std::string(tool) == "git")     return "VIVID_GIT";
    return nullptr;
}

static bool is_executable(const std::string& path) {
#ifdef _WIN32
    return _access(path.c_str(), 0) == 0;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISREG(st.st_mode) && access(path.c_str(), X_OK) == 0;
#endif
}

// Check if tool exists as an executable in a directory.
// On Windows, also checks PATHEXT extensions (.exe, .cmd, .bat).
static std::string check_dir(const std::string& dir, const char* tool) {
    std::string base = dir + kDirSep + tool;

#ifdef _WIN32
    // Bare name first (in case tool already has an extension)
    if (is_executable(base)) return base;

    // Try each PATHEXT extension
    const char* pathext = std::getenv("PATHEXT");
    if (!pathext) pathext = ".COM;.EXE;.BAT;.CMD";
    std::string exts(pathext);
    size_t pos = 0;
    while (pos <= exts.size()) {
        size_t next = exts.find(';', pos);
        std::string ext = exts.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (!ext.empty()) {
            std::string candidate = base + ext;
            if (is_executable(candidate)) return candidate;
        }
        if (next == std::string::npos) break;
        pos = next + 1;
    }
#else
    if (is_executable(base)) return base;
#endif

    return {};
}

std::string find_tool(const char* tool) {
    if (tool_forced_missing(tool)) return {};

    // Check env var override first
    const char* env_name = env_var_for_tool(tool);
    if (env_name) {
        const char* env_val = std::getenv(env_name);
        if (env_val && *env_val) return env_val;
    }

    // Search PATH entries directly (no shell subprocess — reliable from GUI apps)
    const char* path_env = std::getenv("PATH");
    if (path_env && *path_env) {
        std::string path(path_env);
        size_t pos = 0;
        while (pos <= path.size()) {
            size_t next = path.find(kPathSep, pos);
            std::string dir = path.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            if (!dir.empty()) {
                auto result = check_dir(dir, tool);
                if (!result.empty()) return result;
            }
            if (next == std::string::npos) break;
            pos = next + 1;
        }
    }

    // Fallback locations not always in PATH (e.g. when launched from Finder/Dock)
#ifdef _WIN32
    static const char* fallback_dirs[] = {
        "C:\\Program Files\\CMake\\bin",
        "C:\\Program Files\\Git\\cmd",
    };
#else
    static const char* fallback_dirs[] = {
        "/opt/homebrew/bin",
        "/usr/local/bin",
    };
#endif
    for (const char* dir : fallback_dirs) {
        auto result = check_dir(dir, tool);
        if (!result.empty()) return result;
    }

    return {};
}

std::string missing_tool_error(const char* tool) {
    std::string msg = "Missing required build tool: ";
    msg += tool;
    if (std::string(tool) == "clang++") {
#ifdef _WIN32
        msg += ". Install Visual Studio or LLVM/Clang,"
               " or set VIVID_CXX to a custom compiler path.";
#else
        msg += ". Install Xcode Command Line Tools with `xcode-select --install`,"
               " or set VIVID_CXX to a custom compiler path.";
#endif
    } else if (std::string(tool) == "cmake") {
#ifdef _WIN32
        msg += ". Install CMake from https://cmake.org/download/,"
               " or set VIVID_CMAKE to a custom cmake path.";
#else
        msg += ". Install CMake (e.g. `brew install cmake`),"
               " or set VIVID_CMAKE to a custom cmake path.";
#endif
    } else if (std::string(tool) == "git") {
#ifdef _WIN32
        msg += ". Install Git from https://git-scm.com/download/win,"
               " or set VIVID_GIT to a custom git path.";
#else
        msg += ". Install Git (included with Xcode Command Line Tools),"
               " or set VIVID_GIT to a custom git path.";
#endif
    }
    return msg;
}

} // namespace vivid
