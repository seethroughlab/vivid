# Plan: Production Gate — Phase 3 (`RuntimeHealthSnapshot` aggregation)

## Context

Phases 1 and 2 gave the build a single command and a machine-readable report. Phase 3 lays the groundwork for the *runtime* side of the same question — "is this graph safe to run right now?" — by introducing `RuntimeHealthSnapshot`: an in-process struct that aggregates signals already tracked across the runtime, plus a shared JSON serializer that the existing `run_diagnostics` endpoint and the future `get_runtime_health` endpoint (Phase 4) will both use.

This phase adds zero new probes and zero client surface. Its only outward effect is that `run_diagnostics`'s `health` block is built from a typed snapshot instead of inline JSON construction. Same shape, same fields, same tests.

Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`. Phase 1 + 2 changes remain uncommitted alongside Phase 3.

## What changed vs. the parent plan's Phase 3 sketch

The parent plan (`docs/plans/production-gate-and-health-plan.md`) and Phase 2 plan both reference Phase 3. Exploration found three things that materially shrink the scope:

1. **No `SafeMode` exists.** The original `git status` showed an uncommitted `src/runtime/core/safe_mode.h` on master, but those edits were never committed and aren't on the branch we fast-forwarded onto. Only `CrashRecoveryManager` exists (`src/runtime/core/crash_recovery.h`). SafeMode is dropped from Phase 3 entirely.
2. **The existing `health` block is rich and tested.** `handle_run_diagnostics` (`src/runtime/control/control_server_checks.cpp:677-889`) emits `health.audio` (with `top_nodes` and `top_lane_state_nodes` arrays), `health.graph`, and `health.gpu`. Tests at `tests/control/test_control_server_client_perception.inc:109-179` assert the shape. The new serializer must be a strict superset.
3. **Hot reload state has no in-process cache.** `HotReloader::poll_ready()` returns a deque of results that `main.cpp` consumes immediately; nothing keeps "last ReloadResult." Adding that cache is its own work — defer past Phase 3.

The result: Phase 3 becomes a clean POD-and-helper add plus a behavior-preserving refactor of the existing health builder. Smaller, lower risk, lands on the same gate.

## Decisions locked in (with rationale)

- **No new state in `RuntimeCore`.** `runtime_health::collect()` takes the same set of references that `handle_run_diagnostics` takes today (`Graph&`, `RuntimeCore&`, `OperatorRegistry&`, `AudioEngine*`, `GpuContext*`). Matches existing pattern; avoids invasive plumbing into RuntimeCore.
- **`runtime_health::to_json()` returns the existing `health` JSON shape verbatim, plus three new top-level keys.** The new keys (`severity`, `findings`, `version`) are additions, not replacements — back-compat preserved.
- **GpuContext is added as a parameter to `handle_run_diagnostics`.** Today the GPU portion of the existing health block uses only counts derivable from CompiledGraph (`gpu.shader_errors`, `gpu.texture_nodes`). To roll severity for `device_lost`, we need GpuContext access. ControlServer already holds a GpuContext reference (it's created in main.cpp and passed in); threading it to `handle_run_diagnostics` is one extra parameter.
- **Two tiny new accessors:** `AudioFrameBridge::lane_overflow_count()` and `GpuContext::last_error()` (both currently private members). One-line getters each. Justified because severity rollup and the to-be-added `findings` need them; both are read-only and atomic-or-relaxed already.
- **Severity rollup is conservative.** `Fatal` only when the runtime cannot meaningfully execute the current graph (`gpu.device_lost`); `Error` when the graph has compile failures or missing required operators; `Warning` for runtime degradation (underruns, dropped connections, lane overflow); `Ok` otherwise. These rules are pure functions of the snapshot — no time-windowed thresholds yet.
- **Test goes in `tests/control/`, not `tests/runtime/` (which doesn't exist).** Mirrors the convention of `tests/control/test_runtime_core.cpp` — uses `vivid::Graph` + `RuntimeCore::build` + `tick`, asserts via the rolled-our-own `check()` macro from `tests/test_helpers.h`.

## Scope

In scope:
- New `src/runtime/core/runtime_health.h` (struct + free function declarations).
- New `src/runtime/core/runtime_health.cpp` (`collect()`, `to_json()`, severity rollup).
- One-line getter additions: `AudioFrameBridge::lane_overflow_count()` (audio_frame_bridge.h:~85) and `GpuContext::last_error()` (gpu_context.h:~60).
- Refactor `handle_run_diagnostics` (`control_server_checks.cpp:677-889`) to construct `RuntimeHealthSnapshot` then serialize via `runtime_health::to_json()` for the `health` block. Result: identical wire shape, shared code.
- Thread `GpuContext*` through to `handle_run_diagnostics` (touches dispatch.cpp signature).
- New test `tests/control/test_runtime_health_snapshot.cpp` registered in `cmake/tests/10-runtime-control-graph.cmake` with the existing `tests/control/test_runtime_core.cpp`-style pattern.
- Run existing `test_control_server_client_perception` (or its containing test) to confirm the existing `health` shape still passes.

Out of scope (later phases):
- `get_runtime_health` endpoint, MCP tool, UI pill (Phase 4).
- Health budget evaluation in `production_gate_report.py` (Phase 5).
- SafeMode-derived findings (no SafeMode yet).
- ReloadResult caching (needs new `RuntimeCore` state).
- Sustained-silence / sustained-black output probes (no existing probe; `OutputAnalyzer` functions are stateless utilities, not continuous monitors).
- Crash-recovery `recovered_from_crash` finding (deferred until SafeMode lands).

## Files

New:
- `src/runtime/core/runtime_health.h` (~80 lines including doc comments)
- `src/runtime/core/runtime_health.cpp` (~250 lines)
- `tests/control/test_runtime_health_snapshot.cpp` (~200 lines)

Modified:
- `src/runtime/audio/audio_frame_bridge.h` — add `uint32_t lane_overflow_count() const { return lane_overflow_count_; }`
- `src/runtime/gpu/gpu_context.h` — add `const std::string& last_error() const { return last_error_; }`
- `src/runtime/control/control_server_checks.h` — add `GpuContext*` to `handle_run_diagnostics` signature
- `src/runtime/control/control_server_checks.cpp` — refactor health-block builder out, replace inline construction with `runtime_health::to_json()` call
- `src/runtime/control/control_server_dispatch.cpp` — pass GpuContext to the new signature (the ControlServer already holds it)
- `src/runtime/CMakeLists.txt` (or wherever runtime sources are listed) — add the new .cpp to the runtime library
- `cmake/tests/10-runtime-control-graph.cmake` — register `test_runtime_health_snapshot`

## Struct design

```cpp
// src/runtime/core/runtime_health.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace vivid { class Graph; class RuntimeCore; class OperatorRegistry;
                  class AudioEngine; class GpuContext; }

