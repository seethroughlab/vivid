# Phase 3: API Surface & Encapsulation

**Date:** 2026-04-03
**Status:** Complete

## Summary Table

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| A-01 | Info | Operator API | 22 headers, zero runtime dependencies — hermetically sealed | `src/operator_api/` |
| A-02 | Info | Cross-Module | Runtime internals not exposed to operators or test stubs | `tests/operators/`, `operators/` |
| A-03 | Info | Cross-Module | Runtime↔UI includes are intentional boundary crossings | Various |
| A-04 | Low | UI Encapsulation | `node_graph.h` has internal structs above `private:` boundary | `src/ui/graph/node_graph.h` |
| A-05 | Low | Runtime Encapsulation | `compiled_graph.h` exposes internal POD structs publicly | `src/runtime/graph/compiled_graph.h` |
| A-06 | Info | Header Implementation | `operator.h` VIVID_REGISTER macro (~200 lines) is justified | `src/operator_api/operator.h` |

## Severity Definitions

Same scale as Phase 1.

---

## Category A: Operator API

### A-01: Operator API is clean [Info]

The operator API (`src/operator_api/`, 22 headers) has **zero dependencies** on runtime or UI code. Operators are compiled as separate shared libraries and link only against `vivid_operator_api`. Verified:

- All 22 headers include only standard library headers and other operator_api headers
- No runtime types, no UI types, no build system leakage
- Test operator stubs in `tests/operators/` include only `operator_api/operator.h`
- Production operators in `operators/` include only operator_api headers

The API is well-documented through its header structure:
- **Core:** `types.h`, `operator.h`, `input_state.h`, `midi_types.h`, `create_request.h`
- **GPU:** `gpu_operator.h`, `gpu_common.h`, `gpu_types.h`, `texture_readback.h`
- **Filters:** `wgsl_filter.h`, `wgsl_preprocessor.h`, `data_driven_filter.h`
- **Composition:** `child_op.h`, `adsr.h`, `audio_dsp.h`
- **UI:** `draw_ui_helpers.h`, `draw_plot_helpers.h`, `adsr_inspector.h`, `thumbnail.h`
- **Types:** `port_type_registry.h`, `type_id.h`

---

## Category B: Cross-Module Visibility

### A-02: Runtime internals not exposed to operators [Info]

Verified that no operator code (neither built-in operators in `operators/` nor test stubs in `tests/operators/`) includes any runtime headers. The plugin boundary is clean.

### A-03: Runtime↔UI boundary crossings are intentional [Info]

Six runtime files include UI headers (documented in Phase 2, H-04). All are intentional:

| Crossing | Purpose |
|----------|---------|
| `runtime_command_sink.h` → `ui_command_sink.h` | Implements UI command interface (dependency inversion) |
| `graph_snapshot_builder.h` → `graph_snapshot.h` | Builds data-transfer snapshot for UI |
| `operator_info_cache.h` → `graph_snapshot.h` | Caches operator metadata in UI-consumable format |
| `ui_test_runner.h` → `graph_snapshot.h`, `file_dialog.h` | Test infrastructure |
| `graph_file_io.h` → `dialog_types.h` | Uses shared data structs (flagged in Phase 2 as fixable) |

These represent the approved boundary between runtime and UI. The data types at this boundary (`GraphSnapshot`, `UICommandSink`) are stable interfaces.

---

## Category C: Encapsulation

### A-04: `node_graph.h` internal structs above `private:` [Low]

**What:** `NodeGraphUI` (779 lines) defines several internal-use structs (`PortHit`, `SparklineData`, `ClipboardNode`, `PatchJack`, `PatchWire`, etc.) above the `private:` keyword, making them technically public.

**Why it matters:** Cosmetic issue. These structs are implementation details of the node graph UI and aren't used by any external code. Making them public allows accidental coupling.

**Recommendation:** Move these struct definitions below `private:` or into the `.cpp` files. No functional change.

**Effort:** Small

### A-05: `compiled_graph.h` exposes internal POD structs [Low]

**What:** `CompiledEdge`, `AudioNodeState`, `GpuNodeState`, and other structs in `compiled_graph.h` (385 lines) have all public members. These represent the compiled graph's internal representation.

**Why it matters:** This is by design — the frame executor and audio executor need direct, zero-overhead access to compiled graph data. Encapsulating behind getters would add overhead in the hot path. The tradeoff (performance over encapsulation) is correct for a real-time engine.

**Recommendation:** No change needed. Add a brief comment at the top of `compiled_graph.h` noting these are internal runtime types, not stable API.

**Effort:** Trivial

### A-06: `operator.h` VIVID_REGISTER macro is justified [Info]

The VIVID_REGISTER macro (lines 316-511, ~200 lines) generates extern "C" entry points required for the plugin dlopen architecture. It performs:
- Static parameter/port introspection via template metaprogramming
- Capability detection (GPU, Audio, Frame processable)
- Entry point generation for init, shutdown, tick, etc.

This must be header-only because:
1. It's a macro that generates code in the operator's translation unit
2. Template instantiation requires the full definition
3. Moving to a `.cpp` would require operators to link against a runtime library, breaking the plugin architecture

---

## Well-Organized Areas

- **`operator_api/`** — Exemplary API design. Clean separation, no leakage, well-documented through header structure.
- **`common/`** — Zero upward dependencies. Correctly serves as a leaf module.
- **`runtime_core.h`** — Clean facade over internal engine. Direct accessors to `CompiledGraph`, `AudioFrameBridge`, `FrameExecutor` are acceptable for internal use.
- **`runtime_api.h`** — Pure command/query interface with no data member exposure.
- **`audio_engine.h`** — Clean facade over audio execution.

---

## Action Items

No code changes required. Two cosmetic improvements deferred:
1. Move `node_graph.h` internal structs below `private:` (Low, Small effort)
2. Add "internal types" comment to `compiled_graph.h` (Low, Trivial)
