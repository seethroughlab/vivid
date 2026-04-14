#include "runtime/platform/process_runner.h"
#include "runtime/core/build_console.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#error "ProcessRunner: Windows implementation not yet available"
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace vivid {

#if !defined(_WIN32)

// Build a null-terminated argv array from the string vector.
static std::vector<char*> make_argv(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    return argv;
}

// Build an envp array: copy environ and apply overrides.
static std::vector<std::string> build_env_strings(
    const std::vector<std::pair<std::string, std::string>>& overrides) {
    std::vector<std::string> env;
    // Copy existing environment.
    for (char** e = environ; e && *e; ++e)
        env.emplace_back(*e);
    // Apply overrides (replace or append).
    for (auto& [key, value] : overrides) {
        std::string prefix = key + "=";
        bool found = false;
        for (auto& entry : env) {
            if (entry.compare(0, prefix.size(), prefix) == 0) {
                entry = prefix + value;
                found = true;
                break;
            }
        }
        if (!found)
            env.push_back(prefix + value);
    }
    return env;
}

static std::vector<char*> make_envp(const std::vector<std::string>& env_strings) {
    std::vector<char*> envp;
    envp.reserve(env_strings.size() + 1);
    for (auto& s : env_strings)
        envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);
    return envp;
}

static void reap_detached_child(pid_t pid) {
    try {
        std::thread([pid]() {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0) {
                if (errno == EINTR)
                    continue;
                if (errno != ECHILD) {
                    std::fprintf(stderr,
                                 "[vivid] ProcessRunner: failed to reap detached child %d: %s\n",
                                 static_cast<int>(pid), std::strerror(errno));
                }
                return;
            }
        }).detach();
    } catch (const std::system_error& e) {
        std::fprintf(stderr,
                     "[vivid] ProcessRunner: failed to start detached child reaper for %d: %s\n",
                     static_cast<int>(pid), e.what());
    }
}

static pid_t fork_exec_process(const ProcessRunOptions& options,
                               const std::vector<char*>& argv,
                               int pipe_fd[2],
                               int* spawn_err) {
    pid_t pid = fork();
    if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);

        if (!options.working_directory.empty() &&
            chdir(options.working_directory.c_str()) != 0) {
            int err = errno;
            std::fprintf(stderr, "chdir failed for %s: %s\n",
                         options.working_directory.c_str(), std::strerror(err));
            _exit(127);
        }

        for (auto& [key, value] : options.env_overrides)
            setenv(key.c_str(), value.c_str(), 1);

        execvp(argv[0], argv.data());
        int err = errno;
        std::fprintf(stderr, "exec failed for %s: %s\n", argv[0], std::strerror(err));
        _exit(err == ENOENT ? 127 : 126);
    }

    if (pid < 0) {
        *spawn_err = errno;
        return -1;
    }

    *spawn_err = 0;
    return pid;
}

ProcessRunResult run_process(const ProcessRunOptions& options) {
    ProcessRunResult result;

    if (options.argv.empty()) {
        result.error = "empty argv";
        return result;
    }

    // Create a pipe for child stdout+stderr.
    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) {
        result.error = std::string("pipe() failed: ") + std::strerror(errno);
        return result;
    }

    // Set up posix_spawn file actions: dup pipe write end to stdout and stderr.
    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_addclose(&file_actions, pipe_fd[0]);   // close read end in child
    posix_spawn_file_actions_adddup2(&file_actions, pipe_fd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, pipe_fd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, pipe_fd[1]);   // close original write end

    // Working directory: use posix_spawn_file_actions_addchdir if available,
    // otherwise fall back to fork/exec.
    bool need_chdir = !options.working_directory.empty();
#if defined(__APPLE__)
    if (need_chdir) {
        posix_spawn_file_actions_addchdir(&file_actions, options.working_directory.c_str());
        need_chdir = false;
    }
#elif defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 29
    if (need_chdir) {
        posix_spawn_file_actions_addchdir_np(&file_actions, options.working_directory.c_str());
        need_chdir = false;
    }
