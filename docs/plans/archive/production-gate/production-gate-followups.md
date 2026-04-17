# Plan: Production Gate — Follow-up Phases (6 → 9)

## Context

Phases 1–5 of `docs/plans/production-gate-and-health-plan.md` shipped a working production gate, runtime health snapshot, MCP/UI surfaces, and budget-driven status. The audit (in chat) identified a clear set of gaps and code-quality issues that fall into four coherent buckets. This plan formalizes those buckets as Phases 6–9 so they can be picked up incrementally.

The work splits along a natural pre-merge / post-merge line:

- **Phases 6 and 7** are small enough to land before the production-gate branch merges. They make the public schema honest, get the parent plan in sync with reality, and clean up the parts of the implementation that surprised us during integration (CMake bash escaping, duplicated collection logic).
- **Phases 8 and 9** are independent follow-up branches. Phase 8 closes the runtime-health probe gaps the parent plan promised but Phases 3–5 deferred (hot-reload tracking, version mismatches, audio clipping, MCP liveness, sustained silence/black). Phase 9 makes the gate fast enough for per-PR CI and surfaces its verdict in GitHub artifacts.

Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`. Phases 1–5 changes remain uncommitted alongside Phase 6.

## Phase 6 — Pre-merge polish

**Goal:** ship a `production-gate.json` schema that doesn't lie, and align the parent plan with what was actually built.

### Scope
- **Drop or populate dead schema fields** in `production-gate.json` and `runtime_health::to_json`:
  - `tests.skipped_reasons` — populate by parsing skipped JUnit cases against known patterns (`requires_gui`, `requires_package`, `requires_external_io`), or drop the key.
  - `stress.phase6_stress_seconds` — populate from the JUnit `<testcase time>` of the four `phase6_stress` tests when present.
  - `stress.phase6_soak_seconds` — drop the field; soak runs as a standalone executable and isn't captured in JUnit. Document in `production-gate.md`.
  - `vivid_version` — populate from a build-time constant (add `VIVID_VERSION` to `cmake/app.cmake` if absent), or drop until one exists.
- **Extract CMake bash chain** into `scripts/run_production_gate_profile.sh`. Each profile target shrinks to one `COMMAND`. Removes a class of escaping bugs (we hit two during implementation) and makes the pipeline diff-friendly.
- **Rationalize the diagnostics-panel header.** Today shows the new health pill on the left AND a redundant "Stable/Attention/Partial" colored dot+text on the right. Repurpose the right-side dot for MCP-only state with an explicit `MCP` label so the two channels are obviously distinct.
- **CI artifact uploads.** Add `build/reports/production-gate.json` and `build/reports/health/*.json` to both `.github/workflows/smoke.yml` and `.github/workflows/gui-env.yml` artifact globs. A degraded run today leaves no GitHub Actions breadcrumb.
- **Update the parent plan** (`docs/plans/production-gate-and-health-plan.md`):
  - Add a `Status: Implemented` block with links to the 5 phase plans + `docs/testing/production-gate.md`.
  - Note the deferrals (silence/black probes; hot-reload caching; package version mismatch; peak/RMS clipping; MCP liveness in runtime_health).
  - Note the `expected_output` → `domains` substitution.

### Critical files
- `tools/production_gate_report.py` — drop/populate stress + skipped_reasons; bump SCHEMA_VERSION to 3.
- `src/runtime/core/runtime_health.{h,cpp}` — populate or drop `vivid_version`.
- `cmake/tests/90-production-gate.cmake` — replace inline bash with script invocation.
- `scripts/run_production_gate_profile.sh` (new) — accept profile name + paths as args; encode the bash chain.
- `src/ui/graph/node_graph_draw_overlays.cpp` — relabel right-side dot, drop overlap.
- `.github/workflows/{smoke,gui-env}.yml` — extend artifact globs.
- `docs/plans/production-gate-and-health-plan.md` — status block + deviations.

### Existing utilities to reuse
- `tools/production_gate_report.py:_check_budget` — pattern for switching on a code key. Mirror for skipped-reason classification.
- `cmake/tests/90-production-gate.cmake` `production_gate_pretest` target — already a clean shell-script invocation; mirror for the new wrapper.

### Verification
- `uv run --with pytest pytest tests/cli/test_production_gate_report.py -v` — all green incl. new cases for `skipped_reasons` and `stress` population.
- `cmake --build build --target production_gate_core` — same pass result; JSON has populated stress + skipped_reasons (or fields gone).
- Manual: open the editor, confirm panel header doesn't double up status indicators.
- CI dry-run: trigger smoke.yml on a draft PR; confirm `production-gate.json` artifact appears.

### Out of scope
- New runtime probes (Phase 8).
- DRY of `collect`/`collect_summary` (Phase 7).

---

## Phase 7 — Code quality & test depth

**Goal:** make the code easier to keep correct as Phase 8 adds more probes.

### Scope
- **DRY `runtime_health::collect()` and `collect_summary()`.** Extract `populate_minimal(snap, graph, core, audio, gpu)` shared function. `collect()` calls it then adds the per-node top-N aggregation. Roughly halves the file's per-field plumbing.
- **Per-graph health JSON path robustness.** `tests/integration/test_demo_graphs.cpp` accepts `--health-dir <path>`; defaults to `<argv[1]>/../reports/health` so dev `./build/test_demo_graphs build/graphs` writes to the same place as the gate. Document in `docs/testing/production-gate.md`.
- **Filename collision protection.** Replace `<basename>.json` with `<relative_path_with_slashes_to_underscores>.json` in `write_health_dump()`. Today no collision exists; this hardens against future demo additions.
- **CTest `LastTest.log` fallback for classification.** Report tool accepts `--ctest-log-dir <path>`. When a failure's truncated `<system-out>` classifies as `unknown`, search `LastTest.log` for the test's full output and re-classify. Mitigates the documented 1KB truncation.
- **UI pill test.** Extend `tests/ui/test_ui_overlay_interactions.cpp` (already uses `#define private public`). Set `snap.runtime_health.overall = Severity::Warning` then `Severity::Fatal`, draw, assert via a tiny `RecordingRenderer2D` mock that the expected text was emitted. Smaller alternative: state-only assertion that the pill's text source is `severity_name(snap_.runtime_health.overall)`.

### Critical files
- `src/runtime/core/runtime_health.cpp` — extract `populate_minimal()`.
- `tests/integration/test_demo_graphs.cpp` — `--health-dir` flag, collision-safe paths.
- `tools/production_gate_report.py` — `--ctest-log-dir` flag, fallback classifier.
- `tests/ui/test_ui_overlay_interactions.cpp` — pill assertion.
- `tests/ui/recording_renderer.h` (new, optional) — small in-memory render capture.

### Existing utilities to reuse
- `tests/test_helpers.h:check()` and `check_float()` — assertion macros for the pill test.
- `runtime_health::apply_severity_rules()` — already public so a recording renderer can drive a snapshot through it.

### Verification
- `ctest --test-dir build -R 'test_runtime_health_snapshot|test_ui_overlay_interactions'` green.
- Force a `unknown`-classifying failure log shorter than 1KB but with full text in `LastTest.log` — confirm reclassification.
- Run `./build/test_demo_graphs build/graphs` from worktree root — confirm health JSONs land under `build/reports/health/`.

### Out of scope
- New probes (Phase 8).
- Moving away from `tests/ui/`'s `#define private public` pattern (existing convention).

---

## Phase 8 — Deferred runtime probes (split into 8a / 8b / 8c)

**Goal:** close the parent plan's runtime-health gaps so the snapshot covers everything the plan promised.

Each sub-phase is independently shippable.

### Phase 8a — Hot reload + version mismatches (low risk)

- **Hot-reload failure cache on `RuntimeCore`.** New `optional<ReloadResult> last_reload_` field; setter called by `main.cpp` after `HotReloader::poll_ready()`. `runtime_health::collect()` reads it; finding `hot_reload_failed` (Warning for non-required op, Error for required).
- **Package/core version mismatches.** Read from `PackageCatalog::CatalogUpdateSummary::incompatible_updates`; finding `package_version_mismatch` (Warning).
- New budgets in `tools/production_gate_budgets.toml`: `no_hot_reload_failures` (warning), `no_package_version_mismatches` (warning).
- New fields in `RuntimeHealthSnapshot`: `hot_reload.last_failed_target`, `packages.incompatible_count`.
- Tests: snapshot transition cases for both.

### Phase 8b — Audio clipping + MCP liveness (small)

- **Audio peak/clipping.** Wire existing audio peak data (`AnalysisSnapshot::peak`) into `runtime_health.audio` as `peak_max` and `clipping_count` (count of frames > 0.99). Finding `audio_clipping` (Warning).
- **Control server / MCP liveness.** Extend `runtime_health` with `mcp_main_connected` / `mcp_opdev_connected` from `ControlServer::mcp_last_ping_ms()`. Findings: `mcp_main_disconnected` / `mcp_opdev_disconnected` (Warning).
- New budgets: `no_audio_clipping`, optional `mcp_servers_connected`.
- Tests: snapshot transitions; budget evaluation fixtures.

### Phase 8c — Sustained silence / black detection (largest)

- New stateful probe in `runtime_health::collect()`. Tracks rolling-window audio peak + frame brightness samples. State stored on `RuntimeCore` (small fixed-size circular buffers; not allocated when analysis is off).
- New fields: `audio.silence_window_seconds`, `audio.silence_active`, `gpu.black_window_seconds`, `gpu.black_active`.
- Findings: `sustained_silence` / `sustained_black` (Warning, applies_to filtered by `domains`).
- Uncomment the corresponding budget entries in `tools/production_gate_budgets.toml`.
- Wire `tests/integration/test_demo_graphs.cpp` to call `set_analysis_enabled(true)` on the audio engine and FrameExecutor for graphs in `audio` / `gpu` / `av` domains.
- Tests: snapshot transitions; gate breach test on a known-silent fixture graph.

### Critical files (across 8a–8c)
- `src/runtime/core/runtime_core.{h,cpp}` — last-reload cache, sliding-window state.
- `src/runtime/core/runtime_health.{h,cpp}` — new fields + findings + rules.
- `src/runtime/audio/audio_engine.{h,cpp}` — peak surface (8b).
- `src/runtime/control/control_server.{h,cpp}` — accessors for mcp_last_ping_ms (8b).
- `src/runtime/packages/package_catalog.h` — accessor for incompatible_updates (8a).
- `tests/integration/test_demo_graphs.cpp` — analysis enable + budget exercise.
- `tools/production_gate_budgets.toml` — new + uncommented budgets.
- `tools/production_gate_report.py` — extend `_check_budget` switch.
- `mcp/vivid_mcp.py` — docstring update for new fields.

### Existing utilities to reuse
- `runtime_health::apply_severity_rules` — extend, don't duplicate.
- `tests/control/test_runtime_health_snapshot.cpp` — pattern for severity-rollup tests.
- `OutputAnalyzer` (`src/runtime/debug/output_analyzer.h`) — single-shot audio/visual analysis. Phase 8c wraps it in a sliding-window helper.

### Verification (per sub-phase)
- 8a: `cmake --build build --target test_runtime_health_snapshot` + new transition cases. Force a hot-reload failure (intentionally bad operator source); confirm finding fires.
- 8b: same, plus a synthetic clipping fixture in `tests/cli/fixtures/health/`.
- 8c: gate run on a deliberately-silent audio graph (e.g. zero-gain Mixer); confirm `sustained_silence` finding + `degraded` status.

### Out of scope
- Per-node clipping attribution (graph-level signal only in 8b).
- Reset semantics for "this graph has been silent since the last variation switch" — defer.

---

## Phase 9 — Performance & CI ergonomics

**Goal:** the gate is cheap enough for per-PR CI; degraded runs leave clear breadcrumbs.

### Scope
- **`test_demo_graphs` parallelization.** Currently sequential per-graph subprocess (~99s of the 134s gate). Add a `--jobs N` flag (default `nproc / 2` to leave headroom for audio/GPU contexts). Per-child output already goes to disk so no synchronization needed.
- **CI annotations.** A small post-step in `.github/workflows/smoke.yml` parses `production-gate.json` and posts a GitHub status check / PR comment with `status` + breach count + top failing budget.
- **Trend artifact (lightweight).** CI uploads `production-gate-{commit-sha}.json` to a long-lived artifact bucket. New `tools/show_recent_gate_runs.py` reads them and prints a status grid for the last N runs.
- **Document the gate runtime budget** in `docs/testing/production-gate.md` so future authors know to keep `production_gate_core` under N seconds.

### Critical files
- `tests/integration/test_demo_graphs.cpp` — parallel children.
- `.github/workflows/smoke.yml` — annotations + per-commit artifact name.
- `tools/show_recent_gate_runs.py` (new) — small utility.
- `docs/testing/production-gate.md` — runtime budget section.

### Verification
- `time cmake --build build --target production_gate_core` before/after — confirm ~50% reduction.
- Draft PR with a forced breach — confirm CI annotation posts.
- Run `tools/show_recent_gate_runs.py` against a small set of saved reports.

### Out of scope
- Full hosted dashboard.
- Historical regression detection beyond "show me last N runs."

---

## Sequencing & dependencies

```
Phase 6 (1-2 days)  ─── must land before merge ─── unblocks parent-plan honesty
Phase 7 (2-3 days)  ─── any time after 6 ───────── unblocks Phase 8 maintainability
Phase 8a (3-4 days) ─── after 7 ────────────────── independent
Phase 8b (2-3 days) ─── after 7 ────────────────── independent
Phase 8c (1 week)   ─── after 7 ────────────────── largest
Phase 9 (1 week)    ─── any time after 6 ───────── independent of 7/8
```

Phases 6 and 7 are pre-merge. Phases 8 and 9 are independent follow-up branches.

## What I will do on approval

1. Move this plan to `docs/plans/production-gate-followups.md` (alongside the other plan docs in the repo).
2. Start Phase 6 immediately:
   - Drop / populate the dead schema fields in `tools/production_gate_report.py` and `runtime_health.{h,cpp}`.
   - Extract the bash chain into `scripts/run_production_gate_profile.sh`.
   - Rationalize the diagnostics-panel header.
   - Update CI artifact uploads.
   - Update parent plan with status block + deviations.
3. Run pytest, run `production_gate_core`, confirm no regression.
4. Report back.
