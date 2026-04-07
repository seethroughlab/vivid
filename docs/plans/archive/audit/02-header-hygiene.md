# Phase 2: Header Hygiene

**Date:** 2026-04-03
**Status:** Complete

## Summary Table

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| H-01 | Info | Circular Includes | No circular include chains detected | All headers |
| H-02 | Info | Dependency Direction | `operator_api/` and `common/` have zero violations | `src/operator_api/`, `src/common/` |
| H-03 | Medium | Dependency Direction | `graph_file_io.h` includes `ui/graph/dialog_types.h` | `src/runtime/control/` |
| H-04 | Info | Dependency Direction | 4 runtime→UI includes are intentional boundary crossings | Various |
| H-05 | Medium | Heavy Header | `runtime_command_sink.h` has 25 direct includes | `src/runtime/control/` |
| H-06 | Low | Heavy Header | `control_server_internal.h` has 43 includes (internal only) | `src/runtime/control/` |
| H-07 | Medium | JSON in Headers | 3 public headers include full `nlohmann/json.hpp` | `graph.h`, `main_helpers.h`, `operator_source_docs.h` |
| H-08 | Low | Forward Declarations | 8 headers could use forward declarations instead of full includes | Various |

## Severity Definitions

Same scale as Phase 1.

---

## Category A: Circular Includes

### H-01: No circular include chains [Info]

All 112 headers were checked for A→B→A and longer cycles. No circular dependencies found. The codebase properly uses forward declarations and careful include ordering to avoid cycles.

---

## Category B: Dependency Direction

### H-02: `operator_api/` and `common/` are clean [Info]

These foundational modules have zero upward dependencies:
- `operator_api/` includes only standard library headers and its own headers
- `common/` includes only standard library headers

This is correct — these are leaf modules in the dependency graph.

### H-03: `graph_file_io.h` includes `ui/graph/dialog_types.h` [Medium]

**What:** `src/runtime/control/graph_file_io.h` includes `ui/graph/dialog_types.h` to use `ExampleEntry` and `GraphMetaEditData`. This creates a runtime→UI dependency for pure data structs.

**Why it matters:** The runtime control layer shouldn't depend on UI types. `ExampleEntry` and `GraphMetaEditData` are plain data structs with no UI logic — they belong in a shared location.

**Recommendation:** Move `dialog_types.h` (or just its data structs) to `src/common/` or `src/runtime/graph/`, since these types describe graph metadata rather than UI state.

**Effort:** Small

### H-04: 4 intentional runtime→UI boundary crossings [Info]

The following runtime files include UI headers by design:

| File | UI Header | Reason |
|------|-----------|--------|
| `runtime_command_sink.h` | `ui/ui_command_sink.h` | Implements the UI command interface |
| `graph_snapshot_builder.h` | `ui/graph/graph_snapshot.h` | Builds snapshots for UI consumption |
| `operator_info_cache.h` | `ui/graph/graph_snapshot.h` | Caches operator info in UI format |
| `ui_test_runner.h` | `graph_snapshot.h`, `file_dialog.h` | Test infrastructure needs UI types |

These are acceptable. `graph_snapshot.h` is a data-transfer type at the runtime↔UI boundary. `UICommandSink` is the interface that `RuntimeCommandSink` implements (dependency inversion). `UITestRunner` is test infrastructure that inherently interacts with UI.

---

## Category C: Heavy Headers

### H-05: `runtime_command_sink.h` — 25 includes [Medium]

**What:** Despite moving large method bodies to `.cpp` in Wave 1, the header still includes 25 headers because inline setter methods and member pointer types require the full type definitions.

**Why it matters:** Any file that includes `runtime_command_sink.h` transitively pulls in 25+ headers. This slows incremental builds.

**Recommendation:** Replace includes with forward declarations for types used only as pointer members. The setters that take pointer params (`set_registry(OperatorRegistry*)`, `set_graph(Graph*)`, etc.) can use forward-declared types if the setter bodies are moved to the `.cpp` file.

**Effort:** Medium

### H-06: `control_server_internal.h` — 43 includes [Low]

**What:** The internal implementation header for the control server includes all runtime headers plus external deps (nlohmann/json, IXWebSocket).

**Why it matters:** Less concerning because only 3 `.cpp` files include it (control_server.cpp, control_server_query.cpp, control_server_dispatch.cpp). The blast radius is small.

**Recommendation:** Monitor but no immediate action needed. If compilation time becomes an issue, consider splitting into query-specific and dispatch-specific internal headers.

---

## Category D: JSON in Headers

### H-07: 3 headers include full `nlohmann/json.hpp` [Medium]

| Header | Usage | Fix |
|--------|-------|-----|
| `graph.h` | `parse_doc(const nlohmann::json&)` — private method | Use `json_fwd.hpp`, move full include to `.cpp` |
| `main_helpers.h` | `json_str_array(const nlohmann::json&)` — function signature | Use `json_fwd.hpp`, full include already in `.cpp` |
| `operator_source_docs.h` | JSON document storage | Use `json_fwd.hpp`, move full include to `.cpp` |

`nlohmann/json.hpp` is ~25k lines. Using `json_fwd.hpp` (~100 lines) in headers and the full include only in `.cpp` files reduces transitive include weight significantly.

**Effort:** Small

---

## Category E: Forward Declaration Opportunities

### H-08: 8 headers could use forward declarations [Low]

These headers include other headers but only use pointer/reference types from them:

| Header | Could forward-declare | Currently includes |
|--------|----------------------|-------------------|
| `runtime_command_sink.h` | `Graph`, `OperatorRegistry`, `Settings`, `HotReloader`, `PackageManager`, `BuildConsole` | Full headers for all |
| `operator_info_cache.h` | `OperatorLoader`, `OperatorRegistry` | Full headers |
| `package_compiler.h` | `BuildConsole` | Full header |
| `package_manager.h` | `PackageCompiler` | Full header |

**Effort:** Small per file, but each forward-declaration change requires verifying that no `.cpp` file relies on the transitive include.

---

## Prioritized Action Plan

### Immediate (applied with this audit)

1. **H-07** — Replace `nlohmann/json.hpp` with `json_fwd.hpp` in 3 headers
2. **H-03** — Move `dialog_types.h` data structs to shared location

### Deferred

3. **H-05** — Reduce `runtime_command_sink.h` includes via forward declarations (requires moving inline setters to .cpp)
4. **H-08** — Forward declarations in other headers (opportunistic)
5. **H-06** — Monitor `control_server_internal.h` weight

---

## Well-Organized Areas [Info]

- **No circular dependencies** — clean include graph throughout
- **`operator_api/`** — zero upward dependencies, correct as leaf module
- **`common/`** — zero upward dependencies, correct as shared utility layer
- **`control_server_checks.h`** — correctly uses `json_fwd.hpp` (good pattern to follow)
