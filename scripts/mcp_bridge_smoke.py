#!/usr/bin/env python3
"""Run live Vivid investigations through one long-lived MCP bridge session."""

from __future__ import annotations

import argparse
import asyncio
import json
import pathlib
import shutil
import sys
from dataclasses import asdict, dataclass

from vivid_mcp_session import VividMCPSession


DEFAULT_ARTIFACT_DIR = pathlib.Path("/tmp/vivid_live_ui_review")
_CHORD_SAMPLE_EPSILON = 0.01
REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
STEP2_MODULE_FIXTURE = pathlib.Path("tests/fixtures/step2_modulation_smoke.vivid-module.json")
STEP2_GRAPH_FIXTURE = pathlib.Path("tests/fixtures/step2_modulation_smoke_graph.json")
STEP2_STAGED_MODULE_PATHS = [
    pathlib.Path("build/modules/step2_modulation_smoke.vivid-module.json"),
    pathlib.Path("build/vivid.app/Contents/Resources/modules/step2_modulation_smoke.vivid-module.json"),
]


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


def _expect(condition: bool, label: str) -> None:
    if not condition:
        raise RuntimeError(label)


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


def _result_list(payload: dict, label: str) -> list[dict]:
    result = payload.get("result")
    if isinstance(result, list):
        return result
    if isinstance(result, str):
        try:
            parsed = json.loads(result)
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"{label} returned invalid JSON list: {exc}") from exc
        if isinstance(parsed, list):
            return parsed
    raise RuntimeError(f"{label} returned unexpected result payload: {json.dumps(payload, indent=2, sort_keys=True)}")


def _find_named_entry(entries: list[dict] | object, name: str) -> dict:
    if not isinstance(entries, list):
        raise RuntimeError(f"expected list of named entries when looking for '{name}'")
    for entry in entries:
        if isinstance(entry, dict) and entry.get("name") == name:
            return entry
    raise RuntimeError(f"named entry '{name}' not found")


def _graph_param_value(node: dict, param_name: str) -> float:
    param = _find_named_entry(node.get("params", []), param_name)
    value = param.get("value")
    if not isinstance(value, (float, int)):
        raise RuntimeError(f"graph param '{param_name}' is missing numeric value")
    return float(value)


def _graph_output_value(node: dict, port_name: str) -> float:
    port = _find_named_entry(node.get("outputs", []), port_name)
    value = port.get("current_value")
    if not isinstance(value, (float, int)):
        raise RuntimeError(f"graph output '{port_name}' is missing numeric current_value")
    return float(value)


def _intro_param_value(node: dict, param_name: str) -> float:
    params = node.get("params", {})
    if not isinstance(params, dict):
        raise RuntimeError("introspect_nodes params payload is not an object")
    value = params.get(param_name)
    if not isinstance(value, (float, int)):
        raise RuntimeError(f"introspection param '{param_name}' is missing numeric value")
    return float(value)


def _intro_output_value(node: dict, port_name: str) -> float:
    port = _find_named_entry(node.get("outputs", []), port_name)
    value = port.get("scalar")
    if not isinstance(value, (float, int)):
        raise RuntimeError(f"introspection output '{port_name}' is missing numeric scalar")
    return float(value)


def _close_enough(actual: float, expected: float, *, eps: float = 1e-4) -> bool:
    return abs(actual - expected) <= eps


def _stage_step2_module_fixture() -> list[str]:
    src = REPO_ROOT / STEP2_MODULE_FIXTURE
    _expect(src.exists(), f"step2 fixture missing: {src}")
    staged: list[str] = []
    for rel_dst in STEP2_STAGED_MODULE_PATHS:
        dst = REPO_ROOT / rel_dst
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        staged.append(str(dst))
    return staged


async def _poll_until(label: str,
                      fetch,
                      predicate,
                      timeout_seconds: float = 2.0,
                      interval_seconds: float = 0.05):
    deadline = asyncio.get_running_loop().time() + timeout_seconds
    last_value = None
    while True:
        last_value = await fetch()
        if predicate(last_value):
            return last_value
        if asyncio.get_running_loop().time() >= deadline:
            raise RuntimeError(f"{label}: timeout waiting for expected state")
        await asyncio.sleep(interval_seconds)


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


