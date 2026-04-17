#!/usr/bin/env python3
"""Production-gate report generator.

Reads CTest JUnit XML produced by `ctest --output-junit` and emits a
classified, machine-readable summary at the requested output path.

This tool is a pure transformer: it always exits 0 on a successful report
write. The gate target's exit code reflects ctest, not this tool.

See docs/plans/archive/production-gate/production-gate-phase2.md for the schema and design.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import platform
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 4
LOG_EXCERPT_CHARS = 256
LOG_MATCH_CHARS = 1024  # CTest truncates <system-out> at 1KB anyway

# Test names that contribute to phase6 stress duration in the report.
_PHASE6_STRESS_TESTS = frozenset({
    "test_runtime_stress",
    "test_hot_reload_stress",
    "test_package_stress",
    "test_mixed_runtime_stability",
})

# (classification, compiled regex). First match wins.
_CLASSIFICATION_RULES: list[tuple[str, re.Pattern[str]]] = [
    ("webgpu_error", re.compile(r"validation error|device lost|wgpu|webgpu", re.I)),
    ("audio_init",   re.compile(r"miniaudio init|audio device.*(fail|error)|audioengine.*failed", re.I)),
    ("graph_load",   re.compile(r"failed to load graph|FAIL: \S+\.json", re.I)),
    ("missing_operator", re.compile(r"(operator|registry).*(not_found|not_built|abi_mismatch)", re.I)),
    ("crash",        re.compile(r"sigsegv|sigabrt|sigbus|signal \d+|assertion .* failed", re.I)),
]

_MISSING_OPERATOR_NAME = re.compile(
    r"operator\s+['\"]?([A-Za-z0-9_\-]+)['\"]?\s+(?:not_found|not_built|abi_mismatch)",
    re.I,
)


def classify(name: str, log: str, failure_type: str | None) -> str:
    """Return the first-matching classification for a failed test."""
    if failure_type and failure_type.lower() == "timeout":
        return "timeout"
    haystack = log[:LOG_MATCH_CHARS]
    if name == "test_demo_graphs":
        # Always graph_load for the demo loader, regardless of log contents.
        return "graph_load"
    for cls, pat in _CLASSIFICATION_RULES:
        if pat.search(haystack):
            return cls
    return "unknown"


def parse_junit(path: Path) -> list[dict[str, Any]]:
    """Return a list of testcase dicts from one CTest JUnit XML file."""
    tree = ET.parse(path)
    root = tree.getroot()
    suites = root.iter("testsuite") if root.tag == "testsuites" else [root]

    cases: list[dict[str, Any]] = []
    for suite in suites:
        for tc in suite.iter("testcase"):
            failure_el = tc.find("failure")
            skipped_el = tc.find("skipped")
            system_out_el = tc.find("system-out")
            log_text = (system_out_el.text or "") if system_out_el is not None else ""

            labels: list[str] = []
            for prop in tc.iter("property"):
                if prop.get("name") == "cmake_labels":
                    raw = prop.get("value", "") or ""
                    labels = [s for s in raw.split(";") if s]

            failure_msg = ""
            failure_type: str | None = None
            if failure_el is not None:
                failure_type = failure_el.get("type")
                failure_msg = (failure_el.get("message") or "") + "\n" + (failure_el.text or "")

            status = tc.get("status", "run")
            failed = failure_el is not None or status not in ("run", "notrun") and skipped_el is None
            if failure_el is not None:
                failed = True
            skipped = skipped_el is not None or status == "notrun"

            cases.append({
                "name": tc.get("name", ""),
                "classname": tc.get("classname", ""),
                "duration_seconds": float(tc.get("time", "0") or 0),
                "labels": sorted(labels),
                "failed": bool(failed),
                "skipped": bool(skipped) and not failed,
                "log": log_text,
                "failure_msg": failure_msg,
                "failure_type": failure_type,
            })
    return cases


class LastTestLog:
    """Lazy loader + per-test extractor for CTest's non-truncated LastTest.log.

    CTest writes ${BUILD_DIR}/Testing/Temporary/LastTest.log with the full
    unbuffered stdout of every test in the most recent run. Each per-test
    section is delimited by a `N/M Testing: <name>` header and an `Output:`
    block bracketed by dashed lines. We use this when JUnit's `<system-out>`
    (capped at 1KB) doesn't carry enough text for `classify()` to bucket a
    failure — see Phase 7 plan in docs/plans/archive/production-gate/production-gate-phase7.md.
    """

    _SECTION_RE = re.compile(
        r"^\d+/\d+\s+Testing:\s+(?P<name>\S+)\s*$\n.*?^Output:\s*$\n"
        r"^-+\s*$\n(?P<body>.*?)\n^-+\s*$",
        re.MULTILINE | re.DOTALL,
    )

    def __init__(self, log_dir: Path | None):
        self.log_dir = log_dir
        self._index: dict[str, str] | None = None

    def output_for(self, test_name: str) -> str:
        if self.log_dir is None:
            return ""
        if self._index is None:
            self._build_index()
        return self._index.get(test_name, "")

    def _build_index(self) -> None:
        self._index = {}
        if self.log_dir is None:
            return
        log_path = self.log_dir / "LastTest.log"
        try:
            text = log_path.read_text(errors="ignore")
        except OSError:
            return
        for m in self._SECTION_RE.finditer(text):
            self._index[m.group("name")] = m.group("body")


def auto_git_meta(repo_root: Path) -> dict[str, str]:
    def run(*args: str) -> str:
        try:
            out = subprocess.check_output(
                ["git", *args], cwd=repo_root, stderr=subprocess.DEVNULL,
            )
            return out.decode().strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            return ""
    return {"commit": run("rev-parse", "HEAD"),
            "branch": run("rev-parse", "--abbrev-ref", "HEAD")}


def hardware_info() -> dict[str, str]:
    def sysctl(key: str) -> str:
        try:
            out = subprocess.check_output(["sysctl", "-n", key], stderr=subprocess.DEVNULL)
            return out.decode().strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            return ""
    return {
        "machine": platform.machine(),
        "cpu": sysctl("machdep.cpu.brand_string") or platform.processor(),
    }


def load_budgets(toml_path: Path | None) -> list[dict[str, Any]]:
    """Parse production_gate_budgets.toml and return its [[budget]] entries."""
    if toml_path is None or not toml_path.exists():
        return []
    import tomllib
    with toml_path.open("rb") as f:
        data = tomllib.load(f)
    out: list[dict[str, Any]] = []
    for entry in data.get("budget", []):
        if not isinstance(entry, dict) or "code" not in entry:
            continue
        out.append({
            "code": entry["code"],
            "applies_to": entry.get("applies_to", "*"),
            "severity_on_breach": entry.get("severity_on_breach", "warning"),
            "description": entry.get("description", ""),
        })
    return out


def _check_budget(code: str, health: dict[str, Any]) -> str | None:
    """Return a breach message if this budget is breached, else None.

    Adding a new budget code requires extending this switch."""
    g = health.get("graph", {}) or {}
    a = health.get("audio", {}) or {}
    gpu = health.get("gpu", {}) or {}
    if code == "no_graph_load_failures":
        errored = int(g.get("errored_nodes", 0) or 0)
        declared = int(g.get("declared_nodes", 0) or 0)
        compiled = int(g.get("compiled_nodes", 0) or 0)
        if errored > 0 or compiled < declared:
            return f"{errored} errored, {max(0, declared - compiled)} uncompiled"
    elif code == "no_missing_core_operators":
        missing = int(g.get("missing_operators", 0) or 0)
        if missing > 0:
            names = g.get("missing_operator_types", []) or []
            return f"missing: {','.join(names)}" if names else f"{missing} missing operator(s)"
    elif code == "no_dropped_connections":
        dropped = int(g.get("dropped_connections", 0) or 0)
        if dropped > 0:
            return f"{dropped} dropped connection(s)"
    elif code == "no_audio_init_failure":
        if not bool(a.get("running", False)) and int(g.get("audio_nodes", 0) or 0) > 0:
            return "audio engine not running"
    elif code == "no_audio_underruns":
        xruns = int(a.get("xruns", 0) or 0)
        if xruns > 0:
            return f"{xruns} underrun(s)"
    elif code == "no_shader_errors":
        errs = int(gpu.get("shader_errors", 0) or 0)
        if errs > 0:
            return f"{errs} shader error(s)"
    elif code == "no_required_operator_reload_failures":
        hr = health.get("hot_reload", {}) or {}
        if (not hr.get("last_attempt_succeeded", True)
                and hr.get("affects_current_graph", False)):
            return f"hot reload failed for required operator '{hr.get('last_target', '')}'"
    elif code == "no_hot_reload_failures":
        hr = health.get("hot_reload", {}) or {}
        if not hr.get("last_attempt_succeeded", True) and hr.get("last_target"):
            return f"hot reload failed for '{hr.get('last_target', '')}'"
    elif code == "no_package_version_mismatches":
        pkg = health.get("packages", {}) or {}
        n = int(pkg.get("incompatible_updates", 0) or 0)
        if n > 0:
            return f"{n} package(s) with incompatible updates"
    elif code == "no_audio_clipping":
        n = int(a.get("clipping_count", 0) or 0)
        if n > 0:
            return f"{n} audio sample(s) at clipping (peak_max={a.get('peak_max', 0)})"
    elif code == "no_sustained_silence":
        if bool(a.get("silence_active", False)):
            ws = a.get("silence_window_seconds", 0)
            return f"audio silent for {ws:.1f}s"
    elif code == "no_sustained_black":
        if bool(gpu.get("black_active", False)):
            ws = gpu.get("black_window_seconds", 0)
            return f"video black for {ws:.1f}s"
    elif code == "mcp_servers_connected":
        mcp = health.get("mcp", {}) or {}
        # Only fires when at least one server has been seen; never-pinged is silent.
        main_seen = int(mcp.get("main_last_ping_ms", 0) or 0) > 0
        opdev_seen = int(mcp.get("opdev_last_ping_ms", 0) or 0) > 0
        main_ok = bool(mcp.get("main_connected", False))
        opdev_ok = bool(mcp.get("opdev_connected", False))
        bad = []
        if main_seen and not main_ok: bad.append("vivid")
        if opdev_seen and not opdev_ok: bad.append("opdev")
        if bad:
            return f"MCP server(s) disconnected: {', '.join(bad)}"
    return None


def evaluate_budgets(
    budgets: list[dict[str, Any]],
    health_files: list[Path],
) -> list[dict[str, Any]]:
    """Walk every per-graph health JSON and evaluate every applicable budget.

    Returns a flat list of breach dicts:
        {budget_code, graph, severity, message}

    Sorted deterministically (graph, then budget_code) so two runs over the
    same inputs produce byte-identical output."""
    breaches: list[dict[str, Any]] = []
    for hf in sorted(health_files):
        try:
            doc = json.loads(hf.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        graph_name = doc.get("graph", hf.stem)
        domains = set(doc.get("domains", []) or [])
        health = doc.get("health", {}) or {}
        for b in budgets:
            applies_to = b.get("applies_to", "*")
            if applies_to != "*":
                wanted = {tok.strip() for tok in applies_to.split(",") if tok.strip()}
                if not (wanted & domains):
                    continue
            msg = _check_budget(b["code"], health)
            if msg:
                breaches.append({
                    "budget_code": b["code"],
                    "graph": graph_name,
                    "severity": b.get("severity_on_breach", "warning"),
                    "message": msg,
                })
    breaches.sort(key=lambda b: (b["graph"], b["budget_code"]))
    return breaches


def build_report(
    *,
    profile: str,
    junit_paths: list[Path],
    git_meta: dict[str, str],
    build_type: str,
    timestamp: str,
    budgets: list[dict[str, Any]] | None = None,
    health_files: list[Path] | None = None,
    last_test_log: LastTestLog | None = None,
) -> dict[str, Any]:
    cases: list[dict[str, Any]] = []
    for p in junit_paths:
        cases.extend(parse_junit(p))
    last_test_log = last_test_log or LastTestLog(None)

    failures: list[dict[str, Any]] = []
    signals: dict[str, Any] = {
        "webgpu_validation_errors": 0,
        "audio_init_failures": 0,
        "graph_load_failures": 0,
        "missing_operators": [],
        "budget_breaches": [],
    }

    passed = failed = skipped = 0
    duration = 0.0

    for c in cases:
        duration += c["duration_seconds"]
        if c["failed"]:
            failed += 1
            log_for_class = c["log"] + "\n" + c["failure_msg"]
            cls = classify(c["name"], log_for_class, c["failure_type"])
            # Fallback: when CTest's 1KB <system-out> truncation drops the
            # discriminating signature, re-classify against the full per-test
            # log from LastTest.log. No-op if --ctest-log-dir wasn't provided.
            if cls == "unknown":
                fuller = last_test_log.output_for(c["name"])
                if fuller:
                    log_for_class = fuller + "\n" + c["failure_msg"]
                    cls = classify(c["name"], log_for_class, c["failure_type"])
            failures.append({
                "name": c["name"],
                "classname": c["classname"],
                "duration_seconds": c["duration_seconds"],
                "labels": c["labels"],
                "classification": cls,
                "log_excerpt": log_for_class.strip()[:LOG_EXCERPT_CHARS],
            })
            if cls == "webgpu_error":
                signals["webgpu_validation_errors"] += 1
            elif cls == "audio_init":
                signals["audio_init_failures"] += 1
            elif cls == "graph_load":
                signals["graph_load_failures"] += 1
            elif cls == "missing_operator":
                m = _MISSING_OPERATOR_NAME.search(log_for_class[:LOG_MATCH_CHARS])
                if m:
                    name = m.group(1)
                    if name not in signals["missing_operators"]:
                        signals["missing_operators"].append(name)
        elif c["skipped"]:
            skipped += 1
        else:
            passed += 1

    failures.sort(key=lambda f: f["name"])
    signals["missing_operators"].sort()

    breaches = evaluate_budgets(budgets or [], health_files or [])
    signals["budget_breaches"] = breaches

    if failed > 0:
        status = "fail"
    elif any(b["severity"] in ("error", "fatal") for b in breaches):
        status = "fail"
    elif any(b["severity"] == "warning" for b in breaches):
        status = "degraded"
    else:
        status = "pass"

    # Sum runtimes of phase6 stress tests if they ran in this profile (soak
    # only). Cumulative-profile JUnits include them transitively from the
    # earlier ctest invocations; if absent the field is omitted.
    phase6_stress_seconds = round(
        sum(c["duration_seconds"] for c in cases if c["name"] in _PHASE6_STRESS_TESTS),
        3,
    )

    report: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "timestamp": timestamp,
        "profile": profile,
        "git": git_meta,
        "build": {
            "build_type": build_type,
            "macos_version": platform.mac_ver()[0],
            "hardware": hardware_info(),
        },
        "tests": {
            "run": passed + failed,
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "duration_seconds": round(duration, 3),
            "failures": failures,
        },
        "signals": signals,
        "status": status,
    }
    # Stress block only when at least one phase6 test was observed. Avoids
    # claiming "0 seconds" for profiles that never ran stress.
    if phase6_stress_seconds > 0:
        report["stress"] = {"phase6_stress_seconds": phase6_stress_seconds}
    return report


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    p.add_argument("--profile", required=True, choices=["core", "gui", "env", "soak"])
    p.add_argument("--junit", action="append", required=True, type=Path,
                   help="CTest JUnit XML to consume; pass multiple times for cumulative profiles.")
    p.add_argument("--output", required=True, type=Path,
                   help="Where to write the production-gate.json report.")
    p.add_argument("--commit", default=None)
    p.add_argument("--branch", default=None)
    p.add_argument("--build-type", default="")
    p.add_argument("--git-meta-from-git", action="store_true",
                   help="Auto-fill commit/branch from `git` calls in the repo root.")
    p.add_argument("--repo-root", type=Path, default=Path.cwd(),
                   help="Repo root for --git-meta-from-git lookups (default: cwd).")
    p.add_argument("--health-json", action="append", type=Path, default=None,
                   help="Single per-graph health JSON to consume; may be repeated. "
                        "For directory globs use --health-dir instead.")
    p.add_argument("--health-dir", type=Path, default=None,
                   help="Directory of per-graph health JSONs (build/reports/health/). "
                        "All *.json files under it are evaluated against --budgets.")
    p.add_argument("--budgets", type=Path, default=None,
                   help="Path to production_gate_budgets.toml. If omitted, no budget "
                        "evaluation runs.")
    p.add_argument("--ctest-log-dir", type=Path, default=None,
                   help="Path to CTest's Testing/Temporary directory (which contains "
                        "LastTest.log). When a failure's truncated <system-out> "
                        "classifies as 'unknown', the tool re-classifies against the "
                        "full per-test log from LastTest.log. Default: derived from "
                        "the first --junit's parent dir if not set.")
    p.add_argument("--timestamp", default=None,
                   help="Override the timestamp (ISO-8601). Useful for tests/determinism.")
    p.add_argument("--strict", action="store_true",
                   help="Exit non-zero (1) when the computed status is 'fail'. "
                        "The gate uses this so a budget-error breach surfaces as a "
                        "build failure even when ctest itself exited 0. Without "
                        "--strict the tool always exits 0 (transformer mode).")
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    git_meta = {"commit": "", "branch": ""}
    if args.git_meta_from_git:
        git_meta = auto_git_meta(args.repo_root)
    if args.commit is not None:
        git_meta["commit"] = args.commit
    if args.branch is not None:
        git_meta["branch"] = args.branch

    timestamp = args.timestamp or _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    health_files: list[Path] = []
    if args.health_dir is not None and args.health_dir.is_dir():
        health_files.extend(sorted(args.health_dir.glob("*.json")))
    if args.health_json:
        health_files.extend(args.health_json)
    budgets = load_budgets(args.budgets)

    # Default ctest-log-dir: <build>/Testing/Temporary, derived from the first
    # --junit's grandparent (build/reports/ctest-X.xml → build/Testing/Temporary).
    ctest_log_dir = args.ctest_log_dir
    if ctest_log_dir is None and args.junit:
        candidate = args.junit[0].parent.parent / "Testing" / "Temporary"
        if candidate.is_dir():
            ctest_log_dir = candidate
    last_test_log = LastTestLog(ctest_log_dir)

    report = build_report(
        profile=args.profile,
        junit_paths=args.junit,
        git_meta=git_meta,
        build_type=args.build_type,
        timestamp=timestamp,
        budgets=budgets,
        health_files=health_files,
        last_test_log=last_test_log,
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"[production_gate_report] wrote {args.output} (status={report['status']})", file=sys.stderr)
    if args.strict and report["status"] == "fail":
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
