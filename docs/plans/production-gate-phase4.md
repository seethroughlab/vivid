# Plan: Production Gate — Phase 4 (surface health on control server, MCP, UI)

## Context

Phase 3 produced the `RuntimeHealthSnapshot` aggregator + JSON serializer, but only `run_diagnostics` consumes it today. Phase 4 surfaces the snapshot on every client surface that already reads runtime state:

- **Control server**: new `get_runtime_health` HTTP endpoint that returns the full snapshot envelope.
- **MCP**: new `get_runtime_health` tool that wraps the endpoint.
- **UI**: a "Health: OK / WARNING / ERROR / FATAL" pill in the diagnostics panel and a status-bar dot near the diagnostics button.
- **Crash recovery**: when the previous session crashed, the snapshot emits a `recovered_from_crash` finding naming the offending operator.

This phase also clears one debt from Phase 3: GpuContext is plumbed into the snapshot's call sites so `gpu.device_lost`/`gpu.last_error` are populated for real (Phase 3 passed `nullptr`).

Phase 4 adds zero new health probes. It only routes existing snapshot data to clients.

Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`. Phases 1–3 changes remain uncommitted alongside Phase 4.

## Decisions locked in (with rationale)

- **`get_runtime_health` is read-only and parameterless.** Dispatched in the read-only block alongside `run_diagnostics` (`control_server_dispatch.cpp:30`). Returns `{"ok": true, "schema_version": 1, "health": {...}}`. The snapshot's own `schema_version` is independent of `run_diagnostics`'s `schema_version: 2`; they will diverge as Phase 5 adds budgets to one but not the other.

- **GpuContext is plumbed into ControlServer via a setter** (`set_gpu_context()`), mirroring the existing pattern for `AudioEngine`, `AssetLibrary`, `BuildConsole`, etc. (`control_server.h:86`). main.cpp calls `control_server.set_gpu_context(&gpu)` once at startup. dispatch reads `gpu_context_` off the server. No new constructor.

- **UI consumes health via `GraphSnapshot`**, not by calling `runtime_health::collect()` directly. The UI never reaches into runtime objects today — it reads `snap_` populated each frame by `GraphSnapshotBuilder`. We add **one field**: `RuntimeHealthSummary { Severity overall; int finding_count; }` to `GraphSnapshot`. The panel + dot need just enough to render the pill; if the user opens the panel for details, the existing per-finding diagnostic snapshot already covers rich content. This keeps per-frame snapshot cost minimal.

- **`GraphSnapshotBuilder` calls `runtime_health::collect()` once per frame.** Cheap: pure aggregation of already-tracked atomics/counters; no allocation in the hot path beyond the one `findings` vector that we won't read off the snapshot path. To keep the per-frame cost truly trivial, the snapshot builder calls a new lightweight `runtime_health::collect_summary()` that does the rollup but skips the per-node top-N audio analysis (the most expensive part of `collect()`).

- **Crash recovery finding lives in `runtime_health::collect()`.** main.cpp owns the `CrashRecoveryManager` and the `optional<CrashRecord>` returned by `init()`. We add an optional `prior_crash` parameter to `collect()` (defaults to `nullptr`). When set, `apply_severity_rules()` emits one `Fatal` finding `code="recovered_from_crash"` with subject = operator_name. main.cpp threads its `prior_crash` into both `set_prior_crash()` on RuntimeCore (so subsequent `collect()` calls see it) — wait, no: simplest is to thread the optional through ControlServer (new setter `set_prior_crash`) so the dispatch handler can pass it to `collect()`. UI gets it via the same mechanism on RuntimeCore — actually cleanest: store `optional<CrashRecord>` on `RuntimeCore` itself (one new field, one setter), so any caller of `runtime_health::collect(graph, core, ...)` automatically gets it via `core.prior_crash()`. Decision: store on `RuntimeCore`. One field, one setter, one accessor.

- **MCP tool mirrors `get_graph_errors`** (`mcp/vivid_mcp.py:1055`), not `runtime_status`. `runtime_status` is a special HTTP-reachability ping; `get_graph_errors` is the right template for "POST a method, return the envelope" tools. New tool calls `await _post("get_runtime_health")` and returns through `_perception_response`.

- **MCP test goes in the existing `mcp/test_vivid_mcp_perception.py`** harness that stubs FastMCP + httpx. Pattern: mock `_post` to return a synthetic envelope, invoke tool, assert the `health` block round-trips.

- **UI test goes in `tests/ui/test_ui_overlay_interactions.cpp`** (already uses `#define private public` to inspect NodeGraphUI state). New case: feed a `GraphSnapshot` with each severity, draw, assert the pill text/color via state inspection (no screenshot diff).

