"""Tests for tools/production_gate_report.py."""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import production_gate_report as pgr  # noqa: E402

FIXTURES = Path(__file__).parent / "fixtures" / "junit"
HEALTH_FIXTURES = Path(__file__).parent / "fixtures" / "health"
BUDGET_FIXTURES = Path(__file__).parent / "fixtures" / "budgets"
CTEST_LOGS_FIXTURES = Path(__file__).parent / "fixtures" / "ctest_logs"
FIXED_TIMESTAMP = "2026-04-16T13:44:32Z"


def _run(tmp_path: Path, *junits: str, profile: str = "core",
         extra: list[str] | None = None) -> dict:
    """Invoke the tool's main() with the given fixtures, return parsed JSON."""
    out = tmp_path / "production-gate.json"
    argv = ["--profile", profile, "--output", str(out),
            "--timestamp", FIXED_TIMESTAMP, "--build-type", "Test"]
    for j in junits:
        argv += ["--junit", str(FIXTURES / j)]
    if extra:
        argv += extra
    rc = pgr.main(argv)
    assert rc == 0
    return json.loads(out.read_text())


# ---------- schema shape ----------

def test_passing_schema_shape(tmp_path):
    report = _run(tmp_path, "passing.xml")
    assert report["schema_version"] == 4
    assert report["timestamp"] == FIXED_TIMESTAMP
    assert report["profile"] == "core"
    assert report["status"] == "pass"
    assert report["tests"]["passed"] == 2
    assert report["tests"]["failed"] == 0
    assert report["tests"]["skipped"] == 0
    assert report["tests"]["failures"] == []
    for k in ("schema_version", "timestamp", "profile", "git", "build",
              "tests", "signals", "status"):
        assert k in report
    # Phase 6: dead fields dropped — no skipped_reasons, no stress block when
    # no phase6 tests ran, no vivid_version.
    assert "skipped_reasons" not in report["tests"]
    assert "stress" not in report
    # Phase 5: signals always carries an empty breach list, even with no budgets.
    assert report["signals"]["budget_breaches"] == []


# ---------- one fixture per classification ----------

@pytest.mark.parametrize("fixture, classification, signal_key, signal_delta", [
    ("webgpu_failure.xml",        "webgpu_error",     "webgpu_validation_errors", 1),
    ("audio_init_failure.xml",    "audio_init",       "audio_init_failures",      1),
    ("graph_load_failure.xml",    "graph_load",       "graph_load_failures",      1),
    ("missing_operator_failure.xml", "missing_operator", "missing_operators",     None),
    ("crash_failure.xml",         "crash",            None,                       None),
])
def test_classification(tmp_path, fixture, classification, signal_key, signal_delta):
    report = _run(tmp_path, fixture)
    assert report["status"] == "fail"
    assert report["tests"]["failed"] == 1
    assert len(report["tests"]["failures"]) == 1
    f = report["tests"]["failures"][0]
    assert f["classification"] == classification
    if signal_key == "missing_operators":
        assert "Wavetable" in report["signals"]["missing_operators"]
    elif signal_key is not None:
        assert report["signals"][signal_key] == signal_delta


# ---------- cumulative profiles ----------

def test_cumulative_profiles(tmp_path):
    report = _run(tmp_path, "passing.xml", "skipped_only.xml", profile="gui")
    assert report["profile"] == "gui"
    assert report["tests"]["passed"] == 2
    assert report["tests"]["skipped"] == 2
    assert report["tests"]["failed"] == 0
    assert report["status"] == "pass"


# ---------- skipped only ----------

def test_skipped_only(tmp_path):
    report = _run(tmp_path, "skipped_only.xml")
    assert report["status"] == "pass"
    assert report["tests"]["failed"] == 0
    assert report["tests"]["skipped"] == 2
    assert report["tests"]["passed"] == 0


# ---------- mixed failures ----------

def test_mixed_failures(tmp_path):
    report = _run(tmp_path,
                  "webgpu_failure.xml",
                  "missing_operator_failure.xml",
                  "graph_load_failure.xml")
    assert report["status"] == "fail"
    assert report["tests"]["failed"] == 3
    classifications = sorted(f["classification"] for f in report["tests"]["failures"])
    assert classifications == ["graph_load", "missing_operator", "webgpu_error"]
    assert report["signals"]["webgpu_validation_errors"] == 1
    assert report["signals"]["graph_load_failures"] == 1
    assert "Wavetable" in report["signals"]["missing_operators"]


# ---------- git-meta override ----------

def test_git_meta_override(tmp_path):
    report = _run(tmp_path, "passing.xml",
                  extra=["--commit", "abc123", "--branch", "feature/x"])
    assert report["git"]["commit"] == "abc123"
    assert report["git"]["branch"] == "feature/x"


