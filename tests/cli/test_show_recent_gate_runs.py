"""Tests for tools/show_recent_gate_runs.py."""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import show_recent_gate_runs as sr  # noqa: E402

REPORTS = Path(__file__).parent / "fixtures" / "reports"


def _load_report(name: str) -> dict:
    return json.loads((REPORTS / name).read_text())


def _run_row(**overrides) -> dict:
    """Build a synthetic `gh run list` row."""
    base = {
        "databaseId": 14238001,
        "headSha": "db8868f62036d75e58f6e6f9c6de63980ca69503",
        "headBranch": "master",
        "status": "completed",
        "conclusion": "success",
        "displayTitle": "Update something",
        "createdAt": "2026-04-15T12:00:00Z",
        "url": "https://github.com/vivid/vivid/actions/runs/14238001",
        "workflowName": "Smoke Tests",
    }
    base.update(overrides)
    return base


# --- summarize -------------------------------------------------------------

def test_summarize_passing_run():
    row = sr.summarize(_run_row(), _load_report("passing.json"))
    assert row["status"] == "pass"
    assert row["breaches"] == 0
    assert row["tests_passed"] == 21
    assert row["tests_run"] == 21
    assert row["profile"] == "core"
    assert row["commit"] == "db8868f"
    assert row["branch"] == "master"
    assert row["duration_seconds"] == 32.4


def test_summarize_degraded_run():
    row = sr.summarize(_run_row(), _load_report("degraded.json"))
    assert row["status"] == "degraded"
    assert row["breaches"] == 14


def test_summarize_fail_run():
    row = sr.summarize(_run_row(conclusion="failure"), _load_report("fail.json"))
    assert row["status"] == "fail"
    assert row["breaches"] == 2


def test_summarize_no_report_when_artifact_missing():
    run = _run_row(conclusion="failure")
    row = sr.summarize(run, None)
    assert row["status"] == "no-report"
    assert row["breaches"] == "failure"
    assert row["tests_passed"] is None
    assert row["tests_run"] is None
    assert row["duration_seconds"] is None
    assert row["profile"] == "-"
    # Run identity preserved so the user can drill down.
    assert row["run_id"] == 14238001
    assert row["commit"] == "db8868f"


# --- render_grid -----------------------------------------------------------

def test_render_grid_columns_aligned():
    rows = [
        sr.summarize(_run_row(), _load_report("passing.json")),
        sr.summarize(_run_row(databaseId=14238002), _load_report("degraded.json")),
        sr.summarize(_run_row(databaseId=14238003, conclusion="failure"), None),
    ]
    grid = sr.render_grid(rows)
    lines = [l for l in grid.splitlines() if l.strip()]
    assert len(lines) == 1 + len(rows)  # header + N rows
    header_fields = lines[0].split()
    expected_headers = ["COMMIT", "BRANCH", "PROFILE", "STATUS",
                        "TESTS", "BREACHES", "DURATION", "RUN"]
    assert header_fields == expected_headers
    # Every data row has exactly len(headers) whitespace-delimited fields.
    for line in lines[1:]:
        assert len(line.split()) == len(expected_headers), f"misaligned: {line!r}"


def test_render_grid_truncates_commit_to_seven_chars():
    rows = [sr.summarize(_run_row(), _load_report("passing.json"))]
    grid = sr.render_grid(rows)
    # The full sha was 40 chars; row should carry only the leading 7.
    assert "db8868f" in grid
    assert "db8868f6" not in grid


def test_render_grid_color_only_on_status_column():
    rows = [sr.summarize(_run_row(), _load_report("degraded.json"))]
    plain = sr.render_grid(rows, color=False)
    colored = sr.render_grid(rows, color=True)
    assert "\033[" not in plain
    assert "\033[33m" in colored  # warning yellow
    assert colored.count("\033[0m") == 1  # exactly one reset


# --- render_json -----------------------------------------------------------

def test_render_json_round_trips():
    rows = [
        sr.summarize(_run_row(), _load_report("passing.json")),
        sr.summarize(_run_row(databaseId=14238002, conclusion="failure"), None),
    ]
    parsed = json.loads(sr.render_json(rows))
    assert parsed == rows
    assert isinstance(parsed, list)
    assert len(parsed) == 2


# --- main with mocked fetchers --------------------------------------------

def _install_fakes(monkeypatch, runs, reports_by_run_id):
    monkeypatch.setattr(sr, "fetch_runs", lambda workflow, limit, repo=None: runs)

    def fake_fetch_report(run_id, tmp_dir, repo=None):
        return reports_by_run_id.get(int(run_id))
    monkeypatch.setattr(sr, "fetch_report", fake_fetch_report)


def test_main_uses_fetch_overrides(monkeypatch, capsys):
    runs = [
        _run_row(databaseId=1, conclusion="success"),
        _run_row(databaseId=2, conclusion="success"),
        _run_row(databaseId=3, conclusion="failure"),
    ]
    reports = {
        1: _load_report("passing.json"),
        2: _load_report("degraded.json"),
        # 3 → no report, will render as no-report row.
    }
    _install_fakes(monkeypatch, runs, reports)

    rc = sr.main(["--no-color"])
    out = capsys.readouterr().out
    assert rc == 0
    assert "Recent Smoke Tests runs (3):" in out
    assert "OK" in out
    assert "WARN" in out
    assert "????" in out  # no-report badge


def test_main_handles_gh_not_found(monkeypatch, capsys):
    def boom(*args, **kwargs):
        raise FileNotFoundError("gh")
    monkeypatch.setattr(sr, "fetch_runs", boom)

    rc = sr.main([])
    captured = capsys.readouterr()
    assert rc == 2
    assert "gh auth login" in captured.err
    assert captured.out == ""


def test_main_handles_runtime_error_from_gh(monkeypatch, capsys):
    def boom(*args, **kwargs):
        raise RuntimeError("auth required: HTTP 401")
    monkeypatch.setattr(sr, "fetch_runs", boom)

    rc = sr.main([])
    captured = capsys.readouterr()
    assert rc == 2
    assert "auth required" in captured.err


def test_main_json_flag(monkeypatch, capsys):
    runs = [_run_row(databaseId=1)]
    reports = {1: _load_report("passing.json")}
    _install_fakes(monkeypatch, runs, reports)

    rc = sr.main(["--json"])
    out = capsys.readouterr().out
    assert rc == 0
    parsed = json.loads(out)
    assert isinstance(parsed, list)
    assert len(parsed) == 1
    assert parsed[0]["status"] == "pass"
    assert parsed[0]["breaches"] == 0


def test_main_passes_workflow_and_limit(monkeypatch, capsys):
    seen: dict = {}

    def fake_runs(workflow, limit, repo=None):
        seen["workflow"] = workflow
        seen["limit"] = limit
        seen["repo"] = repo
        return []
    monkeypatch.setattr(sr, "fetch_runs", fake_runs)

    rc = sr.main(["--workflow", "Production Gate (PR)", "--limit", "3",
                  "--repo", "owner/name", "--no-color"])
    assert rc == 0
    assert seen == {
        "workflow": "Production Gate (PR)",
        "limit": 3,
        "repo": "owner/name",
    }
