import asyncio
import importlib.util
import json
import os
import pathlib
import sys
import types
import unittest


def _install_stubs() -> None:
    # Stub fastmcp dependency
    fastmcp_mod = types.ModuleType("mcp.server.fastmcp")

    class FakeMCP:
        def __init__(self, *_args, **_kwargs):
            pass

        def tool(self):
            def deco(fn):
                return fn
            return deco

        def run(self):
            return None

    fastmcp_mod.FastMCP = FakeMCP
    sys.modules.setdefault("mcp", types.ModuleType("mcp"))
    sys.modules.setdefault("mcp.server", types.ModuleType("mcp.server"))
    sys.modules["mcp.server.fastmcp"] = fastmcp_mod

    # Stub httpx dependency
    httpx_mod = types.ModuleType("httpx")

    class DummyClient:
        async def __aenter__(self):
            return self

        async def __aexit__(self, exc_type, exc, tb):
            return False

        async def post(self, *_args, **_kwargs):
            raise RuntimeError("DummyClient.post should not be called in these unit tests")

    httpx_mod.AsyncClient = DummyClient
    sys.modules["httpx"] = httpx_mod


def _load_module():
    _install_stubs()
    module_path = pathlib.Path(__file__).with_name("vivid_mcp.py")
    spec = importlib.util.spec_from_file_location("vivid_mcp_under_test", module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(module)
    return module


class PerceptionMCPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = _load_module()

    def setUp(self):
        self.mod._managed_runtime_process = None
        self.mod._managed_runtime_log_path = ""

    def test_runtime_subprocess_env_appends_toolchain_paths(self):
        old_path = os.environ.get("PATH")
        os.environ["PATH"] = "/opt/homebrew/share/info:"
        try:
            env = self.mod._runtime_subprocess_env()
        finally:
            if old_path is None:
                os.environ.pop("PATH", None)
            else:
                os.environ["PATH"] = old_path

        parts = env["PATH"].split(":")
        self.assertEqual(parts[0], "/opt/homebrew/share/info")
        for directory in self.mod._TOOLCHAIN_PATH_DIRS:
            if self.mod.os.path.isdir(directory):
                self.assertIn(directory, parts)

    def test_ensure_path_dirs_does_not_duplicate_existing_entries(self):
        env = {"PATH": "/usr/bin:/bin"}
        self.mod._ensure_path_dirs(env, self.mod._TOOLCHAIN_PATH_DIRS)
        parts = env["PATH"].split(":")
        if self.mod.os.path.isdir("/usr/bin"):
            self.assertEqual(parts.count("/usr/bin"), 1)
        if self.mod.os.path.isdir("/bin"):
            self.assertEqual(parts.count("/bin"), 1)

    def test_launch_runtime_process_passes_repaired_env(self):
        captured = {}

        class FakePopen:
            pid = 1212

            def __init__(self, *args, **kwargs):
                captured["args"] = args
                captured["kwargs"] = kwargs
                kwargs["stdout"].close()

            def poll(self):
                return None

        original_popen = self.mod.subprocess.Popen
        original_resolve = self.mod._resolve_vivid_bin
        old_path = os.environ.get("PATH")
        self.mod.subprocess.Popen = FakePopen
        self.mod._resolve_vivid_bin = lambda: pathlib.Path("/bin/echo")
        os.environ["PATH"] = "/opt/homebrew/share/info:"
        try:
            proc, _log_path = self.mod._launch_runtime_process()
        finally:
            self.mod.subprocess.Popen = original_popen
            self.mod._resolve_vivid_bin = original_resolve
            if old_path is None:
                os.environ.pop("PATH", None)
            else:
                os.environ["PATH"] = old_path

        self.assertEqual(proc.pid, 1212)
        env = captured["kwargs"]["env"]
        self.assertEqual(env["PATH"].split(":")[0], "/opt/homebrew/share/info")
        for directory in self.mod._TOOLCHAIN_PATH_DIRS:
            if self.mod.os.path.isdir(directory):
                self.assertIn(directory, env["PATH"].split(":"))

    def test_run_vivid_cli_json_passes_repaired_env(self):
        captured = {}

        class FakeProc:
            returncode = 0

            async def communicate(self):
                return b'{"ok":true}', b""

        async def fake_create_subprocess_exec(*args, **kwargs):
            captured["args"] = args
            captured["kwargs"] = kwargs
            return FakeProc()

        original_create = self.mod.asyncio.create_subprocess_exec
        original_resolve = self.mod._resolve_vivid_bin
        old_path = os.environ.get("PATH")
        self.mod.asyncio.create_subprocess_exec = fake_create_subprocess_exec
        self.mod._resolve_vivid_bin = lambda: pathlib.Path("/bin/echo")
        os.environ["PATH"] = "/opt/homebrew/share/info:"
        try:
            out = asyncio.run(self.mod._run_vivid_cli_json(["list-types", "--json"]))
        finally:
            self.mod.asyncio.create_subprocess_exec = original_create
            self.mod._resolve_vivid_bin = original_resolve
            if old_path is None:
                os.environ.pop("PATH", None)
            else:
                os.environ["PATH"] = old_path

        self.assertEqual(out, '{"ok":true}')
        self.assertEqual(captured["args"][:3], ("/bin/echo", "list-types", "--json"))
        env = captured["kwargs"]["env"]
        self.assertEqual(env["PATH"].split(":")[0], "/opt/homebrew/share/info")
        for directory in self.mod._TOOLCHAIN_PATH_DIRS:
            if self.mod.os.path.isdir(directory):
                self.assertIn(directory, env["PATH"].split(":"))

    def test_run_diagnostics_compact_summary(self):
        raw = json.dumps({
            "ok": True,
            "schema_version": 2,
            "health": {
                "audio": {"running": True, "sample_rate": 48000, "buffer_size": 256,
                          "node_count": 2, "xruns": 3, "last_buffer_underrun": False, "load": 0.45},
                "graph": {"declared_nodes": 5, "declared_connections": 4, "compiled_nodes": 5,
                          "frame_nodes": 3, "audio_nodes": 2, "total_edges": 4,
                          "frame_edges": 2, "audio_edges": 1, "snapshot_edges": 1,
                          "dropped_connections": 0, "errored_nodes": 0, "missing_operators": 0},
                "gpu": {"texture_nodes": 3, "shader_errors": 0},
            },
            "result": {
                "summary": {"critical": 2, "warning": 1, "info": 0},
                "hints": [{"id": "h1"}, {"id": "h2"}, {"id": "h3"}, {"id": "h4"}],
            },
        })
        out = json.loads(self.mod._perception_response(raw, "run_diagnostics", False))
        self.assertTrue(out["ok"])
        self.assertEqual(out["schema_version"], 2)
        self.assertEqual(out["summary"]["critical"], 2)
        self.assertEqual(out["summary"]["warning"], 1)
        self.assertEqual(out["summary"]["info"], 0)
        self.assertEqual(out["summary"]["top_hint_ids"], ["h1", "h2", "h3"])
        self.assertEqual(out["summary"]["audio_running"], True)
        self.assertEqual(out["summary"]["audio_load"], 0.45)
        self.assertEqual(out["summary"]["audio_xruns"], 3)
        self.assertIn("health", out)
        self.assertEqual(out["health"]["audio"]["xruns"], 3)
        self.assertNotIn("result", out)

    def test_get_runtime_health_passes_through_envelope(self):
        """The MCP tool should return the control-server envelope verbatim
        — no compaction, no perception summarization. Matches get_graph_errors."""
        envelope = {
            "ok": True,
            "schema_version": 1,
            "health": {
                "severity": "warning",
                "findings": [{"code": "audio_underruns", "severity": "warning",
                              "subject": "", "message": "Audio underruns since session start: 3."}],
                "audio": {"running": True, "sample_rate": 48000, "buffer_size": 256,
                          "node_count": 1, "xruns": 3, "load": 0.42,
                          "last_buffer_underrun": False, "lane_overflow_count": 0,
                          "top_nodes": [], "top_lane_state_nodes": []},
                "graph": {"declared_nodes": 1, "declared_connections": 0,
                          "compiled_nodes": 1, "frame_nodes": 0, "audio_nodes": 1,
                          "total_edges": 0, "frame_edges": 0, "audio_edges": 0,
                          "snapshot_edges": 0, "dropped_connections": 0,
                          "errored_nodes": 0, "missing_operators": 0,
                          "missing_operator_types": []},
                "gpu": {"texture_nodes": 0, "shader_errors": 0,
                        "device_lost": False, "last_error": ""},
                "vivid_version": "",
            },
        }
        raw_envelope = json.dumps(envelope)

        captured = {}

        async def fake_post(method, body=None):
            captured["method"] = method
            captured["body"] = body
            return raw_envelope

        original_post = self.mod._post
        self.mod._post = fake_post
        try:
            out_raw = asyncio.run(self.mod.get_runtime_health())
        finally:
            self.mod._post = original_post

        self.assertEqual(captured["method"], "get_runtime_health")
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])
        self.assertEqual(out["schema_version"], 1)
        self.assertEqual(out["health"]["severity"], "warning")
        self.assertEqual(out["health"]["audio"]["xruns"], 3)
        self.assertEqual(out["health"]["findings"][0]["code"], "audio_underruns")
        self.assertEqual(out["health"]["gpu"]["device_lost"], False)

    def test_run_checks_include_payload(self):
        raw = json.dumps({
            "ok": True,
            "schema_version": 1,
            "result": {
                "all_passed": False,
                "all_critical_passed": True,
                "summary": {"passed": 1, "failed": 1, "skipped": 0, "critical_failed": 0},
                "results": [{"id": "c1", "passed": True}, {"id": "c2", "passed": False}],
            },
        })
        out = json.loads(self.mod._perception_response(raw, "run_checks", True))
        self.assertTrue(out["ok"])
        self.assertEqual(out["summary"]["all_passed"], False)
        self.assertEqual(out["summary"]["all_critical_passed"], True)
        self.assertEqual(out["summary"]["failed"], 1)
        self.assertIn("result", out)
        self.assertEqual(len(out["result"]["results"]), 2)

    def test_runtime_error_normalization(self):
        raw = json.dumps({
            "ok": False,
            "schema_version": 1,
            "error": "bad request",
        })
        out = json.loads(self.mod._perception_response(raw, "run_checks", False))
        self.assertFalse(out["ok"])
        self.assertEqual(out["error"]["code"], "runtime_error")
        self.assertEqual(out["error"]["message"], "bad request")

    def test_invalid_json_normalization(self):
        out = json.loads(self.mod._perception_response("not-json", "run_diagnostics", False))
        self.assertFalse(out["ok"])
        self.assertEqual(out["error"]["code"], "invalid_json")

    def test_analyze_output_bridge(self):
        async def fake_post(method, body=None):
            self.assertEqual(method, "analyze_output")
            self.assertEqual(body["mode"], "av")
            self.assertEqual(body["window_seconds"], 2.0)
            self.assertEqual(body["include_payload"], True)
            self.assertEqual(body["node_id"], "video_out1")
            return '{"ok":true}'

        original = self.mod._post
        self.mod._post = fake_post
        try:
            out = asyncio.run(self.mod.analyze_output("av", 2.0, True, "video_out1"))
        finally:
            self.mod._post = original
        self.assertEqual(out, '{"ok":true}')

    def _make_analysis_json(self, **overrides):
        """Build a synthetic analyze_output(mode="av") response."""
        base = {
            "ok": True,
            "schema_version": 1,
            "mode": "av",
            "metrics": {
                "audio": {"rms": 0.2, "peak": 0.5,
                          "spectral_centroid_hz": 440.0,
                          "spectral_brightness": 0.1,
                          "spectral_flatness": 0.1},
                "visual": {"mean_brightness": 0.1, "contrast": 0.15,
                           "motion_magnitude": 0.1},
                "av_reactivity": {
                    "energy_brightness_correlation": 0.1,
                    "energy_motion_correlation": 0.1,
                    "energy_contrast_correlation": 0.1,
                    "window_seconds": 3.0,
                    "visual_samples": 19,
                    "detected_onsets": 0,
                    "onset_response_rate": 0.0,
                    "reactivity_latency_ms": 0.0,
                },
            },
            "notes": [],
        }
        for section, fields in overrides.items():
            if section == "audio" or section == "visual" or section == "av_reactivity":
                base["metrics"][section].update(fields)
            else:
                base[section] = fields
        return json.dumps(base)

    def test_diagnose_near_black_output(self):
        """mean_brightness near zero should fire the critical near-black finding."""
        raw = self._make_analysis_json(visual={"mean_brightness": 0.003})
        out = json.loads(asyncio.run(
            self.mod.diagnose_composition_issue(analysis_json=raw, intent="")))
        self.assertTrue(out["ok"])
        symptoms = [f["symptom"] for f in out["findings"]]
        self.assertTrue(any("near-black" in s for s in symptoms),
                        f"expected near-black finding; got {symptoms}")
        critical = [f for f in out["findings"] if f["severity"] == "critical"]
        self.assertGreaterEqual(len(critical), 1,
                                "expected at least one critical finding")

    def test_diagnose_low_onset_response_on_drum_intent(self):
        """onset_response_rate < 0.3 with drum-driven intent fires the critical peak-without-envelope finding."""
        raw = self._make_analysis_json(av_reactivity={
            "detected_onsets": 11,
            "onset_response_rate": 0.2,
        })
        out = json.loads(asyncio.run(
            self.mod.diagnose_composition_issue(analysis_json=raw, intent="drum-driven")))
        self.assertTrue(out["ok"])
        symptoms = [f["symptom"] for f in out["findings"]]
        self.assertTrue(any("onset_response_rate" in s for s in symptoms),
                        f"expected low-onset finding; got {symptoms}")
        critical_fixes = [f["fix"] for f in out["findings"] if f["severity"] == "critical"]
        self.assertTrue(any("SmoothFr" in fix for fix in critical_fixes),
                        "expected a critical fix to mention SmoothFr")

    def test_diagnose_healthy_graph(self):
        """A graph with healthy metrics returns a single info finding."""
        raw = self._make_analysis_json(
            visual={"mean_brightness": 0.2, "contrast": 0.25, "motion_magnitude": 0.15},
            av_reactivity={"detected_onsets": 10, "onset_response_rate": 0.9,
                           "reactivity_latency_ms": 150.0},
        )
        out = json.loads(asyncio.run(
            self.mod.diagnose_composition_issue(analysis_json=raw, intent="drum-driven")))
        self.assertTrue(out["ok"])
        severities = [f["severity"] for f in out["findings"]]
        self.assertNotIn("critical", severities)
        self.assertNotIn("warning", severities)

    def test_diagnose_phase_lag_is_info_not_warning(self):
        """Negative correlation with high onset_response_rate is expected (feedback lag) → info."""
        raw = self._make_analysis_json(
            visual={"mean_brightness": 0.05, "motion_magnitude": 0.02},
            av_reactivity={
                "energy_brightness_correlation": -0.4,
                "energy_motion_correlation": -0.2,
                "energy_contrast_correlation": -0.3,
                "detected_onsets": 10,
                "onset_response_rate": 0.85,
            },
        )
        out = json.loads(asyncio.run(
            self.mod.diagnose_composition_issue(analysis_json=raw, intent="drum-driven")))
        self.assertTrue(out["ok"])
        info_causes = [f["likely_cause"] for f in out["findings"] if f["severity"] == "info"]
        self.assertTrue(
            any("phase" in c.lower() or "lag" in c.lower() or "event-driven" in c.lower()
                for c in info_causes),
            f"expected an info-level phase/lag/event-driven explanation; got {info_causes}")

    def test_diagnose_invalid_intent_errors(self):
        raw = self._make_analysis_json()
        out = json.loads(asyncio.run(
            self.mod.diagnose_composition_issue(analysis_json=raw, intent="bogus-intent")))
        self.assertFalse(out["ok"])
        self.assertIn("unknown intent", out["error"])

    def test_diagnose_invalid_json_errors(self):
        out = json.loads(asyncio.run(
            self.mod.diagnose_composition_issue(analysis_json="not-json")))
        self.assertFalse(out["ok"])
        self.assertIn("parse", out["error"])

    def test_diagnose_calls_analyze_output_when_no_json_provided(self):
        """Without a pre-supplied analysis_json, the tool should call analyze_output itself."""
        captured = {}

        async def fake_post(method, body=None):
            captured["method"] = method
            captured["body"] = body
            return self._make_analysis_json(
                visual={"mean_brightness": 0.2, "motion_magnitude": 0.15},
                av_reactivity={"detected_onsets": 0, "onset_response_rate": 0.0},
            )

        original = self.mod._post
        self.mod._post = fake_post
        try:
            out = json.loads(asyncio.run(
                self.mod.diagnose_composition_issue(window_seconds=5.0)))
        finally:
            self.mod._post = original

        self.assertTrue(out["ok"])
        self.assertEqual(captured["method"], "analyze_output")
        self.assertEqual(captured["body"]["mode"], "av")
        self.assertEqual(captured["body"]["window_seconds"], 5.0)

    def test_get_composition_patterns_all(self):
        """With no intent, returns all curated patterns."""
        out = json.loads(asyncio.run(self.mod.get_composition_patterns()))
        self.assertTrue(out["ok"])
        ids = [p["id"] for p in out["patterns"]]
        self.assertIn("drum-driven-pulse", ids)
        self.assertIn("continuous-reactivity", ids)
        self.assertIn("parametric-sync", ids)
        self.assertIn("spectral-color", ids)
        self.assertEqual(len(out["patterns"]), 4)

    def test_get_composition_patterns_drum_driven(self):
        """Intent 'drum-driven' narrows to the drum-driven-pulse pattern."""
        out = json.loads(asyncio.run(
            self.mod.get_composition_patterns(intent="drum-driven")))
        self.assertTrue(out["ok"])
        ids = [p["id"] for p in out["patterns"]]
        self.assertEqual(ids, ["drum-driven-pulse"])

    def test_get_composition_patterns_percussive_matches_drum_driven(self):
        """Intent 'percussive' is a synonym for 'drum-driven'."""
        out = json.loads(asyncio.run(
            self.mod.get_composition_patterns(intent="percussive")))
        self.assertTrue(out["ok"])
        ids = [p["id"] for p in out["patterns"]]
        self.assertEqual(ids, ["drum-driven-pulse"])

    def test_get_composition_patterns_continuous(self):
        """Intent 'continuous' matches continuous-reactivity, also 'pad' and 'ambient'."""
        for synonym in ("continuous", "pad", "ambient"):
            out = json.loads(asyncio.run(
                self.mod.get_composition_patterns(intent=synonym)))
            self.assertTrue(out["ok"])
            ids = [p["id"] for p in out["patterns"]]
            self.assertEqual(ids, ["continuous-reactivity"],
                             f"synonym '{synonym}' should match continuous-reactivity")

    def test_get_composition_patterns_schema_fields_present(self):
        """Each pattern includes the required structural fields."""
        out = json.loads(asyncio.run(self.mod.get_composition_patterns()))
        required = {"id", "name", "intents", "one_line", "when_to_use",
                    "signal_flow", "key_operators", "example_connections",
                    "watch_out_for", "expected_metrics", "exemplars"}
        for pattern in out["patterns"]:
            missing = required - set(pattern.keys())
            self.assertFalse(missing,
                             f"pattern {pattern['id']} missing fields: {missing}")
            self.assertIsInstance(pattern["key_operators"], list)
            self.assertIsInstance(pattern["example_connections"], list)
            self.assertIsInstance(pattern["watch_out_for"], list)
            self.assertIsInstance(pattern["exemplars"], list)

    def test_get_composition_patterns_invalid_intent_errors(self):
        out = json.loads(asyncio.run(
            self.mod.get_composition_patterns(intent="xyz-bogus")))
        self.assertFalse(out["ok"])
        self.assertIn("unknown intent", out["error"])

    def _write_graph_fixture(self, nodes: dict, connections: list) -> str:
        """Write a minimal graph JSON to a temp file and return its path."""
        import tempfile
        fd, path = tempfile.mkstemp(suffix=".json", prefix="test_graph_")
        os.close(fd)
        payload = {"schema_version": 1, "nodes": nodes, "connections": connections,
                   "meta": {"id": "fixture", "title": "Fixture"}}
        with open(path, "w") as f:
            json.dump(payload, f)
        return path

    def test_explain_graph_composition_detects_drum_driven_pulse(self):
        """Full drum → smooth → shape (both axes) chain scores confidence=high."""
        path = self._write_graph_fixture(
            nodes={
                "kick": {"type": "DrumKick"},
                "sm_k": {"type": "SmoothFr", "params": {"rise_time": 0.005, "fall_time": 0.4}},
                "shape": {"type": "Shape2D"},
            },
            connections=[
                {"from": "kick/peak",    "to": "sm_k/input"},
                {"from": "sm_k/value",   "to": "shape/scale_x"},
                {"from": "sm_k/value",   "to": "shape/scale_y"},
            ],
        )
        try:
            out = json.loads(asyncio.run(self.mod.explain_graph_composition(path)))
        finally:
            os.unlink(path)
        self.assertTrue(out["ok"])
        pids = [p["pattern_id"] for p in out["patterns_detected"]]
        self.assertIn("drum-driven-pulse", pids)
        ddp = next(p for p in out["patterns_detected"] if p["pattern_id"] == "drum-driven-pulse")
        self.assertEqual(ddp["confidence"], "high",
                         "drum→smooth→shape on BOTH axes should score high")

    def test_explain_graph_composition_detects_missing_envelope(self):
        """drum/peak wired directly to shape scale (no Smooth) is low confidence + warning."""
        path = self._write_graph_fixture(
            nodes={
                "kick": {"type": "DrumKick"},
                "shape": {"type": "Shape2D"},
            },
            connections=[
                {"from": "kick/peak", "to": "shape/scale_x"},
            ],
        )
        try:
            out = json.loads(asyncio.run(self.mod.explain_graph_composition(path)))
        finally:
            os.unlink(path)
        ddp = next(p for p in out["patterns_detected"]
                   if p["pattern_id"] == "drum-driven-pulse")
        self.assertEqual(ddp["confidence"], "low")
        self.assertTrue(any("warning" in ch for ch in ddp["chains"]),
                        "bare drum→shape chain should carry a warning")

    def test_explain_graph_composition_detects_continuous_reactivity(self):
        """gain/rms → displace/amount scores continuous-reactivity."""
        path = self._write_graph_fixture(
            nodes={
                "osc": {"type": "Oscillator"},
                "gain": {"type": "Gain"},
                "displace": {"type": "Displace"},
            },
            connections=[
                {"from": "osc/output",  "to": "gain/input"},
                {"from": "gain/rms",    "to": "displace/amount"},
            ],
        )
        try:
            out = json.loads(asyncio.run(self.mod.explain_graph_composition(path)))
        finally:
            os.unlink(path)
        pids = [p["pattern_id"] for p in out["patterns_detected"]]
        self.assertIn("continuous-reactivity", pids)
        cr = next(p for p in out["patterns_detected"] if p["pattern_id"] == "continuous-reactivity")
        self.assertEqual(cr["confidence"], "high")

    def test_explain_graph_composition_detects_parametric_sync(self):
        """Single LFO forks to osc (audio) AND shape (visual) — parametric sync."""
        path = self._write_graph_fixture(
            nodes={
                "lfo": {"type": "LfoFr"},
                "osc": {"type": "Oscillator"},
                "shape": {"type": "Shape2D"},
            },
            connections=[
                {"from": "lfo/value", "to": "osc/frequency"},
                {"from": "lfo/value", "to": "shape/scale_x"},
            ],
        )
        try:
            out = json.loads(asyncio.run(self.mod.explain_graph_composition(path)))
        finally:
            os.unlink(path)
        pids = [p["pattern_id"] for p in out["patterns_detected"]]
        self.assertIn("parametric-sync", pids)

    def test_explain_graph_composition_empty_graph_reports_no_patterns(self):
        """An empty graph produces zero patterns and a sensible summary."""
        path = self._write_graph_fixture(nodes={}, connections=[])
        try:
            out = json.loads(asyncio.run(self.mod.explain_graph_composition(path)))
        finally:
            os.unlink(path)
        self.assertTrue(out["ok"])
        self.assertEqual(out["patterns_detected"], [])
        self.assertEqual(out["summary"]["node_count"], 0)

    def test_explain_graph_composition_missing_file_errors(self):
        out = json.loads(asyncio.run(
            self.mod.explain_graph_composition("/nonexistent/path.json")))
        self.assertFalse(out["ok"])
        self.assertIn("not found", out["error"])

    def test_explain_graph_composition_invalid_json_errors(self):
        import tempfile
        fd, path = tempfile.mkstemp(suffix=".json")
        os.close(fd)
        with open(path, "w") as f:
            f.write("not-json-at-all")
        try:
            out = json.loads(asyncio.run(self.mod.explain_graph_composition(path)))
        finally:
            os.unlink(path)
        self.assertFalse(out["ok"])
        self.assertIn("parse", out["error"].lower())

    def test_fetch_reference_youtube_id_parsing(self):
        """YouTube URL patterns should all resolve to the same video ID."""
        urls = [
            "https://www.youtube.com/watch?v=dQw4w9WgXcQ",
            "https://youtu.be/dQw4w9WgXcQ",
            "https://www.youtube.com/embed/dQw4w9WgXcQ",
            "https://www.youtube.com/shorts/dQw4w9WgXcQ",
            "https://youtube.com/watch?feature=foo&v=dQw4w9WgXcQ",
        ]
        for url in urls:
            vid = self.mod._youtube_video_id(url)
            self.assertEqual(vid, "dQw4w9WgXcQ", f"failed for url {url}")

    def test_fetch_reference_rejects_non_http_url(self):
        out = json.loads(asyncio.run(
            self.mod.fetch_reference("file:///tmp/foo.jpg")))
        self.assertFalse(out["ok"])
        self.assertIn("http(s)", out["error"])

    def test_fetch_reference_returns_cached_result(self):
        """If a cache file exists for the URL, the tool returns it without re-fetching."""
        import tempfile as _tmp
        url = "https://example.com/some-ref-cached"
        key = self.mod._reference_cache_key(url)
        tmp_cache = pathlib.Path(_tmp.mkdtemp()) / "refs"
        tmp_cache.mkdir(parents=True, exist_ok=True)
        meta_path = tmp_cache / f"{key}.meta.json"
        meta_path.write_text(json.dumps({
            "ok": True, "source_url": url, "source_kind": "webpage",
            "title": "Previously Fetched", "description": "",
            "thumbnail_local_path": "", "text_summary": "",
        }))

        original_cache_dir = self.mod._REFERENCE_CACHE_DIR
        self.mod._REFERENCE_CACHE_DIR = tmp_cache
        try:
            out = json.loads(asyncio.run(self.mod.fetch_reference(url)))
        finally:
            self.mod._REFERENCE_CACHE_DIR = original_cache_dir

        self.assertTrue(out["ok"])
        self.assertTrue(out["cached"])
        self.assertEqual(out["title"], "Previously Fetched")

    def test_fetch_reference_youtube_downloads_thumbnail(self):
        """YouTube URL triggers thumbnail download and metadata scrape."""
        import tempfile as _tmp
        url = "https://www.youtube.com/watch?v=ABCDEFGHIJK"
        tmp_cache = pathlib.Path(_tmp.mkdtemp()) / "refs"

        downloads = []

        async def fake_download(u, path, timeout=20.0):
            downloads.append(u)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fake-png-bytes")
            return True

        async def fake_fetch_text(u, timeout=15.0):
            return (
                "<html><head>"
                "<title>Rickroll</title>"
                '<meta name="description" content="Never gonna give you up">'
                "</head></html>"
            )

        original_cache_dir = self.mod._REFERENCE_CACHE_DIR
        original_dl = self.mod._download_to_path
        original_ft = self.mod._fetch_url_text
        self.mod._REFERENCE_CACHE_DIR = tmp_cache
        self.mod._download_to_path = fake_download
        self.mod._fetch_url_text = fake_fetch_text
        try:
            out = json.loads(asyncio.run(self.mod.fetch_reference(url)))
        finally:
            self.mod._REFERENCE_CACHE_DIR = original_cache_dir
            self.mod._download_to_path = original_dl
            self.mod._fetch_url_text = original_ft

        self.assertTrue(out["ok"])
        self.assertEqual(out["source_kind"], "youtube")
        self.assertTrue(out["thumbnail_local_path"].endswith(".jpg"))
        self.assertEqual(out["title"], "Rickroll")
        self.assertEqual(out["description"], "Never gonna give you up")
        # First attempt should be maxresdefault
        self.assertTrue(any("maxresdefault" in u for u in downloads))
        # Multi-frame support: should also download /1.jpg /2.jpg /3.jpg /0.jpg
        for idx in ("/0.jpg", "/1.jpg", "/2.jpg", "/3.jpg"):
            self.assertTrue(any(idx in u for u in downloads),
                            f"expected to attempt download of {idx}; downloads={downloads}")
        # frame_paths should include the cover + the 4 additional frames (all 5)
        self.assertEqual(len(out["frame_paths"]), 5,
                         f"expected 5 frame paths, got {out['frame_paths']}")

    def test_fetch_reference_youtube_partial_frame_download(self):
        """If only some frame thumbnails succeed, frame_paths reflects what landed."""
        import tempfile as _tmp
        url = "https://youtu.be/PARTIAL1234"
        tmp_cache = pathlib.Path(_tmp.mkdtemp()) / "refs"

        async def fake_download(u, path, timeout=20.0):
            # maxresdefault succeeds, /1.jpg succeeds, /2.jpg /3.jpg /0.jpg fail
            if "maxresdefault" in u or "/1.jpg" in u:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fake")
                return True
            return False

        async def fake_fetch_text(u, timeout=15.0):
            return "<html><title>x</title></html>"

        original_cache_dir = self.mod._REFERENCE_CACHE_DIR
        original_dl = self.mod._download_to_path
        original_ft = self.mod._fetch_url_text
        self.mod._REFERENCE_CACHE_DIR = tmp_cache
        self.mod._download_to_path = fake_download
        self.mod._fetch_url_text = fake_fetch_text
        try:
            out = json.loads(asyncio.run(self.mod.fetch_reference(url)))
        finally:
            self.mod._REFERENCE_CACHE_DIR = original_cache_dir
            self.mod._download_to_path = original_dl
            self.mod._fetch_url_text = original_ft

        self.assertTrue(out["ok"])
        self.assertEqual(out["source_kind"], "youtube")
        # Cover + /1.jpg = 2 successful frames
        self.assertEqual(len(out["frame_paths"]), 2)

    def test_fetch_reference_direct_image(self):
        """A direct image URL is downloaded to cache."""
        import tempfile as _tmp
        url = "https://example.com/art.png"
        tmp_cache = pathlib.Path(_tmp.mkdtemp()) / "refs"

        async def fake_download(u, path, timeout=20.0):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fake-png")
            return True

        original_cache_dir = self.mod._REFERENCE_CACHE_DIR
        original_dl = self.mod._download_to_path
        self.mod._REFERENCE_CACHE_DIR = tmp_cache
        self.mod._download_to_path = fake_download
        try:
            out = json.loads(asyncio.run(self.mod.fetch_reference(url)))
        finally:
            self.mod._REFERENCE_CACHE_DIR = original_cache_dir
            self.mod._download_to_path = original_dl

        self.assertTrue(out["ok"])
        self.assertEqual(out["source_kind"], "image")
        self.assertTrue(out["thumbnail_local_path"].endswith(".png"))

    def test_fetch_reference_generic_webpage_scrapes_og(self):
        """Generic webpage scrapes title + og:image."""
        import tempfile as _tmp
        url = "https://example.com/project-page"
        tmp_cache = pathlib.Path(_tmp.mkdtemp()) / "refs"

        async def fake_fetch_text(u, timeout=15.0):
            return (
                "<html><head>"
                "<title>Project Foo</title>"
                '<meta property="og:image" content="https://example.com/foo-hero.jpg">'
                '<meta property="og:description" content="A thing we made">'
                "</head></html>"
            )

        async def fake_download(u, path, timeout=20.0):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fake-og-image")
            return True

        original_cache_dir = self.mod._REFERENCE_CACHE_DIR
        original_dl = self.mod._download_to_path
        original_ft = self.mod._fetch_url_text
        self.mod._REFERENCE_CACHE_DIR = tmp_cache
        self.mod._download_to_path = fake_download
        self.mod._fetch_url_text = fake_fetch_text
        try:
            out = json.loads(asyncio.run(self.mod.fetch_reference(url)))
        finally:
            self.mod._REFERENCE_CACHE_DIR = original_cache_dir
            self.mod._download_to_path = original_dl
            self.mod._fetch_url_text = original_ft

        self.assertTrue(out["ok"])
        self.assertEqual(out["source_kind"], "webpage")
        self.assertEqual(out["title"], "Project Foo")
        self.assertEqual(out["description"], "A thing we made")
        self.assertTrue(out["thumbnail_local_path"].endswith(".jpg"))

    def test_list_reference_graphs_returns_intro_set(self):
        """Real intro/ graphs are findable without filters."""
        out = json.loads(asyncio.run(self.mod.list_reference_graphs()))
        self.assertTrue(out["ok"])
        self.assertGreater(out["total"], 5, "expected several intro graphs")
        ids = [g["meta"]["id"] for g in out["graphs"]]
        self.assertIn("showcase_demo", ids)

    def test_list_reference_graphs_pattern_filter(self):
        """Filtering by 'drum-driven-pulse' pattern narrows to showcase_demo."""
        out = json.loads(asyncio.run(
            self.mod.list_reference_graphs(pattern_filter="drum-driven-pulse")))
        self.assertTrue(out["ok"])
        self.assertGreaterEqual(out["total"], 1)
        for g in out["graphs"]:
            pat_ids = [p["pattern_id"] for p in g["patterns_detected"]]
            self.assertIn("drum-driven-pulse", pat_ids)

    def test_list_reference_graphs_subdir_filter(self):
        """Subdir filter narrows to a single graph directory."""
        out = json.loads(asyncio.run(
            self.mod.list_reference_graphs(subdir_filter="intro")))
        self.assertTrue(out["ok"])
        for g in out["graphs"]:
            self.assertEqual(g["subdir"], "intro")

    def test_list_reference_graphs_skips_package_dependent_by_default(self):
        """Graphs with meta.requires_packages are omitted unless include_packages=True."""
        out_no_pkg = json.loads(asyncio.run(self.mod.list_reference_graphs()))
        for g in out_no_pkg["graphs"]:
            self.assertEqual(g["meta"]["requires_packages"], [],
                             f"graph {g['meta']['id']} has requires_packages={g['meta']['requires_packages']} but was not filtered out")

    def test_compare_output_to_reference_happy_path(self):
        """Writes capture alongside reference path, returns both."""
        import tempfile as _tmp, base64
        # Build a minimal PNG (1x1 pixel, valid base64)
        fake_png_b64 = base64.b64encode(b"\x89PNG\r\n\x1a\nfake").decode()

        # Set up a reference file
        ref_dir = _tmp.mkdtemp()
        ref_path = pathlib.Path(ref_dir) / "ref.png"
        ref_path.write_bytes(b"ref-bytes")

        async def fake_post(method, body=None):
            self.assertEqual(method, "capture_frame")
            return json.dumps({"ok": True, "width": 100, "height": 100,
                               "png_base64": fake_png_b64})

        original = self.mod._post
        self.mod._post = fake_post
        try:
            out = json.loads(asyncio.run(
                self.mod.compare_output_to_reference(str(ref_path), save_dir=ref_dir)))
        finally:
            self.mod._post = original

        self.assertTrue(out["ok"])
        # Path is .resolve()'d internally, which on macOS expands /var → /private/var
        self.assertEqual(pathlib.Path(out["reference_path"]).resolve(),
                         ref_path.resolve())
        self.assertTrue(out["capture_path"].endswith(".png"))
        self.assertTrue(pathlib.Path(out["capture_path"]).exists())

    def test_compare_output_to_reference_missing_reference_errors(self):
        out = json.loads(asyncio.run(
            self.mod.compare_output_to_reference("/nonexistent/ref.png")))
        self.assertFalse(out["ok"])
        self.assertIn("not found", out["error"])

    def test_compare_output_to_reference_capture_failure_errors(self):
        import tempfile as _tmp
        ref_path = pathlib.Path(_tmp.mkdtemp()) / "ref.png"
        ref_path.write_bytes(b"ref")

        async def fake_post(method, body=None):
            return json.dumps({"ok": False, "error": "no video output"})

        original = self.mod._post
        self.mod._post = fake_post
        try:
            out = json.loads(asyncio.run(
                self.mod.compare_output_to_reference(str(ref_path))))
        finally:
            self.mod._post = original

        self.assertFalse(out["ok"])
        self.assertIn("no video output", out["error"])

    def test_compare_outputs_bridge(self):
        async def fake_post(method, body=None):
            self.assertEqual(method, "compare_outputs")
            self.assertEqual(body["mode"], "audio")
            self.assertEqual(body["include_payload"], False)
            self.assertEqual(body["a"]["window_seconds"], 1.5)
            self.assertEqual(body["b"]["window_seconds"], 3.0)
            self.assertEqual(body["node_id"], "audio_out1")
            return '{"ok":true}'

        original = self.mod._post
        self.mod._post = fake_post
        try:
            out = asyncio.run(self.mod.compare_outputs("audio", 1.5, 3.0, False, "audio_out1"))
        finally:
            self.mod._post = original
        self.assertEqual(out, '{"ok":true}')

    def test_capture_interface_bridge(self):
        async def fake_post(method, body=None):
            self.assertEqual(method, "capture_interface")
            self.assertEqual(body["node_id"], "shapes")
            self.assertEqual(body["save_path"], "/tmp/x.png")
            self.assertEqual(body["ensure_ui_visible"], True)
            return '{"ok":true}'

        original = self.mod._post
        self.mod._post = fake_post
        try:
            out = asyncio.run(self.mod.capture_interface("shapes", "/tmp/x.png", True))
        finally:
            self.mod._post = original
        self.assertEqual(out, '{"ok":true}')

    def test_capture_image_interface_bridge(self):
        async def fake_post(method, body=None):
            self.assertEqual(method, "capture_interface")
            self.assertEqual(body["node_id"], "shapes")
            self.assertEqual(body["save_path"], "/tmp/x.png")
            self.assertEqual(body["ensure_ui_visible"], True)
            return '{"ok":true}'

        original = self.mod._post
        self.mod._post = fake_post
        try:
            out = asyncio.run(self.mod.capture_image("interface", "shapes", "/tmp/x.png", True))
        finally:
            self.mod._post = original
        self.assertEqual(out, '{"ok":true}')

    def test_capture_image_output_bridge(self):
        async def fake_post(method, body=None):
            self.assertEqual(method, "capture_frame")
            self.assertEqual(body, {})
            return '{"ok":true}'

        original = self.mod._post
        self.mod._post = fake_post
        try:
            out = asyncio.run(self.mod.capture_image("output"))
        finally:
            self.mod._post = original
        self.assertEqual(out, '{"ok":true}')

    def test_sample_node_outputs_bridge(self):
        async def fake_post(method, body=None, timeout=10.0):
            self.assertEqual(method, "sample_node_outputs")
            self.assertEqual(body["node_id"], "cp1")
            self.assertEqual(body["duration_seconds"], 8.0)
            self.assertEqual(body["interval_ms"], 250)
            self.assertEqual(body["include_lanes"], True)
            self.assertEqual(timeout, 13.0)
            return '{"ok":true}'

        original = self.mod._post
        self.mod._post = fake_post
        try:
            out = asyncio.run(self.mod.sample_node_outputs("cp1", 8.0, 250, True))
        finally:
            self.mod._post = original
        self.assertEqual(out, '{"ok":true}')

    def test_load_graph_bridge(self):
        async def fake_post(method, body=None):
            self.assertEqual(method, "load_graph")
            self.assertEqual(body["path"], "/tmp/demo.json")
            return '{"ok":true}'

        original_post = self.mod._post
        original_resolve = self.mod._resolve_graph_path
        self.mod._post = fake_post
        self.mod._resolve_graph_path = lambda path: "/tmp/demo.json"
        try:
            out = asyncio.run(self.mod.load_graph("graphs/demo.json"))
        finally:
            self.mod._post = original_post
            self.mod._resolve_graph_path = original_resolve
        self.assertEqual(out, '{"ok":true}')

    def test_list_types_uses_cli(self):
        async def fake_cli(args):
            self.assertEqual(args, ["list-types", "--domain", "audio", "--json"])
            return '{"ok":true,"result":{"types":[]}}'

        original = self.mod._run_vivid_cli_json
        self.mod._run_vivid_cli_json = fake_cli
        try:
            out = asyncio.run(self.mod.list_types("audio"))
        finally:
            self.mod._run_vivid_cli_json = original
        self.assertEqual(out, '{"ok":true,"result":{"types":[]}}')

    def test_operator_docs_uses_cli(self):
        async def fake_cli(args):
            self.assertEqual(args, ["operator-docs", "Lfo", "--json"])
            return '{"ok":true}'

        original = self.mod._run_vivid_cli_json
        self.mod._run_vivid_cli_json = fake_cli
        try:
            out = asyncio.run(self.mod.operator_docs("Lfo"))
        finally:
            self.mod._run_vivid_cli_json = original
        self.assertEqual(out, '{"ok":true}')

    def test_install_package_falls_back_to_cli_when_runtime_is_absent(self):
        async def fake_reachable():
            return False

        async def fake_cli(args):
            self.assertEqual(args, ["install", "https://example.com/pkg.git", "--json"])
            return '{"ok":true}'

        original_reachable = self.mod._runtime_is_reachable
        original_cli = self.mod._run_vivid_cli_json
        self.mod._runtime_is_reachable = fake_reachable
        self.mod._run_vivid_cli_json = fake_cli
        try:
            out = asyncio.run(self.mod.install_package("https://example.com/pkg.git"))
        finally:
            self.mod._runtime_is_reachable = original_reachable
            self.mod._run_vivid_cli_json = original_cli
        self.assertEqual(out, '{"ok":true}')

    def test_runtime_status_external_runtime(self):
        original = self.mod._runtime_is_reachable
        async def fake_reachable():
            return True
        self.mod._runtime_is_reachable = fake_reachable
        try:
            out = json.loads(asyncio.run(self.mod.runtime_status()))
        finally:
            self.mod._runtime_is_reachable = original
        self.assertTrue(out["ok"])
        self.assertEqual(out["status"], "external_running")
        self.assertFalse(out["bridge_managed"])

    def test_bridge_smoke_uses_session_helper(self):
        smoke_src = (
            pathlib.Path(__file__).resolve().parent.parent
            / "scripts"
            / "mcp_bridge_smoke.py"
        ).read_text(encoding="utf-8")
        self.assertIn("from vivid_mcp_session import VividMCPSession", smoke_src)
        self.assertNotIn("spec_from_file_location", smoke_src)
        self.assertNotIn("_load_bridge_module", smoke_src)

    def test_ensure_runtime_reuses_existing(self):
        original_reachable = self.mod._runtime_is_reachable
        original_resolve = self.mod._resolve_graph_path
        original_load = self.mod._load_graph_path

        async def fake_reachable():
            return True

        async def fake_load(path):
            self.assertEqual(path, "/tmp/demo.json")
            return True, '{"ok":true}'

        self.mod._runtime_is_reachable = fake_reachable
        self.mod._resolve_graph_path = lambda path: "/tmp/demo.json"
        self.mod._load_graph_path = fake_load
        try:
            out = json.loads(asyncio.run(self.mod.ensure_runtime("graphs/demo.json")))
        finally:
            self.mod._runtime_is_reachable = original_reachable
            self.mod._resolve_graph_path = original_resolve
            self.mod._load_graph_path = original_load

        self.assertTrue(out["ok"])
        self.assertFalse(out["launched"])
        self.assertTrue(out["reused_existing"])
        self.assertTrue(out["graph_loaded"])

    def test_ensure_runtime_launches_new_process(self):
        class FakeProc:
            pid = 4242
            def poll(self):
                return None

        original_reachable = self.mod._runtime_is_reachable
        original_resolve = self.mod._resolve_graph_path
        original_launch = self.mod._launch_runtime_process
        original_wait = self.mod._wait_for_runtime_ready

        async def fake_reachable():
            return False

        async def fake_wait(proc, timeout):
            self.assertEqual(proc.pid, 4242)
            self.assertGreater(timeout, 0.0)
            return True

        self.mod._runtime_is_reachable = fake_reachable
        self.mod._resolve_graph_path = lambda path: "/tmp/demo.json"
        self.mod._launch_runtime_process = lambda graph: (FakeProc(), "/tmp/vivid_mcp_runtime.log")
        self.mod._wait_for_runtime_ready = fake_wait
        try:
            out = json.loads(asyncio.run(self.mod.ensure_runtime("graphs/demo.json")))
        finally:
            self.mod._runtime_is_reachable = original_reachable
            self.mod._resolve_graph_path = original_resolve
            self.mod._launch_runtime_process = original_launch
            self.mod._wait_for_runtime_ready = original_wait

        self.assertTrue(out["ok"])
        self.assertTrue(out["launched"])
        self.assertFalse(out["reused_existing"])
        self.assertEqual(out["pid"], 4242)

    def test_ensure_runtime_timeout_reports_log_path(self):
        class FakeProc:
            pid = 5150
            def poll(self):
                return None

        original_reachable = self.mod._runtime_is_reachable
        original_launch = self.mod._launch_runtime_process
        original_wait = self.mod._wait_for_runtime_ready

        async def fake_reachable():
            return False

        async def fake_wait(_proc, _timeout):
            return False

        self.mod._runtime_is_reachable = fake_reachable
        self.mod._launch_runtime_process = lambda graph: (FakeProc(), "/tmp/vivid_mcp_runtime.log")
        self.mod._wait_for_runtime_ready = fake_wait
        try:
            out = json.loads(asyncio.run(self.mod.ensure_runtime()))
        finally:
            self.mod._runtime_is_reachable = original_reachable
            self.mod._launch_runtime_process = original_launch
            self.mod._wait_for_runtime_ready = original_wait

        self.assertFalse(out["ok"])
        self.assertTrue(out["launched"])
        self.assertEqual(out["log_path"], "/tmp/vivid_mcp_runtime.log")

    def test_stop_runtime_only_stops_bridge_managed_process(self):
        class FakeProc:
            pid = 777
            terminated = False
            waited = False
            killed = False
            def poll(self):
                return None
            def terminate(self):
                self.terminated = True
            def wait(self, timeout=None):
                self.waited = True
                return 0
            def kill(self):
                self.killed = True

        proc = FakeProc()
        self.mod._managed_runtime_process = proc
        self.mod._managed_runtime_log_path = "/tmp/vivid_mcp_runtime.log"
        out = json.loads(asyncio.run(self.mod.stop_runtime()))
        self.assertTrue(out["ok"])
        self.assertTrue(out["stopped"])
        self.assertTrue(proc.terminated)
        self.assertTrue(proc.waited)
        self.assertIsNone(self.mod._managed_runtime_process)

    def test_stop_runtime_does_not_kill_external_runtime(self):
        original = self.mod._runtime_is_reachable
        async def fake_reachable():
            return True
        self.mod._runtime_is_reachable = fake_reachable
        try:
            out = json.loads(asyncio.run(self.mod.stop_runtime()))
        finally:
            self.mod._runtime_is_reachable = original
        self.assertTrue(out["ok"])
        self.assertFalse(out["stopped"])
        self.assertFalse(out["bridge_managed"])

    def test_wait_for_runtime_ready_polls_until_reachable(self):
        class FakeProc:
            def poll(self):
                return None

        original_reachable = self.mod._runtime_is_reachable

        attempts = {"count": 0}

        async def fake_reachable():
            attempts["count"] += 1
            return attempts["count"] >= 2

        self.mod._runtime_is_reachable = fake_reachable
        try:
            ready = asyncio.run(self.mod._wait_for_runtime_ready(FakeProc(), 0.6))
        finally:
            self.mod._runtime_is_reachable = original_reachable

        self.assertTrue(ready)
        self.assertGreaterEqual(attempts["count"], 2)


