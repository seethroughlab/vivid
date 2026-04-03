# Phase 7: Test Coverage & Quality

**Date:** 2026-04-03
**Status:** Complete

## Summary Table

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| T-01 | High | Test Isolation | 20+ tests use hardcoded `/tmp/` paths — blocks parallel execution | Various |
| T-02 | High | Test Isolation | test_control_server uses fixed port 19876 | `tests/control/` |
| T-03 | Medium | Coverage Gap | Runtime coverage ~63%; UI coverage ~29% | Various |
| T-04 | Medium | Oversized Tests | 3 test files over 1,400 lines | `tests/control/`, `tests/graph/`, `tests/ui/` |
| T-05 | Medium | Framework | Only check()/check_float() — no fixtures, teardown, or RAII cleanup | `tests/test_helpers.h` |
| T-06 | Low | Cleanup | Inconsistent temp file cleanup across tests | Various |
| T-07 | Info | Dependencies | 23+ tests depend on pre-built operator plugins | `CMakeLists.txt` |
| T-08 | Info | Coverage | Export pipeline and platform code have limited but acceptable coverage | `src/export/`, `src/runtime/platform/` |

## Severity Definitions

Same scale as Phase 1.

---

## Findings

### T-01: Hardcoded `/tmp/` paths block parallel execution [High]

**What:** 20+ test files write to fixed paths like `/tmp/vivid_test_valid.json`, `/tmp/vivid_team_regression_a`, etc. When tests run in parallel (`ctest -j4`) or when multiple developers run tests simultaneously, these paths collide.

**Examples:**
- `test_graph.cpp` → `/tmp/vivid_test_*.json`
- `test_package_manager.cpp` → `/tmp/vivid_pkg_test_*`
- `test_subgraph_module.cpp` → `/tmp/vivid_subgraph_*` (no cleanup)

**Recommendation:** Create a `ScopedTempDir` RAII helper in `test_helpers.h` that creates a unique directory per test run (e.g., `/tmp/vivid_test_<pid>_<timestamp>/`) and removes it on destruction. Each test uses the scoped dir instead of hardcoded paths.

**Effort:** Medium

### T-02: Fixed port in test_control_server [High]

**What:** `test_control_server.cpp` starts an HTTP server on port 19876. Two concurrent runs fail with "Address already in use."

**Recommendation:** Use port 0 (OS-assigned) or implement a `get_available_port()` helper that finds a free port.

**Effort:** Small

### T-03: Coverage gaps in runtime and UI [Medium]

**Runtime coverage (32/51 files, ~63%):**

| Module | Tested | Untested |
|--------|--------|----------|
| `graph/` | graph, graph_compiler, frame_executor, subgraph_module, port_type_registry, graph_snapshot_builder | audio_executor |
| `operators/` | operator_loader, operator_creator, operator_source_docs, operator_info_cache, builtin_operators | operator_registry (indirectly tested) |
| `audio/` | audio_engine, audio_frame_bridge | system_midi |
| `control/` | control_server (HTTP-level), runtime_api | control_server_checks, control_server_dispatch, control_server_query, graph_file_io (unit level) |
| `packages/` | All 5 files tested | — |
| `core/` | settings, file_watcher, hot_reload, undo_manager, file_drop_registry, editor_detect, tool_discovery, build_console, app_update_manager, capture_coordinator | window_manager, workspace_manager |
| `gpu/` | wgsl_header_parser | gpu_context, fullscreen_blit |
| `platform/` | app_update_manager | platform, macos_frame_timer, macos_menu |
| `debug/` | output_analyzer, capture_coordinator | — |

**UI coverage (6/21 files, ~29%):**
- Tested: inspector_layout, overlay_layouts, theme_loader, i18n, text_edit (unit tests) + screenshot smoke tests
- Untested: node_graph logic, dialog_manager, renderer_2d (unit level)

**Recommendation:** Prioritize unit tests for: `control_server_dispatch.cpp` (query validation), `audio_executor.cpp` (buffer processing), `system_midi.cpp` (MIDI message handling).

### T-04: Oversized test files [Medium]

| File | Lines | Issue |
|------|-------|-------|
| `test_control_server.cpp` | 2,531 | Tests HTTP API, mutations, packages, introspection — should split by category |
| `test_graph.cpp` | 1,748 | Graph serialization, connections, variations — could split by topic |
| `test_ui_screenshot_smoke.cpp` | 1,457 | Test setup mixed with visual assertions |

**Recommendation:** Split `test_control_server.cpp` into 3-4 focused files (queries, mutations, packages, introspection). Lower priority than isolation fixes.

### T-05: Test framework is minimal [Medium]

**What:** `test_helpers.h` provides only `check()` and `check_float()`. Missing:
- Fixtures / setup-teardown patterns
- String/container assertions
- Skip/xfail mechanisms
- Test discovery (each test is a standalone executable with its own `main()`)
- RAII cleanup helpers

**Why it matters:** Tests manually manage setup/teardown, leading to inconsistent cleanup (T-06) and verbose test code.

**Recommendation:** Extend `test_helpers.h` incrementally:
1. Add `ScopedTempDir` RAII helper (addresses T-01)
2. Add `check_string(actual, expected, msg)` convenience
3. Add `check_gt`, `check_le` for range checks
4. Consider Catch2 migration as a long-term option (major effort, breaks all 108 tests)

### T-06: Inconsistent temp file cleanup [Low]

**What:** Some tests clean up with `std::filesystem::remove_all()`, others use `std::remove()` (files only, not directories), and some don't clean up at all (e.g., `test_subgraph_module.cpp`).

**Recommendation:** Addressed by T-01's `ScopedTempDir` — RAII cleanup eliminates manual cleanup entirely.

### T-07: Tests depend on pre-built operator plugins [Info]

**What:** 23+ test targets have `add_dependencies(test_* operator_plugin)` ensuring operator `.dylib` files are built before the test runs. This is correct behavior — tests need operators to load and test.

**Observation:** If an operator fails to compile, dependent tests fail with opaque "operator not found" errors rather than a clear "dependency build failed" message. This is a CMake limitation, not a code issue.

### T-08: Export and platform coverage is limited but acceptable [Info]

- `test_export_pipeline.cpp` tests the export pipeline but not `standalone_main.cpp` (the exported binary's entry point)
- Platform code (`platform.cpp`, `macos_frame_timer.cpp`) is thin and primarily tested through integration

---

## Prioritized Action Plan

### Immediate
1. **T-01** — `ScopedTempDir` RAII helper + migrate tests off hardcoded paths (High, Medium effort)
2. **T-02** — Dynamic port in test_control_server (High, Small effort)

### Near-term
3. **T-05** — Extend test_helpers.h with additional assertion types (Medium, Small effort)
4. **T-04** — Split test_control_server.cpp (Medium, Medium effort)

### Long-term
5. **T-03** — Add unit tests for uncovered subsystems (Medium, Large effort)
6. **T-05** — Consider Catch2 migration (Medium, Large effort)
