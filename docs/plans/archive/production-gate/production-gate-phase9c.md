# Plan: Production Gate — Phase 9c (trend tool + runtime budget docs)

## Context

Phase 9a parallelized `test_demo_graphs` to bring `production_gate_core` from ~95s → ~33s. Phase 9b wired a per-PR comment + inline annotations using `production-gate.json`. Phase 9c is the **final sub-phase** of the production-gate roadmap. After it lands the system is feature-complete.

Two gaps remain:

1. **No quick way to see "how has the gate trended over the last N runs?"** Each CI run uploads `production-gate.json` as an artifact, but answering "which commit started the audio-clipping breaches?" today requires clicking through the Actions UI run-by-run.
2. **No documented runtime budget.** Authors who add a slow test could quietly push `_core` past the per-PR feedback latency target. There's no policy text to point at when reviewing such a PR.

Phase 9c also fixes a latent bug surfaced during exploration: `smoke.yml` and `gui-env.yml` upload artifacts with **static names** (`production-gate-reports-smoke`, `production-gate-reports-env`). Each new push silently replaces the prior artifact at that name, so within the 14-day retention window only the latest run survives. This destroys trend density before any tool can read it. The PR workflow already uses `production-gate-reports-pr-${{ github.sha }}` (Phase 9b); smoke and env need the same treatment.

Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`. This is the third and last sub-phase of `docs/plans/production-gate-phase9.md`.

## Decisions locked in (with rationale)

- **`tools/show_recent_gate_runs.py` is Python stdlib + `gh` CLI**, mirroring `production_gate_report.py` and `format_pr_comment.py` — no new project deps. `gh` is the official, cached, paginated GitHub client; reimplementing artifact download against the REST API would add hundreds of lines and a token-handling story for nothing.
- **Pattern-match artifact downloads** via `gh run download <run-id> --pattern 'production-gate-reports-*'`. One tool works against all three workflows without a per-workflow naming map; future workflows that follow the prefix automatically participate.
- **Default workflow = `Smoke Tests`.** Per-master-commit trends are the high-signal view (one run per landed commit). PR-workflow trends are noisier (multiple runs per PR push, force-push churn). Override with `--workflow "Production Gate (PR)"`.
- **Tool ships even before smoke.yml/gui-env.yml fixes propagate.** It works with whatever artifacts exist today; the naming fix simply expands historical density past the next 14 days.
- **Runtime budget targets:** `_core` ≤ 60s, `_gui` ≤ 180s. `_core` has ~45% headroom today; `_gui` is roughly comfortable. `_env` is intentionally unbudgeted — it depends on external package install times that vary per machine. Authors who add slow tests must split into a slower profile rather than degrade these targets.
- **`fetch_*` and `summarize/render_*` are split functions** so tests can patch the gh-shelling boundary and run with no live network.
- **`no-report` is a first-class status row.** Workflow infra failures (cancelled, timed out, build failed before report generation) show `status="no-report"` with the run's `conclusion` in the breaches column. This visibly distinguishes infra rot from genuine `fail`.
- **All four file changes ship as one PR.** The artifact-naming edits are one-line each but only meaningful with the trend tool consuming them — splitting them would create dangling intent.

## Scope

**In scope:**

- New `tools/show_recent_gate_runs.py` (~200 LOC, stdlib + subprocess(`gh`)).
- New `tests/cli/test_show_recent_gate_runs.py` — golden tests on the renderer + monkeypatched fetchers; no live `gh` required.
- `.github/workflows/smoke.yml`: artifact name → `production-gate-reports-smoke-${{ github.sha }}`.
- `.github/workflows/gui-env.yml`: artifact name → `production-gate-reports-env-${{ github.sha }}`.
- `.github/workflows/production-gate-pr.yml`: no change — already correctly per-sha (Phase 9b).
- `docs/testing/production-gate.md`: add "Runtime budget" subsection + "Trends" subsection.

**Out of scope:**

- Web dashboard / hosted trend store.
- Cross-workflow correlation (matching a PR-run to its post-merge master-run).
- Extending GitHub's 14-day artifact retention.
- Auto-running the trend tool from CI (intentionally a developer ergonomic, not a required check).
- Schema changes to `production-gate.json` — no new fields needed.

## Files

**New:**
- `tools/show_recent_gate_runs.py`
- `tests/cli/test_show_recent_gate_runs.py`

**Modified:**
- `.github/workflows/smoke.yml` (one line: artifact `name:` field)
- `.github/workflows/gui-env.yml` (one line: artifact `name:` field)
- `docs/testing/production-gate.md` (two new subsections)

## Code sketch — `tools/show_recent_gate_runs.py`

```python
#!/usr/bin/env python3
"""List recent production-gate runs from GitHub Actions and print a status grid.

Reads runs of a named workflow via the `gh` CLI, downloads each run's
production-gate-reports-* artifact, parses production-gate.json, and prints
a fixed-width grid (or JSON) summarising status / breaches / duration.

Requires: `gh` on PATH, authenticated (`gh auth login`) or with GITHUB_TOKEN set.
"""
DEFAULT_WORKFLOW = "Smoke Tests"
DEFAULT_LIMIT = 10
ARTIFACT_PATTERN = "production-gate-reports-*"
STATUS_BADGE = {"pass": "OK", "degraded": "WARN", "fail": "FAIL", "no-report": "????"}

