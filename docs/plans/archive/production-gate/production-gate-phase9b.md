# Plan: Production Gate — Phase 9b (PR-only workflow + CI annotations)

## Context

Phase 9a brought `production_gate_core` from ~95s down to ~33s, making it cheap enough to run per PR. Phase 9b builds on that by adding:

- A new `.github/workflows/production-gate-pr.yml` that triggers on every PR push, runs `production_gate_core`, and posts a single PR comment summarizing the result.
- The comment is **edited in place** on subsequent pushes (not stacked) via a hidden HTML marker.
- GitHub Actions inline annotations (`::warning::` / `::error::`) for the PR's "Files changed" view.
- Status check via the workflow's pass/fail (the existing `--strict` exit semantics already make budget-error breaches fail the build).

Existing `version-guard.yml` already shows the `pull_request:` trigger pattern in this repo, so the addition is consistent with conventions. `smoke.yml` stays push-to-master only — no overlap.

This is Phase 9b of `docs/plans/production-gate-phase9.md`. Phase 9c (trend tool + runtime budget docs) follows.

Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`.

## Decisions locked in (with rationale)

- **New workflow file: `.github/workflows/production-gate-pr.yml`.** Runs `production_gate_core` (not `_gui`) on `pull_request: [opened, synchronize, ready_for_review]` against `master`. Self-hosted macOS runner (same as smoke.yml) — required for the actual build. ~33s gate run + ~30-60s build = ~1-2 min per PR push. Acceptable.
- **Comment formatter is Python**, not inline JS. `tools/format_pr_comment.py` reads `build/reports/production-gate.json` and emits markdown to stdout. Stdlib-only. Testable via pytest with golden fixtures (matches the existing `tools/production_gate_report.py` pattern).
- **Upsert via `actions/github-script@v7`** (built-in to Actions, no install). The Python tool produces the body; a small JS step searches comments for the hidden marker `<!-- production-gate -->` and either updates or creates. This is the cleanest API for the upsert pattern; `gh pr comment` doesn't have native edit-existing semantics.
- **Workflow annotations from the Python tool**, written to stderr in `::warning::`/`::error::` syntax. GitHub's runner picks them up automatically and surfaces them inline in the PR diff view. No extra API calls.
- **Same-repo PRs only.** Forks have a read-only `GITHUB_TOKEN`; the upsert step gracefully no-ops on `permissions: read` failures (try/catch in the github-script). Gate still runs; the PR author just doesn't see the comment.
- **Comment body capped at ~3 KB.** Per-budget breach lists capped at top 5 entries; "+N more" suffix for overflow. Keeps the comment scannable.
- **Workflow fails iff `--strict` exit code is non-zero.** That's the existing semantic (Phase 5): `status=fail` → exit 1; `degraded` and `pass` → exit 0. So degraded PRs surface as a yellow comment but a green check — exactly the intent.
- **Artifact upload mirrors smoke.yml.** `production-gate.json` + `health/*.json` + `ctest-*.xml`, named `production-gate-reports-pr-${{ github.sha }}` so per-commit names don't collide (also feeds Phase 9c's trend tool).

## Scope

In scope:
- New `.github/workflows/production-gate-pr.yml`:
  - `on: pull_request: { types: [opened, synchronize, ready_for_review], branches: [master] }`
  - One job on `[self-hosted, macOS]`
  - Steps: checkout (with submodules), configure cmake, run `production_gate_core` with the same env as smoke.yml's audio-disabled path, generate comment body via `format_pr_comment.py`, upsert comment via `actions/github-script`, upload artifact (always).
- New `tools/format_pr_comment.py`:
  - Reads `build/reports/production-gate.json` (or path passed as argv[1]).
  - Emits markdown comment body to stdout.
  - Emits `::warning::` / `::error::` annotations to stderr.
  - Falls back gracefully if the file is missing (emits a "report tool didn't run" diagnostic).
  - Stdlib-only, no extra deps.
- New `tests/cli/test_format_pr_comment.py`:
  - Golden tests for: pass / degraded / fail status, with/without breaches, missing report file.
  - Uses existing `tests/cli/fixtures/health/` data via synthesized production-gate.json fixtures (small).
- Update `docs/testing/production-gate.md` with a "PR feedback" section.

Out of scope (Phase 9c):
- Trend tool + per-commit artifact downloads.
- Runtime budget documentation (separate doc edit in 9c).

Out of scope (orthogonal):
- Populating `core_version` in the package-mismatch budget. Phase 8a passed empty string but `VIVID_CORE_VERSION` macro exists in `src/runtime/core/main.cpp`. Worth a follow-up; not a 9b concern.

## Files

New:
- `.github/workflows/production-gate-pr.yml`
- `tools/format_pr_comment.py`
- `tests/cli/test_format_pr_comment.py`
- `tests/cli/fixtures/reports/passing.json`, `degraded.json`, `fail.json` — small synthesized `production-gate.json` files for golden tests.

Modified:
- `docs/testing/production-gate.md` — new "CI feedback on PRs" section.

## Comment format (target)

```markdown
<!-- production-gate -->
## Production Gate · ⚠️ degraded

| | |
|---|---|
| **Tests** | 21 / 21 passed |
| **Status** | `degraded` |
| **Wall time** | 33.5s |
| **Profile** | core |
| **Commit** | `db8868f6` |

### 28 budget breaches

- `no_sustained_black` (16): `spirograph_demo`, `edge_demo`, `scanlines_demo`, `crt_effect_demo`, `wgsl_filters_demo`, … +11 more
- `no_audio_clipping` (11): `filter_sweep` (35.7), `four_on_the_floor` (7.0), `drum_stack_demo` (6.0), `granular_synth_demo`, `fm_synth_demo`, … +6 more
- `no_audio_underruns` (1): `granular_synth_demo` (4 underruns)

📦 [Workflow run + artifacts](https://github.com/.../actions/runs/12345)

<sub>Updated automatically by `production-gate-pr.yml`. See [docs/testing/production-gate.md](docs/testing/production-gate.md).</sub>
```

For `pass` status, the breach section is omitted. For `fail`, the breach section also lists the ctest failures with their classification.

## Code sketches

### `tools/format_pr_comment.py` skeleton

```python
#!/usr/bin/env python3
"""Format a production-gate.json into a PR-comment markdown body.

Emits markdown to stdout; emits GitHub Actions annotations to stderr.
Exits 0 unless the input file is missing AND no fallback content was
requested — caller (workflow) decides whether to treat that as a failure.
"""
import json, sys, argparse
from collections import defaultdict
from pathlib import Path

MARKER = "<!-- production-gate -->"
STATUS_BADGE = {"pass": "✅", "degraded": "⚠️", "fail": "❌"}

def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("report", type=Path)
    p.add_argument("--workflow-run-url", default="")
    p.add_argument("--max-graphs-per-budget", type=int, default=5)
    args = p.parse_args(argv)

    if not args.report.exists():
        sys.stdout.write(MARKER + "\n## Production Gate · ❓ no report\n\n"
            "The gate didn't produce a report this run. Check the workflow log.\n")
        print("::warning::production-gate.json missing", file=sys.stderr)
        return 0

    d = json.loads(args.report.read_text())
    status = d.get("status", "unknown")
    badge = STATUS_BADGE.get(status, "❓")
    tests = d.get("tests", {})
    git = d.get("git", {})

    out = [MARKER, ""]
    out.append(f"## Production Gate · {badge} {status}")
    out.append("")
    out.append("| | |")
    out.append("|---|---|")
    out.append(f"| **Tests** | {tests.get('passed', 0)} / {tests.get('run', 0)} passed |")
    out.append(f"| **Status** | `{status}` |")
    out.append(f"| **Wall time** | {tests.get('duration_seconds', 0):.1f}s |")
    out.append(f"| **Profile** | {d.get('profile', '?')} |")
    out.append(f"| **Commit** | `{git.get('commit', '')[:8]}` |")
    out.append("")

    # Test failures (if any)
    failures = tests.get("failures", [])
    if failures:
        out.append(f"### {len(failures)} test failures")
        for f in failures[:args.max_graphs_per_budget]:
            out.append(f"- `{f.get('name', '?')}` — {f.get('classification', '?')}")
            print(f"::error title={f.get('name', '?')}::"
                  f"{f.get('classification', '?')}: "
                  f"{f.get('log_excerpt', '')[:200]}", file=sys.stderr)
        out.append("")

    # Budget breaches grouped by code
    breaches = d.get("signals", {}).get("budget_breaches", [])
    if breaches:
        by_code = defaultdict(list)
        for b in breaches:
            by_code[b["budget_code"]].append(b)
        out.append(f"### {len(breaches)} budget breaches")
        for code, entries in sorted(by_code.items(), key=lambda kv: -len(kv[1])):
            heads = [e["graph"] for e in entries[:args.max_graphs_per_budget]]
            tail = (f", … +{len(entries) - args.max_graphs_per_budget} more"
                    if len(entries) > args.max_graphs_per_budget else "")
            out.append(f"- `{code}` ({len(entries)}): "
                       + ", ".join(f"`{g}`" for g in heads) + tail)
            for b in entries[:3]:  # only annotate top 3 per code
                level = "error" if b["severity"] in ("error", "fatal") else "warning"
                print(f"::{level} title={b['budget_code']}/{b['graph']}::"
                      f"{b['message']}", file=sys.stderr)
        out.append("")

    if args.workflow_run_url:
        out.append(f"📦 [Workflow run + artifacts]({args.workflow_run_url})")
        out.append("")

    out.append("<sub>Updated automatically by "
               "[production-gate-pr.yml](.github/workflows/production-gate-pr.yml). "
               "See [docs/testing/production-gate.md](docs/testing/production-gate.md).</sub>")
    sys.stdout.write("\n".join(out) + "\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

### `production-gate-pr.yml` skeleton

```yaml
name: Production Gate (PR)

on:
  pull_request:
    types: [opened, synchronize, ready_for_review]
    branches: [master]

permissions:
  contents: read
  pull-requests: write   # comment upsert; degrades gracefully on forks

concurrency:
  group: production-gate-pr-${{ github.event.pull_request.number }}
  cancel-in-progress: true

jobs:
  gate:
    runs-on: [self-hosted, macOS]
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64

      - name: Create app bundle skeleton
        run: mkdir -p build/vivid.app/Contents/PlugIns

      - name: Run production gate (core profile)
        id: gate
        # Gate exits non-zero on status=fail thanks to --strict; the workflow
        # surfaces that as a red required check.
        run: cmake --build build --target production_gate_core -j$(sysctl -n hw.logicalcpu)

      - name: Generate PR comment body
        if: always()
        id: comment
        run: |
          set -euo pipefail
          body=$(python3 tools/format_pr_comment.py build/reports/production-gate.json \
                 --workflow-run-url "${{ github.server_url }}/${{ github.repository }}/actions/runs/${{ github.run_id }}")
          { echo "body<<__EOF__"; echo "$body"; echo "__EOF__"; } >> "$GITHUB_OUTPUT"

      - name: Upsert PR comment
        if: always() && github.event.pull_request.head.repo.full_name == github.repository
        uses: actions/github-script@v7
        with:
          script: |
            const marker = '<!-- production-gate -->';
            const body = `${{ steps.comment.outputs.body }}`;
            const { data: comments } = await github.rest.issues.listComments({
              owner: context.repo.owner,
              repo: context.repo.repo,
              issue_number: context.issue.number,
              per_page: 100,
            });
            const existing = comments.find(c => c.body && c.body.includes(marker));
            if (existing) {
              await github.rest.issues.updateComment({
                owner: context.repo.owner,
                repo: context.repo.repo,
                comment_id: existing.id,
                body,
              });
              core.info(`Updated comment #${existing.id}`);
            } else {
              const { data: created } = await github.rest.issues.createComment({
                owner: context.repo.owner,
                repo: context.repo.repo,
                issue_number: context.issue.number,
                body,
              });
              core.info(`Created comment #${created.id}`);
            }

      - name: Upload production gate reports
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: production-gate-reports-pr-${{ github.sha }}
          path: |
            build/reports/ctest-*.xml
            build/reports/production-gate.json
            build/reports/health/*.json
          if-no-files-found: ignore
          retention-days: 14
```

`concurrency:` cancels older runs when the PR is force-pushed mid-build — avoids stacking multiple in-flight runs.

## Test design

`tests/cli/test_format_pr_comment.py`:

1. **Pass status** — minimal `production-gate.json`, expect ✅ badge + tests row + no breach section.
2. **Degraded with breaches** — feed a 28-breach JSON, expect ⚠️, breach groups sorted by count, "+N more" truncation at 5.
3. **Fail with test failures** — JSON with one failed test + budget breaches, expect ❌, both sections present.
4. **Missing report file** — expect ❓ "no report" body, exit 0.
5. **Annotations to stderr** — capture stderr, assert `::error::` / `::warning::` lines for each breach (capped at 3 per code).
6. **Marker present** — every output starts with `<!-- production-gate -->` so the upsert can find it.

Fixture files live in `tests/cli/fixtures/reports/`:
- `passing.json`, `degraded.json`, `fail.json` — synthesized minimal production-gate.json shapes.

## Verification

Local (in worktree):
```bash
# Generate a comment from the current real report
python3 tools/format_pr_comment.py build/reports/production-gate.json \
    --workflow-run-url "https://example/runs/123"

# Pytest
uv run --with pytest pytest tests/cli/test_format_pr_comment.py -v
```

End-to-end (requires a real PR):
- Open a draft PR on the worktree branch.
- Confirm the workflow runs and posts a single comment.
- Push a follow-up commit; confirm the comment is **edited** (same comment ID), not duplicated.
- Add a synthetic budget breach (e.g. lower an underrun budget threshold), confirm the comment updates.
- Force a `status: fail` (synthetic budget elevation to error severity), confirm the workflow run is red and the comment shows ❌.

## Risks

1. **Comment-update race** when two PR pushes land within seconds. Mitigated by `concurrency: cancel-in-progress` — only the latest in-flight run matters.
2. **Self-hosted runner availability**. If the runner is down, PRs sit yellow. Same risk as smoke.yml today; out of scope.
3. **Body too large**. Capped at top-5 per budget; total comment well under GitHub's 65 KB body limit.
4. **Fork PRs** can't post comments (read-only token). The conditional `if: ... github.event.pull_request.head.repo.full_name == github.repository` skips the upsert step on forks; the gate still runs and the workflow log is still inspectable.
5. **Annotations volume**. Capped at 3 per budget code (≤ ~30 annotations even on the noisiest run). GitHub silently caps at 50 anyway.

## What I will do on approval

1. Move this plan to `docs/plans/production-gate-phase9b.md`.
2. Write `tools/format_pr_comment.py` per the sketch.
3. Write `tests/cli/test_format_pr_comment.py` + 3 small fixture JSONs.
4. Run `uv run --with pytest pytest tests/cli/test_format_pr_comment.py -v` — all green.
5. Smoke-test the formatter on the current `build/reports/production-gate.json`; verify the markdown reads well.
6. Write `.github/workflows/production-gate-pr.yml` per the sketch.
7. Update `docs/testing/production-gate.md` with the "CI feedback on PRs" section.
8. Run the existing pytest suite end-to-end to ensure no cross-test regressions (`pytest tests/cli/`).
9. Note that end-to-end PR verification requires a real PR on GitHub — flag this in the report for the user to validate.
10. Report back.
