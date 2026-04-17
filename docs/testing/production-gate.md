# Production Gate

The production gate is Vivid's one-command answer to "is this build safe to put in front of people?" It wraps the existing release-critical CTest lanes into tiered targets, runs them in a deliberate order, and emits a single machine-readable report at `build/reports/production-gate.json`.

## Quick start

```bash
# Day-to-day pre-commit check (fast, deterministic, no display required)
cmake --build build --target production_gate_core

# Before a release (adds GUI smoke; needs a window server)
cmake --build build --target production_gate_gui

# Before a release with external packages installed
cmake --build build --target production_gate_env

# Nightly / pre-release
cmake --build build --target production_gate_soak
```

The bare `production_gate` alias maps to `production_gate_core`.

## Profiles

Each profile is **cumulative** — `gui` runs everything `core` runs plus GUI smoke; `env` runs everything `gui` runs plus the external-package lane.

| Profile | Adds | Use when |
|---------|------|----------|
| `production_gate_core` | `HEADLESS_SMOKE`, `UI_SMOKE`, `PACKAGE` labels + `test_movie_seek_stress` + the report tool's own pytest | Local dev, per-push CI |
| `production_gate_gui`  | `GUI_SMOKE` (windowed editor flows) | Release smoke |
| `production_gate_env`  | `GUI_ENV` (external-package coverage) | Daily CI; release with external packages |
| `production_gate_soak` | `phase6_stress` + soak lane | Nightly; pre-release |

## Runtime budget

Per-PR feedback latency is the gate's first-class metric. Targets:

| Profile | Target wall time | Notes |
|---------|------------------|-------|
| `production_gate_core` | ≤ 60s   | ~33s on M4 Max today; ~45% headroom |
| `production_gate_gui`  | ≤ 180s  | dominated by GUI smoke flows |
| `production_gate_env`  | unbudgeted | external package install times vary per machine |

When you add a slow test (>5s), don't degrade these budgets — split it into a slower profile (`soak`, or a new lane) instead. The trend tool (see "Trends" under "## CI" below) makes regressions visible across recent runs.

## Pre-step: semantic-tag validator

Every gate run starts by invoking `scripts/validate_semantic_tags.sh`. A tag violation fails the gate immediately, before any test cycle burns. Run it standalone with the same script.

## The report

After the gate runs, `build/reports/production-gate.json` carries the verdict:

```json
{
  "schema_version": 2,
  "timestamp": "...",
  "profile": "core",
  "git": {"commit": "...", "branch": "..."},
  "build": {"build_type": "RelWithDebInfo", "macos_version": "...", "hardware": {...}},
  "tests": {
    "run": 20, "passed": 20, "failed": 0, "skipped": 0,
    "duration_seconds": 134.6,
    "failures": []
  },
  "signals": {
    "webgpu_validation_errors": 0,
    "audio_init_failures": 0,
    "graph_load_failures": 0,
    "missing_operators": [],
    "budget_breaches": []
  },
  "stress": {"phase6_stress_seconds": 0, "phase6_soak_seconds": 0},
  "status": "pass"
}
```

### `status` values

- **`pass`** — every test passed, no budget breaches.
- **`degraded`** — every test passed, but at least one budget marked `severity_on_breach = "warning"` was breached. Ship with caution; investigate before the next release cut.
- **`fail`** — a test failed, or a budget marked `severity_on_breach = "error"`/`"fatal"` was breached. Do not ship.

### `tests.failures[]`

Each failure carries a stable `classification` so trends are predictable across runs:

| classification | What it usually means |
|---|---|
| `webgpu_error` | WGSL compile / WebGPU validation / device lost |
| `audio_init` | miniaudio init failed; AudioEngine couldn't start |
| `graph_load` | A demo graph failed to load (per-test or per-graph) |
| `missing_operator` | Operator dylib not found / not built / ABI mismatched |
| `crash` | Subprocess died on a signal |
| `timeout` | CTest reported timeout |
| `unknown` | Couldn't classify; check `log_excerpt` |

