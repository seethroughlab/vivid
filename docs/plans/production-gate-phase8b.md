# Plan: Production Gate — Phase 8b (audio clipping + MCP liveness)

## Context

Phase 8a brought hot-reload + package-mismatch signals into the runtime-health snapshot. Phase 8b closes two more gaps the parent plan called for:

- **Peak/RMS clipping indicators** — already populated in `AnalysisSnapshot::peak` whenever audio analysis is enabled (default), but never aggregated into `runtime_health`.
- **Control server / MCP liveness** — already plumbed to `GraphSnapshot` for the UI's MCP indicator, but not surfaced as a finding in `runtime_health`.

Both are small additive changes — no new probes (the data already exists), just new fields, severity rules, and budget entries. Schema stays at version 4 (additive).

This is Phase 8b of the umbrella Phase 8 plan (`docs/plans/production-gate-phase8.md`), shipped as its own commit on the `worktree-production-gate-and-health` branch.

## Decisions locked in (with rationale)

- **Audio peak read path: `core.audio_frame_bridge().active_analysis().peak`** (`audio_frame_bridge.h:49`, lock-free acquire load). Walk all audio nodes × channels (`vector<array<float, 8>>`) to compute `peak_max` (max across the snapshot) and `clipping_count` (count of values ≥ 0.99). Bounded cost (~80 ops worst case on a heavy graph). No new probe.
- **Clipping threshold = 0.99** mirrors common DAW conventions and the existing audio output silence check threshold pattern in `test_demo_graphs.cpp` (which uses 0.001 for silence — same numeric class).
- **MCP plumbing via a small `McpStatus` bundle** instead of three trailing default arguments to `collect()`. Keeps the signature manageable and avoids growing it again in 8c. Passed by value (3 × `uint64_t` = 24 bytes).
- **`now_ms=0` means "look it up via `steady_clock::now()`"** — provides testability without forcing every caller to compute the timestamp.
- **`mcp_*_disconnected` finding skipped when `last_ping_ms == 0`** — avoids noise on headless dev / CI where MCP servers were never connected. Same logic for the budget. Per-graph health JSONs from `test_demo_graphs` (headless) will always carry zero pings, so the budget never trips for the gate; the live runtime endpoint does trip when an active MCP connection goes stale.
- **Lift `kMcpStaleMs = 30000` to a shared header** (`src/runtime/core/runtime_health.h`) so UI and runtime_health agree on the staleness threshold. Drop the local `static constexpr` from `node_graph_draw_overlays.cpp`.

## Scope

In scope:
- New fields on `RuntimeHealthSnapshot::AudioHealth`:
  ```cpp
  double  peak_max = 0.0;
  int64_t clipping_count = 0;
  ```
- New sub-block on `RuntimeHealthSnapshot`:
  ```cpp
  struct McpHealth {
      uint64_t main_last_ping_ms = 0;
      uint64_t opdev_last_ping_ms = 0;
      uint64_t now_ms = 0;
      bool main_connected = false;   // computed: main > 0 && now - main < threshold
      bool opdev_connected = false;
  };
  McpHealth mcp;
  ```
- New `McpStatus` argument bundle for `collect()` and `collect_summary()`:
  ```cpp
  struct McpStatus {
      uint64_t main_ping_ms = 0;
      uint64_t opdev_ping_ms = 0;
      uint64_t now_ms = 0;     // 0 → resolved internally via steady_clock
  };
  ```
- `runtime_health::collect()` and `collect_summary()` gain trailing `McpStatus mcp = {}` parameter.
- Severity rules in `apply_severity_rules`:
  - `audio_clipping` (Warning) — `audio.clipping_count > 0`. Message: `"N audio sample(s) at or above clipping threshold."`
  - `mcp_main_disconnected` (Warning) — `mcp.main_last_ping_ms > 0 && !mcp.main_connected`.
  - `mcp_opdev_disconnected` (Warning) — same shape for opdev.
- `populate_minimal()` reads peak data from `core.audio_frame_bridge().active_analysis()` (lock-free).
- `populate_minimal()` writes mcp sub-block from `McpStatus`. If `mcp.now_ms == 0`, resolve via `std::chrono::steady_clock`.
- `handle_get_runtime_health` (in `control_server_checks.cpp`) reads MCP pings from a new `ControlServer*` parameter — mirroring the package_catalog pattern from 8a. Built `McpStatus` and passes to `collect()`.
- `handle_run_diagnostics` does the same for back-compat.
- Dispatch passes `package_catalog_` and a new fourth-pillar param: `control_server_ptr` (which is just `this`).
  - Cleaner alternative: add `set_self_for_runtime_health()` is silly. The right move is to pass the four MCP-related uint64_t values directly into the dispatch's call. But dispatch already has `&mcp_last_ping_ms` access? Actually dispatch has `package_manager_` etc. but not direct MCP access. Solution: add `ControlServer* control_server` to the dispatch signature (mirrors how `package_catalog_` was added in Phase 8a). Trivial — `process_requests` is on ControlServer so `this` is in scope.