# --- gh shells (mockable in tests) -----------------------------------------

def fetch_runs(workflow: str, limit: int, repo: str | None = None) -> list[dict]:
    """gh run list --workflow <name> --limit <n>
        --json databaseId,headSha,headBranch,status,conclusion,displayTitle,
               createdAt,url,workflowName"""

def fetch_report(run_id: int, tmp_dir: Path, repo: str | None = None) -> dict | None:
    """gh run download <run-id> --pattern production-gate-reports-* --dir <tmp>
    Walks tmp for production-gate.json (any sub-dir). Returns parsed JSON or
    None if no matching artifact / no JSON inside it."""

# --- pure (covered by unit tests) ------------------------------------------

def summarize(run: dict, report: dict | None) -> dict:
    """Map (run, report) → row dict {commit, branch, profile, status,
    tests_passed, tests_run, breaches, duration_seconds, run_url, conclusion}.
    report=None → status='no-report', breaches=conclusion text."""

def render_grid(rows: list[dict], *, color: bool = False) -> str:
    """Fixed-width text grid. ANSI colors when color=True (default off)."""

def render_json(rows: list[dict]) -> str:
    """json.dumps(rows, indent=2)."""

# --- main ------------------------------------------------------------------

def main(argv=None) -> int:
    # argparse: --workflow (default 'Smoke Tests'), --limit (default 10),
    # --json, --no-color, --repo (optional, passes -R to gh).
    # Flow: fetch_runs → for each, tempfile.TemporaryDirectory() →
    #   fetch_report → summarize → render → print to stdout.
    # Exit 0 on success; 2 if `gh` not found or auth error (helpful stderr).
```

Sample stdout:

```
Recent Smoke Tests runs (10):

COMMIT   BRANCH   PROFILE  STATUS  TESTS    BREACHES  DURATION  RUN
db8868f6 master   gui      WARN    21/21    28        149.3s    14238001
c16e1dd9 master   gui      OK      21/21    0         142.1s    14237998
5821f1a8 master   gui      OK      21/21    0         141.8s    14237990
9f94ac79 master   gui      ????    -        run-failed -        14237985
bd1e215a master   gui      OK      21/21    0         140.5s    14237978
```

## Test design — `tests/cli/test_show_recent_gate_runs.py`

Pattern mirrors `test_format_pr_comment.py`. `REPO_ROOT = Path(__file__).resolve().parents[2]`; `sys.path.insert(0, str(REPO_ROOT / "tools"))`. Reuses `tests/cli/fixtures/reports/{passing,degraded,fail}.json` from Phase 9b.

Cases:

1. `test_summarize_passing_run` — feeds a synthetic gh-run row + `passing.json`; assert `status="pass"`, `breaches=0`, `tests_passed=21`.
2. `test_summarize_degraded_run` — `degraded.json`; assert `status="degraded"`, `breaches=14`.
3. `test_summarize_fail_run` — `fail.json`; assert `status="fail"`, `breaches=2`.
4. `test_summarize_no_report_when_artifact_missing` — `report=None`, `conclusion="failure"` → row has `status="no-report"`, breaches column carries `run-failed` (or the conclusion).
5. `test_render_grid_columns_aligned` — sample rows; assert header present, every data line has same number of whitespace-separated fields as the header.
6. `test_render_grid_truncates_commit_to_seven_chars`.
7. `test_render_json_round_trips` — `json.loads(render_json(rows)) == rows`.
8. `test_main_uses_fetch_overrides(monkeypatch, capsys)` — patch `fetch_runs` + `fetch_report` to return canned data; assert exit 0 and grid printed to stdout.
9. `test_main_handles_gh_not_found(monkeypatch, capsys)` — patch `fetch_runs` to raise `FileNotFoundError("gh")`; assert exit 2 + stderr mentions `gh auth login` / install hint.
10. `test_main_json_flag(monkeypatch, capsys)` — `--json` produces parseable JSON list of expected length.

No live `gh` required.

## Workflow YAML edits

`.github/workflows/smoke.yml` — line ~35:
```yaml
          name: production-gate-reports-smoke-${{ github.sha }}