CTest truncates per-test stdout at 1KB in the JUnit `<system-out>` element. The report tool mitigates this with a fallback: when classification yields `unknown`, it reads the per-test stdout from `${BUILD_DIR}/Testing/Temporary/LastTest.log` (non-truncated) and re-classifies. The gate passes `--ctest-log-dir` automatically; standalone invocations can pass it explicitly. If a failure still lands in `unknown`, check the raw log directly.

### `signals.budget_breaches[]`

Each entry is a runtime-health budget that fired against a per-graph health snapshot. Example:

```json
{
  "budget_code": "no_audio_underruns",
  "graph": "synth_demo",
  "severity": "warning",
  "message": "3 underrun(s)"
}
```

`status` follows the worst severity across all breaches.

## Parallel demo runs

`test_demo_graphs` runs each graph in an isolated child process. By default the parent process spawns up to `nproc / 4` (rounded up, min 1) children concurrently — conservative for GPU + audio init memory cost. On an 8-core machine that's 2 concurrent children; on 16-core it's 4.

Override via the `--jobs N` flag or the `VIVID_DEMO_GRAPH_JOBS` env var:

```bash
./build/test_demo_graphs build/graphs --jobs 1   # serial (debugging)
./build/test_demo_graphs build/graphs --jobs 4   # 4-way parallel
VIVID_DEMO_GRAPH_JOBS=2 ctest -R test_demo_graphs
```

Per-child stderr is captured to a buffer and printed in graph-list order so output reads the same as the serial run, even though execution is concurrent.

## Per-graph health JSON

`test_demo_graphs` writes a snapshot for every graph it loads to `build/reports/health/<rel_path_with_underscores>.json`. The path-with-underscores naming (e.g. `intro_showcase_demo.json` for `intro/showcase_demo.json`) prevents collisions when two demos share a basename across folders.

The output directory defaults to `<graphs_dir>/../reports/health` so the gate's invocation lands at `build/reports/health/`. Pass `--health-dir <path>` explicitly when running `./build/test_demo_graphs <graphs_dir>` from a different working directory:

```bash
./build/test_demo_graphs build/graphs --health-dir /tmp/health_dump
```

Each file looks like:

```json
{
  "schema_version": 1,
  "graph": "showcase_demo",
  "graph_path": "intro/showcase_demo.json",
  "domains": ["gpu", "audio", "control", "av"],
  "test_outcome": "passed",
  "health": { /* runtime_health::to_json output */ }
}
```

The `graph` field is the basename (no extension); the per-graph filename is the relative path with `/` replaced by `_`.

The report tool globs `build/reports/health/*.json` at the end of the gate, evaluates each budget against the matching graphs (filtered by `applies_to` ∩ `domains`), and records the breaches.

## Adding or relaxing a budget

Budgets live in `tools/production_gate_budgets.toml`. Each entry:

```toml
[[budget]]
code = "no_audio_underruns"
applies_to = "audio,av"
severity_on_breach = "warning"
description = "Audio callbacks must keep up during the gate's tick window."
```

- **`code`** — stable identifier; consumers (UI, dashboards, this report) switch on it.
- **`applies_to`** — comma-separated graph domains the budget applies to, or `"*"` for every graph. Match is "any-of" against the graph's `meta.domains` array.
- **`severity_on_breach`** — `warning` flips status to `degraded`; `error` or `fatal` flips to `fail`.

To **add** a budget: append a `[[budget]]` block, then extend the `_check_budget` switch in `tools/production_gate_report.py` with the predicate.

To **relax** a budget temporarily: change `severity_on_breach` from `error` to `warning` (still surfaces, no longer blocks ship). Comment it out entirely only with a date and reason — the framework leaves no trace if the entry is gone.

## Known gaps

