#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vivid {

enum class ProcessOutputStream { Stdout, Stderr };

struct ProcessRunOptions {
    std::vector<std::string> argv;
    std::string working_directory;  // empty = inherit from parent
    std::vector<std::pair<std::string, std::string>> env_overrides;
    size_t output_limit_bytes = 64 * 1024;
    int timeout_ms = 0;  // 0 = no timeout
    std::function<void(ProcessOutputStream, std::string_view)> on_output;
};

struct ProcessRunResult {
    bool launched = false;
    int exit_code = -1;
    bool timed_out = false;
    std::string output;  // accumulated stdout+stderr
    std::string error;   // launch/system error message
};

// Run a child process synchronously with argv-based execution (no shell).
// Captures stdout and stderr into result.output and streams chunks to
// on_output as they arrive.
ProcessRunResult run_process(const ProcessRunOptions& options);

// Launch a detached child process (fire-and-forget). Returns true on success.
bool spawn_detached(const std::vector<std::string>& argv,
                    std::string* error_out = nullptr);

class BuildConsole;
enum class BuildConsoleStreamKind;
using BuildTaskId = uint64_t;

// Run a process and stream its output line-by-line to a BuildConsole task.
// Installs an on_output callback that line-buffers and calls
// console.append_line() for each line, then returns the result.
ProcessRunResult run_build_process(
    const ProcessRunOptions& options,
    BuildConsole& console,
    BuildTaskId task_id,
    BuildConsoleStreamKind stream_kind);

}  // namespace vivid
