#include "test_helpers.h"
#include "runtime/platform/process_runner.h"
#include "runtime/core/build_console.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>

using namespace vivid;

static void test_basic_argv() {
    std::fprintf(stderr, "\n--- test_basic_argv ---\n");
    ProcessRunOptions opts;
    opts.argv = {"/bin/echo", "hello", "world"};
    auto result = run_process(opts);
    check(result.launched, "launched");
    check(result.exit_code == 0, "exit code 0");
    // echo outputs "hello world\n"
    check(result.output.find("hello world") != std::string::npos, "output contains 'hello world'");
}

static void test_argv_with_spaces() {
    std::fprintf(stderr, "\n--- test_argv_with_spaces ---\n");
    ProcessRunOptions opts;
    opts.argv = {"/bin/echo", "hello world", "foo'bar"};
    auto result = run_process(opts);
    check(result.launched, "launched");
    check(result.exit_code == 0, "exit code 0");
    check(result.output.find("hello world") != std::string::npos, "space preserved in arg");
    check(result.output.find("foo'bar") != std::string::npos, "single quote preserved in arg");
}

static void test_output_streaming_callback() {
    std::fprintf(stderr, "\n--- test_output_streaming_callback ---\n");
    std::string streamed;
    ProcessRunOptions opts;
    opts.argv = {"/bin/echo", "streamed output"};
    opts.on_output = [&](ProcessOutputStream, std::string_view chunk) {
        streamed.append(chunk);
    };
    auto result = run_process(opts);
    check(result.launched, "launched");
    check(streamed.find("streamed output") != std::string::npos, "callback received output");
    check(result.output.find("streamed output") != std::string::npos, "result also has output");
}

static void test_output_limit() {
    std::fprintf(stderr, "\n--- test_output_limit ---\n");
    ProcessRunOptions opts;
    // Generate more than 32 bytes of output
    opts.argv = {"/bin/echo", "AAAAAAAAAAAAAAAAAAAABBBBBBBBBBBBBBBBBBBBCCCCCCCCCCCCCCCCCCCC"};
    opts.output_limit_bytes = 32;
    std::string streamed;
    opts.on_output = [&](ProcessOutputStream, std::string_view chunk) {
        streamed.append(chunk);
    };
    auto result = run_process(opts);
    check(result.launched, "launched");
    check(result.output.size() <= 32, "output truncated at limit");
    // Callback should still receive all output.
    check(streamed.size() > 32, "callback received full output beyond limit");
}

static void test_nonzero_exit_code() {
    std::fprintf(stderr, "\n--- test_nonzero_exit_code ---\n");
    ProcessRunOptions opts;
    opts.argv = {"/bin/sh", "-c", "exit 42"};
    auto result = run_process(opts);
    check(result.launched, "launched");
    check(result.exit_code == 42, "exit code 42");
}

static void test_missing_executable() {
    std::fprintf(stderr, "\n--- test_missing_executable ---\n");
    ProcessRunOptions opts;
    opts.argv = {"/nonexistent/binary"};
    auto result = run_process(opts);
    check(!result.launched || result.exit_code != 0, "missing executable fails");
    // posix_spawn may return launched=false or launched=true with exit 127
    // depending on whether the kernel validates the path at spawn time.
}

static void test_empty_argv() {
    std::fprintf(stderr, "\n--- test_empty_argv ---\n");
    ProcessRunOptions opts;
    auto result = run_process(opts);
    check(!result.launched, "empty argv not launched");
    check(!result.error.empty(), "error message set");
}

static void test_timeout() {
    std::fprintf(stderr, "\n--- test_timeout ---\n");
    ProcessRunOptions opts;
    opts.argv = {"/bin/sleep", "10"};
    opts.timeout_ms = 200;
    auto result = run_process(opts);
    check(result.launched, "launched");
    check(result.timed_out, "timed out");
}

static void test_working_directory() {
    std::fprintf(stderr, "\n--- test_working_directory ---\n");
    ScopedTempDir tmp("vivid_proc_test");
    ProcessRunOptions opts;
    opts.argv = {"/bin/pwd"};
    opts.working_directory = tmp.str();
    auto result = run_process(opts);
    check(result.launched, "launched");
    check(result.exit_code == 0, "exit code 0");
    // Resolve symlinks for /tmp → /private/tmp on macOS.
    auto resolved = std::filesystem::canonical(tmp.path).string();
    check(result.output.find(resolved) != std::string::npos, "pwd matches working directory");
}

static void test_stderr_captured() {
    std::fprintf(stderr, "\n--- test_stderr_captured ---\n");
    ProcessRunOptions opts;
    opts.argv = {"/bin/sh", "-c", "echo stdout_msg; echo stderr_msg >&2"};
    auto result = run_process(opts);
    check(result.launched, "launched");
    check(result.output.find("stdout_msg") != std::string::npos, "stdout captured");
    check(result.output.find("stderr_msg") != std::string::npos, "stderr captured");
}

