"""Vivid MCP Server — bridges MCP stdio to the Vivid runtime HTTP control server."""

import asyncio
import json
import os
import pathlib
import subprocess
import tempfile
import time
import httpx
from mcp.server.fastmcp import FastMCP

VIVID_URL = os.environ.get("VIVID_URL", "http://127.0.0.1:9876")
REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_VIVID_BIN = REPO_ROOT / "build" / "vivid"
DEFAULT_RUNTIME_LOG = pathlib.Path(tempfile.gettempdir()) / "vivid_mcp_runtime.log"
_RUNTIME_STARTUP_TIMEOUT_SEC = 20.0
_RUNTIME_STARTUP_POLL_SEC = 0.25
_managed_runtime_process: subprocess.Popen | None = None
_managed_runtime_log_path: str = ""

mcp = FastMCP("vivid", instructions="""Vivid is a real-time audio-visual graph engine. You build node graphs that generate and process visuals, audio, and control signals, all running live.

## Three Domains

- **GPU** — texture-based visual operators (noise, shape, blur, composite, etc.). Ports use type `gpu_texture` for 2D image data. Packages can define custom opaque-pointer port types using `data` with a `data_type` string (e.g. the vivid-3d package defines `"gpu_scene"` for 3D scene fragments). 3D operators (Shape3D, Transform3D, SceneMerge, Light3D, Render3D, etc.) are available via the vivid-3d package. Every visual graph needs a `video_out` node to display output.
- **Audio** — sample-based audio operators (oscillator, gain, reverb, etc.). Ports use type `audio_float`. Every audio graph needs an `audio_out` node to hear output.
- **Control** — scalar or lane-bearing signals for modulation (lfo, clock, math, sequencer, etc.). Ports use type `control_float`. Control outputs can also drive any numeric parameter directly.

## Port Compatibility

Connections must match types: `gpu_texture` → `gpu_texture`, `data` → `data` (with matching `data_type`), `audio_float` → `audio_float`, `control_float` → `control_float` or any numeric parameter. Address format for ports: `"node_id/port_name"`. Packages can define custom `data` port types (e.g. vivid-3d uses `data_type: "gpu_scene"` for 3D scene wires).

## Workflow

1. `ensure_runtime` — make sure a GUI Vivid runtime is running before using the rest of the tool surface
2. `list_types` — discover available operators (compact catalog). Use `operator_docs(name)` to get full param/port/doc details for a specific operator.
3. **Compose first** — build the graph from existing operators before considering custom ones. Most goals are achievable by wiring existing operators together.
4. `add_node` → `connect` → `set_param` — assemble and configure the graph
5. `scaffold_operator` — scaffold a starter template when no existing operator achieves the goal. Creates a minimal working operator; use the opdev MCP server for advanced features (custom ports, params, inspectors, thumbnails, and `prepare_instance_assets()` warmup guidance).
6. `inspect_graph` — verify the graph state, check live output values

## Analyzing an Existing Graph

When asked to examine, analyze, or debug an existing graph, **always call `get_graph_errors` first** before any other analysis. Errors and dropped connections are the most important information — report them prominently at the top of your response, not buried in a summary. A dropped connection means a wire the user drew is silently inactive, which is almost always the root cause of "signal not reaching downstream nodes" problems.

## Common Patterns

- Connect an `lfo` output → a GPU node's parameter for animation
- Connect `clock` → `sequencer` for rhythmic patterns
- Audio chains: oscillator → effects → `audio_out`
- Visual chains: generators → filters → `video_out`
- Control signals modulate both GPU and audio params

## Composite Operators

Control operators can embed other operators internally using ChildOp<T>. Use `scaffold_operator`
with `variant="composite"` to generate a template. Useful for internal modulation (e.g. LFO driving
a gain stage) without exposing child operators as graph nodes. Control env only.

## Custom Operators
If you need to create a custom operator, use `scaffold_operator` to generate the template.
For deeper operator development guidance (API docs, DSP utilities, GPU shader patterns),
the dedicated operator development MCP server provides comprehensive resources, including
when to use `prepare_instance_assets()` for expensive one-time CPU-side setup.
""")


async def _post(method: str, body: dict | None = None, timeout: float = 10.0) -> str:
    """POST to the Vivid control server and return the JSON response as text."""
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            f"{VIVID_URL}/{method}",
            json=body or {},
            timeout=timeout,
        )
        return resp.text


def _json_response(payload: dict) -> str:
    return json.dumps(payload, separators=(",", ":"), sort_keys=True)


def _clear_managed_runtime_if_exited() -> None:
    global _managed_runtime_process, _managed_runtime_log_path
    if _managed_runtime_process is not None and _managed_runtime_process.poll() is not None:
        _managed_runtime_process = None
        _managed_runtime_log_path = ""


def _resolve_vivid_bin() -> pathlib.Path:
    env_bin = os.environ.get("VIVID_BIN")
    if env_bin:
        candidate = pathlib.Path(env_bin).expanduser()
        if candidate.exists():
            return candidate.resolve()
    if DEFAULT_VIVID_BIN.exists():
        return DEFAULT_VIVID_BIN.resolve()
    raise FileNotFoundError(
        "no launchable Vivid runtime binary found; set VIVID_BIN or build ./build/vivid"
    )


