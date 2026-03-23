#!/usr/bin/env python3
"""Run live Vivid investigations through one long-lived MCP bridge session."""

from __future__ import annotations

import argparse
import asyncio
import json
import pathlib
import sys
from dataclasses import asdict, dataclass

from vivid_mcp_session import VividMCPSession


DEFAULT_ARTIFACT_DIR = pathlib.Path("/tmp/vivid_live_ui_review")
_CHORD_SAMPLE_EPSILON = 0.01


@dataclass
class CaptureCase:
    graph_path: str
    node_id: str
    save_path: str


@dataclass
class SampleCase:
    node_id: str
    duration_seconds: float
    interval_ms: int
    include_spreads: bool


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


def _dream_keys_graph() -> str:
    path = "/Users/jeff/Developer/vivid-wavetable/graphs/extended/wavetable_dream_keys_demo.json"
    if not pathlib.Path(path).exists():
        raise FileNotFoundError(
            f"dream_keys graph not found: {path}\n"
            "  This preset requires the vivid-wavetable repo checked out alongside vivid."
        )
    return path


def _build_capture_cases(args: argparse.Namespace, artifact_dir: pathlib.Path) -> list[CaptureCase]:
    cases: list[CaptureCase] = []
    if args.preset == "phase4":
        cases.extend(_phase4_cases(artifact_dir))
    for graph_path, node_id, save_path in args.capture:
        save = pathlib.Path(save_path).expanduser()
        if not save.is_absolute():
            save = (artifact_dir / save).resolve()
        cases.append(CaptureCase(graph_path, node_id, str(save)))
    return cases


def _build_sample_cases(args: argparse.Namespace) -> list[SampleCase]:
    cases: list[SampleCase] = []
    for node_id, duration_str, interval_str in args.sample_node:
        cases.append(
            SampleCase(
                node_id=node_id,
                duration_seconds=float(duration_str),
                interval_ms=int(interval_str),
                include_spreads=not args.sample_without_spreads,
            )
        )
    return cases


def _rounded_signature(values: list[float]) -> tuple[float, ...]:
    return tuple(round(v / _CHORD_SAMPLE_EPSILON) * _CHORD_SAMPLE_EPSILON for v in values)


def _extract_output_series(sample_payload: dict, port_name: str, spread: bool) -> list:
    result = sample_payload.get("result", {})
    samples = result.get("samples", []) if isinstance(result, dict) else []
    series: list = []
    for sample in samples:
        if not isinstance(sample, dict):
            continue
        outputs = sample.get("outputs", {})
        if not isinstance(outputs, dict):
            continue
        port = outputs.get(port_name, {})
        if not isinstance(port, dict):
            continue
        if spread:
            vals = port.get("spread")
            if isinstance(vals, list):
                series.append(vals)
        else:
            val = port.get("scalar")
            if isinstance(val, (float, int)):
                series.append(float(val))
    return series


def _summarize_progression(clock_payload: dict, cp_payload: dict, audio_payload: dict) -> dict:
    beat_series = _extract_output_series(clock_payload, "beat_phase", spread=False)
    note_series = _extract_output_series(cp_payload, "notes", spread=True)
    gate_series = _extract_output_series(cp_payload, "gates", spread=True)

    distinct_chords = []
    seen = set()
    for chord in note_series:
        sig = _rounded_signature([float(v) for v in chord])
        if sig not in seen:
            seen.add(sig)
            distinct_chords.append(list(sig))

    gate_signatures = []
    gate_seen = set()
    for gates in gate_series:
        sig = _rounded_signature([float(v) for v in gates])
        if sig not in gate_seen:
            gate_seen.add(sig)
            gate_signatures.append(list(sig))

    audio_summary = audio_payload.get("summary", {})
    metrics = audio_payload.get("metrics", {})
    peak = float(metrics.get("peak", 0.0)) if isinstance(metrics, dict) else 0.0
    rms = float(metrics.get("rms", 0.0)) if isinstance(metrics, dict) else 0.0

    return {
        "clock_advances": any(beat > 0.001 for beat in beat_series),
        "distinct_chord_count": len(distinct_chords),
        "distinct_chords": distinct_chords,
        "gate_patterns": gate_signatures,
        "gate_cycles": len(gate_signatures) > 1,
        "audio_non_silent": peak > 0.001 or rms > 0.001,
        "audio_level": audio_summary.get("audio_level") if isinstance(audio_summary, dict) else None,
        "pass": (
            any(beat > 0.001 for beat in beat_series) and
            len(distinct_chords) > 1 and
            len(gate_signatures) > 1 and
            (peak > 0.001 or rms > 0.001)
        ),
    }


