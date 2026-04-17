#!/usr/bin/env python3
"""Format a production-gate.json into a PR-comment markdown body.

Emits markdown to stdout; emits GitHub Actions annotations to stderr in
``::warning::`` / ``::error::`` syntax. Exits 0 in all normal cases (including
a missing report file, which produces a "no report" body so the PR comment
still updates with a useful diagnostic).

The leading hidden HTML marker (``<!-- production-gate -->``) lets the
upserting workflow find an existing comment and edit it in place.

See docs/plans/production-gate-phase9b.md for the design.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

MARKER = "<!-- production-gate -->"
STATUS_BADGE = {"pass": "✅", "degraded": "⚠️", "fail": "❌"}

# Per-budget cap on annotations written to stderr. GitHub silently caps total
# annotations per run anyway; this keeps the noisiest budgets from monopolising
# that quota.
MAX_ANNOTATIONS_PER_BUDGET = 3
# Per-test-failure cap on annotations.
MAX_TEST_FAILURE_ANNOTATIONS = 10


def _annotate(level: str, title: str, message: str) -> None:
    """Write a GitHub Actions annotation line to stderr."""
    # Strip newlines: GitHub treats them as command terminators.
    safe_title = title.replace("\n", " ").replace("\r", " ")
    safe_message = message.replace("\n", " ").replace("\r", " ")
    print(f"::{level} title={safe_title}::{safe_message}", file=sys.stderr)


def _format_breach_message(b: dict[str, Any]) -> str:
    msg = b.get("message", "")
    return f"{b.get('graph', '?')}: {msg}" if msg else b.get("graph", "?")


def render(d: dict[str, Any], workflow_run_url: str, max_per_budget: int) -> str:
    """Render the markdown comment body from a parsed production-gate.json."""
    status = d.get("status", "unknown")
    badge = STATUS_BADGE.get(status, "❓")
    tests = d.get("tests", {}) or {}
    git = d.get("git", {}) or {}

    lines: list[str] = [MARKER, ""]
    lines.append(f"## Production Gate · {badge} {status}")
    lines.append("")
    lines.append("| | |")
    lines.append("|---|---|")
    lines.append(
        f"| **Tests** | {tests.get('passed', 0)} / {tests.get('run', 0)} passed |"
    )
    lines.append(f"| **Status** | `{status}` |")
    duration = tests.get("duration_seconds", 0) or 0
    lines.append(f"| **Wall time** | {float(duration):.1f}s |")
    lines.append(f"| **Profile** | {d.get('profile', '?')} |")
    commit = git.get("commit", "") or ""
    lines.append(f"| **Commit** | `{commit[:8]}` |")
    lines.append("")

    # Test failures (if any).
    failures = tests.get("failures", []) or []
    if failures:
        lines.append(f"### {len(failures)} test failure(s)")
        for f in failures[:max_per_budget]:
            name = f.get("name", "?")
            cls = f.get("classification", "?")
            lines.append(f"- `{name}` — `{cls}`")
        if len(failures) > max_per_budget:
            lines.append(f"- … +{len(failures) - max_per_budget} more")
        lines.append("")
        for f in failures[:MAX_TEST_FAILURE_ANNOTATIONS]:
            excerpt = (f.get("log_excerpt", "") or "")[:200]
            _annotate(
                "error",
                f.get("name", "?"),
                f"{f.get('classification', '?')}: {excerpt}".strip(),
            )

    # Budget breaches grouped by code, sorted most-frequent first.
    breaches = (d.get("signals", {}) or {}).get("budget_breaches", []) or []
    if breaches:
        by_code: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for b in breaches:
            by_code[b.get("budget_code", "unknown")].append(b)

        lines.append(f"### {len(breaches)} budget breach(es)")
        for code, entries in sorted(by_code.items(), key=lambda kv: (-len(kv[1]), kv[0])):
            heads = [e.get("graph", "?") for e in entries[:max_per_budget]]
            tail = (
                f", … +{len(entries) - max_per_budget} more"
                if len(entries) > max_per_budget
                else ""
            )
            graph_list = ", ".join(f"`{g}`" for g in heads) + tail
            lines.append(f"- `{code}` ({len(entries)}): {graph_list}")
            for b in entries[:MAX_ANNOTATIONS_PER_BUDGET]:
                level = "error" if b.get("severity") in ("error", "fatal") else "warning"
                _annotate(
                    level,
                    f"{b.get('budget_code', '?')}/{b.get('graph', '?')}",
                    b.get("message", ""),
                )
        lines.append("")

    if workflow_run_url:
        lines.append(f"📦 [Workflow run + artifacts]({workflow_run_url})")
        lines.append("")

    lines.append(
        "<sub>Updated automatically by "
        "[production-gate-pr.yml](.github/workflows/production-gate-pr.yml). "
        "See [docs/testing/production-gate.md](docs/testing/production-gate.md).</sub>"
    )
    return "\n".join(lines) + "\n"


def render_missing(report_path: Path) -> str:
    """Render a fallback body when the report file is absent."""
    lines = [
        MARKER,
        "",
        "## Production Gate · ❓ no report",
        "",
        f"The gate didn't produce a report at `{report_path}` this run. "
        "Check the workflow log for build or test failures that prevented "
        "`production_gate_report.py` from running.",
        "",
        "<sub>Updated automatically by "
        "[production-gate-pr.yml](.github/workflows/production-gate-pr.yml). "
        "See [docs/testing/production-gate.md](docs/testing/production-gate.md).</sub>",
    ]
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("report", type=Path, help="Path to production-gate.json")
    p.add_argument("--workflow-run-url", default="", help="URL surfaced in the comment footer")
    p.add_argument(
        "--max-graphs-per-budget",
        type=int,
        default=5,
        help="Cap per-budget breach list (default: 5)",
    )
    args = p.parse_args(argv)

    if not args.report.exists():
        sys.stdout.write(render_missing(args.report))
        _annotate("warning", "production-gate", f"missing report: {args.report}")
        return 0

    try:
        d = json.loads(args.report.read_text())
    except json.JSONDecodeError as exc:
        sys.stdout.write(render_missing(args.report))
        _annotate("error", "production-gate", f"invalid JSON in {args.report}: {exc}")
        return 0

    sys.stdout.write(render(d, args.workflow_run_url, args.max_graphs_per_budget))
    return 0


if __name__ == "__main__":
    sys.exit(main())
