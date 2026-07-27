#!/usr/bin/env python3
"""Pure-logic tests for the showcase QA harness — gate verdicts, index rollup, registry sanity.

Headless and app-less (the load-bearing logic is pure over recorded step dicts). Run:

    uv run examples/demos/showcase/tests/test_showcase.py
"""
from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
DEMOS = os.path.dirname(os.path.dirname(HERE))          # examples/demos
if DEMOS not in sys.path:
    sys.path.insert(0, DEMOS)

from showcase import gates, registry  # noqa: E402

ALL_CAPS = {"surge": True, "cassette": True, "clang": True}


def green_steps(**over) -> dict:
    """A fully-passing step bag; override individual steps in tests."""
    steps = {
        "regen": {"mode": "import", "ok": True},
        "load": {"ok": True, "path": "/tmp/proj"},
        "validate": {"valid": True, "degraded": False, "issues": [], "summary": "valid"},
        "health": {"health": {"severity": "ok"}},
        "quality": {"overall": "pass", "checks": [{"name": "nonblank_visual_output", "status": "pass"}]},
        "assets": {"ok": True},
        "capture": {"captured": True, "is_blank": False, "warm_attempts": 1, "path": "/tmp/h.png"},
        "analyze": {"analysis": {"is_blank": False, "brightness": 0.4, "contrast": 0.5, "hash": "abc"}},
    }
    steps.update(over)
    return steps


class GateTests(unittest.TestCase):
    def setUp(self):
        # shader-edit has no prereqs, so ALL_CAPS vs empty caps never adds prereq warnings.
        self.shader = registry.by_id("shader-edit")
        self.surge_show = registry.by_id("neon-song")   # declares Surge

    def test_pass(self):
        r = gates.evaluate(self.shader, green_steps(), ALL_CAPS)
        self.assertEqual(r.gate, "pass", r.reasons)
        self.assertEqual(r.reasons, [])
        self.assertEqual(r.image_stats.get("hash"), "abc")

    def test_blank_hero_fails(self):
        r = gates.evaluate(self.shader, green_steps(
            capture={"captured": True, "is_blank": True, "warm_attempts": 12, "path": "/tmp/h.png"},
            analyze={"analysis": {"is_blank": True, "blank_reason": "near-black"}},
        ), ALL_CAPS)
        self.assertEqual(r.gate, "fail")
        self.assertTrue(any("blank" in x for x in r.reasons), r.reasons)

    def test_not_captured_fails(self):
        r = gates.evaluate(self.shader, green_steps(
            capture={"captured": False, "reason": "no visual output"}, analyze=None,
        ), ALL_CAPS)
        self.assertEqual(r.gate, "fail")
        self.assertTrue(any("not captured" in x for x in r.reasons), r.reasons)

    def test_load_failure_fails(self):
        r = gates.evaluate(self.shader, green_steps(load=None), ALL_CAPS)
        self.assertEqual(r.gate, "fail")
        self.assertTrue(any("load_project failed" in x for x in r.reasons), r.reasons)

    def test_degraded_warns_even_when_valid(self):
        r = gates.evaluate(self.shader, green_steps(
            validate={"valid": True, "degraded": True,
                      "issues": [{"level": "error", "issue": "visual operator not registered"}],
                      "summary": "degraded"},
        ), ALL_CAPS)
        self.assertEqual(r.gate, "warn")
        self.assertTrue(any("degraded" in x for x in r.reasons), r.reasons)

    def test_invalid_fails(self):
        r = gates.evaluate(self.shader, green_steps(
            validate={"valid": False, "degraded": True,
                      "issues": [{"level": "error", "issue": "session file missing"}], "summary": "x"},
        ), ALL_CAPS)
        self.assertEqual(r.gate, "fail")

    def test_quality_fail_fails(self):
        r = gates.evaluate(self.shader, green_steps(
            quality={"overall": "fail", "checks": []}), ALL_CAPS)
        self.assertEqual(r.gate, "fail")

    def test_health_warning_warns(self):
        r = gates.evaluate(self.shader, green_steps(
            health={"health": {"severity": "warning"}}), ALL_CAPS)
        self.assertEqual(r.gate, "warn")

    def test_slow_warmup_warns(self):
        r = gates.evaluate(self.shader, green_steps(
            capture={"captured": True, "is_blank": False, "warm_attempts": 8, "path": "/tmp/h.png"}),
            ALL_CAPS)
        self.assertEqual(r.gate, "warn")
        self.assertTrue(any("warmed slowly" in x for x in r.reasons), r.reasons)

    def test_missing_prereq_caps_at_warn(self):
        # neon-song declares Surge; absent capability -> WARN, never FAIL, and visual gate still runs.
        r = gates.evaluate(self.surge_show, green_steps(), {"surge": False, "cassette": True, "clang": True})
        self.assertEqual(r.gate, "warn")
        self.assertIn("surge", r.prereqs_missing)

    def test_missing_prereq_does_not_mask_real_fail(self):
        r = gates.evaluate(self.surge_show, green_steps(load=None),
                           {"surge": False, "cassette": True, "clang": True})
        self.assertEqual(r.gate, "fail")


