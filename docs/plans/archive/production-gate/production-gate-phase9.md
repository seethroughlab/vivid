# Plan: Production Gate — Phase 9 (performance & CI ergonomics)

## Context

Phase 8 closed the runtime-health probe gaps. Phase 9 makes the gate cheap enough for per-PR CI and ensures degraded runs leave readable breadcrumbs. The audit raised four items:

- **Slow gate**: `production_gate_core` is ~95s, of which ~85s is `test_demo_graphs` running 32 graphs sequentially. Each child is GPU+audio-isolated → trivially parallelizable.
- **No PR-level CI feedback**: `smoke.yml` runs only on push-to-master. PRs have no production-gate signal.
- **No trend artifact**: `production-gate.json` is uploaded per-run but there's no quick way to see "how has status trended over the last N pushes?"
- **No documented gate runtime budget** — future authors don't know to keep `_core` under N seconds.

Phase 9 splits into three sub-phases that ship independently:

```
9a — test_demo_graphs parallelization      (2-3 days)  ─── biggest perf win
9b — PR-only workflow + CI annotations     (2-3 days)  ─── per-PR feedback
9c — Trend tool + runtime-budget docs      (1-2 days)  ─── small follow-ups
```

This is the final phase planned in `docs/plans/production-gate-followups.md`. After 9c, the production-gate work is feature-complete.

Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`.

---

## Phase 9a — test_demo_graphs parallelization

**Goal:** cut `test_demo_graphs` from ~85s to ~40-50s by running per-graph children concurrently with bounded parallelism.

### Decisions locked in (with rationale)

- **Worker pool, not std::async / threads.** Children are already isolated processes; we just bound how many run at once. The pool is a `std::vector<RunningChild>` of `{pid_t, std::string filename}`. Main thread spawns until pool is full, then `waitpid(-1, ...)` blocks for any child to exit, harvests the result, removes from pool, spawns the next.
- **Default `--jobs N = nproc / 4`** (rounded up, min 1). Conservative for GPU + audio init memory cost. Configurable via CLI flag and `VIVID_DEMO_GRAPH_JOBS` env var.
- **Output ordering preserved.** Today the `=== <filename> ===` headers print in alphabetical order; with parallelization headers + child output would interleave. Solution: each child's stderr is captured to a per-child string buffer (via pipe), printed atomically by the parent in graph-list order as each child finishes. `pass()/fail()/skip()` calls happen in the same order as today (sorted), even though execution is concurrent.
- **Artifacts already collision-safe** (per-graph health JSONs use `<dir>_<basename>.json` from Phase 7). No shared writes.
- **`--jobs 1` always available** for debugging — explicit serialization without removing the parallel code path.
- **Skipped graphs** (the `headless_skip` set) handled before pool dispatch — they don't consume a slot.

### Scope (9a)

In scope:
- `tests/integration/test_demo_graphs.cpp`: refactor the per-graph loop into a worker-pool driver. Add `--jobs N` flag (default `(nproc + 3) / 4`, env override `VIVID_DEMO_GRAPH_JOBS`).
- Per-child output capture: replace `posix_spawn`'s implicit stderr inheritance with `posix_spawn_file_actions_addopen` redirecting stderr to a pipe; main thread drains and buffers per-child output until that child's `waitpid` returns.
- Print buffered output atomically per-child (not interleaved). Order matches the parent's graph-iteration order, not completion order, by deferring print until the child at the head of the queue finishes.
- Document the flag in `docs/testing/production-gate.md` and `docs/testing/PACKAGE-SMOKE-TEST.md` if relevant.
- Test: a small unit test that runs `test_demo_graphs` against a 3-graph fixture set with `--jobs 2` and asserts the same outcomes as `--jobs 1`.

Out of scope:
- Parallelizing the GUI tests (`UI_SMOKE`) — those share window resources. Keep sequential.
- Distributing across machines.

### Files (9a)
- Modified: `tests/integration/test_demo_graphs.cpp` (worker pool + flag + output capture).
- Modified: `docs/testing/production-gate.md` (document `--jobs`).

### Verification (9a)
```bash
# Sequential baseline
time cmake --build build --target test_demo_graphs && \
    ./build/test_demo_graphs build/graphs --jobs 1

# Parallel
time ./build/test_demo_graphs build/graphs --jobs 4

# Confirm result parity
diff <(./build/test_demo_graphs build/graphs --jobs 1 2>&1 | grep -E '^  (PASS|FAIL|SKIP):' | sort) \
     <(./build/test_demo_graphs build/graphs --jobs 4 2>&1 | grep -E '^  (PASS|FAIL|SKIP):' | sort)

