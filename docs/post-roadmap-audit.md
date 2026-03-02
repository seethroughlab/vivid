# Post-Roadmap Codebase Audit Plan

## Context

The ROADMAP (Phases 1–30) is complete. The codebase has grown from ~33K to ~51K LOC across 25 commits since the last audit (`bf15fac`). That audit (pre-phase-22) covered Phases 1–6 thoroughly; Phase 7 (operator implementations) was never started. Since then, significant new systems were added (package management, export pipeline, macOS menu) and several existing areas expanded substantially (main.cpp doubled, control_server grew 40%, UI gained interactive input + package browser).

**Goal**: Structured, dependency-ordered audit of the full codebase — thorough on new/changed code, verification pass on previously-audited unchanged code.

## Audit Checklist (same as pre-phase-22, applied to every phase)

### Correctness & Safety
- Thread safety (especially anything reachable from audio callback or GPU submission)
- Resource cleanup (GPU buffers, file handles, dynamic libraries, allocations)
- Error handling (graceful failure vs crash, error propagation)
- Edge cases (empty/null inputs, zero-length buffers, missing files, disconnected graphs)
- Undefined behavior (dangling pointers, use-after-free, signed overflow, uninitialized reads)
- Data races and lock ordering

### Code Quality & Design
- API consistency across similar interfaces
- Abstraction quality — is complexity justified? Any unnecessary indirection?
- Naming clarity and consistency
- Dead code or unused parameters
- Duplication that should be factored out
- Separation of concerns — is logic in the right place?
- Const-correctness and value semantics

---

## Phases

### Phase 1: Foundations & Core Engine (delta)
**Scope**: ~5K LOC scan, ~200 LOC new
**Depth**: Verification pass — previously audited, minimal changes
**Files**:
- `src/operator_api/*.h` — types.h +33 LOC (new type additions), input_state.h +6, audio_dsp.h moved here
- `src/common/*.h` — topo_sort.h, string_util.h, gpu_util.h, system_info.h
- `src/runtime/graph.cpp/h` — +56 LOC (load_from_string, new methods)
- `src/runtime/scheduler.cpp/h` — +5 LOC (minor)
- `src/runtime/audio_engine.cpp/h` — unchanged
- `src/runtime/gpu_context.cpp/h` — unchanged
- `src/runtime/macos_frame_timer.cpp/h`, `fullscreen_blit.cpp/h` — unchanged

**Focus**: Verify new type additions are consistent, graph load_from_string is safe (parses untrusted JSON), no regressions in core engine.

---

### Phase 2: New Infrastructure (full audit)
**Scope**: ~2,850 LOC new code + ~1,280 LOC tests
**Depth**: Full audit — all new code

#### 2a: Package System (~1,460 LOC + 1,280 LOC tests)
**Files**:
- `src/runtime/package_manager.cpp/h` (689 + 90 = 779 LOC) — dependency resolution, install/uninstall
- `src/runtime/package_catalog.cpp/h` (300 + 60 = 360 LOC) — remote fetch, caching
- `src/runtime/package_compiler.cpp/h` (274 + 61 = 335 LOC) — cmake builds, vendor includes
- `src/runtime/package_test_runner.cpp/h` (198 + 33 = 231 LOC) — graph & C++ test execution
- `tests/test_package_manager.cpp` (550 LOC)
- `tests/test_package_catalog.cpp` (316 LOC)
- `tests/test_package_compiler.cpp` (210 LOC)
- `tests/test_package_test_runner.cpp` (203 LOC)

**Focus**: File system operations (path traversal, permission errors), cmake generation correctness, catalog HTTP fetching (timeouts, malformed responses), dependency resolution (cycles, missing deps), subprocess management in test runner.

#### 2b: Export Pipeline (~1,150 LOC)
**Files**:
- `src/export/export_pipeline.cpp/h` (569 + 68 = 637 LOC)
- `src/export/standalone_main.cpp` (356 LOC)
- `src/export/standalone.cmake.in` (158 LOC)

**Focus**: Build system generation safety, file I/O error handling, standalone app lifecycle, resource bundling correctness.

---

### Phase 3: Plugin System & Hot Reload (delta)
**Scope**: ~1,500 LOC scan, ~120 LOC new
**Depth**: Delta audit — previously audited, moderate changes
**Files**:
- `src/runtime/operator_registry.cpp/h` — +47 LOC (new registration capabilities)
- `src/runtime/operator_creator.cpp` — +9 LOC
- `src/runtime/operator_loader.cpp/h` — verify unchanged
- `src/runtime/file_watcher.cpp/h` — +44 LOC (new pattern watching)
- `src/runtime/hot_reload.cpp/h` — +14 LOC
- `src/runtime/wgsl_header_parser.cpp/h` — verify unchanged
- `src/runtime/builtin_operators.cpp/h` — verify unchanged