## What changed vs. the parent plan's Phase 4 sketch

The parent plan said the UI should call `runtime_health::collect()` directly. Exploration found the UI never calls runtime objects directly — it consumes `GraphSnapshot`. Honoring that boundary requires the snapshot extension above. Cost: one field. Benefit: doesn't break the architectural rule.

The parent plan said `dispatch.cpp:~25` for endpoint registration. Actual location: line 30. Pattern matches.

## Scope

In scope:
- New `runtime_health::collect_summary()` (severity + finding_count only; skips per-node audio analysis).
- New `RuntimeCore::set_prior_crash()` / `prior_crash()` accessors. One member of type `std::optional<CrashRecord>`.
- New `recovered_from_crash` finding emitted when `prior_crash.has_value()`.
- ControlServer: add `GpuContext* gpu_context_` member + `set_gpu_context()` setter. Pass `&gpu` from main.cpp.
- New `handle_get_runtime_health()` in `control_server_checks.{h,cpp}`.
- Dispatch: register `get_runtime_health` next to `run_diagnostics` (control_server_dispatch.cpp:30). Plumb GpuContext through dispatch's call to both `run_diagnostics` and `get_runtime_health`.
- main.cpp: call `control_server.set_gpu_context(&gpu)` and `runtime_core.set_prior_crash(prior_crash)` at startup.
- Refactor `handle_run_diagnostics` (Phase 3 call site) to pass real `gpu_context` and `core.prior_crash()` instead of nullptr/empty.
- New MCP tool `get_runtime_health` in `mcp/vivid_mcp.py`.
- Update `mcp/CLAUDE.md` tool list.
- Extend `GraphSnapshot` with `RuntimeHealthSummary` field. Populate in `GraphSnapshotBuilder`.
- UI: extend `draw_diagnostics_panel()` with the pill at the top; replace the existing audio-underrun-tinted status-bar dot with a health-severity-tinted dot.
- Tests:
  - C++: `tests/control/test_control_server.cpp` — `get_runtime_health returns ok with health block`.
  - C++: `tests/control/test_control_server_client_perception.inc` — assert envelope shape (mirrors run_diagnostics block ~line 110).
  - Python: `mcp/test_vivid_mcp_perception.py` — assert MCP tool registration + envelope passes through.
  - C++: `tests/ui/test_ui_overlay_interactions.cpp` — assert pill renders the right text/color for each severity.
  - C++: extend `tests/control/test_runtime_health_snapshot.cpp` with a `recovered_from_crash` case using a synthetic `CrashRecord`.

Out of scope (Phase 5):
- Health budgets and `--health-json` consumption in the report tool.
- Per-graph health JSON dumps from `test_demo_graphs`.
- `production-gate.md` user-facing docs.

## Files

New:
- (none — all changes are edits to existing files)