class IndexTests(unittest.TestCase):
    def _result(self, sid, gate):
        s = registry.by_id(sid)
        r = gates.ShowcaseResult(id=s.id, adr0037_type=s.adr0037_type, title=s.title,
                                 kind=s.kind.value, target=s.target, gate=gate)
        return r

    def test_all_pass(self):
        results = [self._result(s.id, "pass") for s in registry.SHOWCASES]
        idx = gates.build_index(results, ALL_CAPS)
        self.assertEqual(idx["gate"], "pass")
        self.assertEqual(idx["counts"]["pass"], len(registry.SHOWCASES))
        self.assertTrue(all(g == "pass" for g in idx["adr0037_coverage"].values()))

    def test_one_fail_dominates(self):
        results = [self._result("shader-edit", "fail")] + \
                  [self._result(s.id, "pass") for s in registry.SHOWCASES if s.id != "shader-edit"]
        idx = gates.build_index(results, ALL_CAPS)
        self.assertEqual(idx["gate"], "fail")
        self.assertEqual(idx["adr0037_coverage"]["4"], "fail")

    def test_warn_is_pass_with_warnings(self):
        results = [self._result("neon-song", "warn")] + \
                  [self._result(s.id, "pass") for s in registry.SHOWCASES if s.id != "neon-song"]
        idx = gates.build_index(results, ALL_CAPS)
        self.assertEqual(idx["gate"], "pass_with_warnings")

    def test_partial_run_marks_missing_coverage(self):
        idx = gates.build_index([self._result("shader-edit", "pass")], ALL_CAPS,
                                registry.missing_types())
        self.assertEqual(idx["adr0037_coverage"]["4"], "pass")
        self.assertEqual(idx["adr0037_coverage"]["1"], "missing")
        self.assertEqual(idx["gate"], "pass_with_warnings")


class RegistryTests(unittest.TestCase):
    def test_all_five_types_covered(self):
        self.assertEqual(registry.missing_types(), [])
        covered = {s.adr0037_type for s in registry.SHOWCASES}
        self.assertEqual(covered, set(registry.ADR0037_TYPES))

    def test_ids_unique(self):
        ids = [s.id for s in registry.SHOWCASES]
        self.assertEqual(len(ids), len(set(ids)))

    def test_tutorial_targets_exist(self):
        for s in registry.SHOWCASES:
            if s.kind is registry.Kind.TUTORIAL:
                self.assertTrue(s.target_path().exists(), f"missing tutorial: {s.target_path()}")

    def test_demo_modules_exist(self):
        for s in registry.SHOWCASES:
            if s.kind is registry.Kind.DEMO:
                self.assertTrue((Path(DEMOS) / f"{s.target}.py").exists(),
                                f"missing demo module: {s.target}.py")

    def test_select_rejects_unknown(self):
        with self.assertRaises(ValueError):
            registry.select(["no-such-showcase"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
