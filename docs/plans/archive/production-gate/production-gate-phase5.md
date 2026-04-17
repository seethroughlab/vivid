# Plan: Production Gate — Phase 5 (health budgets and gate-report integration)

## Context

Phases 1–4 produced a runnable gate, a JSON report, an aggregated runtime-health snapshot, and live exposure of that snapshot through every client surface. Phase 5 closes the loop: the production-gate report stops reflecting only ctest exit codes and starts reflecting *runtime health*. A demo graph that loads but renders silence (when it's supposed to play sound) or fails to compile a shader will show up as a `degraded` or `fail` status with a stable, classified breach — even if every test technically passed.

Two pieces glue the existing snapshot to the report:

1. **Per-graph health dumps**: `test_demo_graphs` writes `build/reports/health/<graph>.json` for every graph it loads, capturing the runtime_health snapshot at the end of that graph's tick window plus the graph's `domains` metadata.
2. **Budget evaluator in the report tool**: `tools/production_gate_report.py` reads `tools/production_gate_budgets.toml`, glob-loads the per-graph health JSONs, and evaluates each budget against the matching graphs. Breaches populate the `signals` block and bump `status`.

Phase 5 adds zero new runtime probes. It only consumes signals that `runtime_health::collect()` already emits, plus the existing graph `domains` array.

Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`. Phases 1–4 changes remain uncommitted alongside Phase 5.

## What changed vs. the parent plan's Phase 5 sketch

The parent plan called for a new graph-side metadata key `expected_output: {"audio": true, "visual": true}`. Exploration found graphs already carry a `meta.domains` array (`"audio"`, `"gpu"`, `"control"`, `"av"`) used today for example browsing and content classification. Reusing it is cleaner than introducing a parallel field — the user has already curated this taxonomy. Phase 5 derives expected output from `domains`:

- `domains` contains `"audio"` or `"av"` → graph is expected to make sound
- `domains` contains `"gpu"` or `"av"` → graph is expected to render visuals
- otherwise → not_applicable

No new metadata field, no demo-graph edits.

The parent plan's §Health Budgets list also mentioned "no sustained output silence" and "no sustained black output" budgets. Those require new probes that don't exist today — `OutputAnalyzer` is a stateless utility, not a continuous monitor (per Phase 3 exploration). Phase 5 ships the **budget framework** and the existing-signal budgets; sustained-silence/sustained-black are flagged as "future probe" entries in `production_gate_budgets.toml` (commented out, pending probe). When the probes land, they slot into the framework with no code changes.

## Decisions locked in (with rationale)

- **Reuse `meta.domains` instead of a new `expected_output` field.** Same classification, no graph edits, no parallel taxonomy.
- **Per-graph health JSON written from the child process to disk**, not stdout. CTest truncates `<system-out>` at 1KB; writing files sidesteps the issue. Child echoes only a one-liner: `[health] wrote build/reports/health/<graph>.json`.
- **Budgets are TOML.** Python 3.11+ `tomllib` is stdlib (the gate's self-test runs `uv run --with pytest python -m pytest`; the existing report tool is stdlib-only and the new TOML parser stays so).
- **`--health-dir` flag** (new) accepts a directory; the report tool globs `*.json` from it. The placeholder `--health-json` from Phase 2 stays for one-off single-file testing but is no longer the primary path.
- **Each gate target chains `--health-dir build/reports/health`** in its bash wrapper, mirroring how `--junit` is plumbed today.
- **Status escalation**: a breach with `severity_on_breach=warning` flips `pass`→`degraded`; `error` or `fatal` flips to `fail`. Test failures continue to flip to `fail` independently (existing behavior).
- **Defer silence/black detection** to a follow-up phase. Document the gap.

## Scope

In scope:
- Add `runtime_health` JSON dump from `tests/integration/test_demo_graphs.cpp` after each graph's tick window. Path: `build/reports/health/<graph_basename>.json`. Includes a `graph_meta.domains` block so the report tool can apply per-graph budgets.
- Augment the per-graph dump with a `passed`/`failed`/`skipped` field reflecting what the test concluded, so the report tool can correlate health signals with test outcomes.
- New `tools/production_gate_budgets.toml` with the initial six budget entries (see below) and commented-out future-probe entries.
- New `--health-dir` flag in `tools/production_gate_report.py`. Loads every `*.json` under the dir, evaluates budgets, populates `signals.budget_breaches[]`, and adjusts `status` accordingly.
- Update `cmake/tests/90-production-gate.cmake` to clear `build/reports/health/` at the start of each gate run and chain `--health-dir build/reports/health` into the report-tool invocation in all four profile targets.
- Extend `tests/cli/test_production_gate_report.py` with golden-fixture tests for budget evaluation: clean run → pass; warning breach → degraded; error breach → fail; non-applicable graphs ignored.
- New `docs/testing/production-gate.md` covering: how to run, profile semantics, where the report lands, how to interpret status/signals, how to add or relax a budget.
- Update `docs/plans/beta-release-readiness/phase-1-inventory-and-gates.md` to point at `production_gate` as the canonical automated baseline.

Out of scope (deferred):
- Sustained-silence and sustained-black detection probes. Document as known gap in `production-gate.md`. Add commented-out budget entries so the wiring is obvious when the probes land.
- Trend dashboards, historical regression detection, PR-comment posting (per parent plan).
- New runtime-side health probes.

## Files

New:
- `tools/production_gate_budgets.toml`
- `docs/testing/production-gate.md`

Modified:
- `tests/integration/test_demo_graphs.cpp` — per-graph health dump.
- `tools/production_gate_report.py` — budget evaluation, `--health-dir` flag, schema additions.
- `tests/cli/test_production_gate_report.py` — budget-eval test cases + new fixture JSONs.
- `tests/cli/fixtures/health/*.json` — new fixtures (clean, audio_underrun, missing_op, etc.).
- `cmake/tests/90-production-gate.cmake` — clear `build/reports/health/` pre-run; pass `--health-dir`.
- `docs/plans/beta-release-readiness/phase-1-inventory-and-gates.md` — reference `production_gate`.

## Per-graph health JSON schema

Written by `test_demo_graphs.cpp` for each graph (after the tick window):

```json
{
  "schema_version": 1,
  "graph": "showcase_demo",
  "graph_path": "intro/showcase_demo.json",
  "domains": ["gpu", "audio", "control", "av"],
  "test_outcome": "passed" | "failed" | "skipped",
  "health": { /* runtime_health::to_json(snapshot) output */ }
}
```

Filename: `<basename>.json` derived from `filesystem::path(graph_path).stem()`. Collisions across subdirectories are not expected today (all demo basenames are unique).

## Budget format (`tools/production_gate_budgets.toml`)

```toml
# Production-gate budgets. Each budget is evaluated against per-graph health
# JSONs in build/reports/health/*.json. A breach contributes to
# signals.budget_breaches and may flip the report's overall status.
#
# applies_to: comma-separated list of graph domains the budget applies to,
#             or "*" for every graph. Match is "any-of" against meta.domains.
# severity_on_breach: "warning" → degraded; "error"/"fatal" → fail.