- `GraphSnapshotBuilder` constructs an `McpStatus` from the values it already reads at `graph_snapshot_builder.cpp:605-606` and passes to `collect_summary()`. The UI pill now reflects MCP staleness too.
- New budgets in `tools/production_gate_budgets.toml`:
  - `no_audio_clipping` (warning, applies_to=audio,av)
  - `mcp_servers_connected` (warning, applies_to=*) — only fires when at least one server has been seen.
- Extend `_check_budget` switch in `production_gate_report.py` for both codes.
- Lift `kMcpStaleMs` to `src/runtime/core/runtime_health.h` as `constexpr uint64_t kMcpStaleMs`. Update `node_graph_draw_overlays.cpp` to use the lifted constant.
- Tests:
  - `test_runtime_health_snapshot.cpp`: severity-rollup cases for each rule (3 cases).
  - `test_production_gate_report.py`: budget evaluation for both codes (2 fixtures).

Out of scope (Phase 8c):
- Per-node clipping attribution — only graph-level peak / clipping_count.
- Stateful sustained-silence/black detection.

## Files

New:
- `tests/cli/fixtures/health/audio_clipping.json`
- `tests/cli/fixtures/health/mcp_disconnected.json`

Modified:
- `src/runtime/core/runtime_health.h` — `kMcpStaleMs` constant, `McpHealth` + `McpStatus` structs, new audio fields, new fn signatures.
- `src/runtime/core/runtime_health.cpp` — populate audio peak + mcp; new severity rules; to_json emits `mcp` sub-block + new audio fields.
- `src/runtime/control/control_server_checks.{h,cpp}` — handlers accept `ControlServer*`, read MCP pings, build `McpStatus`, pass to collect.
- `src/runtime/control/control_server_dispatch.cpp` — pass `ControlServer* (this proxy via the runtime API path)` to checks. Actually: easier, add `ControlServer*` param to dispatch, `process_requests` passes `this`.
- `src/runtime/control/control_server.cpp` — `process_requests` passes `this` to `dispatch`.
- `src/runtime/control/control_server_internal.h` — dispatch signature gains `ControlServer*`.
- `src/runtime/graph/graph_snapshot_builder.cpp` — pass `McpStatus` to `collect_summary`.
- `src/ui/graph/node_graph_draw_overlays.cpp` — drop local `kMcpStaleMs`, use `vivid::runtime_health::kMcpStaleMs`.
- `tools/production_gate_budgets.toml` — new entries.
- `tools/production_gate_report.py` — extend `_check_budget`.
- `tests/control/test_runtime_health_snapshot.cpp` — 3 new severity cases.
- `tests/cli/test_production_gate_report.py` — 2 new budget cases.
- `tests/cli/fixtures/budgets/default.toml` — add the two new budgets.
- `docs/testing/production-gate.md` — note the new fields.

## Code sketches

### `runtime_health.h` additions

```cpp
namespace vivid::runtime_health {

inline constexpr uint64_t kMcpStaleMs = 30000;

struct McpStatus {
    uint64_t main_ping_ms = 0;
    uint64_t opdev_ping_ms = 0;
    uint64_t now_ms = 0;     // 0 → use steady_clock::now()
};

struct McpHealth {
    uint64_t main_last_ping_ms = 0;
    uint64_t opdev_last_ping_ms = 0;
    uint64_t now_ms = 0;
    bool main_connected = false;
    bool opdev_connected = false;
};

// AudioHealth gains:
//   double  peak_max = 0.0;
//   int64_t clipping_count = 0;

// RuntimeHealthSnapshot gains:
//   McpHealth mcp;

RuntimeHealthSnapshot collect(const Graph&, const RuntimeCore&,
                              const OperatorRegistry&,
                              const AudioEngine*, const GpuContext*,
                              const PackageCatalog* = nullptr,
                              McpStatus mcp = {});
RuntimeHealthSummary collect_summary(/*...*/, McpStatus mcp = {});

}
```

### Audio peak aggregation in `populate_minimal`

```cpp
const auto& analysis = core.audio_frame_bridge().active_analysis();
double peak_max = 0.0;
int64_t clipping = 0;
constexpr float kClipThreshold = 0.99f;
for (const auto& node_peaks : analysis.peak) {
    for (float v : node_peaks) {
        const float a = std::fabs(v);
        if (a > peak_max) peak_max = a;
        if (a >= kClipThreshold) ++clipping;
    }
}
snap.audio.peak_max = peak_max;
snap.audio.clipping_count = clipping;
```