class AudioToolingBridgeTests(unittest.TestCase):
    """Bridge-shape tests for the audio-debug tools after the librosa pivot.

    Each tool calls one of three minimal C++ data endpoints (capture_audio,
    capture_node_audio, capture_lane_series, capture_note_window) and runs
    librosa-based analysis/rendering Python-side. These tests verify the
    HTTP wire format (which method, which body fields) and that the wrapper
    decodes responses correctly. Pure analysis correctness lives in
    test_audio_analysis.py."""

    @classmethod
    def setUpClass(cls):
        cls.mod = _load_module()
        # Build a tiny synthetic WAV (440 Hz sine, 100 ms) once — used as
        # the canned `wav_base64` payload for capture_audio responses.
        import base64 as _b64
        import io as _io
        import math as _math
        import struct as _struct
        sr = 48000
        n = sr // 10  # 100 ms
        # Inline minimal WAV writer (32-bit float, mono) so the test doesn't
        # depend on soundfile being importable from the harness path.
        data = bytearray()
        for i in range(n):
            v = 0.5 * _math.sin(2 * _math.pi * 440.0 * i / sr)
            data.extend(_struct.pack("<f", v))
        data_size = len(data)
        wav = bytearray()
        wav.extend(b"RIFF")
        wav.extend(_struct.pack("<I", 36 + data_size))
        wav.extend(b"WAVE")
        wav.extend(b"fmt ")
        wav.extend(_struct.pack("<I", 16))      # fmt chunk size
        wav.extend(_struct.pack("<H", 3))       # IEEE float
        wav.extend(_struct.pack("<H", 1))       # channels
        wav.extend(_struct.pack("<I", sr))
        wav.extend(_struct.pack("<I", sr * 4))  # byte rate
        wav.extend(_struct.pack("<H", 4))       # block align
        wav.extend(_struct.pack("<H", 32))      # bits per sample
        wav.extend(b"data")
        wav.extend(_struct.pack("<I", data_size))
        wav.extend(data)
        cls.SYNTH_WAV_B64 = _b64.b64encode(bytes(wav)).decode("ascii")
        cls.SYNTH_WAV_OK = json.dumps({
            "ok": True, "sample_rate": sr, "channels": 1,
            "samples": n, "wav_base64": cls.SYNTH_WAV_B64,
        })

    def _run_with_fake_post(self, capture_dict, canned, coro_factory):
        """Swap _post for a fake that records the (method, body) and
        returns `canned`. Returns whatever the wrapper returns."""

        async def fake_post(method, body=None, timeout=10.0):
            capture_dict["method"] = method
            capture_dict["body"] = body or {}
            return canned

        original = self.mod._post
        self.mod._post = fake_post
        try:
            return asyncio.run(coro_factory())
        finally:
            self.mod._post = original

    # --- Plot tools (final-mix path) ---------------------------------------

    def test_capture_waveform_plot_calls_capture_audio(self):
        cap: dict = {}
        out_raw = self._run_with_fake_post(
            cap, self.SYNTH_WAV_OK,
            lambda: self.mod.capture_waveform_plot(duration_ms=200.0))
        self.assertEqual(cap["method"], "capture_audio")
        self.assertAlmostEqual(cap["body"]["duration"], 0.2, places=3)
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])
        self.assertEqual(out["kind"], "waveform")
        self.assertIn("capture_path", out)

    def test_capture_spectrogram_calls_capture_audio(self):
        cap: dict = {}
        out_raw = self._run_with_fake_post(
            cap, self.SYNTH_WAV_OK,
            lambda: self.mod.capture_spectrogram(fmin_hz=100.0, fmax_hz=8000.0))
        self.assertEqual(cap["method"], "capture_audio")
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])
        self.assertEqual(out["kind"], "spectrogram")

    def test_capture_envelope_plot_calls_capture_audio(self):
        cap: dict = {}
        out_raw = self._run_with_fake_post(
            cap, self.SYNTH_WAV_OK,
            lambda: self.mod.capture_envelope_plot(duration_ms=300.0))
        self.assertEqual(cap["method"], "capture_audio")
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])
        self.assertEqual(out["kind"], "envelope")

    # --- Per-node path -----------------------------------------------------

    def test_capture_waveform_plot_with_node_id_calls_capture_node_audio(self):
        cap: dict = {}
        canned = json.dumps({
            "ok": True, "node_id": "synth_1", "sample_rate": 48000,
            "channels": 1, "frames": 1024,
            "wav_base64": self.SYNTH_WAV_B64,
        })
        out_raw = self._run_with_fake_post(
            cap, canned,
            lambda: self.mod.capture_waveform_plot(duration_ms=200.0,
                                                    node_id="synth_1"))
        self.assertEqual(cap["method"], "capture_node_audio")
        self.assertEqual(cap["body"]["node_id"], "synth_1")
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])

    # --- Lane strip --------------------------------------------------------

    def test_capture_voice_lane_strip_calls_capture_lane_series(self):
        cap: dict = {}
        canned = json.dumps({
            "ok": True, "node_id": "wt_1", "port_name": "lane_freq",
            "lane_count": 2, "samples_per_lane": 4,
            "duration_ms": 200,
            "samples": [[100.0, 100.0, 100.0, 100.0],
                        [200.0, 200.0, 200.0, 200.0]],
        })
        out_raw = self._run_with_fake_post(
            cap, canned,
            lambda: self.mod.capture_voice_lane_strip(
                "wt_1", "lane_freq", duration_ms=200.0))
        self.assertEqual(cap["method"], "capture_lane_series")
        self.assertEqual(cap["body"]["node_id"], "wt_1")
        self.assertEqual(cap["body"]["port_name"], "lane_freq")
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])
        self.assertEqual(out["lane_count"], 2)

    def test_capture_voice_lane_strip_passes_id_port_name(self):
        cap: dict = {}
        canned = json.dumps({
            "ok": True, "node_id": "wt_1", "port_name": "lane_freq",
            "id_port_name": "lane_id",
            "lane_count": 2, "samples_per_lane": 2, "duration_ms": 100,
            "samples": [[100.0, 100.0], [200.0, 200.0]],
            "ids":     [[10, 10],         [20, 20]],
        })
        out_raw = self._run_with_fake_post(
            cap, canned,
            lambda: self.mod.capture_voice_lane_strip(
                "wt_1", "lane_freq", id_port_name="lane_id"))
        self.assertEqual(cap["body"]["id_port_name"], "lane_id")
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])

    # --- Detail / WAV / compare -------------------------------------------

    def test_analyze_audio_detail_calls_capture_audio(self):
        cap: dict = {}
        out_raw = self._run_with_fake_post(
            cap, self.SYNTH_WAV_OK,
            lambda: self.mod.analyze_audio_detail(duration_ms=200.0))
        self.assertEqual(cap["method"], "capture_audio")
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])
        # librosa pipeline ran; the keys we asked for are present.
        self.assertIn("pitch_track", out)
        self.assertIn("band_energies", out)

    def test_record_audio_to_wav_writes_file(self):
        import tempfile, os
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "ref.wav")
            cap: dict = {}
            out_raw = self._run_with_fake_post(
                cap, self.SYNTH_WAV_OK,
                lambda: self.mod.record_audio_to_wav(path, duration_ms=100.0))
            self.assertEqual(cap["method"], "capture_audio")
            out = json.loads(out_raw)
            self.assertTrue(out["ok"])
            self.assertTrue(os.path.exists(path))
            # File starts with RIFF header.
            with open(path, "rb") as f:
                self.assertEqual(f.read(4), b"RIFF")

    # --- MIDI inject probes -----------------------------------------------

    def test_capture_note_response_calls_capture_note_window(self):
        cap: dict = {}
        canned = json.dumps({
            "ok": True, "midi_node_id": "midi_1",
            "events_scheduled": 2, "events_fired": 2,
            "capture_ms": 400.0, "sample_rate": 48000,
            "channels": 1, "frames": 4800,
            "wav_base64": self.SYNTH_WAV_B64,
        })
        out_raw = self._run_with_fake_post(
            cap, canned,
            lambda: self.mod.capture_note_response("midi_1", note=60,
                                                    sustain_ms=200.0,
                                                    capture_ms=400.0))
        self.assertEqual(cap["method"], "capture_note_window")
        self.assertEqual(cap["body"]["midi_node_id"], "midi_1")
        self.assertEqual(len(cap["body"]["events"]), 2)
        # First event is note_on, second is note_off.
        self.assertEqual(cap["body"]["events"][0]["type"], "on")
        self.assertEqual(cap["body"]["events"][0]["note"], 60)
        self.assertEqual(cap["body"]["events"][1]["type"], "off")
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])
        self.assertEqual(out["kind"], "note_response")

    def test_capture_polyphony_response_emits_chord_schedule(self):
        cap: dict = {}
        canned = json.dumps({
            "ok": True, "midi_node_id": "midi_1",
            "events_scheduled": 6, "events_fired": 6,
            "capture_ms": 1500.0, "sample_rate": 48000,
            "channels": 1, "frames": 4800,
            "wav_base64": self.SYNTH_WAV_B64,
        })
        out_raw = self._run_with_fake_post(
            cap, canned,
            lambda: self.mod.capture_polyphony_response(
                "midi_1", [60, 64, 67]))
        self.assertEqual(cap["method"], "capture_note_window")
        # 3 NOTE_ONs + 3 NOTE_OFFs (default sustain_ms=600 > 0).
        self.assertEqual(len(cap["body"]["events"]), 6)
        on_notes = sorted(e["note"] for e in cap["body"]["events"]
                           if e["type"] == "on")
        self.assertEqual(on_notes, [60, 64, 67])
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])
        self.assertEqual(out["polyphony"]["voice_count"], 3)

    def test_capture_retrigger_response_emits_retrigger_schedule(self):
        cap: dict = {}
        canned = json.dumps({
            "ok": True, "midi_node_id": "midi_1",
            "events_scheduled": 8, "events_fired": 8,
            "capture_ms": 1500.0, "sample_rate": 48000,
            "channels": 1, "frames": 4800,
            "wav_base64": self.SYNTH_WAV_B64,
        })
        out_raw = self._run_with_fake_post(
            cap, canned,
            lambda: self.mod.capture_retrigger_response(
                "midi_1", note=60, count=4, interval_ms=200.0))
        self.assertEqual(cap["method"], "capture_note_window")
        # 4 NOTE_ONs + 3 inter-trigger NOTE_OFFs + 1 final NOTE_OFF = 8.
        self.assertEqual(len(cap["body"]["events"]), 8)
        out = json.loads(out_raw)
        self.assertTrue(out["ok"])
        self.assertEqual(len(out["retrigger"]["per_retrigger"]), 4)




if __name__ == "__main__":
    unittest.main()