def _resolve_graph_path(graph_path: str) -> str:
    if not graph_path:
        return ""
    candidate = pathlib.Path(graph_path).expanduser()
    search = []
    if candidate.is_absolute():
        search.append(candidate)
    else:
        search.append((REPO_ROOT / candidate).resolve())
        search.append((pathlib.Path.cwd() / candidate).resolve())
    for path in search:
        if path.exists():
            return str(path)
    raise FileNotFoundError(f"graph file not found: {graph_path}")


async def _runtime_is_reachable() -> bool:
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.post(f"{VIVID_URL}/list_nodes", json={}, timeout=1.0)
        payload = json.loads(resp.text)
        return bool(payload.get("ok", False))
    except Exception:  # connection refused, timeout, invalid JSON — all mean unreachable
        return False


async def _load_graph_path(graph_path: str) -> tuple[bool, str]:
    resolved_graph = _resolve_graph_path(graph_path)
    raw = await _post("load_graph", {"path": resolved_graph})
    try:
        payload = json.loads(raw)
    except (ValueError, TypeError):
        return False, raw
    return bool(payload.get("ok", False)), raw


def _launch_runtime_process(graph_path: str = "") -> tuple[subprocess.Popen, str]:
    vivid_bin = _resolve_vivid_bin()
    cmd = [str(vivid_bin)]
    if graph_path:
        cmd.append(graph_path)
    log_path = str(DEFAULT_RUNTIME_LOG)
    log_file = open(log_path, "w", encoding="utf-8")
    proc = subprocess.Popen(
        cmd,
        cwd=str(REPO_ROOT),
        stdout=log_file,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    return proc, log_path


async def _wait_for_runtime_ready(proc: subprocess.Popen | None, timeout_sec: float) -> bool:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        if proc is not None and proc.poll() is not None:
            return False
        if await _runtime_is_reachable():
            return True
        await asyncio.sleep(_RUNTIME_STARTUP_POLL_SEC)
    return False


def _runtime_status_payload(reachable: bool) -> dict:
    _clear_managed_runtime_if_exited()
    managed = _managed_runtime_process is not None
    return {
        "ok": True,
        "url": VIVID_URL,
        "reachable": reachable,
        "bridge_managed": managed,
        "pid": _managed_runtime_process.pid if managed else None,
        "log_path": _managed_runtime_log_path if managed else "",
        "status": (
            "bridge_managed_running" if managed and reachable else
            "external_running" if reachable else
            "not_running"
        ),
    }


def _compact_envelope(raw: str) -> dict:
    """Create a compact, deterministic envelope for MCP-facing perception tools."""
    try:
        payload = json.loads(raw)
    except (ValueError, TypeError):
        return {
            "ok": False,
            "schema_version": 1,
            "error": {"code": "invalid_json", "message": "control server returned non-JSON payload"},
        }

    ok = bool(payload.get("ok", False))
    schema_version = int(payload.get("schema_version", 1))
    if not ok:
        err = payload.get("error")
        if isinstance(err, dict):
            return {"ok": False, "schema_version": schema_version, "error": err}
        if isinstance(err, str):
            return {
                "ok": False,
                "schema_version": schema_version,
                "error": {"code": "runtime_error", "message": err},
            }
        return {
            "ok": False,
            "schema_version": schema_version,
            "error": {"code": "runtime_error", "message": "unknown error"},
        }
    return {"ok": True, "schema_version": schema_version, "result": payload.get("result", {})}


def _perception_response(raw: str, kind: str, include_payload: bool = False) -> str:
    env = _compact_envelope(raw)
    if not env.get("ok", False):
        out = env
        if include_payload:
            out["payload"] = raw
        return json.dumps(out, separators=(",", ":"), sort_keys=True)

    result = env.get("result", {})
    summary: dict = {}
    if kind == "introspect_nodes":
        nodes = result.get("nodes", []) if isinstance(result, dict) else []
        if not isinstance(nodes, list):
            nodes = []
        envs = {"audio": 0, "gpu": 0, "control": 0}
        errored = 0
        for n in nodes:
            if not isinstance(n, dict):
                continue
            d = n.get("env")
            if d in envs:
                envs[d] += 1
            health = n.get("health")
            if isinstance(health, dict) and health.get("errored", False):
                errored += 1
        summary = {
            "kind": kind,
            "node_count": len(nodes),
            "errored_nodes": errored,
            "envs": envs,
        }
    elif kind == "run_diagnostics":
        diag_summary = result.get("summary", {}) if isinstance(result, dict) else {}
        hints = result.get("hints", []) if isinstance(result, dict) else []
        top_hint_ids: list[str] = []
        if isinstance(hints, list):
            for h in hints[:3]:
                if isinstance(h, dict) and isinstance(h.get("id"), str):
                    top_hint_ids.append(h["id"])
        summary = {
            "kind": kind,
            "critical": int(diag_summary.get("critical", 0)) if isinstance(diag_summary, dict) else 0,
            "warning": int(diag_summary.get("warning", 0)) if isinstance(diag_summary, dict) else 0,
            "info": int(diag_summary.get("info", 0)) if isinstance(diag_summary, dict) else 0,
            "top_hint_ids": top_hint_ids,
        }
    elif kind == "validate_checks":
        summary = {
            "kind": kind,
            "valid": bool(result.get("valid", False)) if isinstance(result, dict) else False,
            "error_count": int(result.get("error_count", 0)) if isinstance(result, dict) else 0,
        }
    elif kind == "run_checks":
        csum = result.get("summary", {}) if isinstance(result, dict) else {}
        summary = {
            "kind": kind,
            "all_passed": bool(result.get("all_passed", False)) if isinstance(result, dict) else False,
            "all_critical_passed": bool(result.get("all_critical_passed", False)) if isinstance(result, dict) else False,
            "passed": int(csum.get("passed", 0)) if isinstance(csum, dict) else 0,
            "failed": int(csum.get("failed", 0)) if isinstance(csum, dict) else 0,
            "skipped": int(csum.get("skipped", 0)) if isinstance(csum, dict) else 0,
            "critical_failed": int(csum.get("critical_failed", 0)) if isinstance(csum, dict) else 0,
        }
    else:
        summary = {"kind": kind}

    out = {"ok": True, "schema_version": env.get("schema_version", 1), "summary": summary}
    if include_payload:
        out["result"] = result
    return json.dumps(out, separators=(",", ":"), sort_keys=True)


@mcp.tool()
async def introspect_nodes(include_payload: bool = False) -> str:
    """Get per-node introspection with compact summary. Set include_payload=true to include full result."""
    raw = await _post("introspect_nodes")
    return _perception_response(raw, "introspect_nodes", include_payload)


@mcp.tool()
async def run_diagnostics(include_payload: bool = False) -> str:
    """Run graph-level diagnostics and return severity summary + top hints. Set include_payload=true for full findings."""
    raw = await _post("run_diagnostics")
    return _perception_response(raw, "run_diagnostics", include_payload)


@mcp.tool()
async def operator_map() -> str:
    """Show every operator the runtime knows about: dylib path, package, status (loaded/deferred/abi_mismatch), and ABI version. Use this to debug operator loading issues."""
    raw = await _post("operator_map")
    return _json_response(raw)


@mcp.tool()
async def discovery_report() -> str:
    """Show the package discovery report: scopes searched, packages loaded, packages skipped (with reasons). Use this to debug missing operators or package loading issues."""
    raw = await _post("get_discovery_report")
    return _json_response(raw)


@mcp.tool()
async def validate_checks(checks: list[dict], include_payload: bool = False) -> str:
    """Validate check definitions before execution."""
    raw = await _post("validate_checks", {"checks": checks})
    return _perception_response(raw, "validate_checks", include_payload)


@mcp.tool()
async def run_checks(checks: list[dict], include_payload: bool = False) -> str:
    """Evaluate checks against the current introspection/diagnostics snapshot."""
    raw = await _post("run_checks", {"checks": checks})
    return _perception_response(raw, "run_checks", include_payload)


@mcp.tool()
async def analyze_output(mode: str = "frame", window_seconds: float = 1.0,
                         include_payload: bool = False, node_id: str = "") -> str:
    """Analyze the current runtime output.

    Args:
        mode: "frame", "audio", or "av"
        window_seconds: Analysis window for audio/av modes
        include_payload: Include heavyweight capture payloads when available
        node_id: Optional node id to scope analysis to a specific output source
    """
    body = {
        "mode": mode,
        "window_seconds": window_seconds,
        "include_payload": include_payload,
    }
    if node_id:
        body["node_id"] = node_id
    return await _post("analyze_output", body)


@mcp.tool()
async def compare_outputs(mode: str = "frame",
                          window_seconds_a: float = 1.0,
                          window_seconds_b: float = 1.0,
                          include_payload: bool = False,
                          node_id: str = "") -> str:
    """Capture and compare two runtime output windows.

    Args:
        mode: "frame", "audio", or "av"
        window_seconds_a: Analysis window for capture A
        window_seconds_b: Analysis window for capture B
        include_payload: Include heavyweight capture payloads when available
        node_id: Optional node id to scope analysis to a specific output source
    """
    body = {
        "mode": mode,
        "include_payload": include_payload,
        "a": {"window_seconds": window_seconds_a},
        "b": {"window_seconds": window_seconds_b},
    }
    if node_id:
        body["node_id"] = node_id
    return await _post("compare_outputs", body)


@mcp.tool()
async def capture_interface(node_id: str = "",
                            save_path: str = "",
                            ensure_ui_visible: bool = True) -> str:
    """Capture the full running Vivid interface from the live runtime instance.

    Args:
        node_id: Optional node id to select before capture so the inspector is visible
        save_path: Optional absolute PNG path to also write on the runtime machine
        ensure_ui_visible: Force the graph UI visible before capture
    """
    body = {
        "ensure_ui_visible": ensure_ui_visible,
    }
    if node_id:
        body["node_id"] = node_id
    if save_path:
        body["save_path"] = save_path
    return await _post("capture_interface", body)


@mcp.tool()
async def capture_image(mode: str = "interface",
                        node_id: str = "",
                        save_path: str = "",
                        ensure_ui_visible: bool = True) -> str:
    """Capture an image from the running Vivid instance.

    Args:
        mode: "interface" for full-window UI capture, or "output" for output-only frame capture
        node_id: Optional node id to select before interface capture
        save_path: Optional absolute PNG path to also write on the runtime machine for interface capture
        ensure_ui_visible: Force the graph UI visible before interface capture
    """
    if mode == "interface":
        return await capture_interface(node_id, save_path, ensure_ui_visible)
    if mode == "output":
        return await _post("capture_frame", {})
    raise ValueError("mode must be 'interface' or 'output'")


@mcp.tool()
async def sample_node_outputs(node_id: str,
                              duration_seconds: float = 8.0,
                              interval_ms: int = 250,
                              include_lanes: bool = True) -> str:
    """Sample one live node repeatedly over time."""
    body = {
        "node_id": node_id,
        "duration_seconds": duration_seconds,
        "interval_ms": interval_ms,
        "include_lanes": include_lanes,
    }
    timeout = max(10.0, float(duration_seconds) + 5.0)
    return await _post("sample_node_outputs", body, timeout=timeout)


@mcp.tool()
async def runtime_status() -> str:
    """Report whether a Vivid runtime is reachable and whether it is bridge-managed."""
    reachable = await _runtime_is_reachable()
    return _json_response(_runtime_status_payload(reachable))


@mcp.tool()
async def ensure_runtime(graph_path: str = "") -> str:
    """Ensure a GUI Vivid runtime is running, optionally with a graph loaded."""
    global _managed_runtime_process, _managed_runtime_log_path

    _clear_managed_runtime_if_exited()
    resolved_graph = ""
    if graph_path:
        try:
            resolved_graph = _resolve_graph_path(graph_path)
        except Exception as exc:
            return _json_response({
                "ok": False,
                "url": VIVID_URL,
                "error": str(exc),
            })

    reachable = await _runtime_is_reachable()
    if reachable:
        graph_loaded = False
        graph_result = ""
        if resolved_graph:
            graph_loaded, graph_result = await _load_graph_path(resolved_graph)
            if not graph_loaded:
                return _json_response({
                    "ok": False,
                    "url": VIVID_URL,
                    "launched": False,
                    "reused_existing": True,
                    "graph_loaded": False,
                    "graph_path": resolved_graph,
                    "error": "failed to load graph into existing runtime",
                    "runtime_response": graph_result,
                    "pid": _managed_runtime_process.pid if _managed_runtime_process else None,
                    "log_path": _managed_runtime_log_path if _managed_runtime_process else "",
                })
        return _json_response({
            "ok": True,
            "url": VIVID_URL,
            "launched": False,
            "reused_existing": True,
            "graph_loaded": graph_loaded,
            "graph_path": resolved_graph,
            "pid": _managed_runtime_process.pid if _managed_runtime_process else None,
            "log_path": _managed_runtime_log_path if _managed_runtime_process else "",
        })

    try:
        proc, log_path = _launch_runtime_process(resolved_graph)
    except Exception as exc:
        return _json_response({
            "ok": False,
            "url": VIVID_URL,
            "error": str(exc),
        })

    _managed_runtime_process = proc
    _managed_runtime_log_path = log_path
    ready = await _wait_for_runtime_ready(proc, _RUNTIME_STARTUP_TIMEOUT_SEC)
    if not ready:
        _clear_managed_runtime_if_exited()
        return _json_response({
            "ok": False,
            "url": VIVID_URL,
            "launched": True,
            "reused_existing": False,
            "graph_loaded": False,
            "graph_path": resolved_graph,
            "pid": proc.pid,
            "log_path": log_path,
            "error": "runtime failed to become reachable before timeout",
        })

    return _json_response({
        "ok": True,
        "url": VIVID_URL,
        "launched": True,
        "reused_existing": False,
        "graph_loaded": bool(resolved_graph),
        "graph_path": resolved_graph,
        "pid": proc.pid,
        "log_path": log_path,
    })


@mcp.tool()
async def stop_runtime() -> str:
    """Stop the bridge-managed Vivid runtime, if one exists."""
    global _managed_runtime_process, _managed_runtime_log_path
    _clear_managed_runtime_if_exited()
    if _managed_runtime_process is None:
        reachable = await _runtime_is_reachable()
        return _json_response({
            "ok": True,
            "stopped": False,
            "bridge_managed": False,
            "reachable": reachable,
            "url": VIVID_URL,
            "status": "not_bridge_managed",
        })

    proc = _managed_runtime_process
    log_path = _managed_runtime_log_path
    try:
        proc.terminate()
        try:
            proc.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5.0)
    finally:
        _managed_runtime_process = None
        _managed_runtime_log_path = ""

    return _json_response({
        "ok": True,
        "stopped": True,
        "bridge_managed": True,
        "url": VIVID_URL,
        "pid": proc.pid,
        "log_path": log_path,
        "status": "stopped",
    })