Modified:
- `src/runtime/core/runtime_health.h` — add `collect_summary()` declaration + `RuntimeHealthSummary` POD; add `prior_crash` plumbing to `collect()`.
- `src/runtime/core/runtime_health.cpp` — implement `collect_summary()`; emit `recovered_from_crash` finding when `prior_crash` is set.
- `src/runtime/core/runtime_core.{h,cpp}` — add `optional<CrashRecord> prior_crash_` member, `set_prior_crash()`, `prior_crash()` accessor.
- `src/runtime/control/control_server.{h,cpp}` — add `GpuContext* gpu_context_` + `set_gpu_context()`. Forward-declare GpuContext in header.
- `src/runtime/control/control_server_dispatch.cpp` — register `get_runtime_health`; pass GpuContext into both `run_diagnostics` and `get_runtime_health`.
- `src/runtime/control/control_server_checks.{h,cpp}` — add `handle_get_runtime_health()`; update `handle_run_diagnostics` signature to accept GpuContext + use `core.prior_crash()` instead of nullptr.
- `src/runtime/core/main.cpp` — `control_server.set_gpu_context(&gpu)`, `runtime_core.set_prior_crash(std::move(prior_crash))` at startup (after CrashRecoveryManager init).
- `src/runtime/graph/graph_snapshot.h` (or wherever GraphSnapshot is defined) — add `RuntimeHealthSummary` field.
- `src/runtime/graph/graph_snapshot_builder.cpp` — call `runtime_health::collect_summary()` and populate the new field.
- `src/ui/graph/node_graph.h` — no new state needed (panel already has its own state cluster).
- `src/ui/graph/node_graph_draw_overlays.cpp` — add pill near line 722 (panel header); change status-bar dot color source from audio-underrun to health severity (lines 509–510).
- `mcp/vivid_mcp.py` — new `get_runtime_health` tool.
- `mcp/CLAUDE.md` — one row in the tool catalog table.
- `tests/control/test_control_server.cpp` — new endpoint test case.
- `tests/control/test_control_server_client_perception.inc` — envelope-shape assertions.
- `tests/control/test_runtime_health_snapshot.cpp` — new `recovered_from_crash` case.
- `tests/ui/test_ui_overlay_interactions.cpp` — new severity-pill case.
- `mcp/test_vivid_mcp_perception.py` — new tool registration + envelope passthrough case.

## Endpoint design

```
POST /get_runtime_health
Body: {} (or omitted)
Response (success):
{
  "ok": true,
  "schema_version": 1,
  "health": { /* runtime_health::to_json output */ }
}
Response (error):
{
  "ok": false,
  "error": "..."
}
```