### MCP population in `populate_minimal`

```cpp
auto resolve_now = [&]() -> uint64_t {
    if (mcp.now_ms != 0) return mcp.now_ms;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
};
const uint64_t now = resolve_now();
snap.mcp.main_last_ping_ms  = mcp.main_ping_ms;
snap.mcp.opdev_last_ping_ms = mcp.opdev_ping_ms;
snap.mcp.now_ms = now;
snap.mcp.main_connected =
    mcp.main_ping_ms > 0 && (now - mcp.main_ping_ms) < kMcpStaleMs;
snap.mcp.opdev_connected =
    mcp.opdev_ping_ms > 0 && (now - mcp.opdev_ping_ms) < kMcpStaleMs;
```

### Severity rules

```cpp
if (snap.audio.clipping_count > 0) {
    snap.findings.push_back({
        "audio_clipping", Severity::Warning, "",
        std::to_string(snap.audio.clipping_count) + " audio sample(s) at clipping.",
    });
    bump(Severity::Warning);
}
if (snap.mcp.main_last_ping_ms > 0 && !snap.mcp.main_connected) {
    snap.findings.push_back({
        "mcp_main_disconnected", Severity::Warning, "vivid",
        "MCP server 'vivid' has not pinged recently (stale > 30s).",
    });
    bump(Severity::Warning);
}
// same for opdev
```

### Dispatch threading

`control_server.cpp::process_requests` passes `this` as a new `ControlServer*` arg to `dispatch()`. `dispatch()` passes it to `handle_get_runtime_health` / `handle_run_diagnostics`. Each handler builds `McpStatus` from `control_server->mcp_last_ping_ms("vivid"|"opdev")` and calls `collect()`.

## Verification

Local (in worktree):
```bash
# Pytest including the new budget cases
uv run --with pytest pytest tests/cli/test_production_gate_report.py -v

# Snapshot transitions
cmake --build build --target test_runtime_health_snapshot -j$(sysctl -n hw.logicalcpu)
ctest --test-dir build -R test_runtime_health_snapshot --output-on-failure -V

# control_server back-compat
ctest --test-dir build -R test_control_server --output-on-failure

# Full gate
cmake --build build --target production_gate_core -j$(sysctl -n hw.logicalcpu)

# Live: confirm endpoint emits mcp + audio.peak_max + clipping_count
./build/vivid --headless &
sleep 6
curl -s -X POST http://localhost:9876/get_runtime_health \
    | python3 -m json.tool | grep -E 'peak_max|clipping_count|mcp'
kill %1
```

Negative test:
- Synthetic per-graph health JSON with `audio.clipping_count: 5` → gate `degraded` with `audio_clipping` breach.
- Synthetic JSON with `mcp.main_last_ping_ms` non-zero but `now_ms - main_last_ping_ms > 30000` → `mcp_main_disconnected` finding.

## Risks

1. **MCP-disconnected false positives on dev machines** that have MCP-then-no-MCP. Mitigated by "skipped when never-pinged" rule and the 30s threshold. Document in `production-gate.md`.
2. **`peak` snapshot freshness** — `active_analysis()` returns the latest published snapshot. If audio analysis was never published (e.g. audio engine not running), the vector is empty and `peak_max`/`clipping_count` stay 0. Correct: if no audio is running, no clipping signal.
3. **Schema stays at 4** — new fields are additive within the same major version. Document the additions in `production-gate.md`.
4. **`ControlServer*` proliferation** — by the end of 8a/8b, `dispatch` takes 16 parameters. Acceptable for now; if Phase 9 keeps adding, time to bundle into a `DispatchContext` struct.

## What I will do on approval

1. Move this plan to `docs/plans/production-gate-phase8b.md`.
2. Lift `kMcpStaleMs` to `runtime_health.h`. Update overlays.cpp.
3. Add `McpStatus` + `McpHealth` + `audio.peak_max` + `audio.clipping_count` to `runtime_health.{h,cpp}`.
4. Implement `populate_minimal()` peak + mcp population.
5. Add the 3 severity rules + to_json emission.
6. Thread `ControlServer*` through `dispatch()` → `handle_run_diagnostics` / `handle_get_runtime_health`.
7. Update `GraphSnapshotBuilder` to pass `McpStatus` to `collect_summary`.
8. Add 2 new budgets + extend Python switch.
9. Add 3 snapshot test cases + 2 pytest budget cases + 2 fixtures.
10. Run pytest, run targeted ctest, run `production_gate_core`. Live `curl` the endpoint.
11. Report back.