@mcp.tool()
async def inspect_graph() -> str:
    """Get the full graph state: nodes with params (live values + schema metadata including semantic_tag/shape/unit/intent), input/output ports (with current output values), and connections."""
    return await _post("inspect_graph")


@mcp.tool()
async def list_types(domain: str = "") -> str:
    """List all available operator types (compact catalog: name, kind, brief, lane_behavior). Use operator_docs(name) to get full details (params, ports, docs) for a specific operator.

    Args:
        domain: Optional filter — "gpu", "audio", or "control". Omit to list all domains.
    """
    body: dict = {}
    if domain:
        body["domain"] = domain
    return await _post("list_types", body or None)


@mcp.tool()
async def add_node(type: str, id: str) -> str:
    """Add a new node to the graph.

    Args:
        type: Operator type name (e.g. "lfo", "shape", "oscillator")
        id: Unique node identifier
    """
    return await _post("add_node", {"type": type, "node_id": id})


@mcp.tool()
async def remove_node(node_id: str) -> str:
    """Remove a node and all its connections from the graph.

    Args:
        node_id: The node to remove
    """
    return await _post("remove_node", {"node_id": node_id})


@mcp.tool()
async def connect(from_addr: str, to_addr: str, semantic_defaults: bool = True) -> str:
    """Connect two ports. Address format is "node_id/port_name".

    Args:
        from_addr: Source port (e.g. "lfo1/value")
        to_addr: Destination port (e.g. "shape1/rotation")
        semantic_defaults: Apply semantic-tag-based default remap when possible (default true for MCP workflows)
    """
    return await _post("connect", {
        "from_addr": from_addr,
        "to_addr": to_addr,
        "semantic_defaults": semantic_defaults,
    })


