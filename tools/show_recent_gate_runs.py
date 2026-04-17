#!/usr/bin/env python3
"""List recent production-gate runs from GitHub Actions and print a status grid.

Reads runs of a named workflow via the ``gh`` CLI, downloads each run's
``production-gate-reports-*`` artifact, parses ``production-gate.json``, and
prints a fixed-width grid (or JSON) summarising status / breaches / duration.

Requires ``gh`` on PATH, authenticated via ``gh auth login`` (or with
``GITHUB_TOKEN`` set). Workflow artifacts retain for 14 days, so older runs
drop off the bottom of the list.

See docs/plans/production-gate-phase9c.md for the design.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

DEFAULT_WORKFLOW = "Smoke Tests"
DEFAULT_LIMIT = 10
ARTIFACT_PATTERN = "production-gate-reports-*"
RUN_LIST_FIELDS = (
    "databaseId,headSha,headBranch,status,conclusion,"
    "displayTitle,createdAt,url,workflowName"
)
STATUS_BADGE = {
    "pass": "OK",
    "degraded": "WARN",
    "fail": "FAIL",
    "no-report": "????",
}
ANSI = {
    "pass": "\033[32m",
    "degraded": "\033[33m",
    "fail": "\033[31m",
    "no-report": "\033[90m",
    "reset": "\033[0m",
}


# --- gh shells (mockable in tests) -----------------------------------------

def _run_gh(args: list[str]) -> subprocess.CompletedProcess[str]:
    """Invoke gh, return CompletedProcess. Raises FileNotFoundError if gh
    isn't on PATH. Caller is responsible for inspecting returncode/stderr."""
    return subprocess.run(
        ["gh", *args],
        check=False,
        capture_output=True,
        text=True,
    )


def fetch_runs(workflow: str, limit: int, repo: str | None = None) -> list[dict]:
    """Return parsed `gh run list` rows for the given workflow."""
    args = ["run", "list", "--workflow", workflow, "--limit", str(limit),
            "--json", RUN_LIST_FIELDS]
    if repo:
        args = ["-R", repo, *args]
    proc = _run_gh(args)
    if proc.returncode != 0:
        raise RuntimeError(
            f"gh run list failed (exit {proc.returncode}): {proc.stderr.strip()}"
        )
    return json.loads(proc.stdout)


def fetch_report(run_id: int, tmp_dir: Path, repo: str | None = None) -> dict | None:
    """Download the matching artifact and return parsed production-gate.json,
    or None if no matching artifact exists or the JSON is missing."""
    args = ["run", "download", str(run_id),
            "--pattern", ARTIFACT_PATTERN,
            "--dir", str(tmp_dir)]
    if repo:
        args = ["-R", repo, *args]
    proc = _run_gh(args)
    if proc.returncode != 0:
        # gh exits non-zero when no artifact matches — that's expected and
        # surfaces as no-report (not an error).
        if "no artifacts" in proc.stderr.lower() or "no valid artifacts" in proc.stderr.lower():
            return None
        # Anything else (auth failure, network) is a real problem; don't
        # silently swallow.
        if "could not find" in proc.stderr.lower():
            return None
        raise RuntimeError(
            f"gh run download {run_id} failed: {proc.stderr.strip()}"
        )
    for path in tmp_dir.rglob("production-gate.json"):
        try:
            return json.loads(path.read_text())
        except json.JSONDecodeError:
            return None
    return None


# --- pure (unit-tested) ----------------------------------------------------

def summarize(run: dict, report: dict | None) -> dict[str, Any]:
    """Reduce (run, report) → a single row dict ready for rendering."""
    commit = (run.get("headSha") or "")[:7]
    branch = run.get("headBranch") or ""
    run_id = run.get("databaseId") or ""
    run_url = run.get("url") or ""
    conclusion = run.get("conclusion") or run.get("status") or ""

    if report is None:
        return {
            "commit": commit,
            "branch": branch,
            "profile": "-",
            "status": "no-report",
            "tests_passed": None,
            "tests_run": None,
            "breaches": conclusion or "no-artifact",
            "duration_seconds": None,
            "run_id": run_id,
            "run_url": run_url,
            "conclusion": conclusion,
        }

    tests = report.get("tests", {}) or {}
    signals = report.get("signals", {}) or {}
    breaches_list = signals.get("budget_breaches", []) or []

    return {
        "commit": commit,
        "branch": branch,
        "profile": report.get("profile", "-"),
        "status": report.get("status", "unknown"),
        "tests_passed": tests.get("passed"),
        "tests_run": tests.get("run"),
        "breaches": len(breaches_list),
        "duration_seconds": tests.get("duration_seconds"),
        "run_id": run_id,
        "run_url": run_url,
        "conclusion": conclusion,
    }