namespace vivid::runtime_health {

enum class Severity { Ok, Warning, Error, Fatal };

struct Finding {
    std::string code;        // stable identifier, e.g. "missing_operator"
    Severity severity;
    std::string subject;     // node id, operator type, or "" for system-wide
    std::string message;     // human description
};

struct AudioNodeHealth {
    std::string node_id;
    std::string type;
    int64_t last_block_total_us = 0;
    int64_t last_process_us = 0;
    int64_t ema_block_us = 0;
    float   last_block_budget_pct = 0.f;
    int64_t last_lane_count = 0;
    int64_t lane_state_entries = 0;
};

struct AudioHealth {
    bool running = false;
    int  sample_rate = 0;
    int  buffer_size = 0;
    int  node_count = 0;
    uint32_t xruns = 0;
    bool last_buffer_underrun = false;
    float load = 0.f;
    uint32_t lane_overflow_count = 0;          // NEW
    std::vector<AudioNodeHealth> top_nodes;
    std::vector<AudioNodeHealth> top_lane_state_nodes;
};

struct GraphHealth {
    int64_t declared_nodes = 0;
    int64_t declared_connections = 0;
    int64_t compiled_nodes = 0;
    int64_t frame_nodes = 0;
    int64_t audio_nodes = 0;
    int64_t total_edges = 0;
    int64_t frame_edges = 0;
    int64_t audio_edges = 0;
    int64_t snapshot_edges = 0;
    int64_t dropped_connections = 0;
    int64_t errored_nodes = 0;
    int64_t missing_operators = 0;
    std::vector<std::string> missing_operator_types;  // NEW
};

struct GpuHealth {
    int64_t texture_nodes = 0;
    int64_t shader_errors = 0;
    bool device_lost = false;        // NEW
    std::string last_error;          // NEW
};

struct RuntimeHealthSnapshot {
    Severity overall = Severity::Ok;
    std::vector<Finding> findings;
    AudioHealth audio;
    GraphHealth graph;
    GpuHealth   gpu;
    std::string vivid_version;       // best-effort; "" if unknown
};

// Aggregator: pure function of the runtime references it receives.
RuntimeHealthSnapshot collect(const Graph&,
                              const RuntimeCore&,
                              const OperatorRegistry&,
                              const AudioEngine*,
                              const GpuContext*);

// Serializer: emits the JSON shape consumed by run_diagnostics today
// (strict superset). Phase 4 reuses this for get_runtime_health.
nlohmann::json to_json(const RuntimeHealthSnapshot&);

}  // namespace vivid::runtime_health
```

## Severity rollup rules (pure function of snapshot)

Applied in order; first match wins for `overall`. Findings accumulate (one per condition):

| Severity | Condition | Finding code |
|---|---|---|
| `Fatal`   | `gpu.device_lost` | `gpu_device_lost` |
| `Error`   | `graph.compiled_nodes < graph.declared_nodes` AND `graph.missing_operators > 0` | `missing_required_operators` |
| `Error`   | `graph.errored_nodes > 0` | `node_runtime_error` |
| `Error`   | `audio.running == false` AND `graph.audio_nodes > 0` | `audio_not_running` |
| `Warning` | `graph.dropped_connections > 0` | `dropped_connections` |
| `Warning` | `audio.xruns > 0` (any underrun this session) | `audio_underruns` |
| `Warning` | `audio.lane_overflow_count > 0` | `lane_overflow` |
| `Warning` | `gpu.shader_errors > 0` | `shader_errors` |

Each `Finding` carries a stable `code` so consumers (Phase 4 UI pill, Phase 5 budget evaluation) can switch on it without parsing prose.

## JSON shape (back-compat preserved)

`to_json()` produces a top-level object that is **a strict superset** of today's `health` block:

```json
{
  "audio": { /* unchanged + new "lane_overflow_count" */ },
  "graph": { /* unchanged + new "missing_operator_types": [...] */ },
  "gpu":   { /* unchanged + new "device_lost", "last_error" */ },

  "severity": "ok",                  // NEW: "ok" | "warning" | "error" | "fatal"
  "findings": [                      // NEW: empty when severity == "ok"
    {"code": "...", "severity": "...", "subject": "...", "message": "..."}
  ],
  "vivid_version": ""                // NEW: best-effort
}
```

`handle_run_diagnostics` calls `runtime_health::to_json(snapshot)` for the `health` slot, then merges its `findings`/`hints` arrays into the existing `result` object as today (those are diagnostics-handler-specific and stay where they are).

The existing `tests/control/test_control_server_client_perception.inc:109-179` assertions all check for required fields by name — adding new keys does not break them. The new top-level keys (`severity`, `findings`, `vivid_version`) are non-breaking additions.

## Test design

`tests/control/test_runtime_health_snapshot.cpp` follows the `tests/control/test_runtime_core.cpp` pattern: build a real `Graph` programmatically with test operators, call `RuntimeCore::build` + `tick`, then `runtime_health::collect()` and assert.

Cases:

1. **Clean graph → `Severity::Ok`, empty findings.** Build a graph of three known operators wired correctly. `collect()`. Assert `overall == Ok`, `findings.empty()`.
2. **Missing operator → `Severity::Error`, `missing_required_operators` finding.** Use a graph with an unregistered type. Assert overall + finding code + subject equals the bad type name.
3. **Dropped connection → `Severity::Warning`, `dropped_connections` finding.** Programmatically create a connection with mismatched port types. Assert.
4. **GPU device lost → `Severity::Fatal`, `gpu_device_lost` finding.** Pass a stub `GpuContext` (or directly construct snapshot with `gpu.device_lost = true`). Assert.
5. **Severity precedence.** Snapshot with both fatal and warning conditions: overall must be `Fatal`; both findings still present.
6. **JSON round-trip.** Serialize a snapshot, parse, re-serialize, assert byte-identical (deterministic ordering via `nlohmann::json` ordered keys; if not deterministic, sort and compare).
7. **Back-compat shape.** Build a snapshot, call `to_json()`, assert the top-level object contains every field listed in `tests/control/test_control_server_client_perception.inc:143-178`.
8. **Empty graph.** `collect()` on a runtime with no graph compiled → `Ok` with all counts zero, no findings.

Register in `cmake/tests/10-runtime-control-graph.cmake` mirroring the `test_runtime_core` block (no GPU/audio dependencies; uses `vivid_runtime_testlib` + `nlohmann_json`). Label not required since partition 10 runs without labels in the gate (it's all HEADLESS_SMOKE-equivalent).

Wait — to make the new test part of the production gate's HEADLESS_SMOKE lane, label it explicitly:

```cmake
set_tests_properties(test_runtime_health_snapshot PROPERTIES
    LABELS "HEADLESS_SMOKE"
    TIMEOUT 15)