Schema version starts at 1 (independent of `run_diagnostics`'s 2).

## Code sketches

### `RuntimeHealthSummary` (UI-facing)

```cpp
// runtime_health.h
struct RuntimeHealthSummary {
    Severity overall = Severity::Ok;
    int finding_count = 0;
};
RuntimeHealthSummary collect_summary(const Graph& graph,
                                     const RuntimeCore& core,
                                     const OperatorRegistry& registry,
                                     const AudioEngine* audio_engine,
                                     const GpuContext* gpu_context);
```

`collect_summary()` runs the same rollup but skips `top_nodes`/`top_lane_state_nodes` population. Cheap enough to call every frame from `GraphSnapshotBuilder`.

### Recovered-from-crash finding

```cpp
// In apply_severity_rules(), after gpu_device_lost block:
if (snap.prior_crash_operator.has_value()) {
    snap.findings.push_back({
        "recovered_from_crash", Severity::Fatal,
        snap.prior_crash_operator.value(),
        "Previous run crashed in operator '" + snap.prior_crash_operator.value()
            + "'. Affected nodes may be disabled.",
    });
    bump(Severity::Fatal);
}
```

`RuntimeHealthSnapshot` gets a new `optional<string> prior_crash_operator` field (just the operator name — keeps the snapshot self-contained without depending on CrashRecord). `collect()` populates it from `core.prior_crash()`.

### MCP tool

```python
@mcp.tool()
async def get_runtime_health() -> str:
    """Get the runtime's health snapshot: severity (ok/warning/error/fatal),
    structured findings, audio engine state, graph compile state, and GPU state.

    Use this for a quick "is the runtime safe to operate?" check before deeper
    introspection. The same data feeds the diagnostics panel pill in the UI."""
    return await _post("get_runtime_health")
```

### UI pill (sketch)

In `draw_diagnostics_panel()` after line 722:

```cpp
const auto& summary = snap_.runtime_health;
const char* label = vivid::runtime_health::severity_name(summary.overall);
auto [r, g, b] = pill_colors_for(summary.overall);  // ok→green, warning→orange, error→red, fatal→deep-red
draw_pill(tr, x, y, label, r, g, b);
if (summary.finding_count > 0) {
    draw_text(tr, x + pill_w + 6, y, std::to_string(summary.finding_count) + " finding(s)");
}
```

Status-bar dot (lines 509–510): replace the audio-underrun-derived color with `pill_colors_for(snap_.runtime_health.overall)`.

## Verification

Local (in worktree):
```bash
# C++ snapshot tests including the new recovered_from_crash case
cmake --build build --target test_runtime_health_snapshot -j$(sysctl -n hw.logicalcpu)
ctest --test-dir build -R test_runtime_health_snapshot --output-on-failure -V

# Control server round-trip
cmake --build build --target test_control_server -j$(sysctl -n hw.logicalcpu)
ctest --test-dir build -R test_control_server --output-on-failure

# UI overlay test asserting pill text/color
cmake --build build --target test_ui_overlay_interactions -j$(sysctl -n hw.logicalcpu)
ctest --test-dir build -R test_ui_overlay_interactions --output-on-failure

# MCP tool registration + envelope
cd mcp && uv run --with pytest pytest test_vivid_mcp_perception.py -v

# Live: start the runtime, hit the endpoint
./build/vivid &
curl -X POST http://localhost:9876/get_runtime_health | python3 -m json.tool
kill %1

# Full gate end-to-end
cmake --build build --target production_gate_core -j$(sysctl -n hw.logicalcpu)
```

Negative test:
- Force a fake prior crash (write a synthetic crash marker before launching), confirm the panel pill turns Fatal and displays "Previous run crashed in operator 'X'".
- Force a graph with a missing operator, confirm the endpoint returns `severity: "error"` with `missing_required_operators` finding.

## Risks

1. **Per-frame `collect_summary()` cost.** Mitigated by skipping per-node aggregation. If profiling reveals a regression in frame time, defer the call to once-every-N-frames or cache by `reload_serial`.
2. **`GraphSnapshot` is consumed by other UI code paths** that may not expect new fields. Default-constructed `RuntimeHealthSummary` (Ok, 0) is safe; existing call sites unaffected.
3. **`GpuContext` lifecycle**: the setter on ControlServer takes a non-owning pointer. main.cpp's `gpu` outlives ControlServer. Document the borrow contract in the setter doc comment.
4. **CrashRecord type leakage**: `RuntimeCore` storing `optional<CrashRecord>` requires including `crash_recovery.h`. Acceptable — RuntimeCore already includes plenty. Alternative: store just the operator name + signal name as plain strings; that fully decouples and matches what `runtime_health` actually needs. Preferred approach.

## What I will do on approval

1. Add `RuntimeCore::set_prior_crash_operator()` / `prior_crash_operator()` (just the operator-name string — keeps RuntimeCore decoupled from `CrashRecord`).
2. Update `runtime_health.h`/`.cpp`: `RuntimeHealthSummary`, `collect_summary()`, `prior_crash_operator` field, `recovered_from_crash` rule.
3. Add `GpuContext` member + setter to ControlServer; thread through `dispatch()` to both health-related handlers.
4. Add `handle_get_runtime_health()` and register in dispatch.
5. Update `handle_run_diagnostics` callsite to pass real GpuContext + `core.prior_crash_operator()`.
6. main.cpp: wire the two new setters at startup.
7. Extend `GraphSnapshot` + `GraphSnapshotBuilder`.
8. Add the pill + dot in `node_graph_draw_overlays.cpp`.
9. Add the MCP tool + update `mcp/CLAUDE.md`.
10. Add the four new test cases.
11. Run the full gate; confirm `production-gate.json` still reports `status: "pass"` and the new endpoint test is in `HEADLESS_SMOKE`.
12. Live-test the endpoint with `curl`; spot-check the UI pill manually if a window is available.
