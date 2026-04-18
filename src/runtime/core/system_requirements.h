#pragma once

#include <string>
#include <vector>

namespace vivid {

// ToolStatus describes one external build tool the user may need.
// Produced by check_system_requirements() and consumed by the `vivid doctor`
// CLI subcommand and the GUI system-requirements dialog.
struct ToolStatus {
    std::string name;          // display name: "git" | "cmake" | "c++" | "ninja"
    std::string probe_name;    // tool name passed to find_tool() when applicable
    bool required = true;      // hard requirement for package install/build
    bool found = false;
    std::string path;          // resolved absolute path when found
    std::string version;       // best-effort `--version` first-line, trimmed
    std::string install_hint;  // platform-specific advice when !found
};

struct SystemRequirementsReport {
    std::vector<ToolStatus> tools;
    std::string platform;      // "macos" | "linux" | "windows" | "unknown"

    bool all_required_present() const {
        for (const auto& t : tools) {
            if (t.required && !t.found) return false;
        }
        return true;
    }

    const ToolStatus* find(const std::string& name) const {
        for (const auto& t : tools) {
            if (t.name == name) return &t;
        }
        return nullptr;
    }
};

// Inspect the environment for git, cmake, a C++ compiler, and ninja.
// Does not cache — callers that want freshness after the user installs
// something just re-invoke.
SystemRequirementsReport check_system_requirements();

} // namespace vivid