static void test_spawn_detached() {
    std::fprintf(stderr, "\n--- test_spawn_detached ---\n");
    std::string err;
    bool ok = spawn_detached({"/usr/bin/true"}, &err);
    check(ok, "spawn_detached returned true");
    check(err.empty(), "no error");
}

static void test_spawn_detached_missing() {
    std::fprintf(stderr, "\n--- test_spawn_detached_missing ---\n");
    std::string err;
    bool ok = spawn_detached({"/nonexistent/binary"}, &err);
    check(!ok, "spawn_detached failed for missing binary");
    check(!err.empty(), "error message set");
}

static void test_spawn_detached_reaps_children() {
    std::fprintf(stderr, "\n--- test_spawn_detached_reaps_children ---\n");
    ScopedTempDir tmp("vivid_proc_reap");
    std::vector<pid_t> pids;

    for (int i = 0; i < 5; ++i) {
        auto pid_path = tmp.path / ("pid_" + std::to_string(i) + ".txt");
        std::string err;
        bool ok = spawn_detached({"/bin/sh",
                                  "-c",
                                  "printf '%s\\n' \"$$\" > \"$1\"",
                                  "vivid-reap-test",
                                  pid_path.string()},
                                 &err);
        check(ok, "spawn_detached child launched for reap regression");
        if (!ok) {
            std::fprintf(stderr, "    error: %s\n", err.c_str());
            continue;
        }

        std::string pid_text;
        for (int attempt = 0; attempt < 50; ++attempt) {
            std::ifstream ifs(pid_path);
            if (ifs && (ifs >> pid_text) && !pid_text.empty())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        char* end = nullptr;
        long parsed = std::strtol(pid_text.c_str(), &end, 10);
        check(parsed > 0 && end && *end == '\0', "detached child wrote its pid");
        if (parsed > 0)
            pids.push_back(static_cast<pid_t>(parsed));
    }

    bool all_reaped = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        all_reaped = true;
        for (pid_t pid : pids) {
            errno = 0;
            if (kill(pid, 0) == 0 || errno != ESRCH) {
                all_reaped = false;
                break;
            }
        }
        if (all_reaped)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    for (pid_t pid : pids) {
        errno = 0;
        bool reaped = (kill(pid, 0) != 0 && errno == ESRCH);
        if (!reaped) {
            int status = 0;
            waitpid(pid, &status, WNOHANG);
        }
        check(reaped, "detached child was reaped asynchronously");
    }
}

static void test_build_console_streaming() {
    std::fprintf(stderr, "\n--- test_build_console_streaming ---\n");
    BuildConsole console;
    auto task_id = console.begin_task(BuildTaskKind::PackageBuild, "test build");

    ProcessRunOptions opts;
    opts.argv = {"/bin/sh", "-c", "echo line1; echo line2; echo line3"};
    auto result = run_build_process(opts, console, task_id, BuildConsoleStreamKind::Stdout);

    console.finish_task(task_id, result.exit_code == 0 ? BuildTaskState::Succeeded : BuildTaskState::Failed);

    check(result.launched, "launched");
    check(result.exit_code == 0, "exit code 0");

    auto snap = console.snapshot();
    // Should have: TaskStart + 3 lines + TaskFinish = 5 entries
    int line_count = 0;
    bool found_line1 = false, found_line2 = false, found_line3 = false;
    for (auto& line : snap.lines) {
        if (line.entry_kind == BuildConsoleEntryKind::Line) {
            line_count++;
            if (line.text == "line1") found_line1 = true;
            if (line.text == "line2") found_line2 = true;
            if (line.text == "line3") found_line3 = true;
        }
    }
    check(found_line1, "line1 streamed to console");
    check(found_line2, "line2 streamed to console");
    check(found_line3, "line3 streamed to console");
    check(line_count == 3, "exactly 3 output lines");
}

static void test_env_overrides() {
    std::fprintf(stderr, "\n--- test_env_overrides ---\n");
    ProcessRunOptions opts;
    opts.argv = {"/bin/sh", "-c", "echo $VIVID_TEST_VAR"};
    opts.env_overrides = {{"VIVID_TEST_VAR", "hello_from_process_runner"}};
    auto result = run_process(opts);
    check(result.launched, "launched");
    check(result.output.find("hello_from_process_runner") != std::string::npos, "env override applied");
}

int main() {
    std::fprintf(stderr, "=== test_process_runner ===\n");

    test_basic_argv();
    test_argv_with_spaces();
    test_output_streaming_callback();
    test_output_limit();
    test_nonzero_exit_code();
    test_missing_executable();
    test_empty_argv();
    test_timeout();
    test_working_directory();
    test_stderr_captured();
    test_spawn_detached();
    test_spawn_detached_missing();
    test_spawn_detached_reaps_children();
    test_build_console_streaming();
    test_env_overrides();

    std::fprintf(stderr, "\n=== %d failure(s) ===\n", failures);
    return failures > 0 ? 1 : 0;
}
