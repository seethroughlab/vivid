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

**Status**: Complete. 0 moderate, 1 minor finding — fixed. 7 unchanged support files verified clean.

**Finding fixed**:
- m1: `node_graph_input.cpp` — Tab hit-test widths used `strlen * 8 + 16` approximation, diverging from draw code's `tr.text_width() + 16` at non-unit zoom. Added `pkg_browser_tab_widths_` cache populated during draw, with fallback to approximation before first frame.

**False positives dismissed**: Spread badge `snprintf` truncation (bounded, cosmetic), `domain_color()` null return (exhaustive switch with default), package browser entry bounds race (single-threaded by design, clamps on rebuild), `file_dialog.mm` null URL after `NSModalResponseOK` (Cocoa guarantees non-null), description `substr` mid-codepoint split (cosmetic, ASCII in practice), drag-and-drop graph loading (in `main.cpp`, audited in Phase 4).

---

### Phase 6: Operator Implementations (full audit)
**Scope**: ~12,500 LOC across 55 operators + 19 WGSL filters
**Depth**: Full audit — this phase was never completed in the previous audit

#### 6a: Control Operators (~3,400 LOC, 23 operators)
**Files**: `operators/control/*/`
**Focus**: Parameter validation, edge cases with empty/zero inputs, adherence to operator API contract, state management.

#### 6b: Audio Operators (~2,750 LOC, 17 operators + shared DSP headers)
**Files**: `operators/audio/*/`, `src/operator_api/audio_dsp.h`
**Focus**: **Real-time safety** (no allocations, no locks, no syscalls in process()), buffer handling, sample-accurate timing, edge cases (zero-length buffers, extreme parameter values).

#### 6c: GPU Operators (~5,760 LOC, 13 operators)
**Files**: `operators/gpu/*/`
Includes `text` operator (440 LOC), `movie_file_in`, `movie_file_audio_in`.
**Focus**: GPU buffer management, texture lifecycle, WebGPU API usage correctness, error handling on GPU failures.

#### 6d: WGSL Filters (~660 LOC, 19 filters)
**Files**: `filters/*.wgsl`
**Focus**: Shader correctness, out-of-bounds access, numerical stability, consistency with data-driven filter API.

**Status**: Complete. 2 moderate, 4 minor findings — all fixed. 20+ false positives dismissed.

**Findings fixed**:
- M1: `wavetable_synth.cpp` — `ensure_wavetable()` ran expensive trig computation (100K-400K sin/cos/tanh calls) on the audio thread when the wavetable param changed, causing audio dropout. Pre-computed all 6 wavetables in the constructor (~384KB total); `process()` now selects by index with zero generation cost.
- M2: `movie_file_audio_in.cpp` — TOCTOU race: main thread stored nullptr then immediately deleted the old extractor, while the audio thread could still hold the old pointer from a concurrent load. Added deferred deletion — old pointer is held for one frame before deletion, ensuring the audio thread has observed the new value.
- m1: `wavetable_synth.cpp` — Removed redundant `ensure_wavetable()` call (subsumed by M1 fix).
- m2: `mirror.wgsl` — Added `max(u.segments, 2.0)` guard before `TAU / segments` division (defense-in-depth against zero if param constraints are bypassed).
- m3: `halftone.wgsl` — Guarded `sqrt(1.0 - luma)` with `max(0.0)` to prevent NaN from HDR overbrights.
- m4: `chromatic_aberration.wgsl` — Replaced hardcoded `3.14159265` with `PI` constant for consistency; combined two redundant `textureSample` calls into one.

