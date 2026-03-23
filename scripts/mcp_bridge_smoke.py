#!/usr/bin/env python3
"""Run a live Vivid smoke flow through the Python MCP bridge.

This is the checked-in path for bridge-driven live investigations. It talks to
`mcp/vivid_mcp.py` directly instead of hitting the control server with ad hoc
HTTP, so runtime startup, graph loading, inspection, and interface capture all
exercise the same bridge surface that Codex/Claude/Cursor use.
"""

from __future__ import annotations

import argparse
import asyncio
import importlib.util
import json
import logging
import pathlib
import sys
from dataclasses import asdict, dataclass


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_ARTIFACT_DIR = pathlib.Path("/tmp/vivid_live_ui_review")


logging.getLogger("httpx").setLevel(logging.WARNING)
logging.getLogger("httpcore").setLevel(logging.WARNING)


@dataclass
class CaptureCase:
    graph_path: str
    node_id: str
    save_path: str


def _load_bridge_module():
    module_path = REPO_ROOT / "mcp" / "vivid_mcp.py"
    spec = importlib.util.spec_from_file_location("vivid_mcp_live_smoke", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load MCP bridge from {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _parse_json(raw: str, label: str) -> dict:
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"{label} returned invalid JSON: {exc}") from exc


def _require_ok(payload: dict, label: str) -> dict:
    if not payload.get("ok", False):
        raise RuntimeError(f"{label} failed: {json.dumps(payload, indent=2, sort_keys=True)}")
    return payload


def _node_from_introspection(payload: dict, node_id: str) -> dict:
    result = payload.get("result")
    nodes = result.get("nodes", []) if isinstance(result, dict) else []
    for node in nodes:
        if isinstance(node, dict) and node.get("node_id") == node_id:
            return node
    raise RuntimeError(f"node '{node_id}' not found in introspect_nodes payload")


def _node_from_graph(payload: dict, node_id: str) -> dict:
    result = payload.get("result")
    nodes = result.get("nodes", []) if isinstance(result, dict) else []
    for node in nodes:
        if isinstance(node, dict) and node.get("id") == node_id:
            return node
    raise RuntimeError(f"node '{node_id}' not found in inspect_graph payload")


def _phase4_cases(artifact_dir: pathlib.Path) -> list[CaptureCase]:
    return [
        CaptureCase(
            "graphs/gpu/instanced_shapes_demo.json",
            "shapes",
            str(artifact_dir / "instanced_shapes_shapes.png"),
        ),
        CaptureCase(
            "graphs/gpu/instanced_shapes_demo.json",
            "scale_lfo",
            str(artifact_dir / "instanced_shapes_scale_lfo.png"),
        ),
        CaptureCase(
            "graphs/gpu/particle_envelope_demo.json",
            "env",
            str(artifact_dir / "particle_envelope_env.png"),
        ),
    ]


def _build_cases(args: argparse.Namespace) -> list[CaptureCase]:
    artifact_dir = pathlib.Path(args.artifact_dir).expanduser()
    cases: list[CaptureCase] = []

    if args.preset == "phase4":
        cases.extend(_phase4_cases(artifact_dir))

    for graph_path, node_id, save_path in args.capture:
        save = pathlib.Path(save_path).expanduser()
        if not save.is_absolute():
            save = (artifact_dir / save).resolve()
        cases.append(CaptureCase(graph_path, node_id, str(save)))

    if not cases:
        raise RuntimeError("no capture cases specified; use --preset and/or --capture")

    return cases


async def _run(args: argparse.Namespace) -> int:
    bridge = _load_bridge_module()
    artifact_dir = pathlib.Path(args.artifact_dir).expanduser()
    artifact_dir.mkdir(parents=True, exist_ok=True)
    cases = _build_cases(args)

    summary: dict = {
        "artifact_dir": str(artifact_dir),
        "cases": [],
    }

    status_before = _require_ok(
        _parse_json(await bridge.runtime_status(), "runtime_status(before)"),
        "runtime_status(before)",
    )
    print(f"runtime before: {status_before['status']} @ {status_before['url']}")
    summary["runtime_before"] = status_before

    current_graph = ""
    try:
        for idx, case in enumerate(cases, start=1):
            print(f"[{idx}/{len(cases)}] graph={case.graph_path} node={case.node_id}")
            if idx == 1:
                ensure = _require_ok(
                    _parse_json(await bridge.ensure_runtime(case.graph_path), "ensure_runtime"),
                    "ensure_runtime",
                )
                current_graph = case.graph_path
                print(
                    "  ensure_runtime: "
                    f"launched={ensure.get('launched')} "
                    f"reused_existing={ensure.get('reused_existing')} "
                    f"graph_loaded={ensure.get('graph_loaded')}"
                )
            elif case.graph_path != current_graph:
                load = _require_ok(
                    _parse_json(await bridge.load_graph(case.graph_path), "load_graph"),
                    "load_graph",
                )
                current_graph = case.graph_path
                print(f"  load_graph: {load.get('message', 'ok')}")

            inspect_payload = _require_ok(
                _parse_json(await bridge.inspect_graph(), "inspect_graph"),
                "inspect_graph",
            )
            _node_from_graph(inspect_payload, case.node_id)

            intro_payload = _require_ok(
                _parse_json(await bridge.introspect_nodes(True), "introspect_nodes"),
                "introspect_nodes",
            )
            intro_node = _node_from_introspection(intro_payload, case.node_id)
            health = intro_node.get("health", {}) if isinstance(intro_node, dict) else {}
            missing_operator = bool(health.get("missing_operator", False))
            errored = bool(health.get("errored", False))

            capture_payload = _require_ok(
                _parse_json(
                    await bridge.capture_image("interface", case.node_id, case.save_path, True),
                    "capture_image",
                ),
                "capture_image",
            )
            save_path = pathlib.Path(case.save_path)
            if not save_path.exists() or save_path.stat().st_size == 0:
                raise RuntimeError(f"capture_image reported ok but file is missing or empty: {save_path}")

            print(
                "  capture_image: "
                f"missing_operator={missing_operator} "
                f"errored={errored} "
                f"saved={save_path}"
            )

            summary["cases"].append(
                {
                    **asdict(case),
                    "missing_operator": missing_operator,
                    "errored": errored,
                    "capture": {
                        "width": capture_payload.get("width"),
                        "height": capture_payload.get("height"),
                        "path": capture_payload.get("path", str(save_path)),
                    },
                }
            )
    finally:
        status_after = _require_ok(
            _parse_json(await bridge.runtime_status(), "runtime_status(after)"),
            "runtime_status(after)",
        )
        summary["runtime_after"] = status_after
        print(f"runtime after: {status_after['status']} @ {status_after['url']}")

        if args.stop_runtime:
            stop = _require_ok(
                _parse_json(await bridge.stop_runtime(), "stop_runtime"),
                "stop_runtime",
            )
            summary["stop_runtime"] = stop
            print(
                "stop_runtime: "
                f"stopped={stop.get('stopped')} "
                f"bridge_managed={stop.get('bridge_managed')}"
            )

    if args.summary_json:
        summary_path = pathlib.Path(args.summary_json).expanduser()
        if not summary_path.is_absolute():
            summary_path = (artifact_dir / summary_path).resolve()
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
        print(f"summary: {summary_path}")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a live Vivid smoke flow through mcp/vivid_mcp.py",
    )
    parser.add_argument(
        "--preset",
        choices=["phase4"],
        help="Run a built-in bridge smoke preset",
    )
    parser.add_argument(
        "--capture",
        nargs=3,
        metavar=("GRAPH", "NODE", "SAVE_PATH"),
        action="append",
        default=[],
        help="Capture one interface case via the MCP bridge; repeatable",
    )
    parser.add_argument(
        "--artifact-dir",
        default=str(DEFAULT_ARTIFACT_DIR),
        help="Directory used for preset artifacts and relative save paths",
    )
    parser.add_argument(
        "--summary-json",
        help="Optional path to write a machine-readable summary JSON",
    )
    parser.add_argument(
        "--stop-runtime",
        action="store_true",
        help="Stop the runtime at the end if it was bridge-managed",
    )

    args = parser.parse_args()
    try:
        return asyncio.run(_run(args))
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