@mcp.tool()
async def disconnect(from_addr: str, to_addr: str) -> str:
    """Disconnect two ports. Address format is "node_id/port_name".

    Args:
        from_addr: Source port (e.g. "lfo1/value")
        to_addr: Destination port (e.g. "shape1/rotation")
    """
    return await _post("disconnect", {"from_addr": from_addr, "to_addr": to_addr})


@mcp.tool()
async def set_connection_remap(
    from_addr: str, to_addr: str,
    from_min: float = 0.0, from_max: float = 1.0,
    to_min: float = 0.0, to_max: float = 1.0,
    clamp: bool = False,
) -> str:
    """Set the remap on a connection. Values are mapped from [from_min, from_max] to [to_min, to_max].

    Args:
        from_addr: Source port (e.g. "lfo1/value")
        to_addr: Destination port (e.g. "blur1/radius")
        from_min: Input range minimum (default 0.0)
        from_max: Input range maximum (default 1.0)
        to_min: Output range minimum (default 0.0)
        to_max: Output range maximum (default 1.0)
        clamp: Whether to clamp output to [min(to_min,to_max), max(to_min,to_max)]
    """
    return await _post("set_connection_remap", {
        "from_addr": from_addr, "to_addr": to_addr,
        "from_min": from_min, "from_max": from_max,
        "to_min": to_min, "to_max": to_max, "clamp": clamp,
    })