# ---------- determinism ----------

def test_deterministic_output(tmp_path):
    out1 = tmp_path / "a.json"
    out2 = tmp_path / "b.json"
    common = ["--profile", "core", "--timestamp", FIXED_TIMESTAMP, "--build-type", "Test",
              "--commit", "deadbeef", "--branch", "master",
              "--junit", str(FIXTURES / "passing.xml"),
              "--junit", str(FIXTURES / "skipped_only.xml")]
    assert pgr.main(common + ["--output", str(out1)]) == 0
    assert pgr.main(common + ["--output", str(out2)]) == 0
    assert out1.read_bytes() == out2.read_bytes()


# ---------- specific signal extraction ----------

def test_missing_operator_name_extraction(tmp_path):
    report = _run(tmp_path, "missing_operator_failure.xml")
    assert report["signals"]["missing_operators"] == ["Wavetable"]


def test_graph_load_classification_by_test_name(tmp_path):
    """test_demo_graphs failures classify as graph_load even with empty log."""
    report = _run(tmp_path, "graph_load_failure.xml")
    f = report["tests"]["failures"][0]
    assert f["name"] == "test_demo_graphs"
    assert f["classification"] == "graph_load"


# ----------------------------------------------------------------------------
# Phase 5 — budget evaluation
# ----------------------------------------------------------------------------

def _run_with_budgets(tmp_path: Path, junit: str, *health_fixtures: str,
                      budgets_file: str = "default.toml") -> dict:
    """Same as _run but also threads --health-json + --budgets through."""
    out = tmp_path / "production-gate.json"
    argv = ["--profile", "core", "--output", str(out),
            "--timestamp", FIXED_TIMESTAMP, "--build-type", "Test",
            "--junit", str(FIXTURES / junit),
            "--budgets", str(BUDGET_FIXTURES / budgets_file)]
    for h in health_fixtures:
        argv += ["--health-json", str(HEALTH_FIXTURES / h)]
    rc = pgr.main(argv)
    assert rc == 0
    return json.loads(out.read_text())


def test_budget_clean_run_pass(tmp_path):
    report = _run_with_budgets(tmp_path, "passing.xml", "clean.json")
    assert report["status"] == "pass"
    assert report["signals"]["budget_breaches"] == []


def test_budget_audio_underrun_warning_degraded(tmp_path):
    report = _run_with_budgets(tmp_path, "passing.xml", "audio_underrun.json")
    assert report["status"] == "degraded"
    breaches = report["signals"]["budget_breaches"]
    assert len(breaches) == 1
    assert breaches[0]["budget_code"] == "no_audio_underruns"
    assert breaches[0]["graph"] == "synth_demo"
    assert breaches[0]["severity"] == "warning"
    assert "3" in breaches[0]["message"]


def test_budget_missing_operator_error_fail(tmp_path):
    report = _run_with_budgets(tmp_path, "passing.xml", "missing_op.json")
    assert report["status"] == "fail"
    breaches = report["signals"]["budget_breaches"]
    codes = [b["budget_code"] for b in breaches]
    assert "no_missing_core_operators" in codes
    miss = next(b for b in breaches if b["budget_code"] == "no_missing_core_operators")
    assert "GhostOperator" in miss["message"]


def test_budget_domain_filter_excludes_nonmatching(tmp_path):
    """gpu shader_error in an `audio`-only graph: gpu budget MUST NOT fire."""
    report = _run_with_budgets(tmp_path, "passing.xml",
                               "audio_only_with_shader_err.json")
    assert report["status"] == "pass"
    assert report["signals"]["budget_breaches"] == []


def test_budget_no_domains_only_star_budgets_fire(tmp_path):
    """Graph with empty domains: only `applies_to=*` budgets fire."""
    report = _run_with_budgets(tmp_path, "passing.xml", "no_domains.json")
    # no_dropped_connections (warning, *) fires; audio underruns + shader errors do NOT.
    breaches = report["signals"]["budget_breaches"]
    codes = [b["budget_code"] for b in breaches]
    assert codes == ["no_dropped_connections"]
    assert report["status"] == "degraded"


def test_test_failure_overrides_clean_budgets(tmp_path):
    """A test failure must flip status to fail even when budgets are clean."""
    report = _run_with_budgets(tmp_path, "graph_load_failure.xml", "clean.json")
    assert report["status"] == "fail"
    assert report["signals"]["budget_breaches"] == []


