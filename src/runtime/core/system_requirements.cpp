#include "runtime/core/system_requirements.h"

#include "runtime/core/tool_discovery.h"
#include "runtime/platform/process_runner.h"


namespace vivid {

namespace {

const char* current_platform() {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                          s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
    return s.substr(start);
}

std::string first_line(const std::string& s) {
    auto nl = s.find('\n');
    return trim(nl == std::string::npos ? s : s.substr(0, nl));
}

// Probe `<path> --version` and return the first trimmed line. Bounded timeout
// so a hung binary can't stall the preflight.
std::string probe_version(const std::string& path) {
    if (path.empty()) return {};
    ProcessRunOptions opts;
    opts.argv = {path, "--version"};
    opts.timeout_ms = 3000;
    auto result = run_process(opts);
    if (!result.launched || result.exit_code != 0) return {};
    return first_line(result.output);
}

ToolStatus probe(const char* display,
                 const char* probe_name,
                 bool required) {
    ToolStatus status;
    status.name = display;
    status.probe_name = probe_name;
    status.required = required;
    status.path = find_tool(probe_name);
    status.found = !status.path.empty();
    if (status.found) {
        status.version = probe_version(status.path);
    } else {
        status.install_hint = missing_tool_error(probe_name);
    }
    return status;
}

ToolStatus probe_cxx() {
    ToolStatus status;
    status.name = "c++";
    status.probe_name = "clang++";  // used for missing_tool_error lookup
    status.required = true;
    status.path = find_cxx_compiler();
    status.found = !status.path.empty();
    if (status.found) {
        status.version = probe_version(status.path);
    } else {
        // Reuse the clang++ hint — its text already covers the platform cases
        // (Xcode CLT on macOS, Visual Studio on Windows, build-essential advice).
        status.install_hint = missing_tool_error("clang++");
    }
    return status;
}

} // namespace

SystemRequirementsReport check_system_requirements() {
    SystemRequirementsReport report;
    report.platform = current_platform();
    report.tools.push_back(probe_cxx());
    report.tools.push_back(probe("cmake", "cmake", /*required=*/true));
    report.tools.push_back(probe("git",   "git",   /*required=*/true));
    report.tools.push_back(probe("ninja", "ninja", /*required=*/false));
    return report;
}

} // namespace vivid