# Full gate end-to-end
time cmake --build build --target production_gate_core
```

Expected: parallel run ≈ 50% of sequential time on 8-core; identical PASS/FAIL/SKIP set.

### Risks (9a)
1. **GPU init contention.** Multiple WGPUInstance creations concurrently on macOS. Default `nproc/4` keeps it conservative; bump down if observed.
2. **Output ordering**: if a slow child blocks early-list children's output until it completes, the test feels stuck. Mitigation: a periodic "[N still running]" status line every 10s.
3. **Signal handling**: a child crashing with SIGSEGV must not propagate. The existing per-child `signal(SIGSEGV, ...)` handler stays.

---

## Phase 9b — PR-only workflow + CI annotations

**Goal:** every PR push gets a single, updated comment summarizing `production_gate_core` results.

### Decisions locked in

- **New file `.github/workflows/production-gate-pr.yml`.** Triggers on `pull_request: [opened, synchronize]`. Runs `production_gate_core` (not `_gui` — keep it under 60s with 9a's parallelization). `smoke.yml` stays push-to-master only.
- **Single PR comment, edited in place.** Use `actions/github-script` (built-in to Actions, no extra deps) to read `production-gate.json`, format a markdown summary, and `gh api repos/.../issues/{N}/comments` to upsert a comment marked with a hidden HTML header (`<!-- production-gate -->`) so subsequent runs find and edit instead of stacking.
- **Comment format**: status badge (✅/⚠️/❌) + status text + per-failure summary if any + per-breach summary grouped by code, capped at top 5 per code. Falls back gracefully if `production-gate.json` is missing (e.g. ctest crashed before the report tool ran).
- **Annotations via `::warning::` / `::error::` workflow commands** for failures and breaches. These show up inline in the PR's "Files changed" view as GitHub annotations. Cheap, no extra API calls.
- **Status check**: implicit via the workflow's pass/fail. The PR's required-checks UI shows "production-gate / core" green or red.
- **Same-repo PRs only.** Forks won't have `secrets.GITHUB_TOKEN` write access. The script gracefully skips comment posting on fork PRs (still runs the gate, just doesn't comment). Document this.

### Scope (9b)

In scope:
- New `.github/workflows/production-gate-pr.yml`:
  - `on: pull_request: [opened, synchronize, ready_for_review]`
  - One job: configure → submodules → build `production_gate_core` → run report → post/update comment → upload artifact.
- New `tools/format_pr_comment.py`: reads `build/reports/production-gate.json`, emits a markdown comment string to stdout. Stdlib-only.
- Workflow step uses `actions/github-script@v7` to call `format_pr_comment.py` (or do the formatting inline if it's <30 lines), then upsert via `github.rest.issues.listComments` + `github.rest.issues.{create,update}Comment`.
- Workflow always uploads `production-gate.json` + `health/*.json` (mirrors smoke.yml's pattern).
- Update `docs/testing/production-gate.md` with a "CI feedback" section.

Out of scope:
- Annotations on individual graphs/files (would require parsing per-graph health JSONs and computing line numbers — bigger scope).
- Posting failure logs from `LastTest.log` into the comment (too much noise; keep them in the artifact).

### Files (9b)
- New: `.github/workflows/production-gate-pr.yml`.
- New: `tools/format_pr_comment.py`.
- New: `tests/cli/test_format_pr_comment.py` (golden-file tests on `production-gate.json` → markdown).
- Modified: `docs/testing/production-gate.md`.

### Comment format example

```
<!-- production-gate -->
## Production Gate: ⚠️ degraded (Phase 9b)

**Tests**: 21/21 passed in 95s
**Status**: degraded

### Budget breaches (28)
- `no_sustained_black` (16): spirograph_demo, edge_demo, scanlines_demo, ... +13 more
- `no_audio_clipping` (11): filter_sweep (35.7), four_on_the_floor (7.0), ... +9 more
- `no_audio_underruns` (1): granular_synth_demo (4 underruns)

📦 [Full report artifact](workflow-run-link)

_Updated automatically by `production-gate-pr.yml`._
```

### Verification (9b)
- Open a draft PR on the worktree branch; confirm the workflow runs and posts a comment.
- Push a follow-up commit; confirm the comment is edited (not a new comment).
- Force a synthetic fail status (lower a budget threshold); confirm comment shows ❌ + the offending breach.

### Risks (9b)
1. **Self-hosted CI cost** — every PR push runs `production_gate_core` (~50s after 9a). Acceptable for a small team; revisit if PR volume grows.
2. **Comment spam on fork PRs** — script no-ops gracefully when `secrets.GITHUB_TOKEN` lacks write access.
3. **Timing flakiness on degraded runs** — audio underrun budgets are non-deterministic. Mitigation: the comment is descriptive ("4 underruns") not pass/fail; reviewers can judge.

---

## Phase 9c — Trend tool + runtime-budget docs

**Goal:** "how has the gate trended over the last N runs?" is one command away.

### Decisions locked in

- **Trend storage = GitHub Actions workflow_artifact, 90-day retention.** No separate branch, no external storage. The PR workflow + smoke.yml both already upload `production-gate.json`; we just add a per-commit naming convention so old artifacts don't collide.
- **`tools/show_recent_gate_runs.py`**: Python stdlib + `gh` CLI invocation. Reads recent workflow runs, downloads each `production-gate-reports-*` artifact, extracts the JSON, prints a status grid: commit | branch | profile | status | breaches | duration. Defaults: last 10 runs of `Smoke Tests`.
- **Runtime budget = 60 seconds** for `production_gate_core` after Phase 9a. Documented in `production-gate.md` with rationale (per-PR CI feedback latency target). When future authors add a slow test, they're expected to either skip it from the gate or split it into a slower profile.

### Scope (9c)

In scope:
- New `tools/show_recent_gate_runs.py`:
  - Optional `--workflow NAME` (default: `Smoke Tests`).
  - Optional `--limit N` (default: 10).
  - Calls `gh run list --workflow NAME --limit N --json databaseId,headSha,status,conclusion`.
  - For each run, calls `gh run download <run-id> --name production-gate-reports-smoke --dir <tmp>`, parses the JSON, extracts status + breaches + duration.
  - Prints a fixed-width grid to stdout. Optional `--json` for machine consumption.
- `docs/testing/production-gate.md` adds:
  - A "Runtime budget" section (target ≤ 60s for `_core`, ≤ 180s for `_gui`).
  - A "Trends" section pointing at `show_recent_gate_runs.py`.
- Update CI artifact names in `smoke.yml` and `production-gate-pr.yml` to include `${{ github.sha }}` so old artifacts don't shadow new ones in the trend tool's listing.

Out of scope:
- A web dashboard.
- Cross-workflow correlation.
- Long-term storage beyond GitHub's 90-day artifact retention.

### Files (9c)
- New: `tools/show_recent_gate_runs.py`.
- Modified: `.github/workflows/smoke.yml` (artifact naming).
- Modified: `.github/workflows/production-gate-pr.yml` (artifact naming, from 9b).
- Modified: `docs/testing/production-gate.md` (Runtime budget + Trends sections).

### Verification (9c)
```bash
# After at least 2 master-push runs land:
python3 tools/show_recent_gate_runs.py --limit 5

# JSON output for scripting
python3 tools/show_recent_gate_runs.py --limit 5 --json | jq .
```

### Risks (9c)
1. **`gh` CLI auth required.** Document `gh auth login` as a prerequisite. Falls back to `GITHUB_TOKEN` env var when set.
2. **Artifact retention is 90 days.** Older runs simply drop off the trend grid; documented.

---

## Cross-cutting design notes

**No schema changes.** Phase 9 doesn't add fields to `production-gate.json` or `runtime_health`. All work is parallelization, CI orchestration, and tooling.

**No C++ runtime changes outside `test_demo_graphs.cpp`.**

**Tests stay green throughout.** Each sub-phase ends with `production_gate_core` passing.

## Sequencing

```
9a (parallelization) ─── any time, biggest perf win, foundational for 9b's runtime budget
9b (PR workflow)     ─── after 9a so the per-PR runtime stays under 60s
9c (trend tool)      ─── any time after 9b, since artifact naming aligns with PR workflow
```

## What I will do on approval

1. Move this plan to `docs/plans/production-gate-phase9.md`.
2. Start with **Phase 9a**:
   - Refactor `test_demo_graphs.cpp` per-graph loop into a worker-pool driver.
   - Add `--jobs` flag + env var.
   - Per-child stderr capture via pipe + ordered atomic print.
   - Verify timing improvement + result parity.
   - Stop and report.
3. **Phase 9b** as separate commit:
   - Write `tools/format_pr_comment.py` + golden tests.
   - Write `.github/workflows/production-gate-pr.yml`.
   - Document in `production-gate.md`.
   - End-to-end test on a draft PR.
4. **Phase 9c** as the final commit:
   - Write `tools/show_recent_gate_runs.py`.
   - Add per-commit artifact naming.
   - Document runtime budget + trends in `production-gate.md`.
5. Each sub-phase ends with a status report.