@mcp.tool()
async def set_param(node_id: str, param: str, value: float) -> str:
    """Set a parameter value on a node. Takes effect immediately.

    Args:
        node_id: Target node
        param: Parameter name
        value: New value (float)
    """
    return await _post("set_param", {"node_id": node_id, "param": param, "value": value})


@mcp.tool()
async def get_param(node_id: str, param: str) -> str:
    """Get a parameter's current value.

    Args:
        node_id: Target node
        param: Parameter name
    """
    return await _post("get_param", {"node_id": node_id, "param": param})


@mcp.tool()
async def save_graph(path: str | None = None) -> str:
    """Save the graph to disk.

    Args:
        path: File path to save to. If omitted, saves to the original graph file.
    """
    body = {"path": path} if path else {}
    return await _post("save_graph", body)


@mcp.tool()
async def load_graph(path: str) -> str:
    """Load a graph from disk, rebuilding the scheduler."""
    resolved_path = _resolve_graph_path(path)
    return await _post("load_graph", {"path": resolved_path})


@mcp.tool()
async def new_graph() -> str:
    """Reset to a new empty graph with default audio_out and video_out sink nodes.
    Clears all nodes, connections, variations, and undo history."""
    return await _post("new_graph")


@mcp.tool()
async def undo() -> str:
    """Undo the last graph mutation made through MCP/control-server commands."""
    return await _post("undo")


@mcp.tool()
async def redo() -> str:
    """Redo the last undone graph mutation made through MCP/control-server commands."""
    return await _post("redo")


@mcp.tool()
async def set_string_param(node_id: str, param: str, value: str) -> str:
    """Set a string parameter (e.g. file path) on a node.

    Args:
        node_id: Target node
        param: Parameter name
        value: String value (e.g. a file path)
    """
    return await _post("set_string_param", {"node_id": node_id, "param": param, "value": value})


@mcp.tool()
async def set_resolution(node_id: str, width: int, height: int) -> str:
    """Set GPU texture resolution for a node. Use 0,0 to reset to default.

    Args:
        node_id: Target node
        width: Texture width in pixels
        height: Texture height in pixels
    """
    return await _post("set_resolution", {"node_id": node_id, "width": width, "height": height})


@mcp.tool()
async def set_node_layout(node_id: str, x: float, y: float) -> str:
    """Set the visual position of a node in the graph editor.

    Args:
        node_id: Target node
        x: X position
        y: Y position
    """
    return await _post("set_node_layout", {"node_id": node_id, "x": x, "y": y})


@mcp.tool()
async def inspect_node(node_id: str) -> str:
    """Inspect a single node: params with live values, input/output port values.

    Args:
        node_id: The node to inspect
    """
    return await _post("inspect", {"node_id": node_id})


@mcp.tool()
async def list_nodes() -> str:
    """List all nodes in the graph (lightweight: just id and type)."""
    return await _post("list_nodes")


@mcp.tool()
async def add_midi_mapping(node_id: str, param: str, cc: int, channel: int,
                           range_min: float, range_max: float) -> str:
    """Map a MIDI CC to a node parameter.

    Args:
        node_id: Target node
        param: Parameter name to control
        cc: MIDI CC number (0-127)
        channel: MIDI channel (0-15)
        range_min: Parameter value when CC is 0
        range_max: Parameter value when CC is 127
    """
    return await _post("add_midi_mapping", {
        "node_id": node_id, "param": param,
        "cc": cc, "channel": channel,
        "range_min": range_min, "range_max": range_max,
    })


@mcp.tool()
async def remove_midi_mapping(node_id: str, param: str) -> str:
    """Remove a MIDI CC mapping from a node parameter.

    Args:
        node_id: Target node
        param: Parameter name to unmap
    """
    return await _post("remove_midi_mapping", {"node_id": node_id, "param": param})