**False positives dismissed**: Control: FFT resize (control domain = main thread, allocation acceptable), PatTransform tmp[1024] (matches kMaxSpreadCapacity), NoteDuration enum bounds (framework clamps enum params), Clock BPM division (min 1.0), Euclidean seqs bounds (algorithm invariant), MidiInput param write-back (system-wide single-writer design), spread capacity=0 (runtime guarantees non-zero). Audio: distortion 1/tanh(drive) (drive min 1.0, tanh never zero), NaN/Inf propagation (standard float behavior), plexus synth precision (bounded params), drum SVF stability (guard handles all ranges), phase accumulation drift (params prevent phase_inc >= 1.0), delay/reverb lazy_init (one-shot allocation, acceptable), biquad denormals (Apple Silicon handles efficiently). GPU: texture_analysis output_values null (framework guarantees non-null), plexus output_spreads indexing (confirmed correct — indexes match port declaration order), bloom dimension division (output dimensions always > 0), text font path (design choice, not a bug), const_cast on VividProcessContext (intended API pattern for preferred_tex_width/height), movie_file_in detached thread (shared_ptr protects result struct). WGSL: all ClampToEdge out-of-bounds sampling (intentional behavior), transform scale division (graceful degradation via bounds check returning transparent black).

---

### Phase 7: Gap Sweep — Unaudited Production Code
**Scope**: ~4,360 LOC across three domains
**Depth**: Full audit — production code never explicitly assigned to prior phases

#### 7a: Runtime API & Command Infrastructure (~1,900 LOC)
**Files**: `runtime_api.cpp/h`, `runtime_command_sink.h`, `operator_info_cache.h`, `av_exporter.h`

#### 7b: Operator API Infrastructure Headers (~850 LOC)
**Files**: `wgsl_filter.h`, `child_op.h`, `data_driven_filter.h`

#### 7c: UI Support Files (~1,610 LOC)
**Files**: `renderer_2d.cpp/h`, `thumbnail_renderer.cpp/h`, `thumbnail_cache.cpp/h`, `graph_snapshot.h`, `inspector_layout.h`, `ui_command_sink.h`

**Status**: Complete. 3 high, 4 moderate findings — all fixed. 14+ low/info findings documented.

**Findings fixed**:
- H1: `runtime_command_sink.h` — Shell injection: `clone_cpp_operator` passed unquoted `new_stem` to `std::system()`. Added character validation (alphanumeric + underscore) and quoted the argument.
- H2: `runtime_api.cpp` — `reload()` collected saved params/locks/string-params before rebuild but never restored them after. Added restoration loop matching `apply_pending()`'s pattern.
- H3: `renderer_2d.h` — No destructor or copy/move guards; GPU resources leaked if `shutdown()` not called. Added `~Renderer2D() { shutdown(); }`, deleted copy/move, and added `shutdown()` call on partial init failure.
- M1: `runtime_api.cpp` — `prev_sm_state_` default-initialized to `0.0f`, so state-machine transitions to initial state 0 were never detected. Changed to sentinel value `-1.0f` via `emplace()`.
- M2: `runtime_api.cpp` — Crossfade finalization only updated `active_presets_` but didn't snap params to exact target values. Added explicit param assignment on crossfade completion.
- M3: `child_op.h` — Output spread overflow: if a child operator reported `length > capacity`, the host would `resize()` without data, creating a phantom buffer. Added clamp-and-warn after `process()`.
- M4: `thumbnail_cache.cpp/h` + `main.cpp` — No eviction for removed nodes, causing unbounded GPU memory growth. Added `remove()` and `retain_only()` methods, wired into `apply_pending()` to evict stale entries.

**False positives dismissed**: `reload()` degraded state on failure (callers display error, intentional), WGSL uniform layout fragility (correct today, comment-worthy not fix-worthy), hot-reload param count change (theoretical — params are fixed at construction), fallback texture size for RGBA32Float (format never used), `wgpuDeviceGetQueue` per-frame (borrowed ref semantics correct for wgpu-native), non-ASCII text handling (known limitation of minimal renderer), `localtime_r` POSIX-only (macOS-only target), inspector layout col_index validation (callers provide correct metadata), `DataDrivenFilter` `.back()` vs indexed access (equivalent with current reserve pattern), OperatorInfoCache staleness (invalidation correct at call sites), `AVExporter` dimension mismatch (callers always match), `apply_variation` generation bump on unchanged nodes (benign extra work), float `!=` for delta encoding (cosmetic precision, not safety), `active_preset()` static local ref (single-threaded context).

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
