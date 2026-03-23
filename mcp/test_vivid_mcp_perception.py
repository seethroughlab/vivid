import asyncio
import importlib.util
import json
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

    def test_run_diagnostics_compact_summary(self):
        raw = json.dumps({
            "ok": True,
            "schema_version": 1,
            "result": {
                "summary": {"critical": 2, "warning": 1, "info": 0},
                "hints": [{"id": "h1"}, {"id": "h2"}, {"id": "h3"}, {"id": "h4"}],
            },
        })
        out = json.loads(self.mod._perception_response(raw, "run_diagnostics", False))
        self.assertTrue(out["ok"])
        self.assertEqual(out["schema_version"], 1)
        self.assertEqual(out["summary"]["critical"], 2)
        self.assertEqual(out["summary"]["warning"], 1)
        self.assertEqual(out["summary"]["info"], 0)
        self.assertEqual(out["summary"]["top_hint_ids"], ["h1", "h2", "h3"])
        self.assertNotIn("result", out)

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


if __name__ == "__main__":
    unittest.main()