def _format_tests(row: dict) -> str:
    if row["tests_passed"] is None or row["tests_run"] is None:
        return "-"
    return f"{row['tests_passed']}/{row['tests_run']}"


def _format_breaches(row: dict) -> str:
    val = row["breaches"]
    return str(val) if val is not None else "-"


def _format_duration(row: dict) -> str:
    if row["duration_seconds"] is None:
        return "-"
    return f"{float(row['duration_seconds']):.1f}s"


def render_grid(rows: list[dict], *, color: bool = False) -> str:
    """Fixed-width text grid; ANSI status colors when `color` is True."""
    headers = ["COMMIT", "BRANCH", "PROFILE", "STATUS",
               "TESTS", "BREACHES", "DURATION", "RUN"]
    cells: list[list[str]] = [headers]
    for r in rows:
        status_text = STATUS_BADGE.get(r["status"], r["status"].upper())
        cells.append([
            r["commit"] or "-",
            r["branch"] or "-",
            r["profile"] or "-",
            status_text,
            _format_tests(r),
            _format_breaches(r),
            _format_duration(r),
            str(r["run_id"]) if r["run_id"] != "" else "-",
        ])

    widths = [max(len(c) for c in col) for col in zip(*cells)]
    lines: list[str] = []
    for i, row_cells in enumerate(cells):
        parts = []
        for j, cell in enumerate(row_cells):
            padded = cell.ljust(widths[j])
            if color and i > 0 and j == 3:  # colorize STATUS column
                key = rows[i - 1]["status"]
                if key in ANSI:
                    padded = f"{ANSI[key]}{padded}{ANSI['reset']}"
            parts.append(padded)
        lines.append("  ".join(parts).rstrip())
    return "\n".join(lines) + "\n"


def render_json(rows: list[dict]) -> str:
    return json.dumps(rows, indent=2, sort_keys=True)


# --- main ------------------------------------------------------------------

def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--workflow", default=DEFAULT_WORKFLOW,
                   help=f"Workflow name (default: {DEFAULT_WORKFLOW!r})")
    p.add_argument("--limit", type=int, default=DEFAULT_LIMIT,
                   help=f"Number of recent runs to list (default: {DEFAULT_LIMIT})")
    p.add_argument("--json", action="store_true",
                   help="Emit JSON instead of a text grid")
    p.add_argument("--no-color", action="store_true",
                   help="Disable ANSI color (auto-disabled when stdout isn't a TTY)")
    p.add_argument("--repo", default=None,
                   help="owner/name (passed to gh -R); default: gh's auto-detection")
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)

    try:
        runs = fetch_runs(args.workflow, args.limit, repo=args.repo)
    except FileNotFoundError:
        print(
            "error: `gh` not found on PATH. Install GitHub CLI from "
            "https://cli.github.com/ and run `gh auth login`.",
            file=sys.stderr,
        )
        return 2
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    rows: list[dict] = []
    for run in runs:
        run_id = run.get("databaseId")
        if run_id is None:
            continue
        with tempfile.TemporaryDirectory(prefix="vivid-gate-trend-") as td:
            try:
                report = fetch_report(int(run_id), Path(td), repo=args.repo)
            except (FileNotFoundError, RuntimeError) as exc:
                print(f"warning: run {run_id}: {exc}", file=sys.stderr)
                report = None
            rows.append(summarize(run, report))

    if args.json:
        sys.stdout.write(render_json(rows) + "\n")
        return 0

    use_color = not args.no_color and sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
    print(f"Recent {args.workflow} runs ({len(rows)}):\n")
    sys.stdout.write(render_grid(rows, color=use_color))
    return 0


if __name__ == "__main__":
    sys.exit(main())