- **No sustained-silence / sustained-black detection** yet. `OutputAnalyzer` is a stateless utility today; continuous monitoring would need new probes in `runtime_health::collect()`. Commented-out budget entries in `production_gate_budgets.toml` mark the wiring for when the probes land.
- **Domain filter sensitivity** — graphs that omit `meta.domains` only ever trip `*`-applicable budgets. Acceptable: fewer false positives, but tag your demos so audio/visual budgets actually apply.
- **JUnit `<system-out>` truncation** caps classification fidelity at 1KB. Check `LastTest.log` if a failure lands in the `unknown` bucket.

## Requirements

- macOS (the gate is `bash`-wrapped and uses Apple-only test fixtures).
- Python ≥3.11 for the report tool's `tomllib` (the system Python on macOS 14+ qualifies).
- `uv` on PATH for the gate's self-test (`brew install uv`).

## CI

`.github/workflows/smoke.yml` invokes `production_gate_gui`. `.github/workflows/gui-env.yml` invokes `production_gate_env`. `.github/workflows/production-gate-pr.yml` invokes `production_gate_core` on every pull request to `master`. All three upload `build/reports/ctest-*.xml`, `production-gate.json`, and `build/reports/health/*.json`.

### CI feedback on PRs

`production-gate-pr.yml` runs the core gate on every PR push, then posts a single PR comment summarizing the run. Subsequent pushes **edit the same comment in place** (located by the hidden `<!-- production-gate -->` marker) so the PR doesn't accumulate a stack of stale gate results.

The comment shows:

- A status badge (`✅ pass` / `⚠️ degraded` / `❌ fail`) and headline counts (tests passed, wall time, profile, commit).
- For `degraded` / `fail`: the budget breaches grouped by code, sorted most-frequent first, with the top 5 graphs per code (rest as `+N more`).
- For `fail`: the failing tests with their classification.
- A link to the workflow run + downloadable artifacts.

Per-breach **inline annotations** also appear in the PR's *Files changed* view via GitHub Actions `::warning::` / `::error::` directives. Capped at 3 per budget code (and 10 test failures) so the noisiest run still leaves room for other annotations under GitHub's 50-per-run limit.

`tools/format_pr_comment.py` is the formatter; it reads `build/reports/production-gate.json` and writes the markdown body to stdout + annotations to stderr. Stdlib-only, golden-tested in `tests/cli/test_format_pr_comment.py`.

The workflow's pass/fail status is taken straight from the gate's `--strict` exit code: `degraded` runs are a green check with a yellow comment; only `fail` flips the check red. Fork PRs run the gate but skip the comment upsert (the read-only `GITHUB_TOKEN` can't write); the workflow log still shows the result.

`concurrency: cancel-in-progress` cancels older in-flight runs when a PR is force-pushed, so only the latest commit's verdict ever appears.

### Trends

`tools/show_recent_gate_runs.py` lists recent gate runs across any workflow that publishes a `production-gate-reports-*` artifact.

```bash
# Defaults: last 10 runs of "Smoke Tests"
python3 tools/show_recent_gate_runs.py

# Pick a different workflow + last 5
python3 tools/show_recent_gate_runs.py --workflow "Production Gate (PR)" --limit 5

# Machine-consumable
python3 tools/show_recent_gate_runs.py --json | jq '.[] | select(.status != "pass")'
```

Requires `gh` on PATH and `gh auth login` (or `GITHUB_TOKEN` set). Workflow artifacts retain for 14 days, so older runs drop off the bottom of the list. A `no-report` row indicates a workflow infra failure that prevented report generation — distinct from `fail`, which means the gate ran and rejected.

## See also

- `docs/plans/production-gate-and-health-plan.md` — high-level design.
- `docs/plans/production-gate-phase{1..5}.md` — per-phase implementation plans.
- `cmake/tests/90-production-gate.cmake` — gate-target definitions.
- `src/runtime/core/runtime_health.{h,cpp}` — snapshot aggregator + serializer.