@mcp.tool()
async def update_midi_mapping(node_id: str, param: str,
                              range_min: float, range_max: float) -> str:
    """Update the range of an existing MIDI mapping.

    Args:
        node_id: Target node
        param: Parameter name
        range_min: New minimum value
        range_max: New maximum value
    """
    return await _post("update_midi_mapping", {
        "node_id": node_id, "param": param,
        "range_min": range_min, "range_max": range_max,
    })


@mcp.tool()
async def add_mod_assignment(node_id: str, source: str, destination: str,
                              amount: float, polarity: str = "unipolar",
                              curve: str = "linear") -> str:
    """Create a modulation assignment on a module instance.

    Assigns a named modulation source to a named destination with a given
    amount. The assignment is lowered into ordinary graph routing at compile
    time (additive: base_value + source * amount).

    Args:
        node_id: Module instance node ID
        source: Name of a mod_source declared by the module
        destination: Name of a mod_destination declared by the module
        amount: Modulation depth (scaled by source signal range)
        polarity: "unipolar" (0..amount) or "bipolar" (-amount..+amount)
        curve: "linear" (v1 only supports linear)
    """
    return await _post("add_mod_assignment", {
        "node_id": node_id, "source": source, "destination": destination,
        "amount": amount, "polarity": polarity, "curve": curve,
    })


@mcp.tool()
async def remove_mod_assignment(node_id: str, source: str, destination: str) -> str:
    """Remove a modulation assignment from a module instance.

    Args:
        node_id: Module instance node ID
        source: Name of the mod_source in the assignment
        destination: Name of the mod_destination in the assignment
    """
    return await _post("remove_mod_assignment", {
        "node_id": node_id, "source": source, "destination": destination,
    })


@mcp.tool()
async def update_mod_assignment(node_id: str, source: str, destination: str,
                                 amount: float, polarity: str = "unipolar",
                                 curve: str = "linear") -> str:
    """Update the amount, polarity, or curve of an existing modulation assignment.

    Args:
        node_id: Module instance node ID
        source: Name of the mod_source in the assignment
        destination: Name of the mod_destination in the assignment
        amount: New modulation depth
        polarity: "unipolar" or "bipolar"
        curve: "linear" (v1 only supports linear)
    """
    return await _post("update_mod_assignment", {
        "node_id": node_id, "source": source, "destination": destination,
        "amount": amount, "polarity": polarity, "curve": curve,
    })


@mcp.tool()
async def list_mod_sources(node_id: str) -> str:
    """List the modulation sources declared by a module.

    Returns the named sources available for modulation assignments on
    this module instance (e.g. LFOs, envelopes, velocity).

    Args:
        node_id: Module instance node ID
    """
    return await _post("list_mod_sources", {"node_id": node_id})


@mcp.tool()
async def list_mod_destinations(node_id: str) -> str:
    """List the modulation destinations declared by a module.

    Returns the named destinations that can receive modulation on
    this module instance (e.g. filter_cutoff, wt_position).

    Args:
        node_id: Module instance node ID
    """
    return await _post("list_mod_destinations", {"node_id": node_id})


@mcp.tool()
async def list_mod_assignments(node_id: str) -> str:
    """List active modulation assignments on a module instance.

    Returns the current source→destination assignments with their
    amount, polarity, and curve settings.

    Args:
        node_id: Module instance node ID
    """
    return await _post("list_mod_assignments", {"node_id": node_id})


@mcp.tool()
async def add_sticky_note(text: str, x: float, y: float,
                          width: float = 200.0, height: float = 120.0,
                          color: int = 0, id: str = "") -> str:
    """Add a sticky note annotation to the graph canvas.

    Args:
        text: Note text (supports **bold**, - lists, [text](url))
        x: Graph-space X position
        y: Graph-space Y position
        width: Note width (default 200)
        height: Note height (default 120)
        color: Color index (0=yellow, 1=green, 2=blue, 3=pink, 4=orange)
        id: Optional note ID (auto-generated if empty)
    """
    params: dict = {"text": text, "x": x, "y": y, "width": width, "height": height, "color": color}
    if id:
        params["id"] = id
    return await _post("add_sticky_note", params)


@mcp.tool()
async def list_sticky_notes() -> str:
    """List all sticky notes on the graph canvas."""
    return await _post("list_sticky_notes")


@mcp.tool()
async def update_sticky_note(id: str, text: str = None, x: float = None, y: float = None,
                              width: float = None, height: float = None,
                              color: int = None) -> str:
    """Update an existing sticky note.

    Args:
        id: Note ID (required)
        text: New text (optional)
        x: New X position (optional)
        y: New Y position (optional)
        width: New width (optional)
        height: New height (optional)
        color: New color index (optional)
    """
    params: dict = {"id": id}
    if text is not None:
        params["text"] = text
    if x is not None:
        params["x"] = x
    if y is not None:
        params["y"] = y
    if width is not None:
        params["width"] = width
    if height is not None:
        params["height"] = height
    if color is not None:
        params["color"] = color
    return await _post("update_sticky_note", params)


@mcp.tool()
async def remove_sticky_note(id: str) -> str:
    """Remove a sticky note from the graph canvas.

    Args:
        id: Note ID to remove
    """
    return await _post("remove_sticky_note", {"id": id})


@mcp.tool()
async def get_graph_errors() -> str:
    """Get all graph errors: nodes in error state AND dropped connections (wires the compiler rejected).

    **Call this first when analyzing or debugging a graph.** Dropped connections are silent failures —
    the wire exists in graph truth but carries no signal. Always report errors and dropped connections
    prominently before other analysis."""
    return await _post("get_graph_errors")