```

That puts the snapshot's correctness on the same critical path as the report tool's self-test.

## Verification

Local (in worktree):
```bash
cmake --build build --target test_runtime_health_snapshot -j$(sysctl -n hw.logicalcpu)
ctest --test-dir build -R test_runtime_health_snapshot --output-on-failure -V

# Confirm refactored run_diagnostics still works for existing assertions
ctest --test-dir build -R test_control_server --output-on-failure

# Run the full gate; existing JSON shape preserved + new test passes
cmake --build build --target production_gate_core -j$(sysctl -n hw.logicalcpu)
cat build/reports/production-gate.json | python3 -m json.tool | head -10
```

Negative test:
- Manually break a fixture graph (delete an operator type from the registry, run `tests/control/test_runtime_core.cpp`-style harness), confirm `runtime_health::collect()` reports `Severity::Error` with `missing_required_operators` finding.

## Risks

1. **`handle_run_diagnostics` refactor changes a function signature** (adds GpuContext*). Ripple: `control_server_dispatch.cpp` and any internal caller. Both are in this worktree; compile catches the rest.
2. **JSON key ordering**: `nlohmann::json` is ordered by insertion. To get deterministic output for tests, the serializer must add keys in a defined order. Use the same order the existing `handle_run_diagnostics` uses for shared keys.
3. **`tests/control/test_control_server_client_perception.inc` is included by another test** (`.inc` suffix). When refactoring, run that test specifically to confirm no surprise breakage.
4. **GpuContext nullability**: not all callers will have one (e.g. headless tests). `collect()` accepts `nullptr` and just leaves `gpu.*` defaults; serializer emits the same default sub-block as today.
5. **AudioEngine nullability**: `handle_run_diagnostics` already accepts `AudioEngine*` (nullable). Mirror that.

## What I will do on approval

1. Add the two one-line accessors (`AudioFrameBridge::lane_overflow_count`, `GpuContext::last_error`).
2. Write `runtime_health.{h,cpp}` per the design.
3. Refactor `handle_run_diagnostics` to use `runtime_health::collect` + `runtime_health::to_json` for its `health` block. Thread `GpuContext*` through.
4. Add `runtime_health.cpp` to the runtime library's source list.
5. Write `tests/control/test_runtime_health_snapshot.cpp`.
6. Register the test in `cmake/tests/10-runtime-control-graph.cmake` with HEADLESS_SMOKE label.
7. Run the new test alone; run `test_control_server*` to confirm back-compat; run `production_gate_core` for the full picture.
8. Report back.