**Focus**: Thread safety of new file_watcher patterns, registry consistency after changes, hot_reload robustness.

---

### Phase 4: Runtime Integration (focused audit)
**Scope**: ~3,400 LOC, mix of heavily expanded + new
**Depth**: Full audit of new/changed sections, verification of unchanged
**Files**:
- `src/runtime/main.cpp` (1,755 LOC — doubled since last audit, +594 LOC)
- `src/runtime/control_server.cpp/h` (1,399 + headers — +379 LOC, package endpoints)
- `src/runtime/macos_menu.h/mm` (248 LOC — entirely new)
- `src/runtime/settings.cpp/h`, `platform.cpp/h`, `crash_guard.h`, `editor_detect.cpp/h` — verify unchanged
- `src/runtime/capture_coordinator.cpp/h`, `av_exporter.mm`, `screenshot.cpp`, `system_midi.cpp/h` — verify unchanged
- `platform/macos/Info.plist.in` (new)

**Focus**: main.cpp is 1,755 LOC — is it doing too much? Separation of concerns. Control server input validation on new endpoints. macOS menu lifecycle and thread safety. Startup/shutdown ordering with new subsystems.

**Status**: Complete. 2 moderate, 2 minor findings — all fixed. 11 unchanged utility files verified clean.

**Findings fixed**:
- M1: `control_server.cpp` — `impl_->running` was a plain `bool` read from HTTP threads, written from main. Changed to `std::atomic<bool>`.
- M2: `control_server.cpp` — `directory_iterator` in `list_package_examples` could throw on inaccessible dir. Switched to `std::error_code` overloads.
- m1: `control_server.cpp` — Added `is_safe_package_name()` traversal check on `name` in 3 package doc endpoints (defense-in-depth alongside `is_installed()` gate).
- m2: `macos_menu.h` — Documented main-thread threading contract for `MenuCallbacks`.

**False positives dismissed**: Control server start failure path (safe null check), raw pointer UAF (stack lifetimes outlive server), path traversal via symlinks (outside threat model), shell injection via target_name (validated by `is_valid_identifier()`), SSRF via install URL (localhost-only, PM's responsibility), rebuild timeout (acceptable).

---

### Phase 5: UI Subsystem (delta)
**Scope**: ~11K LOC total, ~600 LOC new
**Depth**: Delta audit focused on new features
**Files**:
- `src/ui/node_graph_draw.cpp` — +265 LOC (theme-driven wire styling, package browser rendering)
- `src/ui/node_graph_input.cpp` — +220 LOC (interactive input, drag-and-drop graph loading)
- `src/ui/node_graph.cpp/h` — +71/+36 LOC (action methods, state)
- `src/ui/node_graph_constants.h` — +15 LOC
- `src/ui/file_dialog.h/mm` — +16 LOC (save dialog)
- `src/ui/theme_loader.cpp` — +9 LOC
- `src/ui/ui_style.h` — +3 LOC
- Remaining UI files — verify unchanged

**Focus**: Input handling edge cases (drag-and-drop validation, file path safety), state consistency with new action methods, rendering correctness of new wire styles, package browser UI state management.

---

### Phase 6: Operator Implementations (full audit — never done)
**Scope**: ~13,500 LOC across 45+ operators + 17 WGSL filters
**Depth**: Full audit — this phase was never completed in the previous audit

#### 6a: Control Operators (~3,800 LOC, ~22 operators)
**Files**: `operators/control/*/`
**Focus**: Parameter validation, edge cases with empty/zero inputs, adherence to operator API contract, state management.

#### 6b: Audio Operators (~3,200 LOC, ~20 operators)
**Files**: `operators/audio/*/`
**Focus**: **Real-time safety** (no allocations, no locks, no syscalls in process()), buffer handling, sample-accurate timing, edge cases (zero-length buffers, extreme parameter values).

#### 6c: GPU Operators (~6,500 LOC, ~13 operators)
**Files**: `operators/gpu/*/`
Includes new `text` operator (439 LOC), modified `movie_file_in` and `movie_file_audio_in`.
**Focus**: GPU buffer management, texture lifecycle, WebGPU API usage correctness, error handling on GPU failures.

#### 6d: WGSL Filters (~660 LOC, ~17 filters)
**Files**: `filters/*.wgsl`
**Focus**: Shader correctness, out-of-bounds access, numerical stability, consistency with data-driven filter API.

---

## Process for Each Phase
1. Read all files in scope
2. Produce a findings report organized by severity (critical / moderate / minor / style)
3. Discuss findings and agree on which to fix
4. Implement fixes
5. Run relevant tests to verify nothing broke
6. Move to next phase

## Verification
- `ctest` after each phase to catch regressions
- `vivid build` batch compile of operators after Phase 1 (API changes) and Phase 6 (operator changes)
- Manual smoke test with a demo graph after Phases 4 and 5 (runtime/UI changes)