def test_budget_breaches_sorted_deterministic(tmp_path):
    """Two runs over the same fixture set produce byte-identical breach output."""
    out1 = tmp_path / "a.json"
    out2 = tmp_path / "b.json"
    common = ["--profile", "core", "--timestamp", FIXED_TIMESTAMP, "--build-type", "Test",
              "--commit", "deadbeef", "--branch", "master",
              "--junit", str(FIXTURES / "passing.xml"),
              "--health-json", str(HEALTH_FIXTURES / "audio_underrun.json"),
              "--health-json", str(HEALTH_FIXTURES / "missing_op.json"),
              "--health-json", str(HEALTH_FIXTURES / "no_domains.json"),
              "--budgets", str(BUDGET_FIXTURES / "default.toml")]
    assert pgr.main(common + ["--output", str(out1)]) == 0
    assert pgr.main(common + ["--output", str(out2)]) == 0
    assert out1.read_bytes() == out2.read_bytes()


def test_stress_block_populated_when_phase6_tests_present(tmp_path):
    """Soak profile that ran phase6 stress should report cumulative duration."""
    report = _run(tmp_path, "phase6_stress_pass.xml", profile="soak")
    assert "stress" in report
    # 12.5 + 9.25 + 8.1 + 12.75 = 42.6
    assert report["stress"]["phase6_stress_seconds"] == pytest.approx(42.6, rel=1e-3)


def test_stress_block_omitted_when_no_phase6_tests(tmp_path):
    """Core profile (no stress tests) should omit the stress block entirely."""
    report = _run(tmp_path, "passing.xml")
    assert "stress" not in report


def test_strict_exits_nonzero_on_status_fail(tmp_path):
    """--strict makes the tool surface status=fail as exit 1 for the gate."""
    out = tmp_path / "production-gate.json"
    argv = ["--profile", "core", "--output", str(out),
            "--timestamp", FIXED_TIMESTAMP, "--build-type", "Test",
            "--junit", str(FIXTURES / "passing.xml"),
            "--health-json", str(HEALTH_FIXTURES / "missing_op.json"),
            "--budgets", str(BUDGET_FIXTURES / "default.toml"),
            "--strict"]
    rc = pgr.main(argv)
    assert rc == 1
    report = json.loads(out.read_text())
    assert report["status"] == "fail"


def test_strict_exits_zero_on_degraded(tmp_path):
    """--strict still exits 0 on degraded — degraded is ship-with-caution."""
    out = tmp_path / "production-gate.json"
    argv = ["--profile", "core", "--output", str(out),
            "--timestamp", FIXED_TIMESTAMP, "--build-type", "Test",
            "--junit", str(FIXTURES / "passing.xml"),
            "--health-json", str(HEALTH_FIXTURES / "audio_underrun.json"),
            "--budgets", str(BUDGET_FIXTURES / "default.toml"),
            "--strict"]
    rc = pgr.main(argv)
    assert rc == 0
    report = json.loads(out.read_text())
    assert report["status"] == "degraded"


def test_budget_hot_reload_required_failure_is_fail(tmp_path):
    """Hot reload of an in-graph operator failing flips status to fail."""
    report = _run_with_budgets(tmp_path, "passing.xml", "hot_reload_required_fail.json")
    assert report["status"] == "fail"
    breaches = report["signals"]["budget_breaches"]
    codes = [b["budget_code"] for b in breaches]
    assert "no_required_operator_reload_failures" in codes
    assert "no_hot_reload_failures" in codes  # both fire
    required = next(b for b in breaches if b["budget_code"] == "no_required_operator_reload_failures")
    assert "wavetable" in required["message"].lower()


def test_budget_hot_reload_stale_failure_is_warning(tmp_path):
    """Hot reload of an out-of-graph operator failing flips to degraded only."""
    report = _run_with_budgets(tmp_path, "passing.xml", "hot_reload_stale_fail.json")
    assert report["status"] == "degraded"
    codes = [b["budget_code"] for b in report["signals"]["budget_breaches"]]
    # Stale failure trips no_hot_reload_failures (warning), NOT
    # no_required_operator_reload_failures (error).
    assert "no_hot_reload_failures" in codes
    assert "no_required_operator_reload_failures" not in codes


def test_budget_package_version_mismatch_is_warning(tmp_path):
    """Incompatible installed package flips status to degraded."""
    report = _run_with_budgets(tmp_path, "passing.xml", "package_mismatch.json")
    assert report["status"] == "degraded"
    codes = [b["budget_code"] for b in report["signals"]["budget_breaches"]]
    assert "no_package_version_mismatches" in codes


def test_budget_audio_clipping_is_warning(tmp_path):
    """Audio clipping in an audio-domain graph flips status to degraded."""
    report = _run_with_budgets(tmp_path, "passing.xml", "audio_clipping.json")
    assert report["status"] == "degraded"
    breaches = report["signals"]["budget_breaches"]
    codes = [b["budget_code"] for b in breaches]
    assert "no_audio_clipping" in codes
    clip = next(b for b in breaches if b["budget_code"] == "no_audio_clipping")
    assert "7" in clip["message"]  # 7 clipping samples in fixture


