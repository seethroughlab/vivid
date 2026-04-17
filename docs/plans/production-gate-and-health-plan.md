# Production Gate and Runtime Health Plan

**Status: implemented as Phases 1–9c (Apr 2026).** User-facing reference: [`docs/testing/production-gate.md`](../testing/production-gate.md). Per-phase implementation plans: [phase 1](production-gate-phase1.md), [2](production-gate-phase2.md), [3](production-gate-phase3.md), [4](production-gate-phase4.md), [5](production-gate-phase5.md), [6+7](production-gate-followups.md), [8](production-gate-phase8.md), [8b](production-gate-phase8b.md), [8c](production-gate-phase8c.md), [9](production-gate-phase9.md), [9b](production-gate-phase9b.md), [9c](production-gate-phase9c.md). Follow-up planning index: [production-gate-followups.md](production-gate-followups.md).

### Deviations and resolutions

- **`expected_output` graph metadata → `meta.domains` reuse.** Phase 4/5 chose to derive expected output from the existing `domains` taxonomy (`audio` / `gpu` / `control` / `av`) instead of introducing a new graph-side metadata key. No demo-graph edits required.
- **Sustained-silence and sustained-black detection** — initially deferred; **closed in Phase 8c** with `RuntimeHealthSamplers` (sliding-window probes on `RuntimeCore`) and the previously-commented budget entries activated.
- **Hot-reload failure caching** — initially deferred; **closed in Phase 8a** via `RuntimeCore::last_reload_` plus `no_hot_reload_failures` / `no_required_operator_reload_failures` budgets.
- **Package/core version mismatch detection** — initially deferred; **closed in Phase 8a** via `PackageCatalog::summarize_updates(...)` plumbing and the `no_package_version_mismatches` budget.
- **Peak/RMS clipping** — initially deferred; **closed in Phase 8b** via `AnalysisSnapshot::peak` exposure and the `no_audio_clipping` budget.
- **Control server / MCP liveness in `runtime_health`** — initially deferred; **closed in Phase 8b** via `McpStatus` (built from `ControlServer::mcp_last_ping_ms()`) and the `mcp_servers_connected` budget.

### Beyond the original plan

The audit-driven follow-ups (Phases 6–9c, tracked in `production-gate-followups.md`) added items not in the original proposal:

- **CTest `LastTest.log` fallback for unknown-classified failures** (Phase 7).
- **Parallel demo-graph execution** with bounded worker pool (Phase 9a) — `production_gate_core` from ~95s → ~33s.
- **PR-only workflow** (`production-gate-pr.yml`) with single-comment upsert and inline `::warning::`/`::error::` annotations (Phase 9b).
- **Trend tool** (`tools/show_recent_gate_runs.py`) over per-commit-named CI artifacts (Phase 9c).
- **Documented runtime budget**: `_core` ≤ 60s, `_gui` ≤ 180s (Phase 9c).

### What was originally a plan

What follows is the original proposal — preserved as written so the rationale and intent are still readable. See the per-phase plans for what actually shipped.

---

This consolidates existing release-hardening work into a first-class production readiness workflow.

## Goal

Give Vivid one obvious answer to the question: "Is this build and graph safe to put in front of people?"

The repo already has strong pieces: headless smoke tests, GUI smoke tests, stress lanes, soak tests, movie playback go/no-go docs, release workflows, and beta readiness checklists. The production gap is that these pieces are spread across docs, labels, workflows, and manual judgment.

This plan adds a unified production gate plus runtime health reporting that works for both release engineering and live installations.

## Proposal

Add two related capabilities:

- A `production_gate` build/test target that runs the release-critical automated checks and emits one machine-readable report.
- A runtime health model that reports whether the current live session is safe, degraded, or failing.

The target outcome is that a developer can run one command before release, and a performer/operator can see one in-app health status during a show.

## Production Gate

Add a top-level CMake target:

```bash
cmake --build build --target production_gate
```

The target should run the existing release-critical tests in a deliberate order:

- full non-GUI CTest baseline
- `HEADLESS_SMOKE`
- `UI_SMOKE`
- `test_demo_graphs`
- package smoke tests where local package fixtures are available
- movie playback go/no-go automated subset
- `phase6_stress`
- optional `phase6_soak`