```

`.github/workflows/gui-env.yml` — line ~68:
```yaml
          name: production-gate-reports-env-${{ github.sha }}
```

## Doc edits — `docs/testing/production-gate.md`

**(a) New "Runtime budget" subsection** — inserted between the "Profiles" table (currently ends ~line 32) and "Pre-step: semantic-tag validator" (~line 34):

```markdown
## Runtime budget

Per-PR feedback latency is the gate's first-class metric. Targets:

| Profile | Target wall time | Notes |
|---------|------------------|-------|
| `production_gate_core` | ≤ 60s   | ~33s on M4 Max today; ~45% headroom |
| `production_gate_gui`  | ≤ 180s  | dominated by GUI smoke flows |
| `production_gate_env`  | unbudgeted | external package install times vary per machine |

When you add a slow test (>5s), don't degrade these budgets — split it into
a slower profile (`soak`, or a new lane) instead. The trend tool (see "Trends"
under "## CI" below) makes regressions visible across recent runs.
```

**(b) New "Trends" subsection** — added at the end of "## CI", after the existing `cancel-in-progress` paragraph:

````markdown
### Trends

`tools/show_recent_gate_runs.py` lists recent gate runs across any workflow
that publishes a `production-gate-reports-*` artifact.

```bash
# Defaults: last 10 runs of "Smoke Tests"
python3 tools/show_recent_gate_runs.py

# Pick a different workflow + last 5
python3 tools/show_recent_gate_runs.py --workflow "Production Gate (PR)" --limit 5

# Machine-consumable
python3 tools/show_recent_gate_runs.py --json | jq '.[] | select(.status != "pass")'
```

Requires `gh` on PATH and `gh auth login` (or `GITHUB_TOKEN` set). Workflow
artifacts retain for 14 days, so older runs drop off the bottom of the list.
A `no-report` row indicates a workflow infra failure that prevented report
generation — distinct from `fail`, which means the gate ran and rejected.
````

## Verification

**Local (no live `gh`):**
```bash
uv run --with pytest pytest tests/cli/test_show_recent_gate_runs.py -v
python3 -c "import ast; ast.parse(open('tools/show_recent_gate_runs.py').read())"
# Full pytest suite for regressions
uv run --with pytest pytest tests/cli/ -v
```

**Local (with `gh` authenticated):**
```bash
python3 tools/show_recent_gate_runs.py --limit 3
python3 tools/show_recent_gate_runs.py --workflow "Production Gate (PR)" --limit 3 --json | python3 -m json.tool
```

**CI:**
1. Land PR; observe `production-gate-pr.yml` still passes (no behavioral change to it).
2. After the next master merge, re-run the trend tool — should see two distinct `production-gate-reports-smoke-<sha>` artifacts (proving the naming fix works); previously only one ever survived past the next push.
3. After the next scheduled `gui-env.yml` run, same check for env.

## Risks

1. **`gh run download --pattern` requires `gh ≥ 2.34`.** The tool catches `subprocess.CalledProcessError` whose stderr contains `unknown flag` and surfaces a clear "upgrade gh" message.
2. **Per-sha artifact rename is observable** in the GitHub UI artifact list (humans browsing artifacts will see longer names). No external scripts consume the static names today, so safe.
3. **Rate-limited `gh` API for unauthenticated environments.** `gh` warns; the tool surfaces stderr verbatim.
4. **`tempfile.TemporaryDirectory` cleanup on Ctrl-C** — wrap each per-run download in its own `with` block so partial state never accumulates between runs.
5. **`gh run list` JSON field stability.** GitHub has historically been backward-compatible with `--json` field names. If a field disappears, fail loud (KeyError) rather than silently corrupting the grid.

## What I will do on approval

1. Move this plan to `docs/plans/production-gate-phase9c.md`.
2. Write `tools/show_recent_gate_runs.py` per the sketch.
3. Write `tests/cli/test_show_recent_gate_runs.py` with the 10 cases listed.
4. Run `uv run --with pytest pytest tests/cli/test_show_recent_gate_runs.py -v` — all green.
5. Smoke-test the tool against the live repo (requires `gh auth login`); paste a sample of the grid output into the report.
6. Edit `.github/workflows/smoke.yml` and `gui-env.yml` artifact names.
7. Update `docs/testing/production-gate.md` with both new subsections.
8. Run the full pytest suite (`tests/cli/`) for cross-test regressions.
9. Note in the report that smoke/env naming-fix verification requires a real master push (post-merge); flag for the user.
10. Report back. Phase 9 (and the production-gate roadmap) is then feature-complete.
