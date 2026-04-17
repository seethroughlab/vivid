"""Tests for tools/format_pr_comment.py."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import format_pr_comment as fpc  # noqa: E402

REPORTS = Path(__file__).parent / "fixtures" / "reports"


def _run(report_path: Path, capsys, *, url: str = "", max_per: int = 5) -> tuple[str, str]:
    """Invoke main() with the given report; return (stdout, stderr)."""
    argv = [str(report_path), "--max-graphs-per-budget", str(max_per)]
    if url:
        argv += ["--workflow-run-url", url]
    rc = fpc.main(argv)
    captured = capsys.readouterr()
    assert rc == 0
    return captured.out, captured.err


# --- pass status -----------------------------------------------------------

def test_passing_report_renders_check_badge(capsys):
    out, err = _run(REPORTS / "passing.json", capsys)
    assert out.startswith(fpc.MARKER + "\n")
    assert "✅ pass" in out
    assert "21 / 21 passed" in out
    assert "32.4s" in out
    assert "`abcdef12`" in out
    # No breach or failure section.
    assert "budget breach" not in out
    assert "test failure" not in out
    # No annotations on a clean run.
    assert err == ""


# --- degraded status -------------------------------------------------------

def test_degraded_report_groups_breaches_by_count(capsys):
    out, err = _run(REPORTS / "degraded.json", capsys, url="https://example/runs/123")
    assert "⚠️ degraded" in out
    assert "14 budget breach(es)" in out
    # Most-frequent budget surfaces first.
    black_idx = out.index("`no_sustained_black`")
    clip_idx = out.index("`no_audio_clipping`")
    underrun_idx = out.index("`no_audio_underruns`")
    assert black_idx < clip_idx < underrun_idx
    # Top-5 cap with overflow message.
    assert "+2 more" in out  # 7 black breaches → 5 shown + "+2 more"
    assert "+1 more" in out  # 6 clipping breaches → 5 shown + "+1 more"
    # Workflow link present.
    assert "https://example/runs/123" in out
    # Stderr annotations: warning level for warnings, capped at 3 per code.
    assert "::warning title=no_sustained_black/" in err
    assert "::warning title=no_audio_clipping/" in err
    assert err.count("::warning title=no_sustained_black/") == 3
    assert err.count("::warning title=no_audio_clipping/") == 3
    assert err.count("::warning title=no_audio_underruns/") == 1


def test_degraded_report_default_cap_is_five(capsys):
    out, _ = _run(REPORTS / "degraded.json", capsys)
    # Default --max-graphs-per-budget is 5.
    assert "+2 more" in out


# --- fail status -----------------------------------------------------------

def test_failing_report_lists_test_failures_and_errors(capsys):
    out, err = _run(REPORTS / "fail.json", capsys)
    assert "❌ fail" in out
    assert "2 test failure(s)" in out
    assert "`test_demo_graphs`" in out
    assert "`graph_load`" in out
    assert "`test_runtime_health_snapshot`" in out
    assert "`crash`" in out
    # Error budgets escalate to ::error:: annotations; warnings stay ::warning::.
    assert "::error title=no_missing_core_operators/synth_demo::" in err
    assert "::warning title=no_audio_clipping/filter_sweep::" in err
    # Test failures emit ::error:: annotations too.
    assert "::error title=test_demo_graphs::" in err
    assert "::error title=test_runtime_health_snapshot::" in err


# --- missing / malformed ---------------------------------------------------

def test_missing_report_emits_no_report_body(capsys, tmp_path):
    missing = tmp_path / "does-not-exist.json"
    out, err = _run(missing, capsys)
    assert out.startswith(fpc.MARKER + "\n")
    assert "❓ no report" in out
    assert str(missing) in out
    assert "::warning title=production-gate::" in err


def test_invalid_json_falls_back_to_no_report_body(capsys, tmp_path):
    bad = tmp_path / "bad.json"
    bad.write_text("{not json")
    out, err = _run(bad, capsys)
    assert "❓ no report" in out
    assert "::error title=production-gate::" in err
    assert "invalid JSON" in err


# --- marker invariant ------------------------------------------------------

@pytest.mark.parametrize("name", ["passing.json", "degraded.json", "fail.json"])
def test_marker_always_first_line(capsys, name):
    out, _ = _run(REPORTS / name, capsys)
    # Marker must be the literal first line so the upserter can locate the
    # comment by substring search regardless of what follows.
    assert out.split("\n", 1)[0] == fpc.MARKER


# --- annotation safety -----------------------------------------------------

def test_annotation_strips_newlines(capsys, tmp_path):
    # Forge a report with an embedded newline in a breach message — the
    # formatter must escape it so GitHub doesn't terminate the workflow command.
    payload = {
        "schema_version": 4,
        "status": "degraded",
        "profile": "core",
        "git": {"commit": "deadbeef", "branch": "x"},
        "tests": {"run": 1, "passed": 1, "failed": 0, "skipped": 0,
                  "duration_seconds": 1.0, "failures": []},
        "signals": {"budget_breaches": [
            {"budget_code": "code", "graph": "g", "severity": "warning",
             "message": "line1\nline2"}
        ]},
    }
    import json
    p = tmp_path / "r.json"
    p.write_text(json.dumps(payload))
    _, err = _run(p, capsys)
    # No raw newline inside the annotation directive.
    annotation_lines = [l for l in err.splitlines() if l.startswith("::warning")]
    assert annotation_lines
    for line in annotation_lines:
        assert "\\n" not in line  # we use a space, not a literal backslash-n
        # The message portion should be on a single line.
        assert "line1 line2" in line


# --- workflow URL footer ---------------------------------------------------

def test_workflow_url_omitted_when_blank(capsys):
    out, _ = _run(REPORTS / "passing.json", capsys, url="")
    assert "Workflow run + artifacts" not in out


def test_workflow_url_included_when_provided(capsys):
    out, _ = _run(REPORTS / "passing.json", capsys, url="https://gh/runs/9")
    assert "[Workflow run + artifacts](https://gh/runs/9)" in out