@mcp.tool()
async def scaffold_operator(name: str, env: str, variant: str = "") -> str:
    """Scaffold a starter operator template. Only use after confirming via list_types that no
    existing operator (seed or installed package) achieves the goal, alone or in combination.

    Creates a minimal working operator with env-appropriate defaults. For advanced features
    (custom ports, typed parameters, inspectors), use the opdev MCP server tools.

    Design the operator for reuse: generic name, broadly useful params, clear single responsibility.

    Writes source, patches CMakeLists, triggers build.

    Args:
        name: Operator name in lowercase_with_underscores (e.g. "tone_gen")
        env: One of "control", "audio", "gpu"
        variant: Template variant. Use "composite" for a ChildOp-based control operator with internal LFO + Smooth.
    """
    body: dict = {"name": name, "env": env}
    if variant:
        body["variant"] = variant
    return await _post("scaffold_operator", body)


@mcp.tool()
async def save_variation(name: str) -> str:
    """Save a named snapshot of all current parameter values across every node.

    Args:
        name: Name for the variation (e.g. "Intro", "Drop")
    """
    return await _post("save_variation", {"name": name})


@mcp.tool()
async def recall_variation(name: str) -> str:
    """Instantly recall a saved variation, restoring all parameter values.

    Args:
        name: Name of the variation to recall
    """
    return await _post("recall_variation", {"name": name})


@mcp.tool()
async def remove_variation(name: str) -> str:
    """Delete a saved variation.

    Args:
        name: Name of the variation to remove
    """
    return await _post("remove_variation", {"name": name})


@mcp.tool()
async def rename_variation(old_name: str, new_name: str) -> str:
    """Rename a saved variation.

    Args:
        old_name: Current name
        new_name: New name
    """
    return await _post("rename_variation", {"old_name": old_name, "new_name": new_name})


@mcp.tool()
async def update_variation(name: str) -> str:
    """Overwrite a variation with the current parameter values (re-save in place).

    Args:
        name: Name of the variation to update
    """
    return await _post("update_variation", {"name": name})


@mcp.tool()
async def list_variations() -> str:
    """List all saved variations. Active variation is marked with *."""
    return await _post("list_variations")


@mcp.tool()
async def queue_variation(name: str, quantize: str = "instant") -> str:
    """Queue a variation switch, optionally quantized to a beat boundary.

    Args:
        name: Name of the variation to switch to
        quantize: Timing — "instant", "beat", "bar", or "4bar"
    """
    return await _post("queue_variation", {"name": name, "quantize": quantize})


@mcp.tool()
async def set_quantize_clock(node_id: str) -> str:
    """Designate a Clock node for beat-synced variation switching.

    Args:
        node_id: ID of a Clock node whose beat_phase output drives quantization
    """
    return await _post("set_quantize_clock", {"node_id": node_id})


@mcp.tool()
async def set_analysis(enabled: bool) -> str:
    """Enable or disable GPU and audio analysis metrics (frame_hash, brightness, contrast, dominant_hue, rms, peak, waveform).

    When enabled, GPU operators compute frame metrics via tiny texture readback and
    audio operators compute RMS/peak/waveform. When disabled, analysis ports remain
    but read as 0 with no overhead. State is reflected in the title bar.

    Args:
        enabled: True to enable analysis, False to disable
    """
    return await _post("set_analysis", {"enabled": enabled})


@mcp.tool()
async def save_preset(node_id: str, name: str) -> str:
    """Save a named preset of the current parameter values for a single operator instance.

    Args:
        node_id: The node to snapshot
        name: Name for the preset (e.g. "intro_chords")
    """
    return await _post("save_preset", {"node_id": node_id, "name": name})


@mcp.tool()
async def recall_preset(node_id: str, name: str) -> str:
    """Recall a saved preset, restoring that operator's parameter values.

    Args:
        node_id: The node to restore
        name: Name of the preset to recall
    """
    return await _post("recall_preset", {"node_id": node_id, "name": name})


@mcp.tool()
async def update_preset(node_id: str, name: str) -> str:
    """Overwrite an existing preset with the operator's current parameter values.

    Args:
        node_id: The node whose current params to save
        name: Name of the preset to update
    """
    return await _post("update_preset", {"node_id": node_id, "name": name})


@mcp.tool()
async def remove_preset(node_id: str, name: str) -> str:
    """Delete a saved preset from an operator.

    Args:
        node_id: The node owning the preset
        name: Name of the preset to remove
    """
    return await _post("remove_preset", {"node_id": node_id, "name": name})


@mcp.tool()
async def rename_preset(node_id: str, old_name: str, new_name: str) -> str:
    """Rename a saved preset on an operator.

    Args:
        node_id: The node owning the preset
        old_name: Current preset name
        new_name: New preset name
    """
    return await _post("rename_preset", {"node_id": node_id, "old_name": old_name, "new_name": new_name})


@mcp.tool()
async def list_presets(node_id: str) -> str:
    """List all saved presets for an operator instance.

    Args:
        node_id: The node to list presets for
    """
    return await _post("list_presets", {"node_id": node_id})


@mcp.tool()
async def set_state_preset(sm_node: str, state_idx: int, target_node: str, preset_name: str) -> str:
    """Bind a preset to a state machine state. When the state machine enters this state, the preset is recalled on the target node.

    Args:
        sm_node: ID of the StateMachine node
        state_idx: State index (0-7)
        target_node: ID of the node whose preset to recall
        preset_name: Name of the preset to recall
    """
    return await _post("set_state_preset", {
        "sm_node": sm_node, "state_idx": state_idx,
        "target_node": target_node, "preset_name": preset_name
    })