[[budget]]
code = "no_graph_load_failures"
applies_to = "*"
severity_on_breach = "error"
description = "Graph must load without compile failures."
# breach when: health.graph.compiled_nodes < health.graph.declared_nodes
#              OR health.graph.errored_nodes > 0

[[budget]]
code = "no_missing_core_operators"
applies_to = "*"
severity_on_breach = "error"
description = "All operators referenced by the graph must be loadable."
# breach when: health.graph.missing_operators > 0

[[budget]]
code = "no_dropped_connections"
applies_to = "*"
severity_on_breach = "warning"
description = "All connections must compile (no port-type mismatches)."
# breach when: health.graph.dropped_connections > 0

[[budget]]
code = "no_audio_init_failure"
applies_to = "audio,av"
severity_on_breach = "error"
description = "Audio engine must be running for graphs that produce sound."
# breach when: !health.audio.running AND health.graph.audio_nodes > 0

[[budget]]
code = "no_audio_underruns"
applies_to = "audio,av"
severity_on_breach = "warning"
description = "Audio callbacks must keep up during the gate's tick window."
# breach when: health.audio.xruns > 0

[[budget]]
code = "no_shader_errors"
applies_to = "gpu,av"
severity_on_breach = "warning"
description = "GPU operators must compile their WGSL shaders cleanly."
# breach when: health.gpu.shader_errors > 0

# ---------------------------------------------------------------------------
# Future-probe budgets (commented out; activate when sustained-silence /
# sustained-black detection lands in runtime_health::collect()).
# ---------------------------------------------------------------------------