def test_budget_mcp_disconnected_is_warning(tmp_path):
    """A previously-pinged MCP server going stale flips status to degraded."""
    report = _run_with_budgets(tmp_path, "passing.xml", "mcp_disconnected.json")
    assert report["status"] == "degraded"
    breaches = report["signals"]["budget_breaches"]
    codes = [b["budget_code"] for b in breaches]
    assert "mcp_servers_connected" in codes
    msg = next(b for b in breaches if b["budget_code"] == "mcp_servers_connected")["message"]
    assert "vivid" in msg
    # opdev was never pinged → must NOT be flagged as disconnected.
    assert "opdev" not in msg


def test_budget_sustained_silence_is_warning(tmp_path):
    """Sustained silence on an audio-domain graph flips status to degraded."""
    report = _run_with_budgets(tmp_path, "passing.xml", "sustained_silence.json")
    assert report["status"] == "degraded"
    codes = [b["budget_code"] for b in report["signals"]["budget_breaches"]]
    assert "no_sustained_silence" in codes


def test_budget_sustained_black_is_warning(tmp_path):
    """Sustained black on a gpu-domain graph flips status to degraded."""
    report = _run_with_budgets(tmp_path, "passing.xml", "sustained_black.json")
    assert report["status"] == "degraded"
    codes = [b["budget_code"] for b in report["signals"]["budget_breaches"]]
    assert "no_sustained_black" in codes


def test_budget_sustained_silence_excluded_by_domain(tmp_path):
    """Silence on a control-only graph must NOT trip the audio,av budget."""
    # Reuse the silence fixture but write a temporary copy with control-only domains.
    import shutil
    src = HEALTH_FIXTURES / "sustained_silence.json"
    dst_dir = tmp_path / "health"
    dst_dir.mkdir()
    dst = dst_dir / "control_only_silent.json"
    text = src.read_text().replace('"domains": ["audio"]', '"domains": ["control"]')
    dst.write_text(text)

    out = tmp_path / "production-gate.json"
    argv = ["--profile", "core", "--output", str(out),
            "--timestamp", FIXED_TIMESTAMP, "--build-type", "Test",
            "--junit", str(FIXTURES / "passing.xml"),
            "--health-json", str(dst),
            "--budgets", str(BUDGET_FIXTURES / "default.toml")]
    assert pgr.main(argv) == 0
    report = json.loads(out.read_text())
    codes = [b["budget_code"] for b in report["signals"]["budget_breaches"]]
    assert "no_sustained_silence" not in codes
    assert report["status"] == "pass"


def test_unknown_classification_falls_back_to_lasttestlog(tmp_path):
    """A failure whose <system-out> is too short to classify should reclassify
    once we read CTest's full LastTest.log via --ctest-log-dir."""
    out = tmp_path / "production-gate.json"
    ctest_log_dir = CTEST_LOGS_FIXTURES / "Testing" / "Temporary"

    # Without --ctest-log-dir: no fallback, classification stays "unknown".
    argv_no_fallback = ["--profile", "core", "--output", str(out),
                        "--timestamp", FIXED_TIMESTAMP, "--build-type", "Test",
                        "--junit", str(FIXTURES / "unknown_then_classifiable.xml")]
    assert pgr.main(argv_no_fallback) == 0
    report = json.loads(out.read_text())
    assert report["tests"]["failures"][0]["classification"] == "unknown"

    # With --ctest-log-dir: fallback re-classifies as webgpu_error.
    argv_fallback = argv_no_fallback + ["--ctest-log-dir", str(ctest_log_dir)]
    assert pgr.main(argv_fallback) == 0
    report = json.loads(out.read_text())
    assert report["tests"]["failures"][0]["classification"] == "webgpu_error"
    assert report["signals"]["webgpu_validation_errors"] == 1


def test_budget_evaluation_skipped_when_no_budgets_file(tmp_path):
    """Without --budgets, the report still emits an empty budget_breaches list."""
    out = tmp_path / "production-gate.json"
    argv = ["--profile", "core", "--output", str(out),
            "--timestamp", FIXED_TIMESTAMP, "--build-type", "Test",
            "--junit", str(FIXTURES / "passing.xml"),
            "--health-json", str(HEALTH_FIXTURES / "audio_underrun.json")]
    assert pgr.main(argv) == 0
    report = json.loads(out.read_text())
    assert report["status"] == "pass"
    assert report["signals"]["budget_breaches"] == []