@mcp.tool()
async def remove_state_preset(sm_node: str, state_idx: int, target_node: str) -> str:
    """Remove a preset binding from a state machine state.

    Args:
        sm_node: ID of the StateMachine node
        state_idx: State index (0-7)
        target_node: ID of the target node to unbind
    """
    return await _post("remove_state_preset", {
        "sm_node": sm_node, "state_idx": state_idx, "target_node": target_node
    })


@mcp.tool()
async def clear_state_presets(sm_node: str) -> str:
    """Remove all preset bindings from a state machine.

    Args:
        sm_node: ID of the StateMachine node
    """
    return await _post("clear_state_presets", {"sm_node": sm_node})


@mcp.tool()
async def inspect_state_presets(sm_node: str) -> str:
    """Show all preset bindings for a state machine, organized by state index.

    Args:
        sm_node: ID of the StateMachine node
    """
    return await _post("inspect_state_presets", {"sm_node": sm_node})


@mcp.tool()
async def browse_packages() -> str:
    """Browse the package catalog. Returns available packages with install status, category, and tags."""
    return await _post("package_catalog")


@mcp.tool()
async def install_package(url: str) -> str:
    """Install an operator package from a git URL or local path.

    Args:
        url: Git URL (e.g. "https://github.com/user/vivid-drums") or local directory path
    """
    return await _post("install_package", {"url": url})


@mcp.tool()
async def uninstall_package(name: str) -> str:
    """Uninstall an operator package by name.

    Args:
        name: Package name (e.g. "vivid-drums")
    """
    return await _post("uninstall_package", {"name": name})


@mcp.tool()
async def link_package(path: str) -> str:
    """Link a local package directory for development. Creates a symlink instead
    of copying, so edits to the source are picked up on rebuild without reinstalling.

    Args:
        path: Absolute or relative path to the package directory (must contain vivid-package.json)
    """
    return await _post("link_package", {"path": path})


@mcp.tool()
async def unlink_package(name: str) -> str:
    """Unlink a development package. Removes the symlink but does not touch the source directory.

    Args:
        name: Package name (e.g. "vivid-glitch")
    """
    return await _post("unlink_package", {"name": name})


@mcp.tool()
async def rebuild_package(name: str) -> str:
    """Recompile operators for an installed or linked package. Use after editing
    source files in a linked package.

    Args:
        name: Package name (e.g. "vivid-glitch")
    """
    return await _post("rebuild_package", {"name": name})


@mcp.tool()
async def list_packages() -> str:
    """List installed operator packages with their operators."""
    return await _post("list_packages")


@mcp.tool()
async def check_package_updates(core_version: str = "0.1.0", include_all_installed: bool = False) -> str:
    """Check installed packages for available updates and vivid_core compatibility.

    Args:
        core_version: Core version string used for compatibility checks (default "0.1.0")
        include_all_installed: If true, include installed packages even when no update is available
    """
    return await _post("check_package_updates", {
        "core_version": core_version,
        "include_all_installed": include_all_installed,
    })


@mcp.tool()
async def check_core_updates(force_refresh: bool = False) -> str:
    """Check for Vivid core application updates from the stable appcast.

    Args:
        force_refresh: If true, bypass cached state and fetch immediately
    """
    return await _post("check_core_updates", {
        "force_refresh": force_refresh,
    })


@mcp.tool()
async def read_package_docs(name: str) -> str:
    """Read the README documentation for an installed package."""
    return await _post("read_package_docs", {"name": name})


@mcp.tool()
async def list_package_examples(name: str) -> str:
    """List example graphs included with an installed package."""
    return await _post("list_package_examples", {"name": name})


@mcp.tool()
async def read_package_example(name: str, filename: str) -> str:
    """Read the content of an example graph from an installed package."""
    return await _post("read_package_example", {"name": name, "filename": filename})


@mcp.tool()
async def operator_docs(name: str, package: str = "") -> str:
    """Get merged operator docs from source comments plus runtime metadata for one operator. Set package for installed package operators when needed."""
    body = {"name": name}
    if package:
        body["package"] = package
    return await _post("operator_docs", body)


@mcp.tool()
async def package_operator_docs(name: str) -> str:
    """Get source-comment-derived operator docs plus runtime metadata for every operator in an installed package."""
    return await _post("package_operator_docs", {"name": name})


@mcp.tool()
async def test_package(name: str) -> str:
    """Run tests for an installed package (graph + C++ tests). Returns per-test pass/fail/skip."""
    async with httpx.AsyncClient() as client:
        resp = await client.post(f"{VIVID_URL}/test_package",
                                  json={"name": name}, timeout=90.0)
        return resp.text


def _start_heartbeat() -> None:
    """Start a daemon thread that pings /mcp_ping every 15 s."""
    import threading
    import time

    def _loop():
        while True:
            try:
                import urllib.request
                data = b'{"server":"vivid"}'
                req = urllib.request.Request(
                    f"{VIVID_URL}/mcp_ping",
                    data=data,
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                urllib.request.urlopen(req, timeout=2)
            except Exception:  # heartbeat is best-effort; runtime may not be up yet
                pass
            time.sleep(15)

    t = threading.Thread(target=_loop, daemon=True)
    t.start()


if __name__ == "__main__":
    _start_heartbeat()
    mcp.run()