# [[budget]]
# code = "no_sustained_silence"
# applies_to = "audio,av"
# severity_on_breach = "warning"
# description = "Audio-producing graphs must not output sustained silence."

# [[budget]]
# code = "no_sustained_black"
# applies_to = "gpu,av"
# severity_on_breach = "warning"
# description = "Visual-producing graphs must not output sustained black frames."
```

## Report-tool changes

Add to `tools/production_gate_report.py`:

```python
def load_budgets(toml_path: Path) -> list[dict]:
    """Parse production_gate_budgets.toml. Returns a list of budget dicts."""
    if not toml_path.exists():
        return []
    import tomllib
    with toml_path.open("rb") as f:
        data = tomllib.load(f)
    return data.get("budget", [])

def evaluate_budgets(budgets: list[dict], health_files: list[Path]) -> list[dict]:
    """Returns a list of breaches: [{budget_code, graph, severity, message}, ...]."""
    breaches = []
    for hf in sorted(health_files):
        try:
            doc = json.loads(hf.read_text())
        except (OSError, json.JSONDecodeError):
            continue
        graph_name = doc.get("graph", hf.stem)
        domains = set(doc.get("domains", []))
        health = doc.get("health", {})
        for b in budgets:
            applies_to = b.get("applies_to", "*")
            if applies_to != "*":
                if not (set(applies_to.split(",")) & domains):
                    continue
            breach_msg = check_budget(b["code"], health)
            if breach_msg:
                breaches.append({
                    "budget_code": b["code"],
                    "graph": graph_name,
                    "severity": b.get("severity_on_breach", "warning"),
                    "message": breach_msg,
                })
    return breaches

def check_budget(code: str, health: dict) -> str | None:
    """Return a breach message if this budget is breached, else None."""
    g = health.get("graph", {})
    a = health.get("audio", {})
    gpu = health.get("gpu", {})
    if code == "no_graph_load_failures":
        if g.get("errored_nodes", 0) > 0 or g.get("compiled_nodes", 0) < g.get("declared_nodes", 0):
            return f"{g.get('errored_nodes', 0)} errored, " \
                   f"{g.get('declared_nodes', 0) - g.get('compiled_nodes', 0)} uncompiled"
    elif code == "no_missing_core_operators":
        if g.get("missing_operators", 0) > 0:
            return f"missing: {','.join(g.get('missing_operator_types', []))}"
    elif code == "no_dropped_connections":
        if g.get("dropped_connections", 0) > 0:
            return f"{g.get('dropped_connections', 0)} dropped connection(s)"
    elif code == "no_audio_init_failure":
        if not a.get("running", False) and g.get("audio_nodes", 0) > 0:
            return "audio engine not running"
    elif code == "no_audio_underruns":
        if a.get("xruns", 0) > 0:
            return f"{a['xruns']} underruns"
    elif code == "no_shader_errors":
        if gpu.get("shader_errors", 0) > 0:
            return f"{gpu['shader_errors']} shader error(s)"
    return None
```

Schema additions to the report:

```json
{
  ...
  "signals": {
    /* existing fields preserved */
    "budget_breaches": [
      {"budget_code": "no_audio_underruns", "graph": "synth_demo",
       "severity": "warning", "message": "3 underruns"}
    ]
  },
  "status": "pass" | "degraded" | "fail"
}
```

Status logic (replaces today's simple `failed > 0 → fail`):

1. If any test failed → `fail`.
2. Else if any budget breach with severity `error` or `fatal` → `fail`.
3. Else if any budget breach with severity `warning` → `degraded`.
4. Else → `pass`.

`schema_version` bumps to **2** (from 1) — back-compat note: status values unchanged; new field `budget_breaches` is additive.

## CMake integration

In `cmake/tests/90-production-gate.cmake`, each profile target's bash wrapper gains:
- A `rm -rf build/reports/health` step before ctest (fresh per run).
- `--health-dir '${_pg_reports_dir}/health'` in the report-tool invocation.

Sketch (core profile):
```cmake
COMMAND bash -c "rm -rf '${_pg_reports_dir}/health'; \
    ${CMAKE_CTEST_COMMAND} -L '^(HEADLESS_SMOKE|UI_SMOKE|PACKAGE)$' --output-on-failure \
        --output-junit '${_pg_reports_dir}/ctest-core.xml'; \
    ec=$?; \
    python3 '${_pg_report_tool}' --profile core \
        --junit '${_pg_reports_dir}/ctest-core.xml' \
        --health-dir '${_pg_reports_dir}/health' \
        --budgets '${CMAKE_SOURCE_DIR}/tools/production_gate_budgets.toml' \
        --output '${_pg_report_json}' \
        --repo-root '${CMAKE_SOURCE_DIR}' --git-meta-from-git \
        --build-type '${CMAKE_BUILD_TYPE}'; \
    exit $ec"
