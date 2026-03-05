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


if __name__ == "__main__":
    unittest.main()

