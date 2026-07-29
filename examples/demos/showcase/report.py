"""Report writers for the showcase QA harness — per-showcase JSON, the index gate, and a
human-readable summary table. Serialization only; the verdicts come from gates.py.
"""
from __future__ import annotations

import json
from dataclasses import asdict
from pathlib import Path

from .gates import ShowcaseResult


def showcase_report(result: ShowcaseResult, meta: dict | None = None) -> dict:
    """The per-showcase report dict written to reports/<id>.json."""
    return {
        **(meta or {}),
        "id": result.id,
        "adr0037_type": result.adr0037_type,
        "title": result.title,
        "kind": result.kind,
        "target": result.target,
        "gate": result.gate,
        "reasons": result.reasons,
        "hero": {
            "path": result.hero_path,
            "captured": result.captured,
            "is_blank": result.is_blank,
            "warm_attempts": result.warm_attempts,
            "analysis": result.image_stats,
        },
        "video": {
            "path": result.video_path,
            "captured": result.video_captured,
            "frames": result.video_frames,
            "motion_score": result.video_motion,
        },
        "verify": {
            "validate_project": {
                "valid": result.valid,
                "degraded": result.degraded,
                "issues": result.validate_issues,
            },
            "health_severity": result.health_severity,
            "quality": {
                "overall": result.quality_overall,
                "checks": result.quality_checks,
            },
        },
        "prereqs_missing": result.prereqs_missing,
        "steps": result.steps,
    }


def write_showcase(out_dir: Path, result: ShowcaseResult, meta: dict | None = None) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{result.id}.json"
    path.write_text(json.dumps(showcase_report(result, meta), indent=2, default=str))
    return path


def write_index(out_dir: Path, index: dict) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "index.json"
    path.write_text(json.dumps(index, indent=2, default=str))
    return path


_GATE_MARK = {"pass": "PASS", "warn": "WARN", "fail": "FAIL", "missing": "----"}


def print_summary(index: dict, as_json: bool = False) -> None:
    if as_json:
        print(json.dumps(index, default=str))
        return

    caps = index.get("capabilities", {})
    print("\nShowcase QA — ADR-0037 gate\n" + "=" * 60)
    print("capabilities: " + ", ".join(f"{k}={'yes' if v else 'no'}" for k, v in caps.items()))
    print(f"{'GATE':<6} {'TYPE':<5} {'ID':<16} {'BRIGHT':<7} REASONS")
    print("-" * 60)
    for s in index.get("showcases", []):
        mark = _GATE_MARK.get(s["gate"], s["gate"])
        bright = s.get("brightness")
        bright_s = f"{bright:.2f}" if isinstance(bright, (int, float)) else "-"
        reasons = "; ".join(s.get("reasons", [])) or "-"
        print(f"{mark:<6} {s['type']:<5} {s['id']:<16} {bright_s:<7} {reasons}")
    print("-" * 60)
    cov = index.get("adr0037_coverage", {})
    cov_s = "  ".join(f"{t}:{_GATE_MARK.get(g, g)}" for t, g in sorted(cov.items()))
    print("coverage: " + cov_s)
    c = index.get("counts", {})
    print(f"counts: pass={c.get('pass', 0)} warn={c.get('warn', 0)} fail={c.get('fail', 0)}")
    print(f"OVERALL GATE: {index.get('gate', '?').upper()}\n")
