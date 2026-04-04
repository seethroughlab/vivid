# Phase 6: Build System & Dependencies

**Date:** 2026-04-03
**Status:** Complete

## Summary Table

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| B-01 | Critical | Build Performance | Test targets recompile runtime sources redundantly (key files compiled 40-53x) | `CMakeLists.txt` tests section |
| B-02 | High | Dependencies | webgpu FetchContent pinned to `main` branch — non-deterministic | `CMakeLists.txt:32` |
| B-03 | Medium | Compiler Flags | No warning flags (-Wall -Wextra) configured | `CMakeLists.txt` |
| B-04 | Medium | Organization | Monolithic CMakeLists.txt at 2,889 lines | `CMakeLists.txt` |
| B-05 | Info | Dependencies | 5/6 FetchContent deps properly version-pinned | `CMakeLists.txt:32-114` |
| B-06 | Info | Operator Build | `add_vivid_operator()` macro is clean and well-designed | `CMakeLists.txt:220-310` |
| B-07 | Info | Platform | macOS first-class; Windows/Linux minimal but functional | Various |
| B-08 | Info | Include Dirs | Clean setup with `src/` as include root | Various |

## Severity Definitions

Same scale as Phase 1.

---

## Findings

### B-01: Test source duplication — massive build overhead [Critical]

**What:** Each of 107 test executables lists individual `src/runtime/*.cpp` files as sources rather than linking a shared library. This causes the same source files to be compiled dozens of times.

**Compilation counts for key files:**

| File | Times compiled | Redundant compilations |
|------|---------------|----------------------|
| `operator_registry.cpp` | 53 | 52 |
| `graph.cpp` | 46 | 45 |
| `graph_compiler.cpp` | 42 | 41 |
| `port_type_registry.cpp` | 41 | 40 |
| `runtime_core.cpp` | 37 | 36 |
| `audio_engine.cpp` | 24 | 23 |

**Example (two tests compiling the same files independently):**
```cmake
add_executable(test_hot_reload
    tests/core/test_hot_reload.cpp
    src/runtime/operators/operator_registry.cpp  # compiled here
    src/runtime/graph/graph.cpp                  # compiled here
    # ... 10 more runtime files ...
)

add_executable(test_audio_hot_reload
    tests/audio/test_audio_hot_reload.cpp
    src/runtime/operators/operator_registry.cpp  # COMPILED AGAIN
    src/runtime/graph/graph.cpp                  # COMPILED AGAIN
    # ... same files again ...
)
```

**Impact:** Test build time is estimated 2-4x longer than necessary. A full test build recompiles `operator_registry.cpp` 53 times instead of once.

**Recommendation:** Create a `vivid_runtime` OBJECT or STATIC library containing the commonly-used runtime sources. Test targets would link against this instead of listing individual `.cpp` files:

```cmake
add_library(vivid_runtime OBJECT
    src/runtime/operators/operator_registry.cpp
    src/runtime/graph/graph.cpp
    src/runtime/core/runtime_core.cpp
    # ... all commonly-used runtime files ...
)

add_executable(test_hot_reload tests/core/test_hot_reload.cpp)
target_link_libraries(test_hot_reload PRIVATE vivid_runtime ...)
```

**Effort:** Medium — requires identifying the common source set, creating the library target, and updating all 107 test targets. The test target definitions would shrink from ~15 lines to ~3 lines each, also reducing CMakeLists.txt by ~1,000 lines.

---

### B-02: webgpu dependency pinned to `main` branch [High]

**What:** The WebGPU distribution is fetched from the `main` branch without a version pin:
```cmake
FetchContent_Declare(webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
    GIT_TAG main    # ← not pinned
    GIT_SHALLOW TRUE
)
```

All other FetchContent dependencies are properly pinned:
- IXWebSocket: `v11.4.5`
- CLI11: `v2.6.1`
- snappy: `1.2.1`
- nlohmann/json: `v3.11.3`

**Why it matters:** Building from branch tip introduces non-deterministic builds — a CI run today may produce different results than tomorrow if the upstream changes.

**Recommendation:** Pin to a specific commit hash or tag.

**Effort:** Trivial (1 line change)

---

### B-03: No compiler warning flags configured [Medium]

**What:** The CMakeLists.txt does not set any warning flags (`-Wall`, `-Wextra`, `-Wpedantic`). No warnings are enabled or suppressed.

**Why it matters:** Compiler warnings catch bugs, undefined behavior, and questionable patterns at compile time. Without them, issues go unnoticed until runtime.

**Recommendation:** Add warning flags for the project's own targets (not vendored deps):
```cmake
add_compile_options(-Wall -Wextra)
```

**Effort:** Trivial to add, but may produce many warnings initially that need triaging.

---

### B-04: Monolithic CMakeLists.txt [Medium]

**What:** All build configuration lives in a single 2,889-line file. Major sections:

| Section | Lines | Content |
|---------|-------|---------|
| Dependencies | 1-199 | FetchContent + vendored |
| Operator plugins | 220-731 | ~157 operator targets |
| UI library + app | 733-902 | vivid_ui + vivid executable |
| Bundle/manifest | 904-1072 | macOS app bundle, operator copying |
| Tests | 1073-2889 | 107 test executables (~1,800 lines) |

**Why it matters:** At this size, finding and modifying test targets or adding operators requires scrolling through a large file. The test section alone is 1,800 lines of repetitive target definitions.

**Recommendation:** Consider splitting into:
- `CMakeLists.txt` — top-level, includes subdirectories
- `cmake/dependencies.cmake` — FetchContent + vendored
- `operators/CMakeLists.txt` — operator plugin targets
- `tests/CMakeLists.txt` — test targets (would shrink dramatically with B-01 fix)

**Effort:** Medium, but lower priority than B-01 (which would naturally shrink the file by ~1,000 lines).

---

### B-05: Dependency management is mostly healthy [Info]

**FetchContent dependencies (6 total):**
- 5 of 6 properly version-pinned (IXWebSocket, CLI11, snappy, nlohmann/json, wgpu-native)
- Shallow clones used for faster fetches
- No deprecated or unnecessary dependencies detected

**Vendored dependencies (8 total):**
- GLFW, miniaudio, stb_truetype, NanoSVG, RtMidi, oscpack, HAP codec, Syphon
- All appropriate for their purpose, no redundancy

### B-06: Operator build macro is well-designed [Info]

The `add_vivid_operator()` function cleanly handles:
- Creating MODULE library targets
- Setting platform-specific plugin suffix
- Linking against `vivid_operator_api`
- Optional factory preset copying
- Optional extra library dependencies

### B-07: Platform support [Info]

- **macOS:** First-class support — native menu bar, Sparkle auto-update, Syphon I/O, Metal interop, app bundle packaging
- **Windows:** Builds via GLFW + WebGPU, oscpack win32 variant, minimal native integration
- **Linux:** Builds via GLFW + WebGPU, minimal but functional

### B-08: Include directories are clean [Info]

- `src/` as include root — all includes use relative paths from `src/`
- Operator API uses INTERFACE library for clean include isolation
- No overly broad include paths detected

---

## Prioritized Action Plan

1. **B-01** — Create shared `vivid_runtime` OBJECT library for tests (Critical, Medium effort, biggest build-time win)
2. **B-02** — Pin webgpu to specific commit (High, Trivial effort)
3. **B-03** — Enable compiler warnings (Medium, Trivial effort but may need warning triage)
4. **B-04** — Split CMakeLists.txt into modular files (Medium, Medium effort — partially addressed by B-01)