async def _run_capture_flow(session: VividMCPSession,
                            capture_cases: list[CaptureCase],
                            summary: dict) -> None:
    current_graph = ""
    for idx, case in enumerate(capture_cases, start=1):
        print(f"[{idx}/{len(capture_cases)}] graph={case.graph_path} node={case.node_id}")
        if idx == 1:
            ensure = _require_ok(
                await session.call_tool("ensure_runtime", {"graph_path": case.graph_path}),
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
                await session.call_tool("load_graph", {"path": case.graph_path}),
                "load_graph",
            )
            current_graph = case.graph_path
            print(f"  load_graph: {load.get('message', 'ok')}")

        inspect_payload = _require_ok(await session.call_tool("inspect_graph"), "inspect_graph")
        _node_from_graph(inspect_payload, case.node_id)

        intro_payload = _require_ok(
            await session.call_tool("introspect_nodes", {"include_payload": True}),
            "introspect_nodes",
        )
        intro_node = _node_from_introspection(intro_payload, case.node_id)
        health = intro_node.get("health", {}) if isinstance(intro_node, dict) else {}
        missing_operator = bool(health.get("missing_operator", False))
        errored = bool(health.get("errored", False))

        capture_payload = _require_ok(
            await session.call_tool(
                "capture_image",
                {
                    "mode": "interface",
                    "node_id": case.node_id,
                    "save_path": case.save_path,
                    "ensure_ui_visible": True,
                },
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


async def _run_dream_keys_flow(session: VividMCPSession,
                               artifact_dir: pathlib.Path,
                               summary: dict) -> None:
    graph_path = _dream_keys_graph()
    ensure = _require_ok(
        await session.call_tool("ensure_runtime", {"graph_path": graph_path}),
        "ensure_runtime",
    )
    print(
        "dream_keys ensure_runtime: "
        f"launched={ensure.get('launched')} "
        f"reused_existing={ensure.get('reused_existing')} "
        f"graph_loaded={ensure.get('graph_loaded')}"
    )

    clock_payload = _require_ok(
        await session.call_tool(
            "sample_node_outputs",
            {
                "node_id": "clock1",
                "duration_seconds": 3.0,
                "interval_ms": 250,
                "include_spreads": True,
            },
        ),
        "sample_node_outputs(clock1)",
    )
    cp_payload = _require_ok(
        await session.call_tool(
            "sample_node_outputs",
            {
                "node_id": "cp1",
                "duration_seconds": 20.0,
                "interval_ms": 500,
                "include_spreads": True,
            },
        ),
        "sample_node_outputs(cp1)",
    )
    audio_payload = _require_ok(
        await session.call_tool(
            "analyze_output",
            {
                "mode": "audio",
                "window_seconds": 2.0,
                "include_payload": True,
                "node_id": "out1",
            },
        ),
        "analyze_output(audio)",
    )
    capture_path = artifact_dir / "wavetable_dream_keys_cp1.png"
    capture_payload = _require_ok(
        await session.call_tool(
            "capture_image",
            {
                "mode": "interface",
                "node_id": "cp1",
                "save_path": str(capture_path),
                "ensure_ui_visible": True,
            },
        ),
        "capture_image(cp1)",
    )

    progression = _summarize_progression(clock_payload, cp_payload, audio_payload)
    print(
        "dream_keys progression: "
        f"clock_advances={progression['clock_advances']} "
        f"distinct_chords={progression['distinct_chord_count']} "
        f"gate_cycles={progression['gate_cycles']} "
        f"audio_non_silent={progression['audio_non_silent']}"
    )

    summary["dream_keys"] = {
        "graph_path": graph_path,
        "clock_sample": clock_payload.get("result", {}),
        "cp_sample": cp_payload.get("result", {}),
        "audio_analysis": {
            "summary": audio_payload.get("summary"),
            "metrics": audio_payload.get("metrics"),
            "result": audio_payload.get("result"),
        },
        "capture": {
            "path": capture_payload.get("path", str(capture_path)),
            "width": capture_payload.get("width"),
            "height": capture_payload.get("height"),
        },
        "progression": progression,
    }


async def _run_generic_sampling(session: VividMCPSession,
                                sample_cases: list[SampleCase],
                                summary: dict) -> None:
    if not sample_cases:
        return
    summary["samples"] = []
    for case in sample_cases:
        payload = _require_ok(
            await session.call_tool(
                "sample_node_outputs",
                {
                    "node_id": case.node_id,
                    "duration_seconds": case.duration_seconds,
                    "interval_ms": case.interval_ms,
                    "include_spreads": case.include_spreads,
                },
            ),
            f"sample_node_outputs({case.node_id})",
        )
        result = payload.get("result", {})
        count = result.get("sample_count")
        print(
            f"sample_node_outputs: node={case.node_id} samples={count} "
            f"duration={case.duration_seconds}s interval={case.interval_ms}ms"
        )
        summary["samples"].append({
            **asdict(case),
            "result": result,
        })


async def _run(args: argparse.Namespace) -> int:
    artifact_dir = pathlib.Path(args.artifact_dir).expanduser()
    artifact_dir.mkdir(parents=True, exist_ok=True)

    capture_cases = _build_capture_cases(args, artifact_dir)
    sample_cases = _build_sample_cases(args)
    if not capture_cases and not sample_cases and args.preset != "dream_keys":
        raise RuntimeError("no work specified; use --preset, --capture, or --sample-node")

    summary: dict = {
        "artifact_dir": str(artifact_dir),
        "cases": [],
    }

    async with VividMCPSession() as session:
        status_before = _require_ok(await session.call_tool("runtime_status"), "runtime_status(before)")
        print(f"runtime before: {status_before['status']} @ {status_before['url']}")
        summary["runtime_before"] = status_before

        try:
            if capture_cases:
                await _run_capture_flow(session, capture_cases, summary)
            if sample_cases:
                await _run_generic_sampling(session, sample_cases, summary)
            if args.preset == "dream_keys":
                await _run_dream_keys_flow(session, artifact_dir, summary)
        finally:
            status_after = _require_ok(await session.call_tool("runtime_status"), "runtime_status(after)")
            summary["runtime_after"] = status_after
            print(f"runtime after: {status_after['status']} @ {status_after['url']}")

            if args.stop_runtime:
                stop = _require_ok(await session.call_tool("stop_runtime"), "stop_runtime")
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
        description="Run live Vivid investigations through one MCP bridge session",
    )
    parser.add_argument(
        "--preset",
        choices=["phase4", "dream_keys"],
        help="Run a built-in bridge investigation preset",
    )
    parser.add_argument(
        "--capture",
        nargs=3,
        metavar=("GRAPH", "NODE", "SAVE_PATH"),
        action="append",
        default=[],
        help="Capture one interface case through the MCP bridge; repeatable",
    )
    parser.add_argument(
        "--sample-node",
        nargs=3,
        metavar=("NODE", "DURATION_SECONDS", "INTERVAL_MS"),
        action="append",
        default=[],
        help="Sample one live node repeatedly through the MCP bridge; repeatable",
    )
    parser.add_argument(
        "--sample-without-spreads",
        action="store_true",
        help="Omit spread payloads from --sample-node captures",
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
        help="Stop the runtime at the end if it was bridge-managed by the session",
    )

    args = parser.parse_args()
    try:
        return asyncio.run(_run(args))
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
