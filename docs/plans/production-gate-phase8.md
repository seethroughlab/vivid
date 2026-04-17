# Plan: Production Gate — Phase 8 (deferred runtime probes)

## Context

Phases 6 and 7 closed the pre-merge cleanup pass. Phase 8 closes the parent plan's runtime-health probe gaps that Phases 3–5 deferred:

- **Hot-reload failures** (parent §"hot-reload failures") — Phase 3 had no in-process cache for `ReloadResult`.
- **Package/core version mismatches** (parent §"package/core version mismatches") — never wired.
- **Peak/RMS clipping indicators** (parent §"peak/RMS clipping indicators") — never wired.
- **Control server / MCP liveness** (parent §"control server/MCP liveness where relevant") — UI surfaces it via `GraphSnapshot`, but `runtime_health` doesn't.
- **Sustained silence / black detection** (parent §"output health hints such as sustained black frame or sustained silence when analysis is enabled") — `OutputAnalyzer` is stateless; no continuous monitor exists.

Phase 8 splits into three sub-phases that ship independently. Each one is a self-contained set of new probes, snapshot fields, severity rules, and budget entries. Schema bumps from 3 → 4 once at the start of 8a; subsequent sub-phases are additive.

Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`.

## Sequencing

```
8a — hot reload + version mismatches    (3-4 days)  ─── lowest risk
8b — audio clipping + MCP liveness      (2-3 days)  ─── small surface
8c — sustained silence / black          (1 week)    ─── adds stateful probes
```

Each sub-phase ends with `production_gate_core` green. Schema bumps to 4 in 8a; 8b/8c are additive.

---

## Phase 8a — Hot reload + version mismatches

**Goal:** the snapshot tracks the most recent hot-reload attempt and any package/core version mismatch.

### Decisions locked in
- **`RuntimeCore::set_last_reload(ReloadResult)`** stores the most recent reload outcome (`optional<ReloadResult>` member). Called from `main_helpers.cpp:325` after `HotReloader::poll_ready()` returns each result. `runtime_health::collect()` reads it.
- **"Required" detection** = walk `CompiledGraph::nodes` for any `node.type_name == reload_target.type_part`. If found, severity is `Error`; if not found (stale operator), severity is `Warning`. Reuses existing `compiled_graph()` API; no new index.
- **Package version mismatches via `PackageCatalog::summarize_updates(core_version)`**. Snapshot builder already has `ControlServer*` (which owns `PackageCatalog*`); we reuse that path. Health collect signature gains an optional `const PackageCatalog*` parameter (mirrors Phase 4's `GpuContext*` pattern). When null, the package fields stay at default (zero) and the budget never fires.
- **`core_version`**: pass empty string for now (no `VIVID_VERSION` constant exists). The catalog's compatibility check returns `incompatible_updates = 0` when version is empty, so the budget reads "no mismatch" — honest. When a real version constant lands, swap one string and the budget activates.
- **Schema bump 3 → 4** because `RuntimeHealthSnapshot` gains new sub-blocks. `health` block is a strict superset (no removals); existing consumers continue to work.

### Scope

In scope:
- New fields on `RuntimeHealthSnapshot`:
  ```cpp
  struct HotReloadHealth {
      bool last_attempt_succeeded = true;
      std::string last_target;     // empty if no reload yet
      std::string last_error;      // populated when !success
      bool affects_current_graph = false;
  };
  struct PackagesHealth {
      int64_t installed = 0;
      int64_t updates_available = 0;
      int64_t incompatible_updates = 0;
  };
  // Added to RuntimeHealthSnapshot:
  HotReloadHealth hot_reload;
  PackagesHealth  packages;
  ```
- New severity rules in `apply_severity_rules`:
  - `hot_reload_failed_required` (Error) — `!hot_reload.last_attempt_succeeded && hot_reload.affects_current_graph`.
  - `hot_reload_failed_stale` (Warning) — `!hot_reload.last_attempt_succeeded && !hot_reload.affects_current_graph`.
  - `package_version_mismatch` (Warning) — `packages.incompatible_updates > 0`.
- `RuntimeCore::set_last_reload(ReloadResult)` + `last_reload()` accessor + private `optional<ReloadResult> last_reload_` member.
- `populate_minimal()` reads `core.last_reload()` and walks `compiled_graph()->nodes` to set `hot_reload.affects_current_graph`.
- New `const PackageCatalog*` parameter to `runtime_health::collect()` (default `nullptr`). Both `handle_run_diagnostics` and `handle_get_runtime_health` thread it through (control_server already holds `package_catalog_`).
- main.cpp: insert `runtime.set_last_reload(result)` in the loop at `main_helpers.cpp:325`.
- New budgets in `tools/production_gate_budgets.toml`:
  - `no_hot_reload_failures` (warning)
  - `no_required_operator_reload_failures` (error)
  - `no_package_version_mismatches` (warning)
- Extend `_check_budget` switch in `production_gate_report.py` for the three new codes.
- Bump `SCHEMA_VERSION` 3 → 4.
- Tests:
  - `test_runtime_health_snapshot.cpp`: severity-rollup cases for each rule.
  - `test_production_gate_report.py`: budget evaluation for each new code with synthetic per-graph health JSONs.

### Files (8a)

New: `tests/cli/fixtures/health/hot_reload_required_fail.json`, `hot_reload_stale_fail.json`, `package_mismatch.json`.

Modified: `src/runtime/core/runtime_core.{h,cpp}`, `src/runtime/core/runtime_health.{h,cpp}`, `src/runtime/control/control_server_checks.{h,cpp}`, `src/runtime/control/control_server_dispatch.cpp`, `src/runtime/core/main_helpers.cpp`, `tools/production_gate_report.py`, `tools/production_gate_budgets.toml`, `tests/control/test_runtime_health_snapshot.cpp`, `tests/cli/test_production_gate_report.py`, `mcp/vivid_mcp.py` (docstring), `docs/testing/production-gate.md` (note new fields).

### Verification (8a)
- `pytest tests/cli/test_production_gate_report.py -v` green incl. 3 new cases.
- `ctest -R 'test_runtime_health_snapshot|test_control_server'` green.
- `production_gate_core` exits cleanly; production-gate.json shows `schema_version: 4` and the new sub-blocks under `health`.
- Live curl `get_runtime_health` shows `hot_reload` and `packages` sub-blocks.

---

## Phase 8b — Audio clipping + MCP liveness

**Goal:** the snapshot reports peak audio levels (with a clipping count) and MCP server reachability.

### Decisions locked in
- **Audio peak reading from `AnalysisSnapshot::peak`** — already populated when `audio_engine.analysis_enabled()` is true (default true per `audio_executor.h:108`). `populate_minimal()` reads `core.audio_frame_bridge().active_analysis().peak` and computes `peak_max` (max across nodes/channels) + `clipping_count` (count of values ≥ 0.99). No new probe; just aggregation.
- **MCP liveness via two `uint64_t` parameters to `collect()`** instead of plumbing `ControlServer*`. The snapshot builder already has the values from `control_server->mcp_last_ping_ms("vivid"|"opdev")` (`graph_snapshot_builder.cpp:605`). For `handle_get_runtime_health`, the dispatch handler reads them off `ControlServer` directly. Simpler than adding another nullable pointer to `collect()`'s signature.
- **Stale threshold = 30 s**, matching the existing `kMcpStaleMs` in `node_graph_draw_overlays.cpp:16`. Lift to a shared constant.

### Scope

In scope:
- New fields on `RuntimeHealthSnapshot::AudioHealth`:
  ```cpp
  double peak_max = 0.0;          // max across all nodes/channels in the active snapshot
  int64_t clipping_count = 0;     // count of |sample| >= 0.99 in the active snapshot
  ```
- New sub-block on `RuntimeHealthSnapshot`:
  ```cpp
  struct McpHealth {
      bool main_connected = false;
      bool opdev_connected = false;
      uint64_t main_last_ping_ms = 0;
      uint64_t opdev_last_ping_ms = 0;
      uint64_t now_ms = 0;
      uint64_t stale_threshold_ms = 30000;
  };
  McpHealth mcp;
  ```
- `runtime_health::collect()` and `collect_summary()` gain two `uint64_t` params (`mcp_main_ping_ms`, `mcp_opdev_ping_ms`); default 0. They also accept a `now_ms` for testability.
- New severity rules:
  - `audio_clipping` (Warning) — `audio.clipping_count > 0`.
  - `mcp_main_disconnected` (Warning) — `mcp.main_last_ping_ms > 0 && !mcp.main_connected`. Skipped when never-pinged (avoid noise on headless dev).
  - `mcp_opdev_disconnected` (Warning) — same shape for opdev.
- New `populate_minimal` lines that read peak data from `core.audio_frame_bridge().active_analysis()` (read-only, lock-free).
- New budgets in TOML:
  - `no_audio_clipping` (warning, applies_to=audio,av)
  - `mcp_servers_connected` (warning, applies_to=*) — only fires when at least one MCP server has been seen since startup.
- `handle_get_runtime_health` reads `mcp_last_ping_ms` from the control server (which is `this`); passes both to `collect()`.
- Snapshot builder passes the same values to `collect_summary()` so the UI pill reflects MCP state.
- Lift `kMcpStaleMs` constant to `src/runtime/core/runtime_health.h` (or a shared header) so UI and runtime_health agree.
- Tests: snapshot rollup cases for each rule; budget evaluation fixtures.

### Files (8b)

New: `tests/cli/fixtures/health/audio_clipping.json`, `mcp_disconnected.json`.

Modified: `src/runtime/core/runtime_health.{h,cpp}`, `src/runtime/control/control_server_checks.cpp` (read MCP ping in handler), `src/runtime/graph/graph_snapshot_builder.cpp` (pass MCP ping to collect_summary), `src/ui/graph/node_graph_draw_overlays.cpp` (drop now-duplicate `kMcpStaleMs`), `tools/production_gate_report.py` (extend `_check_budget`), `tools/production_gate_budgets.toml` (new entries), `tests/control/test_runtime_health_snapshot.cpp`, `tests/cli/test_production_gate_report.py`, `docs/testing/production-gate.md`.

### Verification (8b)
- `pytest` + ctest green incl. new cases.
- Live `curl get_runtime_health`: `mcp.main_connected: true` after the MCP server has hit the runtime once.
- Force a clipping fixture; gate goes degraded with `audio_clipping` breach.

---

## Phase 8c — Sustained silence / black detection

**Goal:** the snapshot can detect when an audio-producing graph has been silent for N seconds, or when a visual-producing graph has rendered black for N seconds.

### Decisions locked in
- **Sliding-window state lives on `RuntimeCore`** as a small fixed-capacity ring (`RuntimeHealthSamplers`). Per-frame sampling is driven by a new `RuntimeCore::sample_runtime_health(double now)` called from the main frame loop (mirrors `update_audio_sources()` placement).
- **Window size = 5 s**, sample cadence = once per snapshot-builder call (~60 Hz), so ring capacity = 300 slots. Memory: ~16 bytes/slot × 300 = 4.8 KB. Allocated once at `RuntimeCore::build()`.
- **Audio sample source**: max-of-`AnalysisSnapshot::peak` across all audio nodes/channels each frame. Reuses the same data 8b reads — no extra probe.
- **Visual sample source**: brightness from `find_effective_gpu_sink()`'s `GpuNodeState::frame_analysis->brightness_`. Lazily populated when GPU analysis is enabled (default true). If the sink doesn't exist or no GPU analysis is available, sample is `nullopt` and the sustained-black detector reports `not_applicable`.
- **Detection thresholds**: silence if max peak < 0.001 over the entire window; black if mean brightness < 4 (out of 255) over the entire window. Both numbers mirror values already used elsewhere (the test_demo_graphs silence check uses 0.001).
- **`apply_severity_rules` decision** uses the new `audio.silence_active` / `gpu.black_active` booleans, NOT the raw windows. The window math stays in `RuntimeCore`'s sampler so the rollup function stays pure.
- **Domain filtering happens in budget evaluation, not in `runtime_health`**. The snapshot reports `silence_active=true` regardless of graph type; the budget filters by `applies_to=audio,av` so a control-only graph never trips it. This matches Phase 5's existing pattern.
- **Test integration**: `test_demo_graphs.cpp` calls `runtime.frame_executor().set_analysis_enabled(true)` and `audio->set_analysis_enabled(true)` after build for graphs whose `meta.domains` include `audio`/`gpu`/`av`. The snapshot then carries real silence/black data into the per-graph health JSON.

### Scope

In scope:
- New header `src/runtime/core/runtime_health_samplers.h` with `RuntimeHealthSamplers` (ring buffers + reducers `silence_active(window_seconds)` / `black_active(window_seconds)`).
- New `RuntimeCore` member `RuntimeHealthSamplers samplers_`. `build()` sizes the rings; `sample_runtime_health(double now)` advances them.
- New fields on `RuntimeHealthSnapshot::AudioHealth` and `GpuHealth`:
  ```cpp
  // AudioHealth:
  double silence_window_seconds = 0.0;   // how much of the window is currently silent
  bool   silence_active = false;
  // GpuHealth:
  double black_window_seconds = 0.0;
  bool   black_active = false;
  ```
- `populate_minimal()` reads from `core.samplers()` to fill the new fields.
- New severity rules:
  - `sustained_silence` (Warning) — `audio.silence_active`.
  - `sustained_black` (Warning) — `gpu.black_active`.
- main.cpp calls `runtime.sample_runtime_health(time)` once per frame (next to `pre_tick_audio_sync` / `update_audio_sources`).
- `tests/integration/test_demo_graphs.cpp`: when graph domains include `audio`/`gpu`/`av`, enable audio + frame analysis after build and step the sampler each tick.
- Uncomment the `no_sustained_silence` and `no_sustained_black` budget entries in `tools/production_gate_budgets.toml`. Extend `_check_budget` for the two new codes.
- Tests:
  - `test_runtime_health_samplers.cpp` (new) — unit tests for the sampler: feed N samples, check window detection.
  - `test_runtime_health_snapshot.cpp` — severity rollup cases.
  - `test_production_gate_report.py` — budget cases for silence/black + domain filter.

### Files (8c)

New:
- `src/runtime/core/runtime_health_samplers.{h,cpp}`
- `tests/control/test_runtime_health_samplers.cpp`
- `tests/cli/fixtures/health/sustained_silence.json`, `sustained_black.json`

Modified: `src/runtime/core/runtime_core.{h,cpp}`, `src/runtime/core/runtime_health.{h,cpp}`, `src/runtime/core/main.cpp` (sampler tick), `tests/integration/test_demo_graphs.cpp` (enable analysis + sampler tick), `tools/production_gate_budgets.toml` (uncomment), `tools/production_gate_report.py` (`_check_budget`), `tests/control/test_runtime_health_snapshot.cpp`, `tests/cli/test_production_gate_report.py`, `cmake/tests.cmake` + `cmake/app.cmake` (new .cpp source), `cmake/tests/10-runtime-control-graph.cmake` (register sampler test), `docs/testing/production-gate.md`.

### Verification (8c)
- `ctest -R test_runtime_health_samplers` green (unit-tests the ring + detection thresholds).
- `production_gate_core` on a deliberately-silent fixture graph (e.g. zero-gain Mixer feeding `audio_out`) — confirm `sustained_silence` finding + degraded status.
- `production_gate_core` on a deliberately-black fixture graph (e.g. `noise` operator with brightness param at 0) — confirm `sustained_black` finding + degraded status.
- Normal demo graphs do not trip silence/black (the existing audio-output silence check in `test_demo_graphs.cpp` already enforces that the graph isn't silent at the end).
- Live `curl get_runtime_health` after running a real audio-producing graph: `audio.silence_active: false`.

---

## Cross-cutting design notes

**Schema versioning.** 8a bumps `SCHEMA_VERSION` from 3 → 4 once. 8b and 8c are additive (new optional fields). Consumers should switch on `schema_version` and treat unknown fields as additive. Document in `production-gate.md`.

**Read paths from `runtime_health::collect()`.** Today it takes:
```cpp
collect(graph, core, registry, audio, gpu)
```
8a adds `package_catalog` (nullable). 8b adds `mcp_main_ping_ms`, `mcp_opdev_ping_ms`, `now_ms`. After 8b the signature is:
```cpp
collect(graph, core, registry, audio, gpu, package_catalog,
        mcp_main_ping_ms, mcp_opdev_ping_ms, now_ms)
