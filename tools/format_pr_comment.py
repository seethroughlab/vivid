# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Format a production-gate.json into a Markdown PR comment (P4.5).

Used by production-gate-pr.yml to upsert a single status comment on the PR. Pure stdlib +
unit-smoke-tested (--selftest), so the formatting is verified even though posting the
comment needs CI.

  uv run tools/format_pr_comment.py app/build/reports/production-gate.json
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

MARKER = "<!-- production-gate -->"
BADGE = {"pass": "✅ PASS", "degraded": "⚠️ DEGRADED", "fail": "❌ FAIL"}


def format_comment(report: dict) -> str:
    status = report.get("status", "fail")
    t = report.get("tests", {})
    lines = [
        MARKER,
        f"### Production gate ({report.get('profile', '?')}): {BADGE.get(status, status)}",
        "",
        f"- **{t.get('passed', 0)}/{t.get('run', 0)}** tests passed "
        f"({t.get('failed', 0)} failed) in {t.get('duration_seconds', 0)}s",
    ]
    breaches = report.get("signals", {}).get("budget_breaches", [])
    if breaches:
        lines.append("- Budget breaches:")
        for b in breaches:
            lines.append(f"  - **{b.get('severity', '?')}**: {b.get('code', '?')} — {b.get('detail', '')}")
    return "\n".join(lines) + "\n"


def selftest() -> int:
    passing = {"profile": "core", "status": "pass",
               "tests": {"run": 14, "passed": 14, "failed": 0, "duration_seconds": 0.6},
               "signals": {"budget_breaches": []}}
    out = format_comment(passing)
    assert MARKER in out and "✅ PASS" in out and "14/14" in out, out
    failing = {"profile": "core", "status": "fail",
               "tests": {"run": 14, "passed": 13, "failed": 1, "duration_seconds": 0.6},
               "signals": {"budget_breaches": [{"severity": "error", "code": "test_failures",
                                                "detail": "1 failed (max 0): t_x"}]}}
    outf = format_comment(failing)
    assert "❌ FAIL" in outf and "test_failures" in outf, outf
    print("format_pr_comment selftest: OK")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("report", nargs="?", type=Path)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.report or not args.report.exists():
        print("error: report json not found", file=sys.stderr)
        return 2
    sys.stdout.write(format_comment(json.loads(args.report.read_text())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