Because some tests require a display or external packages, split the gate into profiles:

- `production_gate_core`: headless and deterministic tests only
- `production_gate_gui`: adds `GUI_SMOKE`
- `production_gate_env`: adds package/environment-sensitive `GUI_ENV`
- `production_gate_soak`: adds long-running soak

The default `production_gate` should run `production_gate_core`. Release workflows can opt into the heavier profiles.

## Machine-Readable Report

Emit a JSON report under the build directory:

```text
build/reports/production-gate.json
```

Suggested fields:

- timestamp, branch, commit, build type, macOS version, hardware summary
- tests run, passed, failed, skipped
- skipped reasons grouped by environment requirement
- WebGPU validation errors detected
- audio-device or callback failures detected
- graph load failures
- missing operator/package failures
- stress and soak duration
- final status: `pass`, `degraded`, or `fail`

This report should be generated by a small script or C++ test utility rather than by asking CMake/CTest to do too much. CMake should orchestrate; the report tool should classify.

## Runtime Health Model

Add a compact health snapshot owned by `RuntimeCore` or a nearby diagnostics module.

The health snapshot should include:

- graph compile status
- dropped/rejected connections
- missing or disabled operators
- package/core version mismatches
- hot-reload failures
- audio device status
- audio callback overrun or underrun counts
- peak/RMS clipping indicators
- GPU device status and recent validation errors where available
- output health hints such as sustained black frame or sustained silence when analysis is enabled
- control server/MCP liveness where relevant

Expose this through:

- control server endpoint: `get_runtime_health`
- MCP tool mapping
- diagnostics panel in the UI
- production gate report when a graph is launched for validation

Health status should use stable severity levels:

- `ok`: no known production blockers
- `warning`: degraded but running
- `error`: user-visible failure or missing required dependency
- `fatal`: app cannot safely run the graph

## Health Budgets

Production readiness needs explicit budgets, not only pass/fail tests.

Initial defaults:

- no graph load failures
- no missing core operators
- no WebGPU validation errors
- no audio device initialization failure
- no sustained audio callback overruns
- no sustained output silence for graphs marked as audio/A-V demos
- no sustained black output for graphs marked as visual/A-V demos
- no crashes or hangs during stress lanes

Graph-specific expected-output checks should use existing metadata where possible. If a graph does not declare expected audio or visual output, the gate should avoid guessing and classify that portion as `not_applicable` rather than failing.

## Implementation Steps

1. Add a report utility that can run or consume CTest results and produce `production-gate.json`.
2. Add CMake targets for `production_gate_core`, `production_gate_gui`, `production_gate_env`, `production_gate_soak`, and `production_gate`.
3. Add `RuntimeHealthSnapshot` and populate it from existing diagnostics, graph errors, build console state, audio engine state, and output analyzer signals.
4. Expose `get_runtime_health` through the control server and MCP bridge.
5. Add a concise health indicator to the diagnostics panel.
6. Update release docs to point at `production_gate` as the canonical automated gate.

Keep this incremental. The first version can classify existing tests and diagnostics before adding new probes.

## Testing

Add tests for:

- production gate report generation from passing, failing, and skipped test inputs
- stable classification of missing package, graph load failure, and WebGPU error signatures
- `RuntimeHealthSnapshot` serialization
- control server `get_runtime_health` response shape
- health status transitions for graph compile failure, dropped connection, hot reload failure, and audio device failure

Verification should include:

```bash
cmake --build build --target production_gate_core
ctest --test-dir build --output-on-failure -R "runtime_health|production_gate"
```

Run heavier profiles before release:

```bash
cmake --build build --target production_gate_gui
cmake --build build --target production_gate_soak
```

## Acceptance Criteria

- `production_gate_core` gives a single automated go/no-go result for deterministic release checks.
- The gate emits a machine-readable report with stable classifications.
- Runtime health is available through UI, control server, and MCP.
- Existing smoke, stress, and go/no-go lanes remain usable independently.
- Release docs treat the production gate as the canonical automated baseline.

