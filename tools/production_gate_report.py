# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Production-gate report tool (P4.2).

Parses a ctest JUnit XML into a normalized gate report and evaluates it against the
budgets in production_gate_budgets.toml. Emits build/reports/production-gate.json and,
with --strict, exits non-zero when the gate is not `pass` (so CI can block on it).

The gate's own correctness is covered by --selftest (a synthetic doc through the same
parse + classify path), which the CMake `production_gate_core` target runs first — the
tool that judges the suite is itself judged before it judges.

Usage:
  uv run tools/production_gate_report.py --junit build/reports/ctest-core.xml \\
      --profile core --strict
  uv run tools/production_gate_report.py --selftest
"""
from __future__ import annotations

import argparse
import json
import sys
import tomllib
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

SCHEMA_VERSION = 1


@dataclass
class TestTotals:
    run: int = 0
    passed: int = 0
    failed: int = 0
    duration_seconds: float = 0.0
    failed_names: list[str] = field(default_factory=list)


def parse_junit(xml_text: str) -> TestTotals:
    """Sum a ctest JUnit document into pass/fail/duration totals. A testcase counts as
    failed if it carries a <failure>/<error> child or a status/result attribute that
    looks like a failure — robust across ctest versions."""
    root = ET.fromstring(xml_text)
    totals = TestTotals()
    for tc in root.iter("testcase"):
        totals.run += 1
        try:
            totals.duration_seconds += float(tc.get("time", "0") or 0)
        except ValueError:
            pass
        has_fail_child = any(c.tag in ("failure", "error") for c in tc)
        status = (tc.get("status") or tc.get("result") or "").lower()
        is_fail = has_fail_child or "fail" in status or status == "error"
        if is_fail:
            totals.failed += 1
            totals.failed_names.append(tc.get("name", "?"))
        else:
            totals.passed += 1
    return totals


def load_budgets(path: Path | None) -> dict:
    if path and path.exists():
        with path.open("rb") as f:
            return tomllib.load(f).get("budgets", {})
    return {}


def evaluate(totals: TestTotals, budgets: dict) -> tuple[str, list[dict]]:
    """Return (status, breaches). status: pass | degraded | fail.
    error-severity breaches -> fail; warning-severity -> degraded."""
    breaches: list[dict] = []

    max_failures = budgets.get("max_failures", 0)
    if totals.failed > max_failures:
        breaches.append({"code": "test_failures", "severity": "error",
                         "detail": f"{totals.failed} failed (max {max_failures}): "
                                   + ", ".join(totals.failed_names)})

    min_tests = budgets.get("min_tests")
    if min_tests is not None and totals.run < min_tests:
        breaches.append({"code": "too_few_tests", "severity": "error",
                         "detail": f"{totals.run} ran (min {min_tests})"})

    max_dur = budgets.get("max_duration_seconds")
    if max_dur is not None and totals.duration_seconds > max_dur:
        breaches.append({"code": "slow_gate", "severity": "warning",
                         "detail": f"{totals.duration_seconds:.1f}s (budget {max_dur}s)"})

    if any(b["severity"] == "error" for b in breaches):
        return "fail", breaches
    if breaches:
        return "degraded", breaches
    return "pass", breaches


def build_report(profile: str, totals: TestTotals, budgets: dict) -> dict:
    status, breaches = evaluate(totals, budgets)
    return {
        "schema_version": SCHEMA_VERSION,
        "profile": profile,
        "tests": {
            "run": totals.run,
            "passed": totals.passed,
            "failed": totals.failed,
            "duration_seconds": round(totals.duration_seconds, 3),
        },
        "signals": {"budget_breaches": breaches},
        "status": status,
    }


_SELFTEST_XML = """<?xml version="1.0"?>
<testsuite tests="3" failures="1">
  <testcase name="t_ok_a" time="0.10"/>
  <testcase name="t_ok_b" time="0.20" status="run"/>
  <testcase name="t_bad" time="0.30"><failure>boom</failure></testcase>
</testsuite>"""


def selftest() -> int:
    totals = parse_junit(_SELFTEST_XML)
    assert totals.run == 3, totals
    assert totals.passed == 2, totals
    assert totals.failed == 1, totals
    assert abs(totals.duration_seconds - 0.60) < 1e-6, totals
    # A failing suite must classify as fail under the default zero-failure budget.
    status, breaches = evaluate(totals, {"max_failures": 0})
    assert status == "fail", (status, breaches)
    # An all-green suite passes.
    clean = parse_junit('<testsuite><testcase name="a" time="0.1"/></testsuite>')
    assert evaluate(clean, {"max_failures": 0, "min_tests": 1})[0] == "pass"
    # A slow but green suite degrades (warning), not fails.
    assert evaluate(clean, {"max_duration_seconds": 0.0})[0] == "degraded"
    print("production_gate_report selftest: OK")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Vivid production-gate report tool")
    ap.add_argument("--junit", type=Path, help="ctest JUnit XML to parse")
    ap.add_argument("--profile", default="core")
    ap.add_argument("--budgets", type=Path,
                    default=Path(__file__).with_name("production_gate_budgets.toml"))
    ap.add_argument("--output", type=Path, default=Path("build/reports/production-gate.json"))
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero unless status is pass")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if not args.junit or not args.junit.exists():
        print(f"error: --junit file not found: {args.junit}", file=sys.stderr)
        return 2

    totals = parse_junit(args.junit.read_text())
    budgets = load_budgets(args.budgets)
    report = build_report(args.profile, totals, budgets)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")

    t = report["tests"]
    print(f"[gate:{args.profile}] {report['status'].upper()} — "
          f"{t['passed']}/{t['run']} passed, {t['failed']} failed, "
          f"{t['duration_seconds']}s  -> {args.output}")
    for b in report["signals"]["budget_breaches"]:
        print(f"  {b['severity']}: {b['code']} — {b['detail']}")

    if args.strict and report["status"] != "pass":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