```
8c adds nothing new — it reads from `core.samplers()`.

**Cost.** Per-frame `collect_summary()` cost grows by:
- 8a: O(1) — last_reload field reads + package summary call (cached internally).
- 8b: O(audio_nodes × channels) for peak max (≤ ~80 ops on a heavy graph).
- 8c: O(window_size) for silence/black reductions (300 ops worst case).

Total: still well under 100µs per snapshot. No allocation in the hot path beyond what already happens.

**Out of scope (deferred to Phase 9 or later):**
- Per-node clipping attribution (graph-level only in 8b).
- Reset semantics for "this graph has been silent since the last variation switch."
- Trend storage / dashboards.
- Configurable budget thresholds in TOML (today's switch is hardcoded numeric thresholds; making them TOML-configurable is a separate refactor).

## What I will do on approval

1. Move this plan to `docs/plans/production-gate-phase8.md`.
2. Start with **Phase 8a**:
   - Add the `RuntimeCore::set_last_reload` API + `populate_minimal` reads.
   - Thread `PackageCatalog*` through `collect()` + handlers.
   - Add the three severity rules + budgets.
   - Bump schema 3→4.
   - Pytest + production_gate_core + live `curl`.
   - Stop and report.
3. **Phase 8b** as a separate commit on the same branch:
   - Wire MCP ping params + audio clipping aggregation.
   - Add the three severity rules + two budgets.
   - Verify.
4. **Phase 8c** as the final commit:
   - `RuntimeHealthSamplers` + `RuntimeCore::sample_runtime_health()`.
   - Sustained silence/black rules + uncomment budgets.
   - test_demo_graphs analysis-enable wiring.
   - Verify on synthetic silent/black fixtures.
5. Each sub-phase ends with a status report so you can pause if priorities shift.
