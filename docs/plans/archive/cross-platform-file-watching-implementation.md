# Cross-Platform File Watching Implementation Plan

Status: implementation plan only. This is a follow-up to [Third-Party Library Candidates](third-party-library-candidates.md); it does not by itself approve adding a file-watching dependency.

## Goal

Replace the macOS-specific `kqueue` internals in `FileWatcher` with a backend that can support macOS, Windows, and Linux while preserving the public Vivid API.

Primary target:

- `src/runtime/core/file_watcher.cpp`

Public API to preserve:

- `FileWatcher::start(const std::string& operators_dir)`
- `FileWatcher::stop()`
- `FileWatcher::poll_changes()`
- `FileWatcher::add_watch(const std::string& path, const std::string& target_name)`
- `FileWatcher::add_package_watches(const std::string& packages_dir)`
- `FileWatcher::add_shader_operator_watches(const std::string& directory)`
- `FileChangeEvent{file_path, target_name}`

## Recommended Library

Prefer [`efsw`](https://github.com/SpartanJ/efsw) for this migration.

Why:

- It is a C++ file-system watcher and notifier.
- It supports recursive directory watching.
- It supports Linux, Windows, macOS, FreeBSD/BSD, and a generic polling fallback.
- It reports add, delete, modified, and moved actions with path information that maps naturally to Vivid's hot-reload events.
- It has no dependencies.

Keep [`libuv`](https://docs.libuv.org/en/v1.x/) as an alternative only if Vivid decides it wants a broader async/process dependency. `libuv` includes child processes and file-system events, but its `UV_FS_EVENT_RECURSIVE` support is documented as available only on macOS and Windows in the fs-event API docs, which makes Linux recursive operator watching less direct.

## Backend Design

Keep `FileWatcher` as the public wrapper and replace only its internals. Suggested structure:

- Add a private listener type in `file_watcher.cpp` that implements `efsw::FileWatchListener`.
- Store the efsw watcher instance, listener, and watch IDs behind `FileWatcher`.
- Use `std::mutex` and the existing pending queue to keep `poll_changes()` behavior unchanged.
- Keep debounce in Vivid rather than depending on backend-specific coalescing.

Event mapping:

- Convert each efsw callback's directory + filename into a normalized path.
- Ignore directories.
- Ignore files that do not match a registered interest:
  - `.cpp` for operator and package operator watches.
  - `.wgsl` for shader operator watches.
- Map a changed path to the same `target_name` format used today:
  - Seed operator: `<operator_name>`
  - Package operator: `pkg:<package_name>:<operator_name>`
  - Shader operator: `shader:<path>`
- Enqueue `FileChangeEvent{file_path, target_name}` only after target lookup and debounce succeed.

Directory strategy:

- Prefer recursive directory watches for `start(operators_dir)` and `add_package_watches(packages_dir)` where practical.
- Maintain a map from watched root or individual file path to target metadata so events can be filtered back to the correct operator.
- For `add_watch(path, target_name)`, either add a direct file watch if efsw supports the platform path cleanly, or watch the parent directory and filter to the exact filename.
- For `add_shader_operator_watches(directory)`, watch the filter directory and filter to `.wgsl`.

Debounce:

- Preserve the current target-level debounce behavior with `kDebounceMs = 100`.
- Treat add, modified, delete, and moved as rebuild-worthy if the event maps to a known target.
- For rename-on-save/delete-recreate flows, do not require manual file descriptor reopening; directory-level recursive watching should observe the new file path.

## Dependency Integration

Add efsw in `cmake/dependencies.cmake` as a pinned dependency. Prefer a pinned tag or commit and disable examples/tests if its CMake files expose those options. Link only the targets that compile `file_watcher.cpp`, including the app and `test_file_watcher`.

If efsw's upstream CMake integration proves awkward, vendor it under `deps/efsw` as a submodule or pinned source directory. Do not add a package-manager dependency such as Homebrew or vcpkg for the core build.

## Migration Steps

1. Add efsw dependency and confirm a clean configure/build on macOS.
2. Replace `kqueue`, `open`, `close`, `kevent`, and file-descriptor maps inside `FileWatcher`.
3. Preserve the public header shape unless a private forward declaration is needed.
4. Implement path-to-target mapping for:
   - seed operators under `operators/<domain>/<name>/*.cpp`
   - package operators under `<packages>/<package>/operators/<domain>/<name>/*.cpp`
   - shader filters under a filter directory with `.wgsl`
5. Keep `poll_changes()` as the only main-thread drain point.
6. Update docs/runtime/hot_reload.md if behavior changes in a user-visible way, especially around recursive watching or rename-on-save handling.

## Testing

Update `tests/core/test_file_watcher.cpp` to cover the backend-independent contract:

- `start()` on a valid operator tree returns true.
- Writing a watched `.cpp` produces an event with the correct target.
- Two rapid writes produce at most one target event after debounce.
- `stop()` after `start()` does not hang or crash.
- `add_watch()` on a nonexistent file returns false.
- Idle `poll_changes()` returns empty.
- Delete/recreate or rename-on-save produces a rebuild-worthy event for the target.
- `add_package_watches()` skips bad package directories but still registers good packages.
- `add_shader_operator_watches()` emits `shader:<path>` for `.wgsl` changes and ignores non-WGSL files.
- The test compiles on non-macOS without `kqueue` headers.

Verification commands:

```bash
cmake --build build --target test_file_watcher
ctest --test-dir build --output-on-failure -R "test_file_watcher"
```

When Windows/Linux support becomes active, add this test to those CI lanes before deleting any fallback path.

## Acceptance Criteria

- `FileWatcher` no longer includes macOS-only `kqueue` headers in its public or shared implementation path.
- Existing hot-reload call sites do not change.
- Package operator and shader watches keep their current target naming.
- Rename-on-save and delete/recreate flows are covered by tests.
- The implementation remains a file-watching backend swap, not a rewrite of `HotReloader` or operator compilation.