```

## Test design

`tests/cli/test_production_gate_report.py` gains:

1. **Budget evaluation: clean run** — feed one passing health JSON, expect `status: pass`, no breaches.
2. **Audio underrun on audio graph → degraded** — health JSON with `audio.xruns: 3` and `domains: ["audio"]`, expect `status: degraded`, breach in `signals.budget_breaches`.
3. **Missing operator → fail** — health JSON with `graph.missing_operators: 1`, expect `status: fail`.
4. **Domain filter applies** — gpu shader_error in an `audio`-domain graph: budget doesn't fire (no breach).
5. **No domains → not_applicable** — health JSON with empty `domains[]`: only `*`-applicable budgets fire.
6. **Test failure overrides clean budgets** — feed JUnit with one failure + clean health, expect `status: fail`.

Fixture files: `tests/cli/fixtures/health/{clean.json, audio_underrun.json, missing_op.json, gpu_only_shader_err.json, no_domains.json}` and `tests/cli/fixtures/budgets/{default.toml, single_budget.toml}`.

## Verification

Local (in worktree):
```bash
# Self-tests for the report tool's budget evaluator
uv run --with pytest pytest tests/cli/test_production_gate_report.py -v

# Build + run the gate, inspect the per-graph dumps and final report
cmake --build build --target production_gate_core -j$(sysctl -n hw.logicalcpu)
ls build/reports/health/ | head
cat build/reports/production-gate.json | python3 -m json.tool | grep -E '"status"|"budget_breaches"'

# Negative test: lower a budget threshold to force a breach
# (e.g. set audio_underruns severity to error in a copy of the TOML, re-run)
```

Negative test:
- Add `"audio.xruns": 5` to a synthetic per-graph health JSON before running the report tool, confirm `status: degraded` and one breach entry.
- Set `severity_on_breach = "error"` in the TOML for that budget, re-run, confirm `status: fail`.

CI:
- Smoke workflow already uploads `build/reports/ctest-*.xml`. Add `build/reports/health/*.json` to the artifact glob so a degraded run leaves enough evidence to debug.

## Risks

1. **Per-graph health JSONs accumulate per gate run.** Mitigation: clear `build/reports/health/` at start of each gate target.
2. **Domain filter sensitivity** — graphs missing `domains` only ever trip `*`-applicable budgets. Acceptable: fewer false positives. Document in `production-gate.md`.
3. **TOML parsing requires Python ≥3.11.** CI macOS image already has Python 3.12; document the floor in `production-gate.md`.
4. **Schema version bump from 1 → 2.** Consumers (none today besides our own tests) must handle the new `budget_breaches` field. The fixture set covers both versions.
5. **Silence/black gap is honest, not hidden** — the TOML carries commented-out entries naming the absent probes so the gap is discoverable.

## What I will do on approval

1. Extend `Graph::load_from_string` (or wherever `meta.domains` is parsed) — actually no edits needed; `domains` is already loaded. The test consumes it from the `Graph` object.
2. Edit `tests/integration/test_demo_graphs.cpp` to write per-graph health JSON dumps after the tick window. Echo a one-liner; ensure `build/reports/health/` exists.
3. Create `tools/production_gate_budgets.toml`.
4. Extend `tools/production_gate_report.py` with `--health-dir`, `--budgets`, the budget evaluator, and the `signals.budget_breaches` field. Bump schema to 2.
5. Create `tests/cli/fixtures/health/*.json` and `tests/cli/fixtures/budgets/*.toml`. Extend the pytest suite.
6. Update `cmake/tests/90-production-gate.cmake` — clear health dir + pass new flags in all four profile targets.
7. Write `docs/testing/production-gate.md`.
8. Edit `docs/plans/beta-release-readiness/phase-1-inventory-and-gates.md` to point at `production_gate`.
9. Run pytest, run `production_gate_core`, confirm `production-gate.json` shows `status: pass` and the new `budget_breaches: []` array on a clean run.
10. Force a synthetic breach to verify the degraded/fail path.