async def _run_step2_modulation_flow(session: VividMCPSession, summary: dict) -> None:
    staged_modules = _stage_step2_module_fixture()
    graph_path = (REPO_ROOT / STEP2_GRAPH_FIXTURE).resolve()
    _expect(graph_path.exists(), f"step2 graph fixture missing: {graph_path}")

    ensure = _require_ok(
        await session.call_tool("ensure_runtime", {"graph_path": str(graph_path)}),
        "ensure_runtime(step2_modulation)",
    )
    print(
        "step2_modulation ensure_runtime: "
        f"launched={ensure.get('launched')} "
        f"reused_existing={ensure.get('reused_existing')} "
        f"graph_loaded={ensure.get('graph_loaded')}"
    )

    summary["step2_modulation"] = {
        "fixture_graph": str(graph_path),
        "staged_modules": staged_modules,
        "ensure_runtime": ensure,
    }

    async def fetch_graph_node() -> dict:
        inspect = _require_ok(await session.call_tool("inspect_graph"), "inspect_graph(step2_modulation)")
        return _node_from_graph(inspect, "step2_mod")

    async def fetch_intro_node() -> dict:
        intro = _require_ok(await session.call_tool("introspect_nodes"), "introspect_nodes(step2_modulation)")
        return _node_from_introspection(intro, "step2_mod")

    baseline_graph = await fetch_graph_node()
    baseline_intro = await fetch_intro_node()

    _expect("mod_sources" in baseline_graph, "baseline query mismatch: module mod_sources missing")
    _expect("mod_destinations" in baseline_graph, "baseline query mismatch: module mod_destinations missing")
    _expect("mod_assignments" not in baseline_graph, "baseline query mismatch: mod_assignments should start empty")
    _expect(_close_enough(_graph_param_value(baseline_graph, "level"), 0.4), "baseline query mismatch: module param level should start at 0.4")
    _expect(_close_enough(_graph_output_value(baseline_graph, "value"), 0.4), "baseline query mismatch: module output value should start at 0.4")
    _expect(baseline_intro.get("is_module") is True, "baseline query mismatch: introspect_nodes should mark module instance")
    _expect(_close_enough(_intro_param_value(baseline_intro, "level"), 0.4), "baseline query mismatch: introspection param level should start at 0.4")
    _expect(_close_enough(_intro_output_value(baseline_intro, "value"), 0.4), "baseline query mismatch: introspection output value should start at 0.4")

    sources_payload = _require_ok(await session.call_tool("list_mod_sources", {"node_id": "step2_mod"}), "list_mod_sources")
    dests_payload = _require_ok(await session.call_tool("list_mod_destinations", {"node_id": "step2_mod"}), "list_mod_destinations")
    sources = _result_list(sources_payload, "list_mod_sources")
    dests = _result_list(dests_payload, "list_mod_destinations")
    internal_src = _find_named_entry(sources, "internal_dc")
    external_src = _find_named_entry(sources, "external_mod")
    level_dest = _find_named_entry(dests, "level_dest")
    _expect(internal_src.get("shape") == "scalar", "mod source metadata mismatch: internal_dc shape")
    _expect(internal_src.get("polarity") == "unipolar", "mod source metadata mismatch: internal_dc polarity")
    _expect(external_src.get("kind") == "port", "mod source metadata mismatch: external_mod kind")
    _expect(level_dest.get("shape") == "scalar", "mod destination metadata mismatch: level_dest shape")

    summary["step2_modulation"]["baseline"] = {
        "graph_node": baseline_graph,
        "intro_node": baseline_intro,
        "sources": sources,
        "destinations": dests,
    }

    add_payload = _require_ok(
        await session.call_tool(
            "add_mod_assignment",
            {
                "node_id": "step2_mod",
                "source": "internal_dc",
                "destination": "level_dest",
                "amount": 0.2,
                "polarity": "unipolar",
                "curve": "linear",
            },
        ),
        "add_mod_assignment",
    )
    print(f"step2_modulation add_mod_assignment: {add_payload.get('message', 'ok')}")

    assignments_after_add = await _poll_until(
        "assignment CRUD mismatch after add",
        lambda: session.call_tool("list_mod_assignments", {"node_id": "step2_mod"}),
        lambda payload: _require_ok(payload, "list_mod_assignments(after add)") and len(_result_list(payload, "list_mod_assignments(after add)")) == 1,
    )
    assignments_list = _result_list(assignments_after_add, "list_mod_assignments(after add)")
    _expect(assignments_list[0].get("source") == "internal_dc", "assignment CRUD mismatch after add: wrong source")
    _expect(assignments_list[0].get("destination") == "level_dest", "assignment CRUD mismatch after add: wrong destination")
    _expect(_close_enough(float(assignments_list[0].get("amount", -1.0)), 0.2), "assignment CRUD mismatch after add: wrong amount")

    graph_after_add = await _poll_until(
        "module query-surface mismatch after add",
        fetch_graph_node,
        lambda node: "mod_assignments" in node and len(node["mod_assignments"]) == 1 and _close_enough(_graph_output_value(node, "value"), 0.6),
    )

    update_payload = _require_ok(
        await session.call_tool(
            "update_mod_assignment",
            {
                "node_id": "step2_mod",
                "source": "internal_dc",
                "destination": "level_dest",
                "amount": 0.35,
                "polarity": "bipolar",
                "curve": "linear",
            },
        ),
        "update_mod_assignment",
    )
    print(f"step2_modulation update_mod_assignment: {update_payload.get('message', 'ok')}")

    assignments_after_update = await _poll_until(
        "assignment CRUD mismatch after update",
        lambda: session.call_tool("list_mod_assignments", {"node_id": "step2_mod"}),
        lambda payload: _require_ok(payload, "list_mod_assignments(after update)") and (
            len(_result_list(payload, "list_mod_assignments(after update)")) == 1 and
            _close_enough(float(_result_list(payload, "list_mod_assignments(after update)")[0].get("amount", -1.0)), 0.35) and
            _result_list(payload, "list_mod_assignments(after update)")[0].get("polarity") == "bipolar"
        ),
    )

    graph_after_update = await _poll_until(
        "live modulation mismatch after update",
        fetch_graph_node,
        lambda node: _close_enough(_graph_output_value(node, "value"), 0.75),
    )

    _require_ok(
        await session.call_tool("set_param", {"node_id": "step2_mod", "param": "level", "value": 0.8}),
        "set_param(step2_mod/level)",
    )

    graph_after_set = await _poll_until(
        "live base-value mismatch after set_param",
        fetch_graph_node,
        lambda node: _close_enough(_graph_param_value(node, "level"), 0.8) and _close_enough(_graph_output_value(node, "value"), 1.15),
    )
    intro_after_set = await _poll_until(
        "introspection mismatch after set_param",
        fetch_intro_node,
        lambda node: _close_enough(_intro_param_value(node, "level"), 0.8) and _close_enough(_intro_output_value(node, "value"), 1.15),
    )

    remove_payload = _require_ok(
        await session.call_tool(
            "remove_mod_assignment",
            {
                "node_id": "step2_mod",
                "source": "internal_dc",
                "destination": "level_dest",
            },
        ),
        "remove_mod_assignment",
    )
    print(f"step2_modulation remove_mod_assignment: {remove_payload.get('message', 'ok')}")

    assignments_after_remove = await _poll_until(
        "assignment CRUD mismatch after remove",
        lambda: session.call_tool("list_mod_assignments", {"node_id": "step2_mod"}),
        lambda payload: _require_ok(payload, "list_mod_assignments(after remove)") and len(_result_list(payload, "list_mod_assignments(after remove)")) == 0,
    )
    graph_after_remove = await _poll_until(
        "module query-surface mismatch after remove",
        fetch_graph_node,
        lambda node: "mod_assignments" not in node and _close_enough(_graph_output_value(node, "value"), 0.8),
    )
    intro_after_remove = await _poll_until(
        "live output mismatch after remove",
        fetch_intro_node,
        lambda node: _close_enough(_intro_output_value(node, "value"), 0.8),
    )

    summary["step2_modulation"].update({
        "after_add": {
            "assignments": _result_list(assignments_after_add, "list_mod_assignments(after add)"),
            "graph_node": graph_after_add,
        },
        "after_update": {
            "assignments": _result_list(assignments_after_update, "list_mod_assignments(after update)"),
            "graph_node": graph_after_update,
        },
        "after_set_param": {
            "graph_node": graph_after_set,
            "intro_node": intro_after_set,
        },
        "after_remove": {
            "assignments": _result_list(assignments_after_remove, "list_mod_assignments(after remove)"),
            "graph_node": graph_after_remove,
            "intro_node": intro_after_remove,
        },
    })


async def _run(args: argparse.Namespace) -> int:
    artifact_dir = pathlib.Path(args.artifact_dir).expanduser()
    artifact_dir.mkdir(parents=True, exist_ok=True)

    capture_cases = _build_capture_cases(args, artifact_dir)
    sample_cases = _build_sample_cases(args)
    if not capture_cases and not sample_cases and args.preset not in {"dream_keys", "step2_modulation"}:
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
            if args.preset == "step2_modulation":
                await _run_step2_modulation_flow(session, summary)
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
        choices=["phase4", "dream_keys", "step2_modulation"],
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
