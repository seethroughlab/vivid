#include "runtime/core/tool_discovery.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace vivid {

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

std::string find_tool(const char* tool) {
    if (tool_forced_missing(tool)) return {};

    // Check env var override first
    const char* env_name = env_var_for_tool(tool);
    if (env_name) {
        const char* env_val = std::getenv(env_name);
        if (env_val && *env_val) return env_val;
    }

    std::string cmd = "PATH=$PATH:/opt/homebrew/bin:/usr/local/bin command -v ";
    cmd += tool;
    cmd += " 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return {};
    char buf[512] = {};
    fgets(buf, sizeof(buf), f);
    pclose(f);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

std::string missing_tool_error(const char* tool) {
    std::string msg = "Missing required build tool: ";
    msg += tool;
    if (std::string(tool) == "clang++") {
        msg += ". Install Xcode Command Line Tools with `xcode-select --install`,"
               " or set VIVID_CXX to a custom compiler path.";
    } else if (std::string(tool) == "cmake") {
        msg += ". Install CMake (e.g. `brew install cmake`),"
               " or set VIVID_CMAKE to a custom cmake path.";
    } else if (std::string(tool) == "git") {
        msg += ". Install Git (included with Xcode Command Line Tools),"
               " or set VIVID_GIT to a custom git path.";
    }
    return msg;
}

} // namespace vivid
