# Process Runner Abstraction Implementation Plan

Status: implementation plan only. This is a follow-up to [Third-Party Library Candidates](third-party-library-candidates.md); it does not by itself add a process library or complete the migration.

## Goal

Introduce a Vivid-owned process execution boundary that removes shell command construction from build, package, export, and test paths. Implement the first version internally, then keep [libuv process support](https://docs.libuv.org/en/latest/guide/processes.html) and [Boost.Process](https://www.boost.org/doc/libs/release/libs/process/) as alternatives behind the same API if the internal runner becomes too platform-heavy.

Recommended target:

- `src/runtime/platform/process_runner.h`
- `src/runtime/platform/process_runner.cpp`

Primary migration targets:

- hot reload and package CMake builds
- package compiler and package test runner command execution
- package git clone and export build commands
- detached editor/browser launches only after the blocking runner API is stable

Defer appcast `curl` execution to the libcurl plan in [Libcurl HTTP Fetch Implementation Plan](libcurl-http-fetch-implementation.md).

## API Shape

Use argv-based execution. Do not accept shell command strings for build/package/export paths.

Suggested API:

```cpp
namespace vivid {

struct ProcessRunOptions {
    std::vector<std::string> argv;
    std::string working_directory;
    std::vector<std::pair<std::string, std::string>> env_overrides;
    size_t output_limit_bytes = 64 * 1024;
    int timeout_ms = 0; // 0 means no timeout
    std::function<void(BuildConsoleStreamKind, std::string_view)> on_output;
};

struct ProcessRunResult {
    bool launched = false;
    int exit_code = -1;
    bool timed_out = false;
    std::string output;
    std::string error;
};

ProcessRunResult run_process(const ProcessRunOptions& options);
bool spawn_detached_process(const std::vector<std::string>& argv, std::string* error_out = nullptr);

} // namespace vivid
```

If including `BuildConsoleStreamKind` in a platform header would create an unwanted dependency, replace it with a small local enum such as `ProcessOutputStream { Stdout, Stderr }` and translate at call sites.

Required behavior:

- Treat `argv[0]` as the executable and pass arguments without shell interpolation.
- Capture stdout and stderr. Combining them is acceptable for v1 if the result and callback document that behavior.
- Call `on_output` as chunks arrive so build console behavior remains live.
- Enforce `output_limit_bytes` on accumulated `result.output`; callbacks may still receive full streamed output unless a call site explicitly opts out.
- Return `launched=false` and a stable `error` for missing executable or spawn failure.
- Return `launched=true` with a nonzero `exit_code` for child process failures.
- If `timeout_ms > 0`, terminate the child on timeout, set `timed_out=true`, and return a stable timeout error.

The custom editor command path should remain a documented shell escape hatch because it intentionally supports user-provided shell templates. Keep that shell usage isolated and do not reuse it for internal build commands.

## Implementation Strategy

Implement the first version with platform APIs:

- POSIX/macOS: `posix_spawn` or `fork`/`exec` with pipes for stdout/stderr and `waitpid`.
- Windows, when active: `CreateProcessW` with argument quoting centralized inside `ProcessRunner`.
- Detached launches: use platform-native detached process behavior and avoid waiting.

Keep the abstraction narrow. It should run tools, stream/capture output, and report exit status; it should not become a general job-control or async framework.

Document alternatives in the implementation notes:

- `libuv` is a good fallback if Vivid later wants one broader dependency for process execution, file watching, and event-loop primitives. It brings portable child process management, pipes, cwd, environment, and exit callbacks, but it also brings event-loop lifecycle concerns into mostly synchronous build/package code.
- `Boost.Process` is a good fallback if the project accepts a scoped Boost dependency. It has a rich process API with cwd, pipes, environment, timeout/cancellation patterns, and Boost.Asio integration, but it pulls in Boost dependency surface area that is not otherwise needed today.

## Migration Steps

1. Add `ProcessRunner` and focused unit tests before changing call sites.
2. Migrate hot reload and package CMake build paths first because they need live build-console streaming and currently duplicate shell quoting.
3. Migrate package compiler, package test runner, export build, and package git clone paths next. Preserve output truncation and error codes at each call site.
4. Migrate detached editor/browser launch helpers only if the API fits cleanly. Keep the user custom-editor shell template as the explicit exception.
5. Remove unused local `quote()` helpers and process includes only after each call site is migrated.
6. Update runtime/package docs only when behavior changes visibly. A pure shell-to-argv refactor with preserved behavior should not require user-facing doc churn beyond this plan.

## Testing

Add tests for the ProcessRunner contract:

- argv containing spaces and single quotes runs without shell quoting.
- stdout and stderr are captured or combined as documented.
- streaming callback receives output chunks during execution.
- accumulated output truncates at `output_limit_bytes`.
- nonzero child exit status returns `launched=true` and the correct exit code.
- missing executable returns `launched=false` with a stable error.
- timeout terminates a long-running child and sets `timed_out=true`.
- detached launch returns without blocking.

Then add regression coverage at migrated call sites:

- hot reload still streams CMake output to the build console.
- package configure/build preserve existing error codes.
- package test runner preserves test output truncation.
- export build still reports configure and build failures separately.
- git clone paths with spaces or single quotes no longer require shell quoting.

Verification commands:

```bash
cmake --build build --target test_process_runner test_package_manager test_package_compiler
ctest --test-dir build --output-on-failure -R "process_runner|package_manager|package_compiler"
```

Adjust target names to the existing test targets if needed.

## Acceptance Criteria

- Build/package/export command execution no longer constructs shell strings.
- `ProcessRunner` owns argv handling, output capture, truncation, timeout, and launch errors.
- Existing build-console streaming behavior is preserved.
- Custom editor command templates remain the only intentional shell-based escape hatch in this area.
- libuv and Boost.Process remain documented alternatives, not required dependencies for v1.
