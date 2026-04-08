# Third-Party Library Candidates

## Context

Vivid should stay custom where the code is product-defining: graph compilation, lane transport, the operator API, hot-reload semantics, package manifest semantics, and the RuntimeAPI. Those systems encode Vivid's model of audiovisual graph authoring, so a generic dependency is unlikely to make them simpler without also making them less Vivid-shaped.

The better target is infrastructure code where third-party libraries can reduce fragile glue: network fetching, process execution, file watching, XML parsing, and lightweight source parsing. Prefer small, focused dependencies with clear ownership and CMake-friendly integration. Avoid replacing working, focused dependencies such as `nlohmann/json`, `CLI11`, IXWebSocket, RtMidi, oscpack, miniaudio, GLFW, stb, NanoSVG, Snappy, or Sparkle unless there is a concrete failure mode or maintenance burden.

Boost is not recommended as a broad dependency. Revisit it only for a narrow Boost.Process decision if smaller process-runner options are insufficient.

## Candidate Assessments

### `libcurl` For HTTP/Network Fetches

**Priority:** High

**Candidate replacement:** Shelling out to `curl` for package catalog and app update fetches.

**Likely targets:**

- `src/runtime/packages/package_catalog.cpp`
- `src/runtime/platform/app_update_manager.cpp`

**Reasoning:** These paths currently spend code on subprocess launch, output capture, shell quoting, timeout flags, and curl availability. `libcurl` would remove the shell boundary and make network fetches a normal in-process API call. Its blocking easy interface fits Vivid's existing background-thread fetch model, so adopting it does not require changing the surrounding async architecture.

**Recommended next step:** Prototype a tiny `HttpFetch` helper with `GET(url, timeout_seconds) -> {ok, status/error, body}` and migrate package catalog fetch first. If the helper stays clean, migrate app update fetch next.

### `tinyxml2` For Appcast XML Parsing

**Priority:** High/Medium

**Candidate replacement:** Regex-based XML parsing in `AppUpdateManager`.

**Likely target:**

- `src/runtime/platform/app_update_manager.cpp`

**Reasoning:** Appcast XML can vary in harmless ways: attribute order, whitespace, namespaces, additional tags, or multiple items. Regex parsing is brittle for this shape of data. `tinyxml2` is small, C++-friendly, and appropriate for simple DOM-style extraction of item/enclosure metadata.

**Recommended next step:** First decide whether Vivid needs its own machine-readable update summary. If Sparkle can fully own update discovery and user-facing update UI, prefer that. If Vivid still needs structured update data for MCP or internal UI, replace regex parsing with `tinyxml2` and keep the public `AppUpdateInfo` behavior unchanged.

### Process Runner Abstraction

**Priority:** Medium

**Candidate replacement:** Repeated `popen`, `std::system`, `posix_spawn`, output capture, truncation, and shell quoting patterns.

**Possible libraries:**

- Prefer first: a small in-repo `ProcessRunner` wrapper around platform APIs.
- Consider `libuv` if Vivid wants one broader dependency for process execution plus file watching.
- Consider Boost.Process only if the project accepts a scoped Boost dependency.

**Likely targets:**

- `src/runtime/core/hot_reload.cpp`
- `src/runtime/packages/package_compiler.cpp`
- `src/runtime/packages/package_manager_build.cpp`
- `src/runtime/packages/package_manager_install.cpp`
- `src/runtime/packages/package_test_runner.cpp`
- `src/export/export_pipeline.cpp`
- `src/runtime/operators/operator_creator.cpp`

**Reasoning:** Process launch is one of the most repeated infrastructure patterns in the codebase. A shared abstraction would reduce quoting bugs, normalize stdout/stderr capture, provide consistent output limits, and make future Windows/Linux support less scattered.

**Recommended next step:** Build an internal `ProcessRunner` first rather than adding a dependency immediately. The first API should support argv-based execution, combined stdout/stderr capture, timeout where needed, working directory, environment overrides, and streamed build-console output. Reassess `libuv` or Boost.Process only if that abstraction becomes too platform-heavy.

### Cross-Platform File Watching

**Priority:** Medium, especially when Windows/Linux work starts

**Candidate replacement:** macOS-specific `kqueue` file watcher code.

**Possible libraries:**

- `efsw`
- `libuv`

**Likely target:**

- `src/runtime/core/file_watcher.cpp`

**Reasoning:** File watching is platform-specific and editor-save behavior is subtle. Vivid currently needs open/reopen/debounce handling for operator hot reload and package file watching. A battle-tested watcher would reduce porting work and edge-case behavior once the project moves beyond macOS-first.

**Recommended next step:** Keep the current watcher for macOS 1.0 unless it becomes flaky. When cross-platform support becomes active, evaluate `efsw` versus `libuv` with a small hot-reload stress test covering write, rename-on-save, delete/recreate, and package operator watches.

### `tree-sitter` For Source Indexing And Operator Docs

**Priority:** Medium/Low

**Candidate replacement:** Regex-based C++ symbol/source parsing.

**Likely targets:**

- `src/runtime/core/source_index.cpp`
- `src/runtime/operators/operator_source_docs.cpp`

**Reasoning:** The current implementation is lightweight and probably adequate for MCP source search, simple symbol lookup, and operator documentation extraction. If this grows into a serious code-intelligence surface, regex parsing will become brittle around templates, namespaces, macros, multi-line declarations, and comments. `tree-sitter` would provide a real parse tree without requiring a full compiler frontend.

**Recommended next step:** Do not add this yet. Revisit if source navigation, operator-doc generation, refactoring support, or MCP code understanding becomes a major product surface. At that point, prototype `tree-sitter-cpp` on operator docs extraction before touching the broader source index.

### MIDI File Parser Library

**Priority:** Low/Medium

**Candidate replacement:** The compact in-house MIDI file parser.

**Possible library:**

- Midifile, or another small Standard MIDI File parser with permissive licensing and CMake-friendly integration.

**Likely target:**

- `src/common/midi_file.cpp`

**Reasoning:** The current parser is intentionally narrow and appears appropriate for simple format 0/1 playback. Standard MIDI Files have enough edge cases that this should not grow indefinitely in-house if Vivid starts supporting richer MIDI import behavior, metadata, tempo maps, SMPTE timing, lyrics/markers, or editing workflows.

**Recommended next step:** Keep the current parser while MIDI file playback remains simple. Reevaluate once user-facing MIDI import requirements expand beyond the current event stream.

## Non-Candidates / Keep Custom

- Keep graph compilation, lane transport, operator API, hot-reload semantics, package manifest semantics, and RuntimeAPI custom.
- Keep focused existing dependencies unless they show concrete pain: `nlohmann/json`, `CLI11`, IXWebSocket, RtMidi, oscpack, miniaudio, GLFW, stb, NanoSVG, Snappy, and Sparkle.
- Do not add Boost broadly. If process execution remains the only compelling Boost use case, prefer a small internal `ProcessRunner` first and revisit Boost.Process only if needed.

## Suggested Priority

1. Replace shell-based HTTP fetches with a small `libcurl` helper.
2. Replace appcast regex parsing with either Sparkle-owned behavior or `tinyxml2`.
3. Add an internal process-runner abstraction before considering a process library.
4. Defer file watcher library evaluation until cross-platform support becomes active.
5. Defer `tree-sitter` until source intelligence becomes a real product surface.
6. Defer MIDI parser replacement until MIDI import requirements grow.