#endif

    // Build argv.
    auto argv = make_argv(options.argv);

    // Build envp if overrides are provided.
    std::vector<std::string> env_strings;
    std::vector<char*> envp;
    char** envp_ptr = environ;
    if (!options.env_overrides.empty()) {
        env_strings = build_env_strings(options.env_overrides);
        envp = make_envp(env_strings);
        envp_ptr = envp.data();
    }

    pid_t pid = -1;
    int spawn_err = 0;

    if (need_chdir || options.prefer_fork_exec) {
        pid = fork_exec_process(options, argv, pipe_fd, &spawn_err);
        posix_spawn_file_actions_destroy(&file_actions);
    } else {
        spawn_err = posix_spawnp(&pid, options.argv[0].c_str(),
                                 &file_actions, nullptr, argv.data(), envp_ptr);
        posix_spawn_file_actions_destroy(&file_actions);
        if (spawn_err == EACCES || spawn_err == EPERM) {
            std::fprintf(stderr,
                         "[vivid] ProcessRunner: posix_spawnp failed for %s: %s; retrying with fork/exec\n",
                         options.argv[0].c_str(), std::strerror(spawn_err));
            pid = fork_exec_process(options, argv, pipe_fd, &spawn_err);
        }
    }

    // Close write end in parent.
    close(pipe_fd[1]);

    if (spawn_err != 0 || pid < 0) {
        close(pipe_fd[0]);
        result.error = std::string("spawn failed: ") + std::strerror(spawn_err ? spawn_err : errno);
        return result;
    }

    result.launched = true;

    // Read child output with optional timeout using a deadline-based approach.
    std::array<char, 4096> buf;
    bool output_truncated = false;
    auto deadline_ms = [&]() -> int64_t {
        if (options.timeout_ms <= 0) return -1;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<int64_t>(ts.tv_sec) * 1000 +
               static_cast<int64_t>(ts.tv_nsec) / 1000000 + options.timeout_ms;
    };
    int64_t deadline = deadline_ms();

    while (true) {
        int poll_timeout = -1;  // block indefinitely
        if (deadline >= 0) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t now = static_cast<int64_t>(ts.tv_sec) * 1000 +
                          static_cast<int64_t>(ts.tv_nsec) / 1000000;
            poll_timeout = static_cast<int>(deadline - now);
            if (poll_timeout < 0) poll_timeout = 0;
        }

        struct pollfd pfd;
        pfd.fd = pipe_fd[0];
        pfd.events = POLLIN;
        int poll_rc = poll(&pfd, 1, poll_timeout);

        if (poll_rc == 0 && deadline >= 0) {
            // Timeout expired.
            kill(pid, SIGKILL);
            result.timed_out = true;
            break;
        }

        if (poll_rc < 0) {
            if (errno == EINTR) continue;
            break;
        }

        ssize_t n = read(pipe_fd[0], buf.data(), buf.size());
        if (n <= 0) break;  // EOF or error

        std::string_view chunk(buf.data(), static_cast<size_t>(n));

        // Stream to callback.
        if (options.on_output)
            options.on_output(ProcessOutputStream::Stdout, chunk);

        // Accumulate (respecting limit).
        if (!output_truncated) {
            size_t remaining = options.output_limit_bytes > result.output.size()
                                   ? options.output_limit_bytes - result.output.size()
                                   : 0;
            if (remaining > 0)
                result.output.append(chunk.data(), std::min(chunk.size(), remaining));
            if (result.output.size() >= options.output_limit_bytes)
                output_truncated = true;
        }
    }

    close(pipe_fd[0]);

    // Reap child.
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status))
        result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        result.exit_code = 128 + WTERMSIG(status);
    else
        result.exit_code = -1;

    return result;
}

bool spawn_detached(const std::vector<std::string>& argv, std::string* error_out) {
    if (argv.empty()) {
        if (error_out) *error_out = "empty argv";
        return false;
    }

    auto args = make_argv(argv);

    // Use posix_spawn with stdio redirected to /dev/null.
    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);

    // Redirect child stdin/stdout/stderr to /dev/null so it's fully detached.
    posix_spawn_file_actions_addopen(&file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&file_actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&file_actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, argv[0].c_str(), &file_actions, nullptr,
                          args.data(), environ);
    posix_spawn_file_actions_destroy(&file_actions);

    if (rc != 0) {
        if (error_out) *error_out = std::string("posix_spawn failed: ") + std::strerror(rc);
        return false;
    }

    // Fire-and-forget for callers, but still reap our direct child asynchronously.
    reap_detached_child(pid);
    return true;
}

#endif  // !_WIN32

// --- BuildConsole streaming helper (platform-independent) ---

ProcessRunResult run_build_process(
    const ProcessRunOptions& options,
    BuildConsole& console,
    BuildTaskId task_id,
    BuildConsoleStreamKind stream_kind) {
    // Line-buffering adapter: accumulates partial lines and flushes on '\n'.
    std::string line_buf;
    auto line_callback = [&](ProcessOutputStream, std::string_view chunk) {
        line_buf.append(chunk);
        size_t start = 0;
        while (true) {
            size_t nl = line_buf.find('\n', start);
            if (nl == std::string::npos) break;
            std::string line = line_buf.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            console.append_line(task_id, stream_kind, line);
            start = nl + 1;
        }
        if (start > 0)
            line_buf.erase(0, start);
    };

    // Build a modified options with our line callback installed.
    ProcessRunOptions opts = options;
    opts.on_output = line_callback;

    ProcessRunResult result = run_process(opts);

    // Flush any trailing partial line.
    if (!line_buf.empty()) {
        if (!line_buf.empty() && line_buf.back() == '\r')
            line_buf.pop_back();
        if (!line_buf.empty())
            console.append_line(task_id, stream_kind, line_buf);
    }

    return result;
}

}  // namespace vivid
