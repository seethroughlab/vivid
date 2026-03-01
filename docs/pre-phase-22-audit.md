# Pre-Phase 22 Codebase Audit Plan

## Context
Before moving to Phase 22 of the roadmap, we want to ensure the existing codebase (~33K LOC) is solid. The codebase is too large to audit in one pass, so we'll work through it in logical phases ordered by dependency depth (foundations first). Each phase is scoped to fit in a single conversation.

**Audit focus**: Both correctness/safety AND code quality/design.

## Audit Checklist (applied to every phase)

### Correctness & Safety
- Thread safety (especially anything reachable from the audio callback or GPU submission)
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

## Status

| Phase | Status |
|-------|--------|
| 1. Operator API & Common Utils | **Complete** |
| 2. Graph & Scheduler | **Complete** |
| 3. Audio Engine & GPU Context | **Complete** |
| 4. Plugin System | **Complete** |
| 5. Runtime API & Integration | **Complete** |
| 6. UI Subsystem | **Complete** |
| 7. Operator Implementations | Not started |

## Phases

### Phase 1: Operator API & Common Utils (~1,750 LOC)
**Files**: `src/operator_api/*.h`, `src/common/*.h`
**Why first**: These are the interfaces everything depends on. Issues here propagate everywhere.
**Focus**: API design clarity, type safety, documentation of contracts, const-correctness.

### Phase 2: Graph & Scheduler (~1,170 LOC)
**Files**: `src/runtime/graph.cpp/h`, `src/runtime/scheduler.cpp/h`, `src/common/topo_sort.h`
**Why second**: The execution backbone. All operator execution flows through here.
**Focus**: Serialization correctness, DAG ordering edge cases (cycles, disconnected subgraphs, spread wires), data structure choices.

### Phase 3: Audio Engine & GPU Context (~1,800 LOC)
**Files**: `src/runtime/audio_engine.cpp/h`, `src/runtime/gpu_context.cpp/h`, `src/runtime/macos_frame_timer.cpp/h`, `src/runtime/fullscreen_blit.cpp/h`
**Why third**: Real-time threads are the most dangerous code. Bugs here cause crashes, glitches, or hangs.
**Focus**: Lock-free patterns, allocation in audio callback, GPU synchronization, error recovery.

### Phase 4: Plugin System (~1,400 LOC)
**Files**: `src/runtime/operator_loader.cpp/h`, `src/runtime/operator_registry.cpp/h`, `src/runtime/operator_creator.cpp/h`, `src/runtime/hot_reload.cpp/h`, `src/runtime/file_watcher.cpp/h`, `src/runtime/wgsl_header_parser.cpp/h`, `src/runtime/builtin_operators.cpp/h`
**Why fourth**: Dynamic loading and hot-reload have many failure modes.
**Focus**: Error handling on load failure, resource leaks on unload, thread safety during hot-reload, version migration correctness.

### Phase 5: Runtime API & Integration (~2,500 LOC)
**Files**: `src/runtime/runtime_api.cpp/h`, `src/runtime/control_server.cpp/h`, `src/runtime/settings.cpp/h`, `src/runtime/capture_coordinator.cpp/h`, `src/runtime/av_exporter.mm`, `src/runtime/main.cpp`, `src/runtime/platform.cpp/h`, `src/runtime/screenshot.cpp`, `src/runtime/system_midi.cpp/h`, `src/runtime/editor_detect.cpp/h`, `src/runtime/crash_guard.h`
**Why fifth**: Glue code that ties subsystems together. State management and lifecycle.
**Focus**: API consistency, error propagation across subsystem boundaries, startup/shutdown ordering, state invariants.

### Phase 6: UI Subsystem (~10,400 LOC)
**Files**: `src/ui/node_graph.cpp/h`, `src/ui/node_graph_draw.cpp`, `src/ui/node_graph_input.cpp`, `src/ui/node_graph_util.h`, `src/ui/node_graph_constants.h`, `src/ui/renderer_2d.cpp/h`, `src/ui/ui_style.cpp/h`, `src/ui/thumbnail_renderer.cpp/h`, `src/ui/thumbnail_cache.cpp/h`, `src/ui/theme_loader.cpp/h`, `src/ui/inspector_layout.h`, `src/ui/graph_snapshot.h`, `src/ui/ui_command_sink.h`, `src/ui/file_dialog.h`
**Why sixth**: Largest subsystem but less safety-critical than runtime.
**Sub-phases** (if needed):
  - 6a: State management & input (`node_graph.cpp/h`, `node_graph_input.cpp`)
  - 6b: Rendering (`node_graph_draw.cpp`, `renderer_2d.cpp/h`)
  - 6c: Support systems (`thumbnail_*`, `ui_style`, `theme_loader`, `inspector_layout`)
**Focus**: State consistency, input handling edge cases, rendering correctness, GPU resource management in renderer.

### Phase 7: Operator Implementations (~13,400 LOC, 62 operators)
**Files**: `operators/control/*/`, `operators/audio/*/`, `operators/gpu/*/`, `filters/*.wgsl`
**Why last**: Follow common patterns, so audit gets faster. Issues found in API (Phase 1) inform what to look for here.
**Sub-phases**:
  - 7a: Control operators (22 operators, ~3,800 LOC)
  - 7b: Audio operators (28 operators, ~4,274 LOC) — extra scrutiny on real-time safety
  - 7c: GPU operators (12 operators, ~5,312 LOC)
  - 7d: WGSL filters (17 shaders)
**Focus**: Adherence to operator API contract, parameter validation, real-time safety in audio ops, GPU buffer management in GPU ops.

## Process for Each Phase
1. Read all files in scope
2. Produce a findings report organized by severity (critical / moderate / minor / style)
3. Discuss findings and agree on which to fix
4. Implement fixes
5. Run relevant tests to verify nothing broke
6. Move to next phase

## Verification
- `ctest` after each phase to catch regressions
- `vivid build` batch compile of operators after Phase 1 (API changes) and Phase 7 (operator changes)
- Manual smoke test with a demo graph after Phases 3 and 5 (runtime changes)
