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
MUSIC_EVAL_URL = os.environ.get("VIVID_MUSIC_EVAL_URL", "http://127.0.0.1:9877")
REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_VIVID_BIN = REPO_ROOT / "build" / "vivid"
DEFAULT_RUNTIME_LOG = pathlib.Path(tempfile.gettempdir()) / "vivid_mcp_runtime.log"
_RUNTIME_STARTUP_TIMEOUT_SEC = 20.0
_RUNTIME_STARTUP_POLL_SEC = 0.25
_MUSIC_EVAL_STARTUP_TIMEOUT_SEC = 15.0
_MUSIC_EVAL_STARTUP_POLL_SEC = 0.25
_LIVE_MUSIC_EVAL_MAX_WINDOW_SEC = 60.0
_TOOLCHAIN_PATH_DIRS = (
    "/opt/homebrew/bin",
    "/opt/homebrew/sbin",
    "/usr/local/bin",
    "/usr/bin",
    "/bin",
    "/usr/sbin",
    "/sbin",
)
_managed_runtime_process: subprocess.Popen | None = None
_managed_runtime_log_path: str = ""
_managed_music_eval_process: subprocess.Popen | None = None
_managed_music_eval_log_path: str = ""

mcp = FastMCP("vivid", instructions="""Vivid is a real-time audio-visual graph engine. You build node graphs that generate and process visuals, audio, and control signals, all running live.

## Three Domains

- **GPU** — texture-based visual operators (noise, shape, blur, composite, etc.). Ports use type `gpu_texture` for 2D image data. Packages can define custom opaque-pointer port types using `data` with a `data_type` string (e.g. the vivid-3d package defines `"gpu_scene"` for 3D scene fragments). 3D operators (Shape3D, Transform3D, SceneMerge, Light3D, Render3D, etc.) are available via the vivid-3d package. Every visual graph needs a `video_out` node to display output.
- **Audio** — sample-based audio operators (oscillator, gain, reverb, etc.). Ports use type `audio_float`. Every audio graph needs an `audio_out` node to hear output.
- **Control** — scalar or lane-bearing signals for modulation (lfo, clock, math, sequencer, etc.). Ports use type `control_float`. Control outputs can also drive any numeric parameter directly.

## Port Compatibility

Connections must match types: `gpu_texture` → `gpu_texture`, `data` → `data` (with matching `data_type`), `audio_float` → `audio_float`, `control_float` → `control_float` or any numeric parameter. Address format for ports: `"node_id/port_name"`. Packages can define custom `data` port types (e.g. vivid-3d uses `data_type: "gpu_scene"` for 3D scene wires).

## Workflow

1. `list_types` / `operator_docs` / package lookup tools — use these first for static discovery; they can run without a live runtime
2. `ensure_runtime` — only when you need to inspect, mutate, capture, or analyze a live graph/session
3. **Compose first** — build the graph from existing operators before considering custom ones. Most goals are achievable by wiring existing operators together.
4. `add_node` → `connect` → `set_param` — assemble and configure the graph
5. `scaffold_operator` — scaffold a starter template when no existing operator achieves the goal. Creates a minimal operator in the **project's local-operators package** (auto-created beside the graph the first time). Pass `destination="core"` only when adding a broadly-useful primitive intended to ship with Vivid. Use the opdev MCP server for advanced features (custom ports, params, inspectors, thumbnails, and `prepare_instance_assets()` warmup guidance).
6. `inspect_graph` — verify the graph state, check live output values

## Analyzing an Existing Graph

When asked to examine, analyze, or debug an existing graph, **always call `get_graph_errors` first** before any other analysis. Errors and dropped connections are the most important information — report them prominently at the top of your response, not buried in a summary. A dropped connection means a wire the user drew is silently inactive, which is almost always the root cause of "signal not reaching downstream nodes" problems.

## Common Patterns

- Connect an `lfo` output → a GPU node's parameter for animation
- Connect `clock` → `sequencer` for rhythmic patterns
- Audio chains: oscillator → effects → `audio_out`
- Visual chains: generators → filters → `video_out`
- Control signals modulate both GPU and audio params

## Composing an AV graph — references first

What counts as "compelling" depends on the project and the user. Do not assume defaults. For any
AV composition task, open by asking the user for a **precedent**:

- URL to a project page (Vimeo, artist website, documentation page)
- YouTube video
- Image file path
- Audio track path
- Artist name ("make something like Ryoji Ikeda")
- Text description of the vibe

Then ask **HOW** the reference should be used:
- *imitate closely* — match palette, density, tempo, motion
- *inspired-by* — extract principles, apply loosely
- *style-only* — visual language without replicating specific elements
- *opposite-of* — use as a negative target

### Reference-to-graph workflow

1. **Ingest** — for URLs, `fetch_reference(url)` downloads a representative image + scrapes
   metadata. For local files, use the path directly. For artist names / text descriptions,
   use your own knowledge of the aesthetic.
2. **Extract** — for images, `analyze_image(thumbnail_local_path)` (on the `vivid-analysis`
   MCP server) returns palette, composition, style tags. For audio, `analyze_track(path)`
   returns key/BPM/spectral character/mood tags.
3. **Translate** — map the extracted descriptors onto Vivid's operator vocabulary using your
   judgment. "High-contrast monochrome dense grids" → Shape2D + Repeat + Bloom + minimal palette.
   "Warm organic pulsing" → Metaball + Smooth envelope follower + amber/red color params.
   Browse `list_reference_graphs(pattern_filter=...)` for existing graphs that implement the
   mechanical pattern you're considering — `load_graph` one to study or use as a starting template.
4. **Build** — `add_node` / `connect` / `set_param` incrementally. After each meaningful step,
   `compare_output_to_reference(reference_path)` to pair the current capture with the reference.
5. **Iterate** — Read both returned paths as images, describe the gap, adjust params, repeat.
   If metrics suggest mechanical problems (near-black output, dead motion), use
   `diagnose_composition_issue` for a concrete fix list.
6. **Stop** when it matches the intent the user asked for — not when it matches some abstract "good."

### Mechanical primitives (not aesthetic targets)

`docs/COMPOSITION-GUIDE.md` describes the mechanics of wiring AV graphs — anti-patterns to
avoid (single-axis scale driving, raw `peak` without an envelope, near-zero `to_min`), metric
thresholds that correlate with "mechanically working" (not "compelling"), and the diagnostic
decision tree. `get_composition_patterns(intent)` returns structural templates (drum-driven
pulse, continuous reactivity, parametric sync, spectral color) as one-piece-per-task
wiring references. **Treat both as vocabulary, not targets.** The target comes from the
user's reference.

### Measurement tools (value-neutral)

- `analyze_output(mode="av", window_seconds=3)` — raw metrics (brightness, motion, correlations,
  onset response rate).
- `diagnose_composition_issue(intent=...)` — converts those metrics into actionable findings for
  *mechanical* problems only.
- `explain_graph_composition(graph_path)` — detects which mechanical pattern(s) a graph
  exhibits.

## VST3 Instruments & Presets

A `Vst3Instrument` node hosts a VST3 synth (notes_in → stereo audio). Workflow:
1. `add_node` Vst3Instrument → `list_vst3_plugins` → `set_vst3_plugin(node_id, name)`.
2. **Presets are where the sound lives.** `list_vst3_presets(node_id)` returns the plugin's
   library merged from `.vstpreset` files, VST3 program lists, and per-plugin adapters for
   native formats (e.g. Serum2's `.SerumPreset`). Each entry has `name`, `category`, `tags`,
   `description`, `source`, and `loadable`. Pick by character using tags/category/description.
3. `load_vst3_preset(node_id, preset_id)` loads a preset by its catalog `id` and persists it
   into the node's saved state (survives save/reload).
4. Fine-tune with `set_vst3_param(node_id, name, value)` after loading.

**Audition loop:** load_vst3_preset → drive notes_in → `record_audio_to_wav` /
`analyze_audio_spectrum` / `evaluate_audio_musically` → compare candidates → keep the best.

**Honest limits:** `loadable: false` means Vivid can *describe* the preset (so you can browse
and recommend it to the user) but cannot load it directly — some synths (e.g. Serum) keep presets
in a proprietary format that only the plugin itself can apply. Prefer `source` `vstpreset`/`program`
or `pigments` (all loadable). AU/CLAP instruments do not yet expose a preset catalog.

**Capturing a sound for a discovery-only plugin (the bridge for Serum, etc.):**
1. **Suggest** — `list_vst3_presets(node_id, filter="bass")` and pick candidates by name/category/tags.
2. **Audition** — `open_vst3_gui(node_id)`; the *user* loads the candidates in the plugin's own UI and
   keeps one (you can't load `loadable:false` presets programmatically).
3. **Capture** — `capture_vst3_preset(node_id, "my-bass", source_preset_id="<the factory id>",
   notes="…")` snapshots the live plugin state AND tags it with the factory metadata.
4. **Tweak** — shape it with `set_vst3_param`, then `update_preset` (or capture under a new name).
Later, `list_presets(node_id)` browses the captured library by character, and `recall_preset`
restores any captured sound exactly. Never hand-edit plugin state — capture, recall, and tweak via
parameters.

## Owned Child Operators

Control operators can embed other operators internally using ChildOp<T>. Use `scaffold_operator`
with `variant="child_op"` to generate a template. Useful for internal modulation (e.g. LFO driving
a gain stage) without exposing child operators as graph nodes. Control env only.

## Custom Operators
If you need to create a custom operator, use `scaffold_operator` to generate the template.
By default this writes to the **project's local-operators package** (a `<graph_dir>/operators/`
package, auto-scaffolded the first time) — keeping experimental work out of the core seed
catalog. Save the graph first so the project package can be placed beside it. Use
`destination="core"` only when contributing a primitive back to Vivid itself.

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


async def _post_music_eval(endpoint: str, body: dict, timeout: float = 60.0) -> str:
    """POST to the music_eval sidecar service and return JSON text."""
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.post(
                f"{MUSIC_EVAL_URL}/v1/{endpoint}",
                json=body,
                timeout=timeout,
            )
            return resp.text
    except httpx.ConnectError:
        return json.dumps({"ok": False, "error": {
            "code": "service_unavailable",
            "message": (
                f"music_eval service not reachable at {MUSIC_EVAL_URL}. "
                "Start it with: cd services/music_eval && uv run uvicorn main:app"
            ),
        }})
    except httpx.TimeoutException:
        return json.dumps({"ok": False, "error": {
            "code": "service_unavailable",
            "message": f"music_eval service timed out (>{timeout:.0f}s) at {MUSIC_EVAL_URL}",
        }})


def _json_response(payload: dict) -> str:
    return json.dumps(payload, separators=(",", ":"), sort_keys=True)


def _clear_managed_runtime_if_exited() -> None:
    global _managed_runtime_process, _managed_runtime_log_path
    if _managed_runtime_process is not None and _managed_runtime_process.poll() is not None:
        _managed_runtime_process = None
        _managed_runtime_log_path = ""


def _ensure_path_dirs(env: dict, dirs: list[str] | tuple[str, ...]) -> None:
    current = env.get("PATH", "")
    parts = [part for part in current.split(":") if part]
    for directory in dirs:
        if directory not in parts and os.path.isdir(directory):
            parts.append(directory)
    env["PATH"] = ":".join(parts)


def _runtime_subprocess_env() -> dict:
    env = os.environ.copy()
    _ensure_path_dirs(env, _TOOLCHAIN_PATH_DIRS)
    return env


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


async def _music_eval_is_reachable() -> bool:
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.get(f"{MUSIC_EVAL_URL}/v1/status", timeout=1.0)
            return resp.status_code == 200
    except Exception:
        return False


def _launch_music_eval_service() -> tuple[subprocess.Popen, str]:
    import urllib.parse as _urlparse
    parsed = _urlparse.urlparse(MUSIC_EVAL_URL)
    host = parsed.hostname or "127.0.0.1"
    port = str(parsed.port or 9877)
    service_dir = REPO_ROOT / "services" / "music_eval"
    if not service_dir.exists():
        raise FileNotFoundError(f"music_eval service directory not found: {service_dir}")
    log_path = str(pathlib.Path(tempfile.gettempdir()) / "vivid_music_eval.log")
    env = _runtime_subprocess_env()
    proc = subprocess.Popen(
        ["uv", "run", "uvicorn", "main:app", "--host", host, "--port", port],
        cwd=str(service_dir),
        stdout=open(log_path, "w"),  # noqa: SIM115
        stderr=subprocess.STDOUT,
        env=env,
    )
    return proc, log_path


async def _ensure_music_eval_service() -> str | None:
    """Ensure the music_eval sidecar is running, launching it if needed.

    Returns None on success or an error string if the service cannot be started.
    """
    global _managed_music_eval_process, _managed_music_eval_log_path
    if _managed_music_eval_process is not None and _managed_music_eval_process.poll() is not None:
        _managed_music_eval_process = None
        _managed_music_eval_log_path = ""
    if await _music_eval_is_reachable():
        return None
    try:
        proc, log_path = _launch_music_eval_service()
    except Exception as exc:
        return f"failed to launch music_eval service: {exc}"
    _managed_music_eval_process = proc
    _managed_music_eval_log_path = log_path
    deadline = time.monotonic() + _MUSIC_EVAL_STARTUP_TIMEOUT_SEC
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            return (f"music_eval service exited during startup "
                    f"(exit code {proc.returncode}); check {log_path}")
        if await _music_eval_is_reachable():
            return None
        await asyncio.sleep(_MUSIC_EVAL_STARTUP_POLL_SEC)
    proc.terminate()
    _managed_music_eval_process = None
    return (f"music_eval service did not become ready within "
            f"{_MUSIC_EVAL_STARTUP_TIMEOUT_SEC:.0f}s; check {log_path}")


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
        env=_runtime_subprocess_env(),
    )
    return proc, log_path


async def _run_vivid_cli_json(args: list[str]) -> str:
    vivid_bin = _resolve_vivid_bin()
    proc = await asyncio.create_subprocess_exec(
        str(vivid_bin),
        *args,
        cwd=str(REPO_ROOT),
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        env=_runtime_subprocess_env(),
    )
    stdout, stderr = await proc.communicate()
    out = stdout.decode("utf-8", errors="replace").strip()
    err = stderr.decode("utf-8", errors="replace").strip()
    if out:
        try:
            json.loads(out)
            return out
        except Exception:
            pass
    message = err or out or f"CLI command failed with exit code {proc.returncode}"
    return _json_response({"ok": False, "error": message})


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
    env = {"ok": True, "schema_version": schema_version, "result": payload.get("result", {})}
    if "health" in payload:
        env["health"] = payload["health"]
    return env


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
        health = env.get("health", {})
        audio_h = health.get("audio", {}) if isinstance(health, dict) else {}
        summary = {
            "kind": kind,
            "critical": int(diag_summary.get("critical", 0)) if isinstance(diag_summary, dict) else 0,
            "warning": int(diag_summary.get("warning", 0)) if isinstance(diag_summary, dict) else 0,
            "info": int(diag_summary.get("info", 0)) if isinstance(diag_summary, dict) else 0,
            "top_hint_ids": top_hint_ids,
            "audio_running": bool(audio_h.get("running", False)),
            "audio_load": round(float(audio_h.get("load", 0.0)), 3),
            "audio_xruns": int(audio_h.get("xruns", 0)),
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
    if "health" in env:
        out["health"] = env["health"]
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
    return await _run_vivid_cli_json(["operator-map", "--json"])


@mcp.tool()
async def discovery_report() -> str:
    """Show the package discovery report: scopes searched, packages loaded, packages skipped (with reasons). Use this to debug missing operators or package loading issues."""
    return await _run_vivid_cli_json(["discovery-report", "--json"])


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
        window_seconds: Analysis window for audio/av modes (>= 1.0 recommended; AV
            mode samples intermediate frames at ~6 fps and needs at least a few
            samples for meaningful correlation)
        include_payload: Include heavyweight capture payloads when available
        node_id: Optional node id to scope analysis to a specific output source

    AV mode returns three complementary reactivity metrics:

    1. **Per-axis correlations** — Pearson r between audio energy and each visual
       axis. Best for graphs with continuous coupling (LFO drives both, audio
       envelope drives a parameter directly):
       - energy_brightness_correlation
       - energy_motion_correlation (necessary to detect displacement/position
         reactivity that doesn't change brightness)
       - energy_contrast_correlation

    2. **Onset-aligned reactivity** — for each detected audio onset, did the
       visual change within ~400ms? Best for percussive/feedback-rich graphs
       where smoothing or visual decay shifts the visual peak relative to the
       audio peak (Pearson correlation breaks down here):
       - detected_onsets — count of audio onsets in the window
       - onset_response_rate — 0-1, fraction of onsets with a visible response
       - reactivity_latency_ms — median onset→peak latency

    3. **Per-band correlations** — energy_*_correlation split by frequency band
       (bass <250 Hz, mid 250–2000 Hz, treble >2000 Hz). Surfaces cases where
       overall correlation is near zero but a specific band drives a specific
       visual axis strongly — e.g., "bass→brightness works, treble→brightness
       doesn't." Available under:
       - band_brightness_correlations.{bass,mid,treble}
       - band_motion_correlations.{bass,mid,treble}
       - band_contrast_correlations.{bass,mid,treble}

    Use all three lenses. Overall correlation ≈ 0 and onset_response_rate ≈ 0
    doesn't necessarily mean the graph is dead — check the per-band numbers;
    the graph may be selectively coupled to one frequency range.

    Note: after `load_graph`, the audio engine takes ~3-4s to begin producing
    samples for the recording tap. Calling analyze_output sooner may report
    rms=0 even on graphs that produce audio. Wait at least 4s after load_graph,
    or re-call once if the first result reports silent audio.

    For diagnosing a dead or weakly-reactive graph, the decision tree in
    docs/COMPOSITION-GUIDE.md § "Diagnosing a dead graph" maps the metric
    pattern (e.g., "motion near zero, brightness near zero") to the likely
    cause and fix.
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
async def wait_for_audio_settle(timeout_seconds: float = 6.0,
                                window_seconds: float = 0.5,
                                rms_threshold: float = 0.001) -> str:
    """Wait until the runtime is producing audio (or until timeout).

    Useful right after `load_graph` on graphs with audio: the audio engine
    takes a few seconds to begin filling the recording tap, so an immediate
    `analyze_output(mode="audio")` may return rms=0 even though the graph
    produces sound. Call this first to gate subsequent analysis on a settled
    audio engine.

    Args:
        timeout_seconds: Give up after this many seconds (default 6.0)
        window_seconds: Each probe's analysis window (default 0.5)
        rms_threshold: Treat audio as settled once RMS exceeds this (default 0.001)

    Returns JSON {ok, settled, elapsed_seconds, final_rms}. `settled` is true
    if RMS exceeded the threshold within the timeout. On a graph that is
    legitimately silent (no audio operators connected), this will report
    settled=false after the full timeout — that is expected.
    """
    import time as _time
    start = _time.monotonic()
    final_rms = 0.0
    while True:
        elapsed = _time.monotonic() - start
        remaining = timeout_seconds - elapsed
        if remaining <= 0:
            return _json_response({
                "ok": True,
                "settled": False,
                "elapsed_seconds": round(elapsed, 3),
                "final_rms": final_rms,
            })
        body = {"mode": "audio", "window_seconds": min(window_seconds, max(0.1, remaining))}
        try:
            raw = await _post("analyze_output", body)
            doc = json.loads(raw)
            final_rms = float(doc.get("metrics", {}).get("audio", {}).get("rms", 0.0))
            if final_rms >= rms_threshold:
                return _json_response({
                    "ok": True,
                    "settled": True,
                    "elapsed_seconds": round(_time.monotonic() - start, 3),
                    "final_rms": final_rms,
                })
        except Exception:
            pass
        await asyncio.sleep(0.2)


# ---------------------------------------------------------------------------
# fetch_reference — download a representative image + metadata for a URL
# reference (YouTube, project page, direct image). Cached locally so repeated
# calls don't re-download. Paired with analyze_image for style extraction.
# ---------------------------------------------------------------------------

import hashlib
import re

_REFERENCE_CACHE_DIR = pathlib.Path.home() / ".cache" / "vivid" / "references"

_YOUTUBE_ID_PATTERNS = [
    re.compile(r"(?:youtube\.com/watch\?(?:[^&]*&)*v=)([A-Za-z0-9_-]{11})"),
    re.compile(r"youtu\.be/([A-Za-z0-9_-]{11})"),
    re.compile(r"youtube\.com/embed/([A-Za-z0-9_-]{11})"),
    re.compile(r"youtube\.com/shorts/([A-Za-z0-9_-]{11})"),
]

_IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tiff", ".tif"}


def _youtube_video_id(url: str) -> str | None:
    for pattern in _YOUTUBE_ID_PATTERNS:
        m = pattern.search(url)
        if m:
            return m.group(1)
    return None


def _url_extension(url: str) -> str:
    # Strip query string / fragment, take the final path segment's extension
    path_part = url.split("?", 1)[0].split("#", 1)[0]
    last_seg = path_part.rstrip("/").rsplit("/", 1)[-1]
    if "." in last_seg:
        return ("." + last_seg.rsplit(".", 1)[-1]).lower()
    return ""


def _reference_cache_key(url: str) -> str:
    return hashlib.sha256(url.encode("utf-8")).hexdigest()[:16]


def _og_and_title_from_html(html: str) -> dict:
    """Very lightweight extraction — no BeautifulSoup dep, just regex.
    Extracts <title>, <meta name="description">, and <meta property="og:image">.
    """
    out = {"title": "", "description": "", "og_image": ""}
    m = re.search(r"<title>([^<]+)</title>", html, re.IGNORECASE | re.DOTALL)
    if m:
        out["title"] = m.group(1).strip()
    m = re.search(r'<meta[^>]+name=["\']description["\'][^>]+content=["\']([^"\']+)',
                  html, re.IGNORECASE)
    if m:
        out["description"] = m.group(1).strip()
    m = re.search(r'<meta[^>]+property=["\']og:image["\'][^>]+content=["\']([^"\']+)',
                  html, re.IGNORECASE)
    if m:
        out["og_image"] = m.group(1).strip()
    # Also grab og:title / og:description as fallback
    if not out["title"]:
        m = re.search(r'<meta[^>]+property=["\']og:title["\'][^>]+content=["\']([^"\']+)',
                      html, re.IGNORECASE)
        if m:
            out["title"] = m.group(1).strip()
    if not out["description"]:
        m = re.search(r'<meta[^>]+property=["\']og:description["\'][^>]+content=["\']([^"\']+)',
                      html, re.IGNORECASE)
        if m:
            out["description"] = m.group(1).strip()
    return out


async def _download_to_path(url: str, target_path: pathlib.Path, timeout: float = 20.0) -> bool:
    """Download a URL to target_path. Returns True on success."""
    try:
        async with httpx.AsyncClient(follow_redirects=True, timeout=timeout) as client:
            resp = await client.get(url)
            if resp.status_code != 200 or not resp.content:
                return False
            target_path.parent.mkdir(parents=True, exist_ok=True)
            target_path.write_bytes(resp.content)
            return True
    except Exception:
        return False


async def _fetch_url_text(url: str, timeout: float = 15.0) -> str | None:
    try:
        async with httpx.AsyncClient(follow_redirects=True, timeout=timeout) as client:
            resp = await client.get(url, headers={"User-Agent": "Mozilla/5.0 (Vivid MCP)"})
            if resp.status_code != 200:
                return None
            return resp.text
    except Exception:
        return None


@mcp.tool()
async def fetch_reference(url: str, refresh: bool = False) -> str:
    """Download a URL reference (YouTube, project page, direct image) and cache it locally.

    Used as the first step of the reference-translation workflow: the user supplies
    a URL, this tool turns it into a local image path + metadata that `analyze_image`
    can consume for style/palette/composition extraction.

    Handles three kinds of URLs:
    - **YouTube** — extracts video ID, downloads the maxresdefault cover frame
      PLUS up to 4 additional frame thumbnails spaced through the video
      (YouTube's /0.jpg mid-point + /1.jpg, /2.jpg, /3.jpg evenly-spaced
      snapshots). Frame paths are returned in `frame_paths` so the LLM can
      see temporal style variation, not just the cover. Scrapes page
      title/description from the watch page.
    - **Direct image** (URL ends in .jpg/.png/.webp/etc.) — downloads to cache.
    - **General webpage** — downloads Open Graph image if present, scrapes title
      and description. If no OG image, returns with `thumbnail_local_path=""`
      (the LLM can still work from the text metadata).

    Args:
        url: The reference URL (any of the above kinds).
        refresh: If True, re-download even when cached locally. Default False.

    Returns JSON `{ok, source_url, source_kind, title, description, thumbnail_local_path, frame_paths, text_summary, cached}`.
    `frame_paths` is a list of all local image paths — for YouTube it contains
    the cover frame plus up to 4 additional frames; for other kinds it's empty.

    Cache location: ~/.cache/vivid/references/<hash>.{jpg,meta.json}

    Next step for Claude: call `analyze_image(path)` on the analysis MCP server
    for each of the relevant `frame_paths` (or just `thumbnail_local_path` if
    there's only one image). Use `title` and `description` to contextualize.
    """
    if not url or not url.startswith(("http://", "https://")):
        return _json_response({"ok": False, "error": "url must be an http(s) URL"})

    _REFERENCE_CACHE_DIR.mkdir(parents=True, exist_ok=True)
    key = _reference_cache_key(url)
    meta_path = _REFERENCE_CACHE_DIR / f"{key}.meta.json"

    if not refresh and meta_path.exists():
        try:
            cached = json.loads(meta_path.read_text())
            cached["cached"] = True
            return _json_response(cached)
        except (json.JSONDecodeError, OSError):
            pass  # fall through to re-fetch

    # Classify URL
    video_id = _youtube_video_id(url)
    ext = _url_extension(url)

    result = {
        "ok": True,
        "source_url": url,
        "source_kind": "unknown",
        "title": "",
        "description": "",
        "thumbnail_local_path": "",
        "text_summary": "",
        "cached": False,
    }

    if video_id:
        result["source_kind"] = "youtube"
        result["frame_paths"] = []
        # Primary cover-frame thumbnail (maxresdefault → hqdefault fallback)
        thumb_path = _REFERENCE_CACHE_DIR / f"{key}.jpg"
        ok_dl = await _download_to_path(
            f"https://img.youtube.com/vi/{video_id}/maxresdefault.jpg", thumb_path)
        if not ok_dl:
            ok_dl = await _download_to_path(
                f"https://img.youtube.com/vi/{video_id}/hqdefault.jpg", thumb_path)
        if ok_dl:
            result["thumbnail_local_path"] = str(thumb_path)
            result["frame_paths"].append(str(thumb_path))
        # Additional frames spaced through the video: YouTube exposes
        # /0.jpg (mid-point), /1.jpg, /2.jpg, /3.jpg at predictable URLs.
        # These are lower-res (~480x360 by default) but give the LLM temporal
        # style variation — not just the cover frame.
        for idx in (1, 2, 3, 0):
            frame_path = _REFERENCE_CACHE_DIR / f"{key}_f{idx}.jpg"
            if await _download_to_path(
                    f"https://img.youtube.com/vi/{video_id}/{idx}.jpg", frame_path):
                result["frame_paths"].append(str(frame_path))
        # Scrape page title/description best-effort
        html = await _fetch_url_text(url)
        if html:
            meta = _og_and_title_from_html(html)
            result["title"] = meta["title"]
            result["description"] = meta["description"]
            result["text_summary"] = meta["description"][:500] if meta["description"] else ""

    elif ext in _IMAGE_EXTENSIONS:
        result["source_kind"] = "image"
        thumb_path = _REFERENCE_CACHE_DIR / f"{key}{ext}"
        ok_dl = await _download_to_path(url, thumb_path)
        if ok_dl:
            result["thumbnail_local_path"] = str(thumb_path)
        else:
            result["ok"] = False
            result["error"] = "failed to download image"
    else:
        result["source_kind"] = "webpage"
        html = await _fetch_url_text(url)
        if html:
            meta = _og_and_title_from_html(html)
            result["title"] = meta["title"]
            result["description"] = meta["description"]
            result["text_summary"] = meta["description"][:500] if meta["description"] else ""
            if meta["og_image"]:
                og_url = meta["og_image"]
                if og_url.startswith("//"):
                    og_url = "https:" + og_url
                elif og_url.startswith("/"):
                    # Try to resolve to absolute using the source URL's host
                    m = re.match(r"^(https?://[^/]+)", url)
                    if m:
                        og_url = m.group(1) + og_url
                if og_url.startswith(("http://", "https://")):
                    og_ext = _url_extension(og_url) or ".jpg"
                    thumb_path = _REFERENCE_CACHE_DIR / f"{key}{og_ext}"
                    if await _download_to_path(og_url, thumb_path):
                        result["thumbnail_local_path"] = str(thumb_path)
        else:
            result["ok"] = False
            result["error"] = "failed to fetch page"

    # Cache the metadata (best-effort; don't fail the call if caching fails)
    try:
        meta_path.write_text(json.dumps(result, indent=2))
    except OSError:
        pass

    return _json_response(result)


# ---------------------------------------------------------------------------
# fetch_reference_audio — download the audio track from a YouTube URL (or
# direct audio file URL) so the LLM can call analyze_track() on real music.
# fetch_reference() handles visual frames; this closes the audio gap.
# ---------------------------------------------------------------------------

import shutil as _shutil

_AUDIO_EXTENSIONS = {".mp3", ".wav", ".m4a", ".flac", ".ogg", ".aac", ".opus"}


def _find_ytdlp() -> str | None:
    found = _shutil.which("yt-dlp")
    if found:
        return found
    fallback = "/opt/homebrew/bin/yt-dlp"
    if pathlib.Path(fallback).is_file():
        return fallback
    return None


@mcp.tool()
async def fetch_reference_audio(url: str, refresh: bool = False) -> str:
    """Download the audio track from a YouTube URL (or direct audio file URL)
    and return a path to a WAV file for music analysis.

    This is the second step of the reference-translation workflow when the
    reference is a music video or audio track. Call fetch_reference() first
    to get visual frames, then this tool to get the audio, then:
      - analyze_image(frame_path) for visual style
      - analyze_track(audio_path) for harmonic, rhythmic, spectral, mood profile

    Requires yt-dlp to be installed (brew install yt-dlp).

    Args:
        url: YouTube URL (watch, youtu.be, shorts, embed) or direct audio file
            URL (.mp3, .wav, .m4a, .flac, .ogg).
        refresh: Re-download even if cached locally. Default False.

    Returns JSON:
        {ok, source_url, audio_path, title, duration_seconds, cached}
    """
    if not url or not url.startswith(("http://", "https://")):
        return _json_response({"ok": False, "error": "url must be an http(s) URL"})

    _REFERENCE_CACHE_DIR.mkdir(parents=True, exist_ok=True)
    key = _reference_cache_key(url)
    wav_path = _REFERENCE_CACHE_DIR / f"{key}_audio.wav"
    meta_path = _REFERENCE_CACHE_DIR / f"{key}_audio.meta.json"

    # Cache hit
    if not refresh and wav_path.exists() and meta_path.exists():
        try:
            cached = json.loads(meta_path.read_text())
            cached["cached"] = True
            cached["audio_path"] = str(wav_path)
            return _json_response(cached)
        except (json.JSONDecodeError, OSError):
            pass

    ytdlp = _find_ytdlp()
    if ytdlp is None:
        return _json_response({
            "ok": False,
            "error": {
                "code": "ytdlp_not_found",
                "message": "yt-dlp not found. Install with: brew install yt-dlp",
            },
        })

    # Output template: yt-dlp appends the container extension, then converts.
    # Use a temp stem so the final .wav lands at wav_path cleanly.
    out_stem = str(_REFERENCE_CACHE_DIR / f"{key}_audio")

    proc = await asyncio.create_subprocess_exec(
        ytdlp,
        "-x", "--audio-format", "wav", "--audio-quality", "0",
        "--no-playlist",
        "--print-json",
        "-o", out_stem + ".%(ext)s",
        url,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    stdout, stderr = await proc.communicate()

    if proc.returncode != 0:
        err_text = stderr.decode("utf-8", errors="replace").strip()
        return _json_response({
            "ok": False,
            "error": {
                "code": "download_failed",
                "message": err_text or f"yt-dlp exited with code {proc.returncode}",
            },
        })

    # yt-dlp --print-json emits one JSON line per item. Parse the first.
    title = ""
    duration: float = 0.0
    for line in stdout.decode("utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            info = json.loads(line)
            title = info.get("title", "")
            duration = float(info.get("duration") or 0)
            break
        except (json.JSONDecodeError, ValueError):
            continue

    # yt-dlp writes e.g. <stem>.wav after conversion
    if not wav_path.exists():
        return _json_response({
            "ok": False,
            "error": {
                "code": "download_failed",
                "message": f"yt-dlp finished but {wav_path.name} not found in cache",
            },
        })

    result = {
        "ok": True,
        "source_url": url,
        "audio_path": str(wav_path),
        "title": title,
        "duration_seconds": duration,
        "cached": False,
    }
    try:
        meta_path.write_text(json.dumps(result, indent=2))
    except OSError:
        pass

    return _json_response(result)


# ---------------------------------------------------------------------------
# compare_output_to_reference — pair the current runtime output with a
# reference image so the LLM can compare visually. No automated scoring.
# ---------------------------------------------------------------------------

import tempfile as _tempfile
import base64 as _base64
import time as _time_module


@mcp.tool()
async def compare_output_to_reference(reference_path: str,
                                      save_dir: str = "") -> str:
    """Capture the current runtime output alongside a reference image for visual comparison.

    Intentionally does NOT compute an automated similarity score. The LLM is
    the judge — the tool just pairs the two images side-by-side in its hands
    so it can Read both and make a qualitative comparison.

    Args:
        reference_path: Absolute path to the reference image (typically the
            `thumbnail_local_path` returned by `fetch_reference`).
        save_dir: Directory to write the capture to. Defaults to a temp dir.

    Returns JSON `{ok, capture_path, reference_path}`. Claude should then Read
    each path as an image (via the built-in Read tool) and describe the
    differences, asking the user whether to iterate.

    Typical workflow:
        1. fetch_reference(url) → reference_path
        2. [build or tweak the graph]
        3. compare_output_to_reference(reference_path)
        4. Read both paths, describe differences, suggest targeted param changes
        5. Repeat from step 2 until the user is satisfied.
    """
    ref_path = pathlib.Path(reference_path).expanduser().resolve()
    if not ref_path.exists():
        return _json_response({
            "ok": False,
            "error": f"reference not found: {ref_path}",
        })

    # Resolve capture save path
    if save_dir:
        save_root = pathlib.Path(save_dir).expanduser()
    else:
        save_root = pathlib.Path(_tempfile.gettempdir()) / "vivid_captures"
    save_root.mkdir(parents=True, exist_ok=True)
    ts = _time_module.strftime("%Y%m%d_%H%M%S")
    capture_path = save_root / f"vivid_capture_{ts}.png"

    # Call capture_frame on the runtime. The runtime's capture_frame endpoint
    # returns {ok, width, height, png_base64}. Decode and write locally.
    raw = await _post("capture_frame", {})
    try:
        doc = json.loads(raw)
    except json.JSONDecodeError as exc:
        return _json_response({
            "ok": False,
            "error": f"capture_frame returned non-JSON: {exc}",
        })
    if not doc.get("ok", False):
        return _json_response({
            "ok": False,
            "error": doc.get("error", "capture_frame failed"),
        })
    png_b64 = doc.get("png_base64", "")
    if not png_b64:
        return _json_response({
            "ok": False,
            "error": "capture_frame returned no png_base64",
        })
    try:
        capture_path.write_bytes(_base64.b64decode(png_b64))
    except (ValueError, OSError) as exc:
        return _json_response({
            "ok": False,
            "error": f"failed to write capture PNG: {exc}",
        })

    return _json_response({
        "ok": True,
        "capture_path": str(capture_path),
        "reference_path": str(ref_path),
        "hint": "Read both paths as images and compare visually. No automated score — LLM is the judge.",
    })


# ---------------------------------------------------------------------------
# AV-coupling visual evaluation tools.
#
# A still single frame can't tell you whether visuals respond to audio. These
# three tools expose the time/audio dimension to a single image Read so an
# LLM can verify "the kick actually flashes the bloom" rather than trust an
# `onset_response_rate=0.62` metric in isolation.
# ---------------------------------------------------------------------------


async def _capture_frame_pil():
    """Capture one frame from the runtime and decode it to a PIL Image.

    Returns (PIL.Image | None, error_string | None).
    """
    raw = await _post("capture_frame", {})
    try:
        doc = json.loads(raw)
    except json.JSONDecodeError as exc:
        return None, f"capture_frame returned non-JSON: {exc}"
    if not doc.get("ok", False):
        return None, doc.get("error", "capture_frame failed")
    png_b64 = doc.get("png_base64", "")
    if not png_b64:
        return None, "capture_frame returned no png_base64"
    try:
        from PIL import Image
        import io
        return Image.open(io.BytesIO(_base64.b64decode(png_b64))).convert("RGB"), None
    except Exception as exc:
        return None, f"failed to decode capture PNG: {exc}"


def _resolve_save_dir(save_dir: str) -> pathlib.Path:
    if save_dir:
        root = pathlib.Path(save_dir).expanduser()
    else:
        root = pathlib.Path(_tempfile.gettempdir()) / "vivid_captures"
    root.mkdir(parents=True, exist_ok=True)
    return root


@mcp.tool()
async def capture_frame_strip(count: int = 8,
                              interval_ms: int = 125,
                              save_dir: str = "") -> str:
    """Capture N frames spaced by `interval_ms` and stitch them into a single
    horizontal strip image with frame-index + timestamp labels.

    Use this to evaluate AV coupling visually: in one Read of the returned
    image, you can see whether visuals scroll, pulse, flash, or sit completely
    static across the captured window. A single still capture can't reveal
    motion; this can.

    Args:
        count: Number of frames in the strip (default 8). Range 2-24.
        interval_ms: Wall-clock spacing between frames (default 125 = 8 fps).
            Smaller values catch faster motion; larger values cover more
            transport time. 125 ms × 8 frames = 1 second of total coverage.
        save_dir: Where to write the strip PNG. Defaults to a temp dir.

    Returns JSON `{ok, strip_path, frame_count, total_seconds}`. Read the
    strip path as an image to inspect.
    """
    count = max(2, min(24, int(count)))
    interval_s = max(0.0, float(interval_ms) / 1000.0)

    frames = []
    timestamps = []
    t0 = _time_module.time()
    for i in range(count):
        if i > 0:
            await asyncio.sleep(interval_s)
        img, err = await _capture_frame_pil()
        if img is None:
            return _json_response({"ok": False, "error": f"frame {i}: {err}"})
        frames.append(img)
        timestamps.append(_time_module.time() - t0)

    from PIL import Image, ImageDraw

    # Scale each frame down to a strip-friendly height (so 8 wide frames fit).
    target_h = 180
    scaled = []
    for img in frames:
        ratio = target_h / img.height
        w = int(img.width * ratio)
        scaled.append(img.resize((w, target_h), Image.LANCZOS))

    gap = 4
    total_w = sum(im.width for im in scaled) + gap * (len(scaled) - 1)
    label_h = 18
    strip = Image.new("RGB", (total_w, target_h + label_h), (0, 0, 0))
    draw = ImageDraw.Draw(strip)
    x = 0
    for i, im in enumerate(scaled):
        strip.paste(im, (x, label_h))
        label = f"#{i}  +{timestamps[i]*1000:.0f}ms"
        draw.text((x + 4, 2), label, fill=(255, 255, 255))
        x += im.width + gap

    save_root = _resolve_save_dir(save_dir)
    ts = _time_module.strftime("%Y%m%d_%H%M%S")
    out_path = save_root / f"frame_strip_{ts}.png"
    strip.save(out_path)

    return _json_response({
        "ok": True,
        "strip_path": str(out_path),
        "frame_count": len(frames),
        "total_seconds": timestamps[-1],
        "hint": "Read the strip as an image. If frames look identical, the visuals are static.",
    })


@mcp.tool()
async def capture_frame_diff(interval_ms: int = 200,
                             save_dir: str = "") -> str:
    """Capture two frames `interval_ms` apart and produce a colored difference
    image showing exactly which pixels changed.

    Pixels that brightened render as red; pixels that dimmed render as blue;
    unchanged areas are dark. Saturation scales with the magnitude of the
    change. Reveals where in the frame motion is actually happening — a
    completely-static graph produces a near-black diff.

    Args:
        interval_ms: Wall-clock gap between the two captures (default 200).
        save_dir: Where to write the diff PNG. Defaults to a temp dir.

    Returns JSON `{ok, diff_path, before_path, after_path, mean_abs_delta}`.
    `mean_abs_delta` is the average per-pixel brightness change in [0,1] — a
    quick numeric sanity check (close to 0 = nothing moved).
    """
    img_a, err_a = await _capture_frame_pil()
    if img_a is None:
        return _json_response({"ok": False, "error": f"first frame: {err_a}"})
    await asyncio.sleep(max(0.0, float(interval_ms) / 1000.0))
    img_b, err_b = await _capture_frame_pil()
    if img_b is None:
        return _json_response({"ok": False, "error": f"second frame: {err_b}"})

    if img_a.size != img_b.size:
        img_b = img_b.resize(img_a.size)

    from PIL import Image, ImageChops

    # Per-channel signed delta on luminance (mean of RGB). PIL clamps to
    # [0,255], so split into "brighter" and "dimmer" channels separately
    # and boost magnitude so small AV-coupling pulses are visible.
    lum_a = img_a.convert("L")
    lum_b = img_b.convert("L")
    brighter = ImageChops.subtract(lum_b, lum_a, scale=0.25)  # b - a, clamped
    dimmer   = ImageChops.subtract(lum_a, lum_b, scale=0.25)  # a - b, clamped
    # Mean absolute delta (for the numeric sanity check) before boosting.
    delta_abs = ImageChops.add(
        ImageChops.subtract(lum_a, lum_b),
        ImageChops.subtract(lum_b, lum_a),
    )
    n_pixels = lum_a.size[0] * lum_a.size[1]
    mean_abs_delta = (sum(delta_abs.getdata()) / n_pixels) / 255.0
    # Boost the brighter/dimmer channels for visibility (×4 ≈ 6dB).
    boost = lambda im: im.point(lambda x: min(255, x * 4))
    brighter = boost(brighter)
    dimmer   = boost(dimmer)
    zero     = Image.new("L", lum_a.size, 0)
    diff_img = Image.merge("RGB", (brighter, zero, dimmer))

    save_root = _resolve_save_dir(save_dir)
    ts = _time_module.strftime("%Y%m%d_%H%M%S")
    before_path = save_root / f"diff_before_{ts}.png"
    after_path = save_root / f"diff_after_{ts}.png"
    diff_path = save_root / f"diff_{ts}.png"
    img_a.save(before_path)
    img_b.save(after_path)
    diff_img.save(diff_path)

    return _json_response({
        "ok": True,
        "diff_path": str(diff_path),
        "before_path": str(before_path),
        "after_path": str(after_path),
        "mean_abs_delta": float(mean_abs_delta),
        "hint": "Read diff_path. Near-black = nothing moved. Bright red/blue = real motion.",
    })


@mcp.tool()
async def evaluate_live_frame(include_payload: bool = False) -> str:
    """Capture one live output frame and return SigLIP-based style tags plus the
    frame PNG so the calling LLM can make its own visual judgment.

    Style tags come from a fixed 35-descriptor vocabulary spanning four
    categories: visual_style, mood, movement, color_mood. Each tag carries a
    confidence score from SigLIP image-text cosine similarity.

    The frame PNG is always included in the response — it is the primary
    material for the LLM's own evaluation.

    When SigLIP / torch is not installed, style_tags will be empty and
    siglip_available will be false; the frame PNG is still returned.

    Args:
        include_payload: When true, adds siglip_available to the response.

    Returns JSON:
        {ok, frame_width, frame_height, style_tags, frame_png_b64}
        style_tags: [{tag, category, confidence}, ...]
    """
    img, err = await _capture_frame_pil()
    if img is None:
        return _json_response({"ok": False, "error": str(err)})

    import io as _io
    import base64 as _b64mod

    vst = _visual_style_tags()
    style_tags = vst.compute_style_tags(img)
    siglip_available = len(style_tags) > 0

    buf = _io.BytesIO()
    img.save(buf, format="PNG")
    png_b64 = _b64mod.b64encode(buf.getvalue()).decode()

    result: dict = {
        "ok": True,
        "frame_width": img.width,
        "frame_height": img.height,
        "style_tags": style_tags,
        "frame_png_b64": png_b64,
    }
    if include_payload:
        result["payload"] = {"siglip_available": siglip_available}
    return _json_response(result)


@mcp.tool()
async def compare_frame_to_intent(intent: str,
                                   reference_path: str = "",
                                   include_payload: bool = False) -> str:
    """Capture the live output frame, score it against a text intent using
    SigLIP cosine similarity, and return the frame PNG so the calling LLM can
    make its own visual judgment.

    The match_score is raw SigLIP cosine similarity — typically 0.05–0.35 for
    meaningful matches. This is NOT the same 0–1 scale used by music_eval.
    Use it as a relative indicator, not an absolute percentage.

    The frame PNG is always included. A multimodal LLM should use it together
    with the score to form an overall evaluation.

    When SigLIP / torch is not installed, match_score will be null and
    style_tags will be empty; the frame PNG is still returned.

    Args:
        intent: Free-text description of the intended visual character,
            e.g. "dark, geometric, low energy" or "warm cinematic glow".
        reference_path: Optional absolute path to a reference image. When
            provided, also computes a reference_match_score for the reference
            against the same intent.
        include_payload: When true, adds reference_match_score, reference_path,
            and siglip_available to the response.

    Returns JSON:
        {ok, intent, match_score, style_tags, frame_png_b64}
        match_score: float (raw SigLIP cosine sim ~0.05-0.35) or null
    """
    if not intent:
        return _json_response({"ok": False, "error": "intent must be non-empty"})

    if reference_path:
        import os as _os
        if not _os.path.isfile(reference_path):
            return _json_response({"ok": False, "error": f"reference_path not found: {reference_path}"})

    img, err = await _capture_frame_pil()
    if img is None:
        return _json_response({"ok": False, "error": str(err)})

    import io as _io
    import base64 as _b64mod

    vst = _visual_style_tags()

    frame_emb = vst._extract_image_embedding(img)
    intent_emb = vst._extract_text_embedding(intent)
    siglip_available = frame_emb is not None and intent_emb is not None

    match_score: float | None = None
    if siglip_available:
        import numpy as _np
        f = frame_emb / (_np.linalg.norm(frame_emb) + 1e-9)
        t = intent_emb / (_np.linalg.norm(intent_emb) + 1e-9)
        match_score = float(_np.dot(f, t))

    style_tags = vst.compute_style_tags(img)

    buf = _io.BytesIO()
    img.save(buf, format="PNG")
    png_b64 = _b64mod.b64encode(buf.getvalue()).decode()

    result: dict = {
        "ok": True,
        "intent": intent,
        "match_score": match_score,
        "style_tags": style_tags,
        "frame_png_b64": png_b64,
    }

    if include_payload:
        payload: dict = {
            "siglip_available": siglip_available,
            "reference_path": reference_path,
        }
        if reference_path and siglip_available:
            from PIL import Image as _PIL_Image
            ref_img = _PIL_Image.open(reference_path).convert("RGB")
            ref_emb = vst._extract_image_embedding(ref_img)
            if ref_emb is not None:
                r = ref_emb / (_np.linalg.norm(ref_emb) + 1e-9)
                payload["reference_match_score"] = float(_np.dot(r, t))
            else:
                payload["reference_match_score"] = None
        else:
            payload["reference_match_score"] = None
        result["payload"] = payload

    return _json_response(result)


@mcp.tool()
async def capture_onset_montage(window_seconds: float = 4.0,
                                fps: int = 12,
                                onset_threshold: float = 0.15,
                                save_dir: str = "") -> str:
    """Record audio peak + visual frames over a window, detect audio onsets,
    and produce a contact sheet pairing the frame AT each onset with the
    frame ~100ms after — so you can verify "did the kick visibly flash?"

    Detection is client-side and approximate: at each captured frame, the
    runtime's `master/peak` (or whatever output port the implementation
    samples) is read; an onset fires when the peak rises sharply. For
    drum-driven graphs this catches kick/snare hits reliably.

    Args:
        window_seconds: Total recording window (default 4 s).
        fps: Capture rate in frames per second (default 12).
        onset_threshold: Minimum peak rise (in [0,1]) to count as an onset.
            Lower = more sensitive. Default 0.15.
        save_dir: Where to write the montage PNG. Defaults to a temp dir.

    Returns JSON `{ok, montage_path, onset_count, frame_count}`. Read the
    montage path; each row pairs an at-onset frame (left) with a +100ms
    follow-up frame (right). If the visual is identical between the two
    columns, audio is not driving any visible change for that hit.
    """
    fps = max(4, min(30, int(fps)))
    interval_s = 1.0 / fps
    total_frames = max(8, int(window_seconds * fps))

    frames = []
    timestamps = []
    peaks = []
    t0 = _time_module.time()
    for i in range(total_frames):
        target = t0 + i * interval_s
        slack = target - _time_module.time()
        if slack > 0:
            await asyncio.sleep(slack)
        img, err = await _capture_frame_pil()
        if img is None:
            return _json_response({"ok": False, "error": f"frame {i}: {err}"})
        frames.append(img)
        timestamps.append(_time_module.time() - t0)
        # Sample audio peak from a tiny analysis window.
        audio_raw = await _post("analyze_output", {
            "mode": "audio",
            "window_seconds": 0.05,
            "include_payload": False,
        })
        try:
            audio_doc = json.loads(audio_raw)
            peak = float(audio_doc.get("metrics", {}).get("audio", {}).get("peak", 0.0))
        except (json.JSONDecodeError, ValueError, TypeError):
            peak = 0.0
        peaks.append(peak)

    # Onset detection: rising edge with refractory period.
    refractory_frames = max(1, int(0.15 * fps))
    onset_indices = []
    last_onset = -refractory_frames
    for i in range(1, len(peaks)):
        if i - last_onset < refractory_frames:
            continue
        rise = peaks[i] - peaks[i - 1]
        if rise > onset_threshold and peaks[i] > onset_threshold:
            onset_indices.append(i)
            last_onset = i

    if not onset_indices:
        return _json_response({
            "ok": True,
            "montage_path": "",
            "onset_count": 0,
            "frame_count": len(frames),
            "peak_max": max(peaks) if peaks else 0.0,
            "hint": (f"No onsets detected (max peak {max(peaks):.3f}, "
                     f"threshold {onset_threshold}). Either the audio is silent, "
                     "the threshold is too high, or peaks are too smooth. Try a "
                     "lower onset_threshold or check that the graph produces audio."),
        })

    follow_offset = max(1, int(0.10 * fps))  # ~100 ms after onset

    from PIL import Image, ImageDraw
    cell_h = 160
    pairs = []
    for idx in onset_indices:
        follow_idx = min(idx + follow_offset, len(frames) - 1)
        a = frames[idx]
        b = frames[follow_idx]
        ratio = cell_h / a.height
        cell_w = int(a.width * ratio)
        a_s = a.resize((cell_w, cell_h), Image.LANCZOS)
        b_s = b.resize((cell_w, cell_h), Image.LANCZOS)
        pairs.append((a_s, b_s, timestamps[idx], peaks[idx],
                      timestamps[follow_idx]))

    cell_w = pairs[0][0].width
    pair_w = cell_w * 2 + 4
    label_h = 22
    row_h = cell_h + label_h
    montage = Image.new("RGB", (pair_w, row_h * len(pairs)), (10, 10, 14))
    draw = ImageDraw.Draw(montage)
    for row, (a_s, b_s, t_on, peak, t_follow) in enumerate(pairs):
        y = row * row_h
        montage.paste(a_s, (0, y + label_h))
        montage.paste(b_s, (cell_w + 4, y + label_h))
        draw.text((4, y + 2),
                  f"onset @ {t_on*1000:.0f}ms (peak {peak:.2f})",
                  fill=(255, 200, 200))
        draw.text((cell_w + 8, y + 2),
                  f"+ {(t_follow - t_on)*1000:.0f}ms",
                  fill=(200, 220, 255))

    save_root = _resolve_save_dir(save_dir)
    ts = _time_module.strftime("%Y%m%d_%H%M%S")
    out_path = save_root / f"onset_montage_{ts}.png"
    montage.save(out_path)

    return _json_response({
        "ok": True,
        "montage_path": str(out_path),
        "onset_count": len(onset_indices),
        "frame_count": len(frames),
        "peak_max": max(peaks),
        "hint": ("Read the montage. Each row is a detected hit: left = frame "
                 "AT the onset, right = frame ~100ms later. Identical pairs "
                 "mean audio is not driving any visible change for that hit."),
    })


# ---------------------------------------------------------------------------
# diagnose_composition_issue — rule-based interpretation of analyze_output
# metrics. Encodes the decision tree from docs/COMPOSITION-GUIDE.md §
# "Diagnosing a dead graph." Metric-only for v1; a future revision can add
# graph-structure inspection (e.g., flag peak→scale wires that skip Smooth).
# ---------------------------------------------------------------------------

_KNOWN_INTENTS = {"", "drum-driven", "percussive", "continuous", "pad", "ambient", "parametric"}


def _intent_expects_onsets(intent: str) -> bool:
    return intent in ("drum-driven", "percussive")


def _intent_expects_continuous(intent: str) -> bool:
    return intent in ("continuous", "pad", "ambient")


def _compute_composition_findings(analysis: dict, intent: str) -> list[dict]:
    """Map metric patterns to a ranked list of findings.

    Each finding has severity (critical/warning/info), symptom (what metric
    triggered it), likely_cause, fix (concrete action), confidence
    (high/medium/low). Findings are returned severity-sorted.
    """
    metrics = analysis.get("metrics", {}) or {}
    audio = metrics.get("audio", {}) or {}
    visual = metrics.get("visual", {}) or {}
    reactivity = metrics.get("av_reactivity", {}) or {}
    notes = analysis.get("notes", []) or []

    rms = float(audio.get("rms", 0.0))
    brightness = float(visual.get("mean_brightness", 0.0))
    motion = float(visual.get("motion_magnitude", 0.0))
    contrast = float(visual.get("contrast", 0.0))
    br_corr = float(reactivity.get("energy_brightness_correlation", 0.0))
    mo_corr = float(reactivity.get("energy_motion_correlation", 0.0))
    ct_corr = float(reactivity.get("energy_contrast_correlation", 0.0))
    onsets = int(reactivity.get("detected_onsets", 0))
    response_rate = float(reactivity.get("onset_response_rate", 0.0))
    latency_ms = float(reactivity.get("reactivity_latency_ms", 0.0))
    band_br = reactivity.get("band_brightness_correlations", {}) or {}
    band_mo = reactivity.get("band_motion_correlations", {}) or {}
    band_ct = reactivity.get("band_contrast_correlations", {}) or {}

    findings: list[dict] = []

    # Rule 1 — audio silence on a graph where the caller expects audio.
    # Only flag when the intent explicitly expects audio; pure-visual graphs
    # legitimately report rms=0 and shouldn't trigger this finding.
    if rms < 0.001 and intent in ("drum-driven", "percussive", "continuous", "pad"):
        findings.append({
            "severity": "warning",
            "symptom": f"audio.rms={rms:.4f} (silent)",
            "likely_cause": "Audio engine still settling after graph load, OR graph has no completed audio path to audio_out.",
            "fix": "Call `wait_for_audio_settle` then re-run. If still silent, inspect the audio chain for missing audio_out connection or a disconnected wire.",
            "confidence": "medium",
        })

    # Rule 2 — near-black output.
    if brightness < 0.01:
        findings.append({
            "severity": "critical",
            "symptom": f"visual.mean_brightness={brightness:.4f} (near-black)",
            "likely_cause": (
                "Shapes may be too small to register on average, often because: "
                "(a) single-axis scale driving — only scale_x is modulated, scale_y stays tiny; "
                "(b) to_min on peak→scale remap is near zero so shapes vanish between hits; "
                "(c) upstream visual chain is broken (disconnected wire, zero alpha/opacity)."
            ),
            "fix": (
                "Inspect each Shape2D: confirm BOTH scale_x and scale_y are driven when a source modulates size. "
                "Verify to_min >= 0.03 on any peak→scale remap. Trace upstream with `inspect_graph` for broken wires."
            ),
            "confidence": "high",
        })

    # Rule 3 — very low motion on a graph that should have animated visuals.
    if motion < 0.01 and rms > 0.001:
        findings.append({
            "severity": "warning",
            "symptom": f"visual.motion_magnitude={motion:.4f} (near-static)",
            "likely_cause": (
                "Visuals aren't changing between frames. Common causes: "
                "(a) single-axis scale driving (shape distorts but inter-frame delta is small); "
                "(b) Smooth fall_time too long (visual holds previous value); "
                "(c) LFO frequency below 0.5 Hz over a 3s window (under-sampled motion)."
            ),
            "fix": (
                "Drive both scale_x and scale_y together. Try Smooth fall_time ≤ 0.3s for snappier response. "
                "For slow LFOs, increase `window_seconds` to 5-8s so the analysis window covers a full cycle."
            ),
            "confidence": "medium",
        })

    # Rule 4 — drum-driven graph with poor onset response.
    if onsets >= 3 and response_rate < 0.3 and _intent_expects_onsets(intent):
        findings.append({
            "severity": "critical",
            "symptom": f"onset_response_rate={response_rate:.2f} with {onsets} onsets detected (drum-driven intent)",
            "likely_cause": (
                "Audio has rhythmic onsets but most don't produce a visible visual response. "
                "Typical causes: peak → scale wired directly (no envelope, sub-frame flash); "
                "to_min too low (shape vanishes between hits); feedback decay swallowing the pulse."
            ),
            "fix": (
                "Insert a SmoothFr (rise_time=0.005, fall_time=0.4) between drum/peak and shape/scale. "
                "Bump to_min on peak→scale remaps to 0.05+. "
                "If Feedback is in the chain, try reducing decay to 0.85–0.9."
            ),
            "confidence": "high",
        })

    # Rule 5 — onset response great but correlations all near zero.
    # This is a GOOD outcome; flag it as info so Claude doesn't "fix" it.
    if response_rate > 0.5 and onsets >= 3 \
            and abs(br_corr) < 0.2 and abs(mo_corr) < 0.2 and abs(ct_corr) < 0.2:
        findings.append({
            "severity": "info",
            "symptom": f"onset_response_rate={response_rate:.2f} but all correlations < |0.2|",
            "likely_cause": (
                "This is healthy, not broken. The graph is event-driven (reacts to onsets) rather than "
                "continuously coupled. Pearson correlation is the wrong lens — it assumes zero-lag linear tracking."
            ),
            "fix": "No action needed. Trust onset_response_rate for this graph's reactivity character.",
            "confidence": "high",
        })

    # Rule 6 — strongly negative correlation suggests phase lag from feedback/smoothing.
    neg_corrs = [c for c in (br_corr, mo_corr, ct_corr) if c <= -0.2]
    if neg_corrs and response_rate > 0.5:
        findings.append({
            "severity": "info",
            "symptom": f"negative correlation ({min(neg_corrs):.2f}) with onset_response_rate={response_rate:.2f}",
            "likely_cause": (
                "Feedback decay or Smooth fall_time is introducing a phase lag — the visual peak arrives "
                "LATER than the audio peak. Pearson correlation goes negative when two series are offset "
                "by more than their autocorrelation length; this does not mean the graph is broken."
            ),
            "fix": (
                "No action required if visuals look compelling — the phase lag is often the aesthetic "
                "(trails, sustain). If you want tighter coupling, reduce Feedback/decay or Smooth/fall_time."
            ),
            "confidence": "medium",
        })

    # Rule 7 — continuous content flagged: no onsets detected but audio is present.
    if onsets == 0 and rms > 0.01:
        findings.append({
            "severity": "info",
            "symptom": f"detected_onsets=0 with audio.rms={rms:.3f}",
            "likely_cause": "Continuous (non-percussive) audio. Onset detection returns nothing for pads / drones / sustained tones.",
            "fix": (
                "For continuous content, read energy_*_correlation instead of onset_response_rate. "
                "If you expected onsets, check that the graph has percussive operators (drums, gate, envelope) routing to audio_out."
            ),
            "confidence": "high",
        })

    # Rule 8 — high reactivity latency suggests the visual chain is slow to respond.
    if latency_ms > 300.0 and response_rate > 0.5:
        findings.append({
            "severity": "warning",
            "symptom": f"reactivity_latency_ms={latency_ms:.0f} (visual peak > 300ms after audio onset)",
            "likely_cause": "Smooth fall_time too long, or Feedback decay persisting the trail beyond the next beat.",
            "fix": "Try reducing Smooth/fall_time to 0.2-0.3s, or Feedback/decay to 0.85-0.9.",
            "confidence": "medium",
        })

    # Rule 10 — band-selective coupling: overall correlation is weak but a
    # specific frequency band's correlation is strong. Value-neutral
    # observation — the graph is frequency-selective, which may or may not
    # match the user's intent.
    band_observations = []
    band_name_map = {"bass": "bass (<250 Hz)", "mid": "mid (250–2000 Hz)",
                     "treble": "treble (>2000 Hz)"}
    for axis_name, overall, band in (
        ("brightness", br_corr, band_br),
        ("motion",     mo_corr, band_mo),
        ("contrast",   ct_corr, band_ct),
    ):
        if abs(overall) >= 0.3:
            continue  # overall coupling is measurable; no need to call out a band
        band_values = {k: float(band.get(k, 0.0)) for k in ("bass", "mid", "treble")}
        strong = [(k, v) for k, v in band_values.items() if abs(v) > 0.5]
        if strong:
            # Sort strongest first
            strong.sort(key=lambda kv: -abs(kv[1]))
            k, v = strong[0]
            band_observations.append(
                f"{band_name_map[k]} → {axis_name} r={v:+.2f} "
                f"(overall r={overall:+.2f})"
            )
    if band_observations:
        findings.append({
            "severity": "info",
            "symptom": "band-selective reactivity: " + "; ".join(band_observations),
            "likely_cause": (
                "Overall energy↔visual correlation is weak, but a specific frequency band "
                "correlates strongly with the visual. The graph IS reactive — just only to "
                "part of the audio spectrum. This may be intentional (e.g., bass-driven "
                "brightness with treble-independent visuals) or accidental (e.g., the user "
                "expected wideband coupling but the wire is tapping only low frequencies)."
            ),
            "fix": (
                "Decide whether this band-selectivity matches your intent. If it does, no action. "
                "If you wanted wideband reactivity, add additional audio→visual wires that tap "
                "different bands — e.g., via FFTAnalysis + Math to isolate treble energy into a "
                "second control signal."
            ),
            "confidence": "high",
        })

    # Rule 9 — healthy case.
    if not findings and brightness >= 0.03 and motion >= 0.01 \
            and (response_rate > 0.7 or onsets == 0):
        findings.append({
            "severity": "info",
            "symptom": "no issues detected",
            "likely_cause": "All measured metrics are in healthy ranges for this intent.",
            "fix": "No action required.",
            "confidence": "medium",
        })

    # Structure-aware follow-up (not automated in v1).
    # If the findings suggest a structural cause, add a hint for the caller
    # to cross-check via inspect_graph.
    if any(f["severity"] == "critical" for f in findings):
        findings.append({
            "severity": "info",
            "symptom": "critical issue detected — recommended next step",
            "likely_cause": "Metric-only diagnosis can't see the graph topology.",
            "fix": (
                "Call `inspect_graph(detail='full')` and check: "
                "(1) are there any peak→scale connections that skip SmoothFr? "
                "(2) do Shape2D nodes have both scale_x and scale_y driven? "
                "(3) what to_min values are on audio→visual remap wires?"
            ),
            "confidence": "medium",
        })

    # Sort: critical > warning > info
    severity_rank = {"critical": 0, "warning": 1, "info": 2}
    findings.sort(key=lambda f: severity_rank.get(f["severity"], 3))

    return findings


@mcp.tool()
async def diagnose_composition_issue(analysis_json: str = "",
                                     intent: str = "",
                                     window_seconds: float = 3.0) -> str:
    """Diagnose why an audio-visual graph feels wrong or weakly reactive.

    Encodes the decision tree from docs/COMPOSITION-GUIDE.md § "Diagnosing a
    dead graph" as a programmatic rule engine. Maps metric patterns (from
    analyze_output) onto ranked findings — each with a symptom, likely cause,
    concrete fix, confidence level, and severity (critical / warning / info).

    Args:
        analysis_json: Pass a previous `analyze_output` JSON response to
            diagnose without re-running the analysis. If empty, this tool
            calls `analyze_output(mode="av", window_seconds=...)` itself.
        intent: Optional hint about the graph's compositional strategy:
            "drum-driven" / "percussive" — expect onsets, trust onset_response_rate
            "continuous" / "pad" / "ambient" — expect continuous coupling, trust correlation
            "parametric" — shared source to both domains; reactivity metrics are n/a
            "" — infer from metric shape (default)
        window_seconds: Analysis window used when analysis_json is empty.

    Returns JSON `{ok, analysis, findings: [...]}`. Each finding carries
    {severity, symptom, likely_cause, fix, confidence}. A graph with no
    issues still returns a single "info" finding confirming the healthy state.

    Tip: call `wait_for_audio_settle` first if you just loaded the graph,
    otherwise audio silence can mask other problems.
    """
    if intent not in _KNOWN_INTENTS:
        return _json_response({
            "ok": False,
            "error": f"unknown intent '{intent}'; use one of {sorted(_KNOWN_INTENTS - {''})} or leave empty",
        })

    raw = analysis_json
    if not raw:
        raw = await _post("analyze_output", {
            "mode": "av",
            "window_seconds": window_seconds,
        })

    try:
        analysis = json.loads(raw)
    except (json.JSONDecodeError, ValueError) as exc:
        return _json_response({
            "ok": False,
            "error": f"could not parse analysis JSON: {exc}",
        })

    if not analysis.get("ok", False):
        return _json_response({
            "ok": False,
            "error": analysis.get("error", "analyze_output did not report ok"),
            "analysis": analysis,
        })

    findings = _compute_composition_findings(analysis, intent)
    return _json_response({
        "ok": True,
        "intent": intent,
        "analysis": analysis,
        "findings": findings,
    })


# ---------------------------------------------------------------------------
# get_session_view_guide — static reference for Session view features.
# ---------------------------------------------------------------------------

@mcp.tool()
async def get_session_view_guide() -> str:
    """Return a guide to Vivid's Session model: Tracks, Clips, and Scenes.

    Describes the Track/Clip/Scene performance model and the relevant tools for
    authoring and launching session content. Call this when you need to understand
    how to set up a session or drive live performance from code.
    """
    return json.dumps({
        "session_model": {
            "track": "A named group of nodes. Each track owns one or more nodes and holds a list of Clips.",
            "clip": "A named snapshot of a track's node params. Launching a clip restores those params to the live graph.",
            "scene": "A collection of track→clip assignments. Launching a scene fires all assignments at once.",
        },
        "workflow": [
            "create_track(name) — create a track and give it a name",
            "assign_nodes_to_track(track_id, node_ids) — assign nodes to the track",
            "set_param(...) — dial in params on the owned nodes",
            "save_clip(track_id, 'Verse') — capture current param state as a clip",
            "save_scene('Drop') — capture all current active clips as a scene",
            "queue_scene(scene_id, 'bar') — fire all scene assignments at next bar boundary",
        ],
        "inspection": {
            "inspect_session": "Full session state: all tracks, clips, scenes, active/queued clip per track",
            "inspect_clip": "Stored param values inside a specific clip",
            "inspect_scene": "Scene assignments with resolved names, leave-unchanged set, unassigned tracks",
        },
        "launch": {
            "launch_clip": "Immediately apply a clip's params to its track's nodes",
            "queue_clip": "Apply at a beat boundary (instant/beat/bar/4bar)",
            "queue_scene": "Fire all scene assignments at a beat boundary (instant/beat/bar/4bar)",
        },
        "quantize_modes": ["instant", "beat", "bar", "4bar"],
        "legacy_advanced_tools": {
            "note": "StateMachine-based clip launcher still works for existing graphs. Whole-graph Variations were removed in schema v2 — use Tracks/Clips instead.",
            "tools": ["add_clip_track", "ensure_state_mapping", "queue_state_transition",
                      "set_state_preset", "inspect_state_presets"],
        },
    }, indent=2)


# ---------------------------------------------------------------------------
# Session: Tracks, Clips, Scenes — inspection, CRUD, and launch
# ---------------------------------------------------------------------------

@mcp.tool()
async def inspect_session() -> str:
    """Return all tracks, clips, and scenes with their active/queued state.

    Call this before any session operation to understand the current structure:
    which tracks exist, what clips each has, which clip is active/queued per track,
    and what scenes are defined. Also reports the queued scene (if any).
    """
    return await _post("inspect_session", {})


@mcp.tool()
async def inspect_clip(track_id: str, clip_id: str) -> str:
    """Return the stored param values inside a specific clip.

    Shows the float params and string params captured when the clip was saved or
    last updated. Use this to verify what a clip will restore before launching it.
    """
    return await _post("inspect_clip", {"track_id": track_id, "clip_id": clip_id})


@mcp.tool()
async def inspect_scene(scene_id: str) -> str:
    """Return scene assignments with resolved track/clip names.

    Shows each track→clip assignment with human-readable names, the leave-unchanged
    set (tracks explicitly excluded from this scene), and unassigned tracks (tracks
    that are neither assigned nor leave-unchanged in this scene).
    """
    return await _post("inspect_scene", {"scene_id": scene_id})


@mcp.tool()
async def create_track(name: str) -> str:
    """Create a new session track with the given name. Returns the new track_id."""
    return await _post("create_track", {"name": name})


@mcp.tool()
async def rename_track(track_id: str, name: str) -> str:
    """Rename a session track."""
    return await _post("rename_track", {"track_id": track_id, "name": name})


@mcp.tool()
async def remove_track(track_id: str) -> str:
    """Remove a session track and all its clips."""
    return await _post("remove_track", {"track_id": track_id})


@mcp.tool()
async def move_track(track_id: str, to_index: int) -> str:
    """Move a track to a new position in the track list (0-based)."""
    return await _post("move_track", {"track_id": track_id, "to_index": to_index})


@mcp.tool()
async def assign_nodes_to_track(track_id: str, node_ids: list[str]) -> str:
    """Assign graph nodes to a track. These nodes' params will be captured by save_clip."""
    return await _post("assign_nodes_to_track", {"track_id": track_id, "node_ids": node_ids})


@mcp.tool()
async def unassign_nodes_from_track(track_id: str, node_ids: list[str]) -> str:
    """Remove nodes from a track's owned set."""
    return await _post("unassign_nodes_from_track", {"track_id": track_id, "node_ids": node_ids})


@mcp.tool()
async def save_clip(track_id: str, name: str) -> str:
    """Capture current param state of the track's owned nodes as a new clip.

    Returns the new clip_id. Wire-driven params and params with PARAM_LOCK_WIRES
    are excluded from the capture.
    """
    return await _post("save_clip", {"track_id": track_id, "name": name})


@mcp.tool()
async def update_clip(track_id: str, clip_id: str) -> str:
    """Re-capture the current param state into an existing clip, replacing its stored values."""
    return await _post("update_clip", {"track_id": track_id, "clip_id": clip_id})


@mcp.tool()
async def rename_clip(track_id: str, clip_id: str, name: str) -> str:
    """Rename a clip."""
    return await _post("rename_clip", {"track_id": track_id, "clip_id": clip_id, "name": name})


@mcp.tool()
async def remove_clip(track_id: str, clip_id: str) -> str:
    """Remove a clip from a track."""
    return await _post("remove_clip", {"track_id": track_id, "clip_id": clip_id})


@mcp.tool()
async def move_clip(track_id: str, clip_id: str, to_index: int) -> str:
    """Move a clip to a new position within its track's clip list (0-based)."""
    return await _post("move_clip", {"track_id": track_id, "clip_id": clip_id, "to_index": to_index})


@mcp.tool()
async def launch_clip(track_id: str, clip_id: str) -> str:
    """Immediately apply a clip's stored params to its track's owned nodes.

    This is the same as queue_clip with quantize='instant'. The active_clip for
    the track is updated to clip_id.
    """
    return await _post("launch_clip", {"track_id": track_id, "clip_id": clip_id})


@mcp.tool()
async def queue_clip(track_id: str, clip_id: str, quantize: str = "instant") -> str:
    """Queue a clip launch at a beat boundary.

    quantize: 'instant' (apply now), 'beat' (next beat), 'bar' (next bar), '4bar' (next 4-bar).
    A new queue_clip for the same track replaces any existing queued launch for that track.
    """
    return await _post("queue_clip", {"track_id": track_id, "clip_id": clip_id, "quantize": quantize})


@mcp.tool()
async def save_scene(name: str) -> str:
    """Capture the current active clips as a new scene.

    Creates a scene whose assignments are the current active_clip for every track
    that has one. Returns the new scene_id.
    """
    return await _post("save_scene", {"name": name})


@mcp.tool()
async def update_scene(scene_id: str) -> str:
    """Re-capture current active clips into an existing scene, replacing its assignments."""
    return await _post("update_scene", {"scene_id": scene_id})


@mcp.tool()
async def rename_scene(scene_id: str, new_name: str) -> str:
    """Rename a scene."""
    return await _post("rename_scene", {"scene_id": scene_id, "new_name": new_name})


@mcp.tool()
async def remove_scene(scene_id: str) -> str:
    """Remove a scene."""
    return await _post("remove_scene", {"scene_id": scene_id})


@mcp.tool()
async def move_scene(scene_id: str, to_index: int) -> str:
    """Move a scene to a new position in the scene list (0-based)."""
    return await _post("move_scene", {"scene_id": scene_id, "to_index": to_index})


@mcp.tool()
async def set_scene_assignment(scene_id: str, track_id: str, clip_id: str) -> str:
    """Set the clip assignment for a track within a scene."""
    return await _post("set_scene_assignment",
                       {"scene_id": scene_id, "track_id": track_id, "clip_id": clip_id})


@mcp.tool()
async def set_scene_leave_unchanged(scene_id: str, track_id: str) -> str:
    """Mark a track as intentionally excluded from a scene (leave unchanged when scene fires)."""
    return await _post("set_scene_leave_unchanged",
                       {"scene_id": scene_id, "track_id": track_id})


@mcp.tool()
async def clear_scene_assignment(scene_id: str, track_id: str) -> str:
    """Remove the clip assignment for a track from a scene (track becomes unassigned)."""
    return await _post("clear_scene_assignment",
                       {"scene_id": scene_id, "track_id": track_id})


@mcp.tool()
async def queue_scene(scene_id: str, quantize: str = "instant") -> str:
    """Queue a scene launch at a beat boundary.

    Fires all track→clip assignments in the scene simultaneously. Tracks marked
    leave_unchanged are skipped. Unassigned tracks are untouched.

    quantize: 'instant' (apply now), 'beat' (next beat), 'bar' (next bar), '4bar' (next 4-bar).
    A new queue_scene replaces any previously queued scene.
    """
    return await _post("queue_scene", {"scene_id": scene_id, "quantize": quantize})


# ---------------------------------------------------------------------------
# get_composition_patterns — curated signal-flow templates. Pure-static data;
# no runtime calls. Paired with diagnose_composition_issue, this closes the
# authoring loop: diagnose tells you what's wrong, patterns tell you what works.
# ---------------------------------------------------------------------------

_COMPOSITION_PATTERNS = [
    {
        "id": "drum-driven-pulse",
        "name": "Drum-driven pulse",
        "intents": ["drum-driven", "percussive"],
        "one_line": "Each drum's peak feeds an envelope follower, which drives a shape's scale on both axes.",
        "when_to_use": (
            "When you want each audible drum hit to produce a clearly visible discrete "
            "visual event. The most legible form of AV for a general audience."
        ),
        "signal_flow": "drum/peak → SmoothFr → Shape2D/scale_x + scale_y",
        "key_operators": [
            {"type": "DrumKick",  "role": "audio source (or DrumSnare, DrumHiHat, or any drum synth)"},
            {"type": "SmoothFr",  "role": "envelope follower",
             "recommended_params": {"rise_time": 0.005, "fall_time": 0.4},
             "factory_preset": "Envelope follower (snappy)"},
            {"type": "Shape2D",   "role": "visual responder (one shape per drum for legibility)"},
        ],
        "example_connections": [
            {"from": "drum/peak",   "to": "smooth/input",       "bridge": "peak"},
            {"from": "smooth/value", "to": "shape/scale_x",
             "from_min": 0.0, "from_max": 0.8, "to_min": 0.05, "to_max": 0.35, "clamp": True},
            {"from": "smooth/value", "to": "shape/scale_y",
             "from_min": 0.0, "from_max": 0.8, "to_min": 0.05, "to_max": 0.35, "clamp": True},
        ],
        "watch_out_for": [
            "Drive BOTH scale_x AND scale_y from the same smoothed value — "
            "driving only scale_x distorts the shape into a line.",
            "Keep to_min >= 0.03 so shapes have baseline presence between hits.",
            "Feedback decay > 0.95 can swallow the pulse entirely.",
            "Smooth rise_time > 0.05 will audibly dull the attack.",
        ],
        "expected_metrics": {
            "onset_response_rate": "> 0.7 for a full drum kit (0.85+ is great)",
            "reactivity_latency_ms": "< 300 (higher suggests Feedback or Smooth fall is too long)",
            "mean_brightness": "0.03–0.2 (higher than 0.2 means the shape is always large)",
            "motion_magnitude": "0.02–0.2",
        },
        "exemplars": ["graphs/intro/showcase_demo.json"],
    },
    {
        "id": "continuous-reactivity",
        "name": "Continuous reactivity",
        "intents": ["continuous", "pad", "ambient"],
        "one_line": "Audio RMS (or another continuous feature) drives a visual parameter directly.",
        "when_to_use": (
            "When audio is sustained (oscillator, pad, drone) rather than percussive. "
            "The coupling is smooth and always-on, producing a feeling of the visual "
            "'breathing with' the sound rather than reacting to discrete events."
        ),
        "signal_flow": "audio → Gain.rms → visual/param (displace.amount, bloom.intensity, shape.scale, etc.)",
        "key_operators": [
            {"type": "Oscillator", "role": "audio source (or any sustained synth)"},
            {"type": "Gain",       "role": "supplies continuous RMS output on the `rms` port"},
            {"type": "Displace",   "role": "visual responder (or Bloom / Feedback / Shape2D)",
             "alternatives": ["Bloom", "Feedback", "Shape2D", "Blur"]},
        ],
        "example_connections": [
            {"from": "osc/output",  "to": "gain/input"},
            {"from": "gain/rms",    "to": "displace/amount",
             "from_min": 0.0, "from_max": 0.5, "to_min": 0.0, "to_max": 0.6, "bridge": "rms"},
        ],
        "watch_out_for": [
            "RMS from a silent audio signal is 0 — make sure the audio source is actually producing sound.",
            "If the coupling drives motion or displacement (not brightness), "
            "check energy_motion_correlation instead of energy_brightness_correlation.",
            "For a more dramatic response, layer TWO reactivity axes (e.g., "
            "rms → displace.amount AND rms → bloom.intensity with different remap ranges).",
        ],
        "expected_metrics": {
            "energy_motion_correlation or energy_brightness_correlation": (
                "> 0.5 depending on which axis the coupling drives"
            ),
            "mean_brightness": "varies with visual source; should clearly change as audio RMS changes",
            "onset_response_rate": "not applicable for continuous audio (onsets will be 0 or few)",
        },
        "exemplars": ["graphs/intro/audio_reactive_demo.json"],
    },
    {
        "id": "parametric-sync",
        "name": "Parametric sync (shared source)",
        "intents": ["parametric", "sync"],
        "one_line": "One LFO or metronome forks to both audio and visual parameters, making them move in lockstep.",
        "when_to_use": (
            "When you want deterministic, phase-locked choreography rather than emergent "
            "audio→visual coupling. Useful for structured compositions where the AV "
            "relationship is 'both sides follow the same conductor'."
        ),
        "signal_flow": "LfoFr or metronome → (osc.frequency OR synth.pitch) AND (shape.pos OR scale OR hue)",
        "key_operators": [
            {"type": "LfoFr",      "role": "shared timing source",
             "recommended_params": {"rate_mode": 2.0, "sync_division": 4.0},
             "notes": "rate_mode=2.0 sync_division=4.0 gives 16th-note sync against the graph metronome"},
            {"type": "Oscillator", "role": "audio responder (LFO drives .frequency)"},
            {"type": "Metaball",   "role": "visual responder (LFO drives .pos_x, or Shape2D.scale_x/y)",
             "alternatives": ["Shape2D", "NoiseTexture"]},
        ],
        "example_connections": [
            {"from": "lfo/value", "to": "osc/frequency",
             "from_min": 0.0, "from_max": 1.0, "to_min": 200.0, "to_max": 800.0, "bridge": "hold"},
            {"from": "lfo/value", "to": "metaball/pos_x",
             "from_min": 0.0, "from_max": 1.0, "to_min": 0.15, "to_max": 0.85},
        ],
        "watch_out_for": [
            "This is NOT audio-reactive — the visual doesn't respond to the audio output, "
            "it responds to the shared source. Pearson correlation and onset response rate "
            "are not the right lens for evaluating this pattern.",
            "If you want both parametric sync AND audio reactivity, layer them: "
            "LFO drives overall motion, audio RMS modulates a secondary parameter (color, size).",
        ],
        "expected_metrics": {
            "reactivity metrics": "Do not interpret per-axis correlation or onset_response_rate here — the coupling isn't measurable through the analyzer.",
            "subjective check": "Audio and visual should feel phase-locked. Is the visual peak sitting exactly on the audio peak?",
        },
        "exemplars": [
            "graphs/intro/av_demo.json",
            "graphs/intro/av_metronome_demo.json",
        ],
    },
    {
        "id": "spectral-color",
        "name": "Spectral color",
        "intents": ["spectral", "spectral-color", "hue"],
        "one_line": "Audio spectral features (bass/treble energy, centroid) drive color or hue parameters.",
        "when_to_use": (
            "When you want frequency content to shape the visual palette — e.g., bass hits "
            "push warm hues, treble hits push cool hues. Adds a third reactivity axis "
            "beyond scale/position."
        ),
        "signal_flow": "audio → FFTAnalysis → Math/Select (reduce to scalar) → Shape/Colormap (hue, r/g/b)",
        "key_operators": [
            {"type": "FFTAnalysis",  "role": "splits audio into 512 frequency bins (lane-array output)"},
            {"type": "Math",         "role": "reduces a range of bins to a single scalar (e.g., bass energy)",
             "notes": "Currently no dedicated SpectralFeatures operator; reduction is manual."},
            {"type": "Colormap",     "role": "maps scalar to color lanes",
             "alternatives": ["Shape2D.r/g/b directly"]},
        ],
        "example_connections": [
            {"from": "osc/output",  "to": "fft/audio_in"},
            {"from": "fft/spectrum", "to": "math/input_0"},
            {"from": "math/output",  "to": "shape/r",
             "from_min": 0.0, "from_max": 0.5, "to_min": 0.2, "to_max": 1.0},
        ],
        "watch_out_for": [
            "No first-class SpectralFeatures operator yet — this pattern is harder than A/B/C. "
            "You must manually tap specific FFT bin ranges via Math/Select operators.",
            "Layer with Pattern A or B for richness — spectral color alone can feel subtle.",
            "Phase 2 roadmap includes a SpectralFeatures operator that will simplify this.",
        ],
        "expected_metrics": {
            "subjective check": "Does the visual color shift as the audio's spectral content shifts?",
            "energy_brightness_correlation": "Less useful here — hue changes don't always change luminance.",
        },
        "exemplars": [],
        "roadmap_note": "The planned `SpectralFeatures` operator (Phase 2 task 2) will reduce the FFT manually-chunking step to a single wire.",
    },
]


def _filter_patterns_by_intent(patterns: list, intent: str) -> list:
    """Return patterns matching the intent; if intent is empty, return all."""
    if not intent:
        return patterns
    intent_l = intent.lower()
    return [p for p in patterns if intent_l in p.get("intents", [])]


@mcp.tool()
async def get_composition_patterns(intent: str = "") -> str:
    """Return curated audio-visual composition patterns as structured templates.

    The inverse of `diagnose_composition_issue`: that tool tells you what's
    wrong with a graph; this tool tells you what a working graph looks like.
    Each pattern gives a high-level summary, signal flow, the key operators
    with recommended parameters, example connection wiring, common gotchas,
    expected metric ranges, and exemplar graph paths.

    Args:
        intent: Optional filter. Accepted values:
            "drum-driven" / "percussive" — rhythmic discrete events
            "continuous" / "pad" / "ambient" — sustained audio, smooth coupling
            "parametric" / "sync" — shared source to both domains (not audio-reactive)
            "spectral" / "spectral-color" / "hue" — frequency content drives color
            "" — return all patterns (default)

    Returns JSON `{ok, intent, patterns: [...]}`. Pair with `load_graph` on
    an exemplar to see the pattern running, or use the `key_operators` +
    `example_connections` to build from scratch with `add_node` + `connect`.

    Tip: after applying a pattern, verify with
    `diagnose_composition_issue(intent="drum-driven")` (or matching intent).
    """
    accepted = {"", "drum-driven", "percussive", "continuous", "pad", "ambient",
                "parametric", "sync", "spectral", "spectral-color", "hue"}
    if intent not in accepted:
        return _json_response({
            "ok": False,
            "error": f"unknown intent '{intent}'; accepted: {sorted(accepted - {''})} or empty",
        })

    patterns = _filter_patterns_by_intent(_COMPOSITION_PATTERNS, intent)
    return _json_response({
        "ok": True,
        "intent": intent,
        "patterns": patterns,
    })


# ---------------------------------------------------------------------------
# explain_graph_composition — reverse of get_composition_patterns.
# Reads a graph JSON, detects which mechanical patterns the graph exhibits,
# returns a structured breakdown. Value-neutral — describes mechanics, does
# not judge aesthetic quality.
# ---------------------------------------------------------------------------

_DRUM_TYPES = {"DrumKick", "DrumSnare", "DrumHiHat", "DrumClap", "DrumCymbal",
               "DrumTom", "DrumKit"}
_LFO_TYPES = {"Lfo", "LfoFr"}
_CLOCK_TYPES = {"Clock", "ClockFr"}
_SHAPE_TYPES = {"Shape2D", "Metaball", "SdfShape"}
_AUDIO_OUT_TYPES = {"audio_out"}
_VIDEO_OUT_TYPES = {"video_out"}


def _index_graph(graph: dict) -> dict:
    """Build quick-lookup indexes for pattern detection."""
    nodes = graph.get("nodes", {}) or {}
    connections = graph.get("connections", []) or []

    # Map node_id -> type, and type -> list of node_ids
    node_type = {nid: ndef.get("type", "") for nid, ndef in nodes.items()}
    type_nodes: dict[str, list[str]] = {}
    for nid, t in node_type.items():
        type_nodes.setdefault(t, []).append(nid)

    # Build outgoing and incoming adjacency by node id
    outgoing: dict[str, list[dict]] = {}   # node_id -> list of connection dicts
    incoming: dict[str, list[dict]] = {}
    for c in connections:
        from_addr = c.get("from", "")
        to_addr = c.get("to", "")
        if "/" not in from_addr or "/" not in to_addr:
            continue
        from_node, _ = from_addr.split("/", 1)
        to_node, _ = to_addr.split("/", 1)
        outgoing.setdefault(from_node, []).append(c)
        incoming.setdefault(to_node, []).append(c)

    return {
        "nodes": nodes,
        "connections": connections,
        "node_type": node_type,
        "type_nodes": type_nodes,
        "outgoing": outgoing,
        "incoming": incoming,
    }


def _port_name(addr: str) -> str:
    return addr.split("/", 1)[1] if "/" in addr else addr


def _detect_drum_driven_pulse(idx: dict) -> dict | None:
    """Match: drum/peak → SmoothFr → Shape2D/scale_* (both axes ideally).

    confidence:
      high — full chain: drum → smooth → shape on both scale axes
      medium — drum → smooth → shape, but only one scale axis driven
      low — drum → shape directly (no smoothing), or smooth present but not on scale
    """
    drum_nodes = [n for n, t in idx["node_type"].items() if t in _DRUM_TYPES]
    if not drum_nodes:
        return None

    matches = []  # list of per-drum-chain details
    best_confidence = "low"

    for drum in drum_nodes:
        # Find peak-sourced connections
        peak_out = [c for c in idx["outgoing"].get(drum, [])
                    if _port_name(c.get("from", "")) == "peak"]
        for pc in peak_out:
            to_node = pc["to"].split("/", 1)[0]
            to_type = idx["node_type"].get(to_node, "")

            # Case A: drum/peak → Smooth*
            if to_type in ("SmoothFr", "Smooth"):
                # Walk forward from smooth
                smooth_out = idx["outgoing"].get(to_node, [])
                scale_axes_driven = set()
                shape_targets = set()
                for sc in smooth_out:
                    dest_node, dest_port = sc["to"].split("/", 1)
                    dest_type = idx["node_type"].get(dest_node, "")
                    if dest_type in _SHAPE_TYPES and dest_port.startswith("scale_"):
                        scale_axes_driven.add(dest_port)
                        shape_targets.add(dest_node)
                if scale_axes_driven:
                    matches.append({
                        "drum": drum,
                        "smoother": to_node,
                        "shape_targets": sorted(shape_targets),
                        "scale_axes": sorted(scale_axes_driven),
                        "chain": f"{drum}/peak → {to_node} → {','.join(sorted(shape_targets))}",
                    })
                    if len(scale_axes_driven) >= 2:
                        best_confidence = "high"
                    elif best_confidence == "low":
                        best_confidence = "medium"

            # Case B: drum/peak → Shape.scale_* directly (no envelope — the anti-pattern)
            elif to_type in _SHAPE_TYPES and _port_name(pc["to"]).startswith("scale_"):
                matches.append({
                    "drum": drum,
                    "smoother": None,
                    "shape_targets": [to_node],
                    "scale_axes": [_port_name(pc["to"])],
                    "chain": f"{drum}/peak → {to_node} (no envelope — peak flashes are sub-frame)",
                    "warning": "peak wired directly to shape scale; insert SmoothFr for sustained pulse",
                })

    if not matches:
        return None

    return {
        "pattern_id": "drum-driven-pulse",
        "confidence": best_confidence,
        "chains": matches,
    }


def _detect_continuous_reactivity(idx: dict) -> dict | None:
    """Match: Gain.rms → some visual parameter.

    confidence:
      high — gain/rms → GPU visual param (displace, bloom, shape, etc.)
      medium — gain/rms → something, but target type isn't a known visual op
      low — (not reported)
    """
    gain_nodes = [n for n, t in idx["node_type"].items() if t == "Gain"]
    if not gain_nodes:
        return None

    matches = []
    best_confidence = "low"
    for gain in gain_nodes:
        rms_out = [c for c in idx["outgoing"].get(gain, [])
                   if _port_name(c.get("from", "")) == "rms"]
        for c in rms_out:
            dest_node, dest_port = c["to"].split("/", 1)
            dest_type = idx["node_type"].get(dest_node, "")
            # Known visual targets
            is_visual = dest_type in _SHAPE_TYPES or dest_type in {
                "Displace", "Bloom", "Feedback", "Blur", "NoiseTexture",
                "Composite", "Fluid", "ReactionDiffusion"
            }
            matches.append({
                "gain": gain,
                "target": c["to"],
                "target_type": dest_type,
                "chain": f"{gain}/rms → {c['to']}",
            })
            if is_visual:
                best_confidence = "high"
            elif best_confidence == "low":
                best_confidence = "medium"

    if not matches:
        return None
    return {
        "pattern_id": "continuous-reactivity",
        "confidence": best_confidence,
        "chains": matches,
    }


def _detect_parametric_sync(idx: dict) -> dict | None:
    """Match: one LFO/Clock forks to BOTH audio and visual parameter targets.

    confidence:
      high — single source reaches at least one audio-domain target AND one visual-domain target
      medium — source reaches multiple targets, ambiguity on domain split
      low — (not reported)
    """
    candidates = [n for n, t in idx["node_type"].items() if t in _LFO_TYPES | _CLOCK_TYPES]
    if not candidates:
        return None

    # Classify target types as audio-side or visual-side
    audio_side_types = {"Oscillator", "Gain", "Filter", "DualFilter", "Reverb",
                        "Delay", "PingPongDelay", "Chorus", "Flanger", "Phaser",
                        "Distortion", "BitCrush", "Compressor", "Limiter",
                        "RingMod", "Vocoder", "ParametricEQ", "Slicer",
                        "DrumKick", "DrumSnare", "DrumHiHat"}
    visual_side_types = (_SHAPE_TYPES | {
        "Displace", "Bloom", "Feedback", "Blur", "NoiseTexture",
        "Composite", "Fluid", "ReactionDiffusion", "Particles2D",
        "Flocking2D", "MeshWarp", "Instancer2D", "Trails"
    })

    matches = []
    best_confidence = "low"
    for src in candidates:
        audio_targets = []
        visual_targets = []
        for c in idx["outgoing"].get(src, []):
            dest_node = c["to"].split("/", 1)[0]
            dest_type = idx["node_type"].get(dest_node, "")
            if dest_type in audio_side_types:
                audio_targets.append(c["to"])
            elif dest_type in visual_side_types:
                visual_targets.append(c["to"])
        if audio_targets and visual_targets:
            matches.append({
                "source": src,
                "audio_targets": audio_targets,
                "visual_targets": visual_targets,
                "chain": f"{src} → audio: {audio_targets} AND visual: {visual_targets}",
            })
            best_confidence = "high"

    if not matches:
        return None
    return {
        "pattern_id": "parametric-sync",
        "confidence": best_confidence,
        "chains": matches,
    }


def _detect_spectral_color(idx: dict) -> dict | None:
    """Match: FFTAnalysis → (eventually) a color/hue parameter.

    confidence:
      high — FFT output is reduced (Math/Select) and feeds r/g/b/hue/color on a shape
      medium — FFT output goes somewhere but not clearly color-bound
      low — FFT node present but no downstream
    """
    fft_nodes = [n for n, t in idx["node_type"].items() if t == "FFTAnalysis"]
    if not fft_nodes:
        return None

    color_params = {"r", "g", "b", "hue", "saturation", "color"}
    matches = []
    best_confidence = "low"
    for fft in fft_nodes:
        direct_out = idx["outgoing"].get(fft, [])
        if not direct_out:
            continue
        # Trace downstream one or two hops for color targets
        for c in direct_out:
            dest_node, dest_port = c["to"].split("/", 1)
            if dest_port in color_params:
                matches.append({"fft": fft, "target": c["to"], "hops": 1,
                                "chain": f"{fft} → {c['to']}"})
                best_confidence = "high"
                continue
            # One more hop
            for cc in idx["outgoing"].get(dest_node, []):
                hop2_node, hop2_port = cc["to"].split("/", 1)
                if hop2_port in color_params:
                    matches.append({"fft": fft, "target": cc["to"], "hops": 2,
                                    "chain": f"{fft} → {dest_node} → {cc['to']}"})
                    best_confidence = "high"
        if not matches and direct_out:
            matches.append({"fft": fft, "target": direct_out[0]["to"], "hops": 1,
                            "chain": f"{fft} → {direct_out[0]['to']} (not color-bound)"})
            if best_confidence == "low":
                best_confidence = "medium"

    if not matches:
        return None
    return {
        "pattern_id": "spectral-color",
        "confidence": best_confidence,
        "chains": matches,
    }


def _summarize_reactivity(idx: dict) -> dict:
    """Lightweight structural summary — node counts by domain, audio/video out presence."""
    types = idx["type_nodes"]
    has_audio_out = any(t in _AUDIO_OUT_TYPES for t in types)
    has_video_out = any(t in _VIDEO_OUT_TYPES for t in types)
    has_drums = any(t in _DRUM_TYPES for t in types)
    has_lfo = any(t in _LFO_TYPES for t in types)
    has_fft = "FFTAnalysis" in types
    has_smooth = "SmoothFr" in types or "Smooth" in types
    return {
        "has_audio_out": has_audio_out,
        "has_video_out": has_video_out,
        "has_drums": has_drums,
        "has_lfo_or_clock": has_lfo or any(t in _CLOCK_TYPES for t in types),
        "has_fft_analysis": has_fft,
        "has_smoother": has_smooth,
        "node_count": len(idx["node_type"]),
        "connection_count": len(idx["connections"]),
    }


def _compose_graph_explanation(graph: dict) -> dict:
    """Return the structured breakdown — patterns detected + a summary."""
    idx = _index_graph(graph)
    detected = []
    for detector in (_detect_drum_driven_pulse, _detect_continuous_reactivity,
                     _detect_parametric_sync, _detect_spectral_color):
        result = detector(idx)
        if result is not None:
            detected.append(result)

    return {
        "meta": graph.get("meta", {}),
        "summary": _summarize_reactivity(idx),
        "patterns_detected": detected,
        "notes": ([
            "This is a value-neutral description — it reports what the graph IS, "
            "not whether it's compelling. Use `get_composition_patterns` to compare "
            "against reference templates; use `diagnose_composition_issue` to check "
            "for mechanical problems (near-black output, dead motion, etc.)."
        ]),
    }


@mcp.tool()
async def explain_graph_composition(graph_path: str) -> str:
    """Read a graph JSON file and return a structured breakdown of its composition.

    Value-neutral: describes which mechanical patterns the graph exhibits
    (drum-driven pulse, continuous reactivity, parametric sync, spectral color)
    with confidence levels, plus a structural summary. Does NOT judge aesthetic
    quality — that's the user's domain.

    Args:
        graph_path: Absolute path to a Vivid graph JSON file.

    Returns JSON `{ok, meta, summary, patterns_detected, notes}`. Each detected
    pattern has `pattern_id`, `confidence` (high/medium/low), and `chains` —
    the specific node paths where the pattern appears.

    Use cases:
    - Understanding an unfamiliar graph ("what does this graph do?")
    - Auditing a graph for the patterns from `get_composition_patterns`
    - Scaffolding annotations for a reference corpus
    """
    path = os.path.abspath(graph_path)
    if not os.path.exists(path):
        return _json_response({"ok": False, "error": f"graph file not found: {path}"})
    try:
        with open(path, "r") as f:
            graph = json.load(f)
    except json.JSONDecodeError as exc:
        return _json_response({
            "ok": False,
            "error": f"could not parse graph JSON: {exc}",
        })
    except OSError as exc:
        return _json_response({
            "ok": False,
            "error": f"could not read graph file: {exc}",
        })

    explanation = _compose_graph_explanation(graph)
    return _json_response({
        "ok": True,
        "graph_path": path,
        **explanation,
    })


# ---------------------------------------------------------------------------
# list_reference_graphs — browse the curated graph set as a value-neutral
# catalog. No quality ranking; each entry reports auto-detected mechanical
# patterns + the graph's own metadata. Intended use: Claude browses by
# pattern when translating a reference, or by tag when looking for a
# starting-point graph that matches the user's precedent.
# ---------------------------------------------------------------------------


def _graphs_root() -> pathlib.Path:
    """Resolve the repository-root graphs/ directory.

    vivid_mcp.py lives at mcp/vivid_mcp.py, so graphs/ is one level up from there.
    """
    return pathlib.Path(__file__).resolve().parent.parent / "graphs"


def _load_reference_graph(path: pathlib.Path) -> dict | None:
    """Parse a graph JSON file and return its summary entry, or None on failure."""
    try:
        with open(path, "r") as f:
            graph = json.load(f)
    except (OSError, json.JSONDecodeError):
        return None
    explanation = _compose_graph_explanation(graph)
    meta = graph.get("meta", {}) or {}
    return {
        "path": str(path),
        "subdir": path.parent.name,
        "meta": {
            "id": meta.get("id", ""),
            "title": meta.get("title", ""),
            "description": meta.get("description", ""),
            "tags": meta.get("tags", []),
            "difficulty": meta.get("difficulty", ""),
            "domains": meta.get("domains", []),
            "featured_rank": meta.get("featured_rank", 0),
            "requires_packages": meta.get("requires_packages", []),
        },
        "summary": explanation["summary"],
        "patterns_detected": [
            {"pattern_id": p["pattern_id"], "confidence": p["confidence"]}
            for p in explanation["patterns_detected"]
        ],
    }


@mcp.tool()
async def list_reference_graphs(pattern_filter: str = "",
                                subdir_filter: str = "",
                                tag_filter: str = "",
                                include_packages: bool = False) -> str:
    """Browse the curated graph catalog with auto-detected mechanical patterns.

    Walks `graphs/` under the repo root and returns each graph's metadata
    plus the patterns `explain_graph_composition` detects. No quality
    ranking — this is a value-neutral catalog, not a "best graphs" list.
    Use it to find starting points when translating a reference: browse by
    pattern ("show me drum-driven graphs"), by subdir ("show me intro
    graphs"), or by tag ("show me graphs tagged 'feedback'").

    Args:
        pattern_filter: Only include graphs that exhibit this pattern_id
            (drum-driven-pulse / continuous-reactivity / parametric-sync /
            spectral-color). Empty = no pattern filter.
        subdir_filter: Only include graphs under graphs/<subdir>/.
            Useful values: "intro", "audio", "gpu", "filters", "io", "media".
            Empty = all subdirs.
        tag_filter: Only include graphs whose meta.tags contains this tag.
            Empty = no tag filter.
        include_packages: If False (default), skip graphs that require
            non-core packages (meta.requires_packages is non-empty). These
            graphs may fail to load on a fresh install.

    Returns JSON `{ok, total, graphs: [...]}`. Each graph entry has
    {path, subdir, meta, summary, patterns_detected}.

    Typical flow:
        1. fetch_reference(url) to ingest the user's precedent.
        2. analyze_image(thumbnail_local_path) to extract style descriptors.
        3. list_reference_graphs(pattern_filter=<the pattern you want to try>)
           to see exemplars.
        4. load_graph on an exemplar, or use its wiring as a starting template.
    """
    root = _graphs_root()
    if not root.exists():
        return _json_response({
            "ok": False,
            "error": f"graphs/ directory not found at {root}",
        })

    graphs: list[dict] = []
    for graph_path in sorted(root.rglob("*.json")):
        # Skip files we don't recognize as graphs (e.g., meta manifests)
        if graph_path.name in ("README.md", "package.json"):
            continue
        entry = _load_reference_graph(graph_path)
        if entry is None:
            continue

        # Apply filters
        if subdir_filter and entry["subdir"] != subdir_filter:
            continue
        if not include_packages and entry["meta"]["requires_packages"]:
            continue
        if tag_filter and tag_filter not in (entry["meta"]["tags"] or []):
            continue
        if pattern_filter:
            pat_ids = [p["pattern_id"] for p in entry["patterns_detected"]]
            if pattern_filter not in pat_ids:
                continue

        graphs.append(entry)

    # Sort: featured graphs first (by rank), then by title
    graphs.sort(key=lambda g: (
        g["meta"]["featured_rank"] if g["meta"]["featured_rank"] > 0 else 999,
        g["meta"]["title"] or g["path"],
    ))

    return _json_response({
        "ok": True,
        "total": len(graphs),
        "graphs": graphs,
    })


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


# ---- Editor-window LLM-transparency tools ----------------------------------
#
# These five tools expose the editor-window test-support surface that
# EditorWindowManager already implemented for headed test harnesses. They let
# LLM workflows open / close / drive / inspect native operator editor windows
# programmatically, so the C++-painted surface doesn't become an opaque wall
# in an automated pipeline. All five endpoints return a useful error if the
# control server isn't wired to an EditorWindowManager instance.


@mcp.tool()
async def open_editor(node_id: str) -> str:
    """Open the editor window for the given node (or refocus if already open).

    Works for any operator that declares a VIVID_EDITOR; a no-editor operator
    returns ok=false with a "no editor available" message.

    Args:
        node_id: Target node id (must exist in the live graph).
    """
    return await _post("open_editor", {"node_id": node_id})


@mcp.tool()
async def close_editor(node_id: str) -> str:
    """Close the editor window for the given node. No-op if not open."""
    return await _post("close_editor", {"node_id": node_id})


@mcp.tool()
async def is_editor_open(node_id: str) -> str:
    """Return {ok:true, open:bool} for whether the editor window is live."""
    return await _post("is_editor_open", {"node_id": node_id})


@mcp.tool()
async def editor_inject_event(
    node_id: str,
    type: int,
    x: float = 0.0,
    y: float = 0.0,
    button: int = 0,
    action: int = 0,
    scroll_dx: float = 0.0,
    scroll_dy: float = 0.0,
    key: int = 0,
    scancode: int = 0,
    codepoint: int = 0,
    modifiers: int = 0,
) -> str:
    """Inject a synthetic input event into an editor window.

    Event type codes (match VividEditorEventType in src/operator_api/types.h):
      0 = MOUSE_MOVE    — uses (x, y) in editor-window-local pixels.
      1 = MOUSE_BUTTON  — uses (x, y, button, action, modifiers);
                          button: 0=left, 1=right, 2=middle;
                          action: 0=release, 1=press, 2=repeat.
      2 = MOUSE_SCROLL  — uses (x, y, scroll_dx, scroll_dy).
      3 = KEY           — uses (key, scancode, action, modifiers); key is a
                          GLFW key code (see include/GLFW/glfw3.h).
      4 = CHAR          — uses (codepoint) — Unicode code point for text input.

    Useful for driving editor flows from an LLM: e.g. click a step grid cell
    with (type=1, x, y, button=0, action=1) followed by (action=0).

    Args:
        node_id: Target editor's node id.
        type: Event type (see above).
        x, y: Editor-window-local pixel coordinates.
        button, action, modifiers: Mouse / key state as above.
        scroll_dx, scroll_dy: Scroll deltas.
        key, scancode: GLFW key event fields.
        codepoint: Unicode code point for CHAR events.
    """
    body = {
        "node_id":   node_id,
        "type":      int(type),
        "x":         float(x),
        "y":         float(y),
        "button":    int(button),
        "action":    int(action),
        "scroll_dx": float(scroll_dx),
        "scroll_dy": float(scroll_dy),
        "key":       int(key),
        "scancode":  int(scancode),
        "codepoint": int(codepoint),
        "modifiers": int(modifiers),
    }
    return await _post("editor_inject_event", body)


@mcp.tool()
async def capture_editor(node_id: str, save_path: str = "") -> str:
    """Capture the editor window's current rendered surface as PNG.

    Returns {ok, width, height, png_base64}. If save_path is provided (absolute
    path ending in .png), also writes the decoded PNG to that path on the
    runtime machine so an LLM caller can Read it directly. Response shape
    matches capture_interface.

    Args:
        node_id: Target editor's node id.
        save_path: Optional absolute PNG path to also write on the runtime machine.
    """
    body: dict = {"node_id": node_id}
    if save_path:
        body["save_path"] = save_path
    return await _post("capture_editor", body)


@mcp.tool()
async def inspect_editor(node_id: str) -> str:
    """Return a structured JSON tree of widgets currently drawn by the editor.

    Each entry carries bounds (x/y/w/h in editor-window-local pixels) plus
    widget-specific state: slider value/range, step_grid rows/cols/anchor,
    text_field contents, toggle state, etc. Use this to target
    editor_inject_event clicks without OCR'ing the captured PNG.

    Response: {ok, node_id, widgets: [...]}. Widget shapes:
      button:     {kind, x, y, w, h, label, flags}
      toggle:     {kind, x, y, w, h, label, flags: [..."value_true"]}
      radio:      {kind, x, y, w, h, cols, int_value, label}
      slider_h:   {kind, x, y, w, h, label, value, value_lo, value_hi}
      slider_v:   {kind, x, y, w, h, value, value_lo, value_hi}
      step_grid:  {kind, x, y, w, h, rows, cols, anchor_row, anchor_col, active_cols}
      drag_handle:{kind, x, y, w, h}
      text_field: {kind, x, y, w, h, text, placeholder, int_value (cursor), flags}

    Args:
        node_id: Target editor's node id.
    """
    return await _post("inspect_editor", {"node_id": node_id})


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


# ---------------------------------------------------------------------------
# Audio debug tools (librosa-backed).
#
# These wrappers fetch raw audio (WAV bytes) from the C++ runtime via three
# minimal data endpoints — capture_audio, capture_node_audio,
# capture_lane_series, capture_note_window — then run all DSP, feature
# extraction, and plot rendering Python-side via mcp/audio_analysis.py
# (librosa + matplotlib + soundfile). The C++ runtime no longer carries any
# of the analysis or rendering code.
# ---------------------------------------------------------------------------

# Lazy import so that callers who never touch audio tools don't pay the
# librosa/numba cold-start cost. Once loaded, librosa stays warm.
_audio_analysis_mod = None

def _audio_analysis():
    global _audio_analysis_mod
    if _audio_analysis_mod is None:
        # Sibling file `audio_analysis.py` next to this module. We can't
        # `from mcp import audio_analysis` because `mcp` already resolves to
        # the installed FastMCP PyPI package, not this directory.
        import importlib.util
        _path = pathlib.Path(__file__).resolve().parent / "audio_analysis.py"
        _spec = importlib.util.spec_from_file_location("vivid_audio_analysis", _path)
        _aa = importlib.util.module_from_spec(_spec)
        _spec.loader.exec_module(_aa)
        _audio_analysis_mod = _aa
    return _audio_analysis_mod


_visual_style_tags_mod = None

def _visual_style_tags():
    global _visual_style_tags_mod
    if _visual_style_tags_mod is None:
        # Same pattern as _audio_analysis() — can't use package-relative import
        # because `mcp` resolves to the installed FastMCP package.
        # visual/style_tags.py has no relative imports so bare exec works cleanly.
        import importlib.util
        _path = pathlib.Path(__file__).resolve().parent / "visual" / "style_tags.py"
        _spec = importlib.util.spec_from_file_location("vivid_visual_style_tags", _path)
        _vst = importlib.util.module_from_spec(_spec)
        _spec.loader.exec_module(_vst)
        _visual_style_tags_mod = _vst
    return _visual_style_tags_mod


_recording_tap_started = False


async def _ensure_recording_tap(duration_ms: float) -> None:
    """Start the runtime's recording tap on first use and let it fill.

    The tap is a global ring fed by the audio thread. `capture_audio` only
    pops what's already available — if the tap was never started we get
    "no audio samples available". Start once per Python-server lifetime
    and wait long enough for `duration_ms` worth of samples to land.
    """
    global _recording_tap_started
    if _recording_tap_started:
        return
    await _post("start_recording_tap", {})
    _recording_tap_started = True
    await asyncio.sleep(max(0.05, duration_ms / 1000.0) + 0.1)


async def _fetch_wav_bytes(node_id: str, duration_ms: float):
    """Returns (samples_ndarray, sample_rate) or (None, error_string).

    When `node_id` is provided, snapshots that audio node's 1024-sample
    waveform ring (~21ms @ 48kHz; duration_ms is ignored for per-node).
    Otherwise, captures `duration_ms` of the final mix via the recording
    tap. Both paths return WAV bytes that we decode via soundfile.
    """
    async def _do_capture():
        if node_id:
            return await _post("capture_node_audio", {"node_id": node_id})
        return await _post("capture_audio",
                           {"duration": float(duration_ms) / 1000.0},
                           timeout=max(10.0, duration_ms / 1000.0 + 6.0))

    if not node_id:
        await _ensure_recording_tap(duration_ms)
    raw = await _do_capture()
    try:
        doc = json.loads(raw)
    except json.JSONDecodeError as exc:
        return None, f"non-JSON response: {exc}"
    # If the tap was active in a previous run but reset, retry once after
    # explicitly (re-)starting it.
    if not doc.get("ok", False) and not node_id and "recording tap" in doc.get("error", ""):
        global _recording_tap_started
        _recording_tap_started = False
        await _ensure_recording_tap(duration_ms)
        raw = await _do_capture()
        try:
            doc = json.loads(raw)
        except json.JSONDecodeError as exc:
            return None, f"non-JSON response: {exc}"
    if not doc.get("ok", False):
        return None, doc.get("error", "capture failed")
    b64 = doc.get("wav_base64", "")
    if not b64:
        return None, "no wav_base64 in response"
    try:
        samples, sr = _audio_analysis().load_wav_bytes(b64)
        return (samples, sr), None
    except Exception as exc:  # noqa: BLE001
        return None, f"failed to decode WAV: {exc}"


def _validate_live_music_eval_request(node_id: str, window_seconds: float) -> dict | None:
    """Return an error envelope when a live music-eval request is unsupported."""
    if window_seconds <= 0:
        return {"ok": False, "error": {
            "code": "invalid_window",
            "message": "window_seconds must be > 0",
        }}
    if window_seconds > _LIVE_MUSIC_EVAL_MAX_WINDOW_SEC:
        return {"ok": False, "error": {
            "code": "invalid_window",
            "message": (
                f"live music evaluation supports up to {_LIVE_MUSIC_EVAL_MAX_WINDOW_SEC:.0f}s. "
                "Use evaluate_audio_file(...) for longer material."
            ),
        }}
    if node_id:
        return {"ok": False, "error": {
            "code": "unsupported_feature",
            "message": (
                "node_id-scoped live music evaluation is not supported yet. "
                "Use final-mix capture or evaluate a rendered file instead."
            ),
        }}
    return None


async def _fetch_audio_for_music_eval(node_id: str, window_seconds: float) -> tuple[dict, str | None]:
    """Capture audio and return a body dict ready to merge into a music_eval request.

    Live music eval uses the final mix recording tap and supports windows up to 60s.
    """
    import base64 as _base64
    import io as _io
    import soundfile as _sf

    if window_seconds > _LIVE_MUSIC_EVAL_MAX_WINDOW_SEC:
        return {}, (
            f"live music evaluation supports up to {_LIVE_MUSIC_EVAL_MAX_WINDOW_SEC:.0f}s; "
            "use evaluate_audio_file(...) for longer material"
        )

    fetched, err = await _fetch_wav_bytes(node_id, window_seconds * 1000.0)
    if err:
        return {}, err
    samples, sr = fetched
    channels = int(samples.shape[1]) if samples.ndim > 1 else 1

    try:
        buf = _io.BytesIO()
        _sf.write(buf, samples, int(sr), format="WAV", subtype="FLOAT")
        b64 = _base64.b64encode(buf.getvalue()).decode("ascii")
    except Exception as exc:  # noqa: BLE001
        return {}, f"failed to encode audio: {exc}"
    return {"audio_b64": b64, "sample_rate": int(sr), "channels": channels}, None


def _write_png_to_disk(png_bytes: bytes, save_dir: str, prefix: str) -> str:
    save_root = _resolve_save_dir(save_dir)
    ts = _time_module.strftime("%Y%m%d_%H%M%S_%f")
    out_path = save_root / f"{prefix}_{ts}.png"
    out_path.write_bytes(png_bytes)
    return str(out_path)


@mcp.tool()
async def capture_waveform_plot(duration_ms: float = 1000.0,
                                width: int = 720,
                                height: int = 200,
                                channel: int = -1,
                                save_dir: str = "",
                                node_id: str = "") -> str:
    """Render the live audio output as a time-domain waveform PNG.

    With `node_id`, snapshots that audio node's 1024-sample ring (~21 ms);
    otherwise captures `duration_ms` of the final mix. PNG is written to
    `save_dir` (or a temp folder); the returned `capture_path` is what
    you Read to see the image.

    Args:
        duration_ms: Length of the captured window (50 - 10000). Ignored
            for per-node captures (ring is fixed 1024 samples).
        width, height: Plot pixel dimensions.
        channel: -1 stacks all channels; otherwise the 0-indexed channel
            to isolate. Per-node captures honour this; final-mix captures
            currently always include all channels (mono mix happens in
            librosa as needed).
        save_dir: Optional output directory.
        node_id: If set, capture from this audio node's waveform ring.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    if channel >= 0 and samples.ndim > 1 and channel < samples.shape[1]:
        samples = samples[:, channel]
    png = _audio_analysis().render_waveform_png(samples, sr,
                                                  width=int(width),
                                                  height=int(height))
    path = _write_png_to_disk(png, save_dir, "vivid_waveform")
    return _json_response({
        "ok": True, "capture_path": path,
        "kind": "waveform", "sample_rate": int(sr),
        "frames": int(samples.shape[0] if samples.ndim > 0 else 0),
        "channels": int(samples.shape[1]) if samples.ndim > 1 else 1,
        "node_id": node_id,
        "hint": "Read capture_path as an image to see the audio.",
    })


@mcp.tool()
async def capture_spectrogram(duration_ms: float = 1000.0,
                              width: int = 720,
                              height: int = 240,
                              fmin_hz: float = 0.0,
                              fmax_hz: float = 0.0,
                              save_dir: str = "",
                              node_id: str = "") -> str:
    """Render the live audio output as a magnitude spectrogram PNG.

    STFT with Hann window and 75% overlap, dB-scaled magnitude on a
    viridis colormap. Time on x, linear frequency on y from `fmin_hz`
    (default 0) to `fmax_hz` (default Nyquist).

    Args:
        duration_ms: Length of the captured window (50 - 10000). Ignored
            for per-node captures.
        width, height: Plot pixel dimensions.
        fmin_hz, fmax_hz: Visible frequency band. fmax_hz=0 picks Nyquist.
        save_dir: Optional output directory.
        node_id: If set, capture from this audio node's waveform ring.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    fmax = float(fmax_hz) if fmax_hz > 0 else None
    png = _audio_analysis().render_spectrogram_png(samples, sr,
                                                     fmin=float(fmin_hz),
                                                     fmax=fmax,
                                                     width=int(width),
                                                     height=int(height))
    path = _write_png_to_disk(png, save_dir, "vivid_spectrogram")
    return _json_response({
        "ok": True, "capture_path": path,
        "kind": "spectrogram", "sample_rate": int(sr),
        "fmin_hz": float(fmin_hz),
        "fmax_hz": float(fmax_hz) if fmax_hz > 0 else float(sr) / 2.0,
        "node_id": node_id,
        "hint": "Read capture_path as an image to see the spectrum.",
    })


@mcp.tool()
async def capture_envelope_plot(duration_ms: float = 1000.0,
                                width: int = 720,
                                height: int = 160,
                                channel: int = -1,
                                save_dir: str = "",
                                node_id: str = "") -> str:
    """Render the live audio output as a per-channel RMS envelope PNG.

    Each x column is the windowed RMS of its time slice; channels are
    overlaid with different colors. Useful for ADSR shape verification.

    Args:
        duration_ms: Length of the captured window (50 - 10000).
        width, height: Plot pixel dimensions.
        channel: -1 for all channels overlaid; 0-indexed for one channel.
        save_dir: Optional output directory.
        node_id: If set, capture from this audio node's waveform ring.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    if channel >= 0 and samples.ndim > 1 and channel < samples.shape[1]:
        samples = samples[:, channel]
    png = _audio_analysis().render_envelope_png(samples, sr,
                                                  width=int(width),
                                                  height=int(height))
    path = _write_png_to_disk(png, save_dir, "vivid_envelope")
    return _json_response({
        "ok": True, "capture_path": path,
        "kind": "envelope", "sample_rate": int(sr),
        "node_id": node_id,
        "hint": "Read capture_path as an image to see the envelope.",
    })


@mcp.tool()
async def capture_voice_lane_strip(node_id: str,
                                    port_name: str,
                                    duration_ms: float = 500.0,
                                    width: int = 720,
                                    per_lane_height: int = 32,
                                    save_dir: str = "",
                                    id_port_name: str = "") -> str:
    """Render a node's lane-array output as a stacked per-lane PNG over time.

    Samples the named output port every frame for `duration_ms`, builds
    one horizontal strip per lane (lane 0 on top), writes the PNG to disk.
    Generic — no synth-specific assumptions.

    When `id_port_name` is provided (e.g. `lane_id` for a poly synth),
    each row is colored by a hashed function of its identity. Otherwise,
    rows alternate between two colors.

    Args:
        node_id: Audio-cadence node id.
        port_name: Output port name (must be a lane-array port).
        duration_ms: Sample window length in ms.
        width: PNG width.
        per_lane_height: Height per lane row in pixels.
        save_dir: Optional output directory.
        id_port_name: Optional second port carrying per-lane identity.
            If set, rows are colored by stable id (golden-angle HSV hash).
    """
    body = {
        "node_id": node_id,
        "port_name": port_name,
        "duration_ms": float(duration_ms),
    }
    if id_port_name:
        body["id_port_name"] = id_port_name
    raw = await _post("capture_lane_series", body,
                      timeout=max(10.0, duration_ms / 1000.0 + 6.0))
    try:
        doc = json.loads(raw)
    except json.JSONDecodeError as exc:
        return _json_response({"ok": False, "error": f"non-JSON: {exc}"})
    if not doc.get("ok", False):
        return _json_response({"ok": False, "error": doc.get("error")})

    lanes = doc.get("samples", [])
    ids = doc.get("ids") if id_port_name else None
    png = _audio_analysis().render_lane_strip_png(
        lanes, ids=ids, width=int(width), lane_height=int(per_lane_height))
    path = _write_png_to_disk(png, save_dir, "vivid_lane_strip")
    return _json_response({
        "ok": True, "capture_path": path,
        "node_id": doc.get("node_id"),
        "port_name": doc.get("port_name"),
        "lane_count": doc.get("lane_count"),
        "samples_per_lane": doc.get("samples_per_lane"),
        "duration_ms": doc.get("duration_ms"),
        "hint": ("Read capture_path. Each row = one lane. Compare row N's "
                 "values with what lane N is supposed to carry."),
    })


@mcp.tool()
async def analyze_audio_detail(duration_ms: float = 1000.0,
                               hop_ms: float = 50.0,
                               pitch_track: bool = True,
                               band_energies: bool = True,
                               onset_times: bool = True,
                               scalar_summary: bool = True,
                               node_id: str = "") -> str:
    """Time-series audio analysis over a captured window.

    Returns JSON with whichever of these arrays are requested:
      - `pitch_track`: list of {t, hz, conf} via librosa.pyin
      - `band_energies`: list of {t, bass, mid, treble} via STFT bandsums
      - `onset_times`: list of seconds via librosa.onset.onset_detect
      - `scalar`: window-averaged values

    Args:
        duration_ms: Length of captured window (50 - 10000). Ignored
            when `node_id` is set (per-node ring is fixed at 1024
            samples ≈ 21 ms @ 48 kHz).
        hop_ms: Hop interval for time-series points (5 - 500).
        pitch_track / band_energies / onset_times / scalar_summary: per-array
            opt-out flags. Note: per-node captures may be too short for
            pitch_track (needs ≥ 2048 samples) — the field is omitted
            in that case rather than returned partial.
        node_id: If set, snapshot the named audio node's 1024-sample
            waveform ring instead of the final mix.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    detail = _audio_analysis().analyze_detail(
        samples, sr, hop_ms=float(hop_ms),
        want_pitch_track=bool(pitch_track),
        want_band_energies=bool(band_energies),
        want_onset_times=bool(onset_times),
        want_scalar_summary=bool(scalar_summary))
    detail["ok"] = True
    detail["node_id"] = node_id
    detail["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(detail)


@mcp.tool()
async def aggregate_audio_scalars(duration_ms_per_capture: float = 1000.0,
                                    n_captures: int = 5,
                                    inter_capture_ms: float = 200.0,
                                    node_id: str = "") -> str:
    """Run analyze_scalars across N captures and report per-metric statistics.

    Stochastic / LFO-driven graphs produce different metrics every
    capture. This tool drives N captures with a configurable inter-
    capture pause (so LFOs cycle and the recording tap drains), runs
    `analyze_scalars` on each, and reports `{mean, std, min, max, n}`
    per metric — turning flaky single-capture A/B comparisons into
    statistically meaningful ones.

    Total wall time ≈ `n_captures × (duration_ms_per_capture + inter_capture_ms)`.
    Soft-capped at `n_captures ≤ 20` and `duration_ms_per_capture ≤ 5000`
    to keep runs bounded.

    Args:
        duration_ms_per_capture: Capture length per run (50 - 5000).
        n_captures: Number of captures (1 - 20).
        inter_capture_ms: Pause between captures (≥ 0). 200 ms default
            lets short LFOs cycle and the recording tap drain.
        node_id: Audio node id, or empty for the final mix.
    """
    n = max(1, min(20, int(n_captures)))
    dur = max(50.0, min(5000.0, float(duration_ms_per_capture)))
    delay_s = max(0.0, float(inter_capture_ms) / 1000.0)
    aa = _audio_analysis()
    captures: list = []
    errors: list = []
    for i in range(n):
        if i > 0 and delay_s > 0:
            await asyncio.sleep(delay_s)
        fetched, err = await _fetch_wav_bytes(node_id, dur)
        if err:
            errors.append({"capture_index": i, "error": err})
            continue
        samples, sr = fetched
        try:
            captures.append(aa.analyze_scalars(samples, sr))
        except Exception as exc:  # noqa: BLE001
            errors.append({"capture_index": i, "error": f"analyze: {exc}"})
            continue
    agg = aa.aggregate_scalar_runs(captures)
    return _json_response({
        "ok": True,
        "n_captures_requested": n,
        "n_captures_succeeded": agg["n_runs"],
        "duration_ms_per_capture": dur,
        "inter_capture_ms": float(inter_capture_ms),
        "node_id": node_id,
        "source": "node_waveform_ring" if node_id else "final_mix",
        "captures": captures,
        "stats": agg["stats"],
        "errors": errors,
    })


@mcp.tool()
async def dump_audio_samples(duration_ms: float = 200.0,
                              node_id: str = "",
                              channel: int = -1) -> str:
    """Capture a short window of audio and return raw float32 samples.

    Returns base64-encoded little-endian float32, one string per channel
    in `samples_b64`. Decode in the client:
        np.frombuffer(base64.b64decode(samples_b64[0]), dtype="<f4")

    Use this when scalar metrics or rendered plots aren't enough — e.g.
    to compute autocorrelation yourself, locate sample-exact glitches,
    or feed the buffer into another DSP tool. For longer windows use
    `record_audio_to_wav` (writes to disk).

    Args:
        duration_ms: Capture window length (10 - 500). Soft-capped at
            500 ms because the JSON payload grows linearly with samples.
            Ignored when `node_id` is set (per-node ring is fixed at
            1024 samples ≈ 21 ms @ 48 kHz).
        node_id: If set, snapshot the named audio node's 1024-sample
            waveform ring instead of the final mix.
        channel: -1 returns all channels; 0/1/... isolates one channel
            before encoding (smaller payload).
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    if channel >= 0 and samples.ndim > 1 and channel < samples.shape[1]:
        samples = samples[:, channel]
    out = _audio_analysis().dump_audio_samples(samples, sr)
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def analyze_audio_spectrum(duration_ms: float = 1000.0,
                                  node_id: str = "",
                                  fft_size: int = 4096,
                                  mode: str = "single",
                                  db_scale: bool = True,
                                  fmin_hz: float = 0.0,
                                  fmax_hz: float = 0.0) -> str:
    """Numeric FFT magnitudes over a captured window.

    Returns `bins` as a list of `{hz, magnitude}` for inspection of
    *which* frequencies hold energy — complements `capture_spectrogram`
    (PNG) when you need quantitative bin values. Hann-windowed.

    For per-node captures (`node_id` set) the input is the 1024-sample
    waveform ring, so `fft_size > 1024` auto-clamps; `fft_size_used` and
    a `note` field appear in the response when clamping occurs.

    Args:
        duration_ms: Capture window length (50 - 10000). Ignored for
            per-node captures.
        node_id: Audio node id, or empty for the final mix.
        fft_size: Power of two (2 - 16384). Auto-clamped to fit input.
        mode: "single" picks one Hann-windowed slice from the center;
            "average" returns the mean magnitude across an STFT with
            75 % overlap.
        db_scale: True returns dB (ref=peak); False returns linear
            magnitude.
        fmin_hz, fmax_hz: Visible frequency band. fmax_hz=0 ⇒ Nyquist.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    fmax = float(fmax_hz) if fmax_hz > 0 else None
    out = _audio_analysis().analyze_spectrum(
        samples, sr,
        fft_size=int(fft_size),
        mode=str(mode),
        db_scale=bool(db_scale),
        fmin_hz=float(fmin_hz),
        fmax_hz=fmax)
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def detect_discontinuities(duration_ms: float = 1000.0,
                                   node_id: str = "",
                                   threshold_multiplier: float = 8.0,
                                   min_magnitude: float = 0.05) -> str:
    """Find sample-indexed click/discontinuity events in a captured window.

    Flags samples where |x[n]-x[n-1]| > max(threshold_multiplier * MAD,
    min_magnitude). Returns each event with its sample index, time in
    ms, and magnitude — useful for distinguishing periodic clicks
    (sub-block boundary, phase wrap) from random ones.

    Operates on the mono mix. The aggregate count returned by
    `analyze_audio_detail` is `len(events) / duration_seconds`.

    Args:
        duration_ms: Capture window length (50 - 10000). Per-node mode
            is fixed at the 1024-sample ring (~21 ms @ 48 kHz) — useful
            for tight inspection of a single synth output, not long
            scans.
        node_id: Audio node id, or empty for the final mix.
        threshold_multiplier: MAD multiplier for the threshold (default
            8.0 matches the value used internally by `analyze_scalars`).
        min_magnitude: Absolute floor below which diffs are ignored
            even if they exceed the MAD threshold (default 0.05).
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    out = _audio_analysis().detect_discontinuities(
        samples, sr,
        threshold_multiplier=float(threshold_multiplier),
        min_magnitude=float(min_magnitude))
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def analyze_discontinuity_pattern(duration_ms: float = 1000.0,
                                          node_id: str = "",
                                          threshold_multiplier: float = 8.0,
                                          min_magnitude: float = 0.05,
                                          fft_size: int = 1024) -> str:
    """Capture audio, find discontinuity events, then classify their pattern.

    Combines `detect_discontinuities` with FFT-of-event-times to answer:
    are these clicks periodic or random? Periodic clicks indicate
    structural bugs (sub-block boundary @ every 64 samples, phase wrap @
    every period of the fundamental). Random clicks usually mean
    numerical issues (denormals, mis-ordered ops).

    Returns the full discontinuity result plus a `pattern` block:
        {periodic, peak_to_mean_ratio, period_samples, period_hz,
         interval_jitter_samples, top_intervals[]}
    Or `{periodic: null, reason: ...}` when there are too few events.

    Args:
        duration_ms: Capture window length. Longer windows give a
            cleaner pattern read.
        node_id: Audio node id, or empty for the final mix.
        threshold_multiplier, min_magnitude: Same as `detect_discontinuities`.
        fft_size: FFT size for the impulse-train spectrum (auto-grown to
            cover the event span). Default 1024.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    aa = _audio_analysis()
    discont = aa.detect_discontinuities(
        samples, sr,
        threshold_multiplier=float(threshold_multiplier),
        min_magnitude=float(min_magnitude))
    indices = [e["sample_idx"] for e in discont["events"]]
    pattern = aa.analyze_discontinuity_pattern(indices, sr,
                                                  fft_size=int(fft_size))
    out = dict(discont)
    out["pattern"] = pattern
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def detect_dropouts(duration_ms: float = 1000.0,
                           node_id: str = "",
                           threshold: float = 0.001,
                           min_run_ms: float = 10.0) -> str:
    """Find runs of near-silence within a captured window.

    Flags stretches where |x| < threshold lasting at least min_run_ms.
    Catches voice dropouts in the middle of an otherwise non-silent
    capture — invisible to RMS / peak because they're averaged out.

    Args:
        duration_ms: Capture window length (50 - 10000).
        node_id: Audio node id, or empty for the final mix.
        threshold: Sample magnitude below which we call the sample
            "silent" (default 0.001 = -60 dB).
        min_run_ms: Minimum run length in ms (default 10.0). Shorter
            runs are ignored to avoid false positives during natural
            decays / quiet passages.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    out = _audio_analysis().detect_dropouts(
        samples, sr,
        threshold=float(threshold),
        min_run_ms=float(min_run_ms))
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def measure_clipping(duration_ms: float = 1000.0,
                            node_id: str = "",
                            threshold: float = 0.99) -> str:
    """Count clipped samples and the longest clip run.

    Aggregate companion to `peak`: `peak` shows the maximum sample value;
    this tool tells you *how many* samples reached or exceeded the
    threshold and how long the longest contiguous clip ran for. Two
    graphs both peaking at 1.0 sound very different if one clips for 30
    samples and the other clips for 30 000.

    Args:
        duration_ms: Capture window length (50 - 10000).
        node_id: Audio node id, or empty for the final mix.
        threshold: |x| ≥ threshold counts as a clipped sample
            (default 0.99 — slightly below 1.0 to catch near-clips).
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    out = _audio_analysis().measure_clipping(samples, sr,
                                                threshold=float(threshold))
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def dump_envelope_data(duration_ms: float = 1000.0,
                              node_id: str = "",
                              hop_ms: float = 5.0) -> str:
    """Per-channel RMS envelope as numeric arrays.

    The data behind the `capture_envelope_plot` PNG, exposed as JSON
    so attack/decay timings can be quantified without reading the image.
    Each channel's envelope is `[{t_ms, rms}, ...]`.

    Args:
        duration_ms: Capture window length (50 - 10000).
        node_id: Audio node id, or empty for the final mix.
        hop_ms: Hop interval in ms (default 5.0). Frame length is
            2 × hop, matching the PNG renderer.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    out = _audio_analysis().dump_envelope_data(samples, sr,
                                                  hop_ms=float(hop_ms))
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def analyze_harmonic_profile(duration_ms: float = 1000.0,
                                     node_id: str = "",
                                     fundamental_hz: float = 0.0,
                                     n_partials: int = 10,
                                     fft_size: int = 8192) -> str:
    """Decompose a tonal signal into its strongest partials.

    For each integer multiple n of the fundamental, returns the observed
    frequency and magnitude of the nearest spectral peak. Computes total
    harmonic distortion (THD, the power ratio of partials 2..N to the
    fundamental) and inharmonicity (mean fractional deviation of
    partials from the perfect harmonic series).

    Useful for diagnosing distortion (high THD) and for synth-preset
    shaping (which partials are present, in what proportion).

    Args:
        duration_ms: Capture window length (need ≥ ~50 ms of tonal
            signal for pyin pitch detection).
        node_id: Audio node id, or empty for the final mix.
        fundamental_hz: Skip pitch detection by passing the fundamental
            directly (0 = auto-detect via librosa.pyin).
        n_partials: Maximum number of harmonics to report.
        fft_size: FFT size (auto-clamped to the largest power of two
            that fits the input).
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    f0 = float(fundamental_hz) if fundamental_hz > 0 else None
    out = _audio_analysis().analyze_harmonic_profile(
        samples, sr,
        fundamental_hz=f0,
        n_partials=int(n_partials),
        fft_size=int(fft_size))
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def analyze_aliasing(duration_ms: float = 1000.0,
                            node_id: str = "",
                            fundamental_hz: float = 0.0,
                            n_partials: int = 20,
                            fft_size: int = 8192,
                            top_n_peaks: int = 8) -> str:
    """Detect aliasing / non-harmonic energy in a tonal signal.

    For signals that *should* be tonal (oscillator output, sustained
    notes), this flags energy at frequencies that aren't integer
    multiples of the fundamental — and especially fold-back energy from
    would-be ultrasonic harmonics reflected below Nyquist (the classic
    naïve-oscillator aliasing fingerprint).

    Returns `{tonality_ok: false, reason: ...}` for non-tonal signals
    (chords, noise, percussion). When tonal:
      - `aliasing_score` (0..1) — fraction of total energy in
        non-harmonic bins.
      - `top_non_harmonic_peaks[]` — strongest off-comb peaks with
        `is_likely_alias`, `above_comb`, `is_foldback` flags.

    Args:
        duration_ms: Capture window length (need ≥ ~50 ms of stable
            tone for reliable pitch detection).
        node_id: Audio node id, or empty for the final mix.
        fundamental_hz: Skip pitch detection by passing the fundamental
            directly (0 = auto-detect via librosa.pyin).
        n_partials: How many harmonics define the expected comb extent.
        fft_size: FFT size (auto-clamped to fit input).
        top_n_peaks: Number of strongest non-harmonic peaks to report.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    f0 = float(fundamental_hz) if fundamental_hz > 0 else None
    out = _audio_analysis().analyze_aliasing(
        samples, sr,
        fundamental_hz=f0,
        n_partials=int(n_partials),
        fft_size=int(fft_size),
        top_n_peaks=int(top_n_peaks))
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def measure_loudness(duration_ms: float = 3000.0,
                            node_id: str = "",
                            short_term: bool = True) -> str:
    """ITU-R BS.1770 / EBU R128 perceptual loudness.

    Returns:
      - integrated_lufs: single LUFS value over the captured window.
        Streaming targets typically aim for around -14 LUFS; -23 LUFS is
        the EBU broadcast reference.
      - short_term_lufs: list of {t_ms, lufs} from a 3 s sliding window
        every 100 ms. Empty when the buffer is shorter than 3 s.
      - peak_db: max sample magnitude in dBFS.
      - true_peak_db: 4× oversampled peak (catches inter-sample peaks
        per BS.1770).

    Args:
        duration_ms: Capture window length. Should be ≥ 3000 for
            short_term to populate; integrated works at any length.
        node_id: Audio node id, or empty for the final mix.
        short_term: Compute the sliding-window track. Disable for a
            faster integrated-only read.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    out = _audio_analysis().measure_loudness(samples, sr,
                                                short_term=bool(short_term))
    out["ok"] = True
    out["node_id"] = node_id
    out["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(out)


@mcp.tool()
async def analyze_stereo_image(duration_ms: float = 1000.0,
                                node_id: str = "",
                                save_dir: str = "",
                                lissajous_size: int = 320) -> str:
    """Mid/side decomposition + balance + correlation + Lissajous PNG.

    Reports left/right RMS, balance (dB), mid/side RMS and ratio,
    inter-channel correlation, and renders a goniometer plot rotated 45°
    so mid (L+R) is vertical and side (L−R) is horizontal:
      - vertical line  → mono signal
      - horizontal line → phase-inverted (will collapse on mono fold-down)
      - tilted ellipse → typical stereo program
      - circular cloud → uncorrelated (e.g. dual-mono noise)

    Args:
        duration_ms: Capture window length (50 - 10000).
        node_id: Audio node id, or empty for the final mix.
        save_dir: Optional directory for the Lissajous PNG.
        lissajous_size: Square pixel size of the Lissajous plot.
    """
    fetched, err = await _fetch_wav_bytes(node_id, duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    aa = _audio_analysis()
    result = aa.analyze_stereo_image(samples, sr,
                                        lissajous_size=int(lissajous_size))
    png_bytes = result.pop("lissajous_png_bytes", None)
    if png_bytes is not None:
        png_path = _write_png_to_disk(png_bytes, save_dir, "vivid_lissajous")
        result["lissajous_png_path"] = png_path
        result["hint"] = ("Read lissajous_png_path. Vertical line = mono, "
                          "horizontal = phase-inverted, tilted ellipse = "
                          "typical stereo, circular cloud = uncorrelated.")
    else:
        result["lissajous_png_path"] = ""
    result["ok"] = True
    result["sample_rate"] = int(sr)
    result["node_id"] = node_id
    result["source"] = "node_waveform_ring" if node_id else "final_mix"
    return _json_response(result)


@mcp.tool()
async def record_audio_to_wav(path: str,
                               duration_ms: float = 1000.0) -> str:
    """Capture `duration_ms` of final-mix audio and write it as a 32-bit
    float WAV to `path`. Use this to snapshot a "golden" reference take
    that `compare_audio_to_reference` can diff future captures against.

    Args:
        path: Absolute filesystem path to write to (.wav).
        duration_ms: Capture window length (50 - 30000).
    """
    fetched, err = await _fetch_wav_bytes("", duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    samples, sr = fetched
    try:
        wav_bytes = _audio_analysis().write_wav_bytes(samples, sr)
        with open(path, "wb") as f:
            f.write(wav_bytes)
    except Exception as exc:  # noqa: BLE001
        return _json_response({"ok": False, "error": f"write failed: {exc}"})
    return _json_response({
        "ok": True, "path": path,
        "sample_rate": int(sr),
        "frames": int(samples.shape[0] if samples.ndim > 0 else 0),
        "channels": int(samples.shape[1]) if samples.ndim > 1 else 1,
        "duration_ms": float(duration_ms),
        "bytes_written": len(wav_bytes),
    })


@mcp.tool()
async def compare_audio_to_reference(reference_path: str,
                                      duration_ms: float = 1000.0,
                                      plot_width: int = 720,
                                      plot_height: int = 240,
                                      save_dir: str = "") -> str:
    """Capture current audio, diff against a reference WAV.

    Returns scalar deltas (rms, fundamental_hz, harmonic_purity, ...) and
    a side-by-side spectrogram PNG (left = reference, right = current).

    Args:
        reference_path: Absolute path to a WAV (typically from
            `record_audio_to_wav`). soundfile decodes most formats.
        duration_ms: Capture window length for the current audio.
        plot_width / plot_height: Dimensions of the diff PNG.
        save_dir: Optional output directory for the diff PNG.
    """
    fetched, err = await _fetch_wav_bytes("", duration_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    cur_samples, cur_sr = fetched
    try:
        import soundfile as _sf
        ref_samples, ref_sr = _sf.read(reference_path, dtype="float32",
                                        always_2d=False)
    except Exception as exc:  # noqa: BLE001
        return _json_response({
            "ok": False,
            "error": f"failed to read reference {reference_path}: {exc}",
        })

    cmp = _audio_analysis().compare_to_reference(
        ref_samples, ref_sr, cur_samples, cur_sr)
    png = _audio_analysis().render_spectrogram_diff_png(
        ref_samples, ref_sr, cur_samples, cur_sr,
        width=int(plot_width), height=int(plot_height))
    diff_path = _write_png_to_disk(png, save_dir, "vivid_diff")
    return _json_response({
        "ok": True,
        "reference_path": reference_path,
        "diff_png_path": diff_path,
        "deltas": cmp["deltas"],
        "reference_metrics": cmp["reference_metrics"],
        "current_metrics": cmp["current_metrics"],
        "hint": ("Read diff_png_path: left = reference spectrogram, "
                 "right = current."),
    })


def _build_note_window_events(notes_or_pairs):
    """Build a [{t_ms, type, note, vel, channel}, ...] events list.

    `notes_or_pairs` is the schedule the wrapper computed, expressed as a
    flat list of dicts. Passed straight to capture_note_window.
    """
    return list(notes_or_pairs)


async def _post_note_window(midi_node_id: str, events: list,
                            capture_ms: float) -> tuple[dict | None, str | None]:
    body = {
        "midi_node_id": midi_node_id,
        "events": events,
        "capture_ms": float(capture_ms),
    }
    raw = await _post("capture_note_window", body,
                      timeout=max(10.0, capture_ms / 1000.0 + 8.0))
    try:
        doc = json.loads(raw)
    except json.JSONDecodeError as exc:
        return None, f"non-JSON: {exc}"
    if not doc.get("ok", False):
        return None, doc.get("error", "capture_note_window failed")
    return doc, None


@mcp.tool()
async def capture_note_response(midi_node_id: str,
                                 note: int = 60,
                                 velocity: int = 100,
                                 sustain_ms: float = 600.0,
                                 capture_ms: float = 1200.0,
                                 midi_channel: int = 1,
                                 width: int = 720,
                                 height: int = 200,
                                 save_dir: str = "") -> str:
    """Inject a MIDI note into a MidiInput-like node, capture the response.

    Sends a NOTE_ON, holds for `sustain_ms`, sends NOTE_OFF, captures
    `capture_ms` of audio total. Returns a waveform PNG plus scalar
    metrics (rms, fundamental_hz, dc_offset, ...).

    The named node must export the optional `vivid_op_inject_midi` symbol
    (currently MidiInput, Arpeggiator, and MidiClip).

    Args:
        midi_node_id: Node id of a MidiInput-style operator.
        note: MIDI note number (0-127).
        velocity: Note-on velocity (1-127).
        sustain_ms: How long to hold the note before NOTE_OFF (0 = never).
        capture_ms: Total capture window length, including release tail.
        midi_channel: MIDI channel 1..16.
        width, height: Waveform PNG pixel dimensions.
        save_dir: Optional output directory for the PNG.
    """
    events = [{"t_ms": 0.0, "type": "on", "note": int(note),
               "velocity": int(velocity), "channel": int(midi_channel)}]
    if sustain_ms > 0:
        events.append({"t_ms": float(sustain_ms), "type": "off",
                       "note": int(note), "channel": int(midi_channel)})
    doc, err = await _post_note_window(midi_node_id, events, capture_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    try:
        samples, sr = _audio_analysis().load_wav_bytes(doc.get("wav_base64", ""))
    except Exception as exc:  # noqa: BLE001
        return _json_response({"ok": False, "error": f"WAV decode: {exc}"})
    aa = _audio_analysis()
    metrics = aa.analyze_scalars(samples, sr)
    adsr = aa.extract_adsr(samples, sr, sustain_ms=float(sustain_ms))
    png = aa.render_waveform_png(samples, sr,
                                    width=int(width), height=int(height))
    path = _write_png_to_disk(png, save_dir, "vivid_note_response")
    return _json_response({
        "ok": True, "capture_path": path,
        "kind": "note_response",
        "midi_node_id": midi_node_id,
        "note": int(note), "velocity": int(velocity),
        "sustain_ms": float(sustain_ms), "capture_ms": float(capture_ms),
        "channel": int(midi_channel),
        "sample_rate": int(sr),
        "frames": int(samples.shape[0] if samples.ndim > 0 else 0),
        "channels": int(samples.shape[1]) if samples.ndim > 1 else 1,
        "metrics": metrics,
        "adsr": adsr,
        "hint": ("Read capture_path — full attack→sustain→release waveform. "
                 "`adsr` block has the four canonical synth parameters "
                 "extracted automatically."),
    })


@mcp.tool()
async def capture_polyphony_response(midi_node_id: str,
                                      notes: list[int],
                                      velocity: int = 100,
                                      stagger_ms: float = 5.0,
                                      sustain_ms: float = 600.0,
                                      capture_ms: float = 1500.0,
                                      midi_channel: int = 1,
                                      width: int = 720,
                                      height: int = 200,
                                      save_dir: str = "") -> str:
    """Strike a chord, capture the polyphonic response.

    Schedules note-ons separated by `stagger_ms`, holds for `sustain_ms`,
    then sends note-offs. Reports rms / peak / fundamental plus a
    `polyphony` block with the expected sqrt(N) gain-staging factor.

    Args:
        midi_node_id: Node id of an inject-supporting operator.
        notes: List of MIDI note numbers (e.g. [60, 64, 67] for C major).
        velocity: Note-on velocity (1-127).
        stagger_ms: Spacing between successive note-ons.
        sustain_ms: Hold time before sending note-offs.
        capture_ms: Total capture window length.
        midi_channel, width, height: As elsewhere.
        save_dir: Optional output directory for the PNG.
    """
    n = len(notes)
    chan = int(midi_channel)
    events: list = []
    for i, nn in enumerate(notes):
        events.append({"t_ms": float(i * stagger_ms), "type": "on",
                       "note": int(nn), "velocity": int(velocity),
                       "channel": chan})
    if sustain_ms > 0:
        for i, nn in enumerate(notes):
            events.append({"t_ms": float((n - 1) * stagger_ms + sustain_ms +
                                          i * stagger_ms),
                           "type": "off", "note": int(nn), "channel": chan})

    doc, err = await _post_note_window(midi_node_id, events, capture_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    try:
        samples, sr = _audio_analysis().load_wav_bytes(doc.get("wav_base64", ""))
    except Exception as exc:  # noqa: BLE001
        return _json_response({"ok": False, "error": f"WAV decode: {exc}"})
    metrics = _audio_analysis().analyze_scalars(samples, sr)
    png = _audio_analysis().render_waveform_png(samples, sr,
                                                  width=int(width),
                                                  height=int(height))
    path = _write_png_to_disk(png, save_dir, "vivid_polyphony")
    import math as _math
    return _json_response({
        "ok": True, "capture_path": path,
        "kind": "polyphony_response",
        "midi_node_id": midi_node_id,
        "events_scheduled": doc.get("events_scheduled"),
        "events_fired": doc.get("events_fired"),
        "capture_ms": float(capture_ms),
        "sample_rate": int(sr),
        "frames": int(samples.shape[0] if samples.ndim > 0 else 0),
        "channels": int(samples.shape[1]) if samples.ndim > 1 else 1,
        "metrics": metrics,
        "polyphony": {
            "voice_count": n,
            "sqrt_n_factor": _math.sqrt(max(1, n)),
            "hint": ("Under sqrt(N) summing, rms should scale ~sqrt(voice_count)x "
                     "vs single-note rms."),
        },
        "hint": ("Read capture_path. Compare metrics.rms with a single-note "
                 "baseline (should scale ~polyphony.sqrt_n_factor× under proper "
                 "gain staging)."),
    })


@mcp.tool()
async def capture_retrigger_response(midi_node_id: str,
                                      note: int = 60,
                                      velocity: int = 100,
                                      count: int = 4,
                                      interval_ms: float = 250.0,
                                      capture_ms: float = 1500.0,
                                      midi_channel: int = 1,
                                      width: int = 720,
                                      height: int = 200,
                                      save_dir: str = "") -> str:
    """Hammer the same note `count` times at `interval_ms` spacing.

    Catches the "wubb" / phase-discontinuity bug class. Returns waveform
    PNG plus a `retrigger.per_retrigger` array — one entry per trigger
    with `discontinuity_hits` measured in a ±5 ms window around the
    expected retrigger time.

    Args:
        midi_node_id: Node id of an inject-supporting operator.
        note: MIDI note number to hammer.
        velocity: Note-on velocity.
        count: Number of retriggers (1-64).
        interval_ms: Time between successive note-ons.
        capture_ms: Total capture window length.
        midi_channel, width, height: As elsewhere.
        save_dir: Optional output directory for the PNG.
    """
    n = max(1, min(64, int(count)))
    chan = int(midi_channel)
    events: list = []
    for i in range(n):
        if i > 0:
            t_off = max(0.0, i * interval_ms - 5.0)
            events.append({"t_ms": t_off, "type": "off",
                           "note": int(note), "channel": chan})
        events.append({"t_ms": float(i * interval_ms), "type": "on",
                       "note": int(note), "velocity": int(velocity),
                       "channel": chan})
    events.append({"t_ms": float((n - 1) * interval_ms + interval_ms * 0.5),
                   "type": "off", "note": int(note), "channel": chan})

    doc, err = await _post_note_window(midi_node_id, events, capture_ms)
    if err:
        return _json_response({"ok": False, "error": err})
    try:
        samples, sr = _audio_analysis().load_wav_bytes(doc.get("wav_base64", ""))
    except Exception as exc:  # noqa: BLE001
        return _json_response({"ok": False, "error": f"WAV decode: {exc}"})

    # Per-retrigger discontinuity scoring — robust threshold on
    # |sample[k]-sample[k-1]| in ±5 ms windows around each retrigger time.
    import numpy as _np
    aa = _audio_analysis()
    mono = aa.to_mono(samples) if samples.ndim > 1 else samples
    diffs = _np.abs(_np.diff(mono))
    if diffs.size > 0:
        med = _np.median(diffs)
        thresh = max(8.0 * med, 0.05)
    else:
        thresh = 0.05
    win = max(64, sr // 200)  # ±5ms
    per_retrigger = []
    for t in range(n):
        center = int(t * interval_ms * 0.001 * sr)
        lo = max(0, center - win)
        hi = min(diffs.size, center + win) if diffs.size > 0 else 0
        hits = int(_np.sum(diffs[lo:hi] > thresh)) if hi > lo else 0
        per_retrigger.append({
            "index": t,
            "t_ms": int(t * interval_ms),
            "discontinuity_hits": hits,
        })

    metrics = aa.analyze_scalars(samples, sr)
    png = aa.render_waveform_png(samples, sr,
                                  width=int(width), height=int(height))
    path = _write_png_to_disk(png, save_dir, "vivid_retrigger")
    return _json_response({
        "ok": True, "capture_path": path,
        "kind": "retrigger_response",
        "midi_node_id": midi_node_id,
        "events_scheduled": doc.get("events_scheduled"),
        "events_fired": doc.get("events_fired"),
        "capture_ms": float(capture_ms),
        "sample_rate": int(sr),
        "frames": int(samples.shape[0] if samples.ndim > 0 else 0),
        "channels": int(samples.shape[1]) if samples.ndim > 1 else 1,
        "metrics": metrics,
        "retrigger": {
            "per_retrigger": per_retrigger,
            "hint": ("Non-zero discontinuity_hits at trigger boundaries "
                     "indicates clicks/wubb."),
        },
        "hint": "Read capture_path. Examine retrigger.per_retrigger.",
    })



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
async def inspect_graph(detail: str = "summary") -> str:
    """Get graph state: nodes, params, ports, connections.

    Args:
        detail: "summary" (default) — compact overview with flat param values and port stubs.
                "full" — complete output with param schema metadata, live port values, lane arrays.
                Use inspect_node(node_id) for individual node detail.
    """
    return await _post("inspect_graph", {"detail": detail})


@mcp.tool()
async def list_types(domain: str = "") -> str:
    """List all available operator types (compact catalog: name, display_name, kind, brief, lane_behavior, plus optional keywords/summary when authors set them). Use operator_docs(name) to get full details (params, ports, docs) for a specific operator.

    Args:
        domain: Optional filter — "gpu", "audio", or "control". Omit to list all domains.
    """
    args = ["list-types"]
    if domain:
        args.extend(["--domain", domain])
    args.append("--json")
    return await _run_vivid_cli_json(args)


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
    curve: str = "linear",
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
        curve: Easing curve — "linear", "exponential", "logarithmic", "ease_in", "ease_out", "ease_in_out", "s_curve"
    """
    _CURVE_MAP = {
        "linear": 0, "exponential": 1, "logarithmic": 2,
        "ease_in": 3, "ease_out": 4, "ease_in_out": 5, "s_curve": 6,
    }
    curve_idx = _CURVE_MAP.get(curve, 0)
    return await _post("set_connection_remap", {
        "from_addr": from_addr, "to_addr": to_addr,
        "from_min": from_min, "from_max": from_max,
        "to_min": to_min, "to_max": to_max, "clamp": clamp,
        "curve": curve_idx,
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
async def list_clap_params(node_id: str) -> str:
    """List all parameters exposed by the CLAP plugin currently loaded in a
    CLAPInstrument or CLAPEffect node.

    Returns a JSON array of parameter descriptors:
      [{"name": "Cutoff", "id": 42, "min": 20.0, "max": 20000.0, "default": 1000.0,
        "value": 1200.0, "step_count": 0}, ...]

    Returns an empty array if no plugin is loaded yet.
    Use set_clap_param(node_id, "Cutoff", 1000.0) to control a parameter directly.

    Args:
        node_id: ID of the CLAPInstrument or CLAPEffect node
    """
    return await _post("list_clap_params", {"node_id": node_id})


@mcp.tool()
async def list_clap_plugins() -> list[dict]:
    """List all installed CLAP plugins found in standard search paths.

    Scans /Library/Audio/Plug-Ins/CLAP and ~/Library/Audio/Plug-Ins/CLAP.
    Returns a list of dicts with 'name' and 'path' keys.
    Use set_clap_plugin to load one into a CLAPInstrument or CLAPEffect node.
    """
    search_paths = [
        pathlib.Path("/Library/Audio/Plug-Ins/CLAP"),
        pathlib.Path(os.path.expanduser("~/Library/Audio/Plug-Ins/CLAP")),
    ]
    results = []
    seen: set[str] = set()
    for base in search_paths:
        if not base.exists():
            continue
        for clap_path in sorted(base.rglob("*.clap")):
            key = str(clap_path)
            if key in seen:
                continue
            seen.add(key)
            results.append({"name": clap_path.stem, "path": key})
    return results


@mcp.tool()
async def set_clap_plugin(node_id: str, path: str, plugin_id: str = "") -> str:
    """Load a CLAP plugin into a CLAPInstrument or CLAPEffect node.

    Args:
        node_id: ID of the target CLAPInstrument or CLAPEffect node
        path: Full filesystem path to the .clap bundle (from list_clap_plugins)
        plugin_id: CLAP plugin ID within the bundle; leave blank to use the first plugin
    """
    await _post("set_string_param", {"node_id": node_id, "param": "plugin_path", "value": path})
    if plugin_id:
        await _post("set_string_param", {"node_id": node_id, "param": "plugin_id", "value": plugin_id})
    return json.dumps({"ok": True, "node_id": node_id, "path": path, "plugin_id": plugin_id})


@mcp.tool()
async def set_clap_param(node_id: str, param_name: str, value: float) -> str:
    """Set a CLAP plugin parameter by name, in the plugin's native value range.

    Normalizes the value and delivers it directly to the audio thread via the
    direct-param queue — no macro slot needed, no ceiling on simultaneous parameters.

    Args:
        node_id: ID of the CLAPInstrument or CLAPEffect node
        param_name: Exact parameter name (from list_clap_params)
        value: Value in the plugin's native range (min..max from list_clap_params)
    """
    raw = await _post("list_clap_params", {"node_id": node_id})
    data = json.loads(raw)
    params_list = data.get("params", [])
    match = next((p for p in params_list if p["name"] == param_name), None)
    if not match:
        return json.dumps({"ok": False, "error": f"param '{param_name}' not found"})

    mn = float(match.get("min", 0.0))
    mx = float(match.get("max", 1.0))
    rng = mx - mn if mx != mn else 1.0
    normalized = max(0.0, min(1.0, (value - mn) / rng))
    param_id = int(match["id"])

    await _post("set_string_param", {
        "node_id": node_id,
        "param": "_clap_direct_params",
        "value": f"{param_id}:{normalized:.9g}"
    })
    return json.dumps({"ok": True, "param": param_name, "id": param_id,
                       "value": value, "normalized": normalized})


@mcp.tool()
async def list_au_plugins() -> list[dict]:
    """List all installed AU instrument plugins via Core Audio's component registry.

    Returns a list of dicts with a 'name' key (the full AudioComponentCopyName string,
    e.g. "Toontrack: EZdrummer 3"). Pass this name to set_au_plugin to load the plugin.

    Requires a running Vivid instance.
    """
    raw = await _post("list_au_plugins", {})
    data = json.loads(raw)
    return data.get("plugins", [])


@mcp.tool()
async def set_au_plugin(node_id: str, plugin_name: str) -> str:
    """Load an AU instrument plugin into an AUInstrument node.

    Args:
        node_id: ID of the target AUInstrument node
        plugin_name: Full plugin name from list_au_plugins (e.g. "Toontrack: EZdrummer 3")
    """
    await _post("set_string_param", {"node_id": node_id, "param": "plugin_name", "value": plugin_name})
    return json.dumps({"ok": True, "node_id": node_id, "plugin_name": plugin_name})


@mcp.tool()
async def list_au_params(node_id: str) -> str:
    """List all parameters exposed by the AU plugin currently loaded in an AUInstrument node.

    Returns a JSON array of parameter descriptors:
      [{"name": "Cutoff", "id": 42, "min": 20.0, "max": 20000.0, "default": 1000.0,
        "value": 1200.0, "units": "Hz", "step_count": 0}, ...]

    Returns an empty array if no plugin is loaded yet.
    Use set_au_param(node_id, "Cutoff", 1000.0) to control a parameter directly.

    Args:
        node_id: ID of the AUInstrument node
    """
    return await _post("list_au_params", {"node_id": node_id})


@mcp.tool()
async def set_au_param(node_id: str, param_name: str, value: float) -> str:
    """Set an AU plugin parameter by name, in the plugin's native value range.

    Normalizes the value and delivers it directly to the audio thread via the
    direct-param queue — no macro slot needed, no ceiling on simultaneous parameters.

    Args:
        node_id: ID of the AUInstrument node
        param_name: Exact parameter name (from list_au_params)
        value: Value in the plugin's native range (min..max from list_au_params)
    """
    raw = await _post("list_au_params", {"node_id": node_id})
    data = json.loads(raw)
    params_list = data.get("params", [])
    match = next((p for p in params_list if p["name"] == param_name), None)
    if not match:
        return json.dumps({"ok": False, "error": f"param '{param_name}' not found"})

    mn = float(match.get("min", 0.0))
    mx = float(match.get("max", 1.0))
    rng = mx - mn if mx != mn else 1.0
    normalized = max(0.0, min(1.0, (value - mn) / rng))
    param_id = int(match["id"])

    await _post("set_string_param", {
        "node_id": node_id,
        "param": "_au_direct_params",
        "value": f"{param_id}:{normalized:.9g}"
    })
    return json.dumps({"ok": True, "param": param_name, "id": param_id,
                       "value": value, "normalized": normalized})


@mcp.tool()
async def list_vst3_plugins() -> list[dict]:
    """List all installed VST3 instrument plugins by scanning standard VST3 directories.

    Returns a list of dicts with a 'name' key (e.g. "Serum2 [Xfer Records]").
    Pass this name to set_vst3_plugin to load the plugin into a Vst3Instrument node.

    Requires a running Vivid instance.
    """
    raw = await _post("list_vst3_plugins", {})
    data = json.loads(raw)
    return data.get("plugins", [])


@mcp.tool()
async def set_vst3_plugin(node_id: str, plugin_name: str) -> str:
    """Load a VST3 instrument plugin into a Vst3Instrument node.

    Args:
        node_id: ID of the target Vst3Instrument node
        plugin_name: Full plugin key from list_vst3_plugins (e.g. "Serum2 [Xfer Records]")
    """
    await _post("set_string_param", {"node_id": node_id, "param": "plugin_name", "value": plugin_name})
    return json.dumps({"ok": True, "node_id": node_id, "plugin_name": plugin_name})


@mcp.tool()
async def list_vst3_params(node_id: str) -> str:
    """List all parameters exposed by the VST3 plugin currently loaded in a Vst3Instrument node.

    Returns a JSON array of parameter descriptors:
      [{"name": "Cutoff", "id": 101, "min": 20.0, "max": 20000.0, "default": 1000.0,
        "value": 1200.0, "units": "Hz", "step_count": 0}, ...]

    The 'min', 'max', 'default', and 'value' fields are all plain values.
    Use set_vst3_param(node_id, "Cutoff", 1000.0) to control a parameter directly.

    Args:
        node_id: ID of the Vst3Instrument node
    """
    return await _post("list_vst3_params", {"node_id": node_id})


@mcp.tool()
async def set_vst3_param(node_id: str, param_name: str, value: float) -> str:
    """Set a VST3 plugin parameter by name, using the plugin's plain value range.

    Normalizes the value and delivers it directly to the audio thread via the
    direct-param queue — no macro slot needed, no ceiling on simultaneous parameters.

    Args:
        node_id: ID of the Vst3Instrument node
        param_name: Exact parameter name (from list_vst3_params)
        value: Value in the plugin's plain range (min..max from list_vst3_params)
    """
    raw = await _post("list_vst3_params", {"node_id": node_id})
    data = json.loads(raw)
    params_list = data.get("params", [])
    match = next((p for p in params_list if p["name"] == param_name), None)
    if not match:
        return json.dumps({"ok": False, "error": f"param '{param_name}' not found"})

    mn = float(match.get("min", 0.0))
    mx = float(match.get("max", 1.0))
    rng = mx - mn if mx != mn else 1.0
    normalized = max(0.0, min(1.0, (value - mn) / rng))
    param_id = int(match["id"])

    await _post("set_string_param", {
        "node_id": node_id,
        "param": "_vst3_direct_params",
        "value": f"{param_id}:{normalized:.9g}"
    })
    return json.dumps({"ok": True, "param": param_name, "id": param_id,
                       "value": value, "normalized": normalized})


@mcp.tool()
async def list_vst3_presets(node_id: str, filter: str = "") -> str:
    """List the preset library of the VST3 plugin loaded in a Vst3Instrument node.

    Presets are discovered from three sources and merged into one catalog:
      - ".vstpreset" files in the standard tree (<root>/<Vendor>/<Plugin>/)
      - VST3 program lists (factory programs the plugin exposes via IUnitInfo)
      - per-plugin adapters for native formats (e.g. Serum2's .SerumPreset,
        Arturia Pigments, Xfer Cthulhu), which provide rich metadata even when
        the format predates .vstpreset

    Returns a JSON object {"ok": true, "presets": [...]} where each preset is:
      {"id": "...", "name": "PN - Piano Dance", "category": "Piano",
       "author": "...", "description": "...", "tags": ["Pad", ...],
       "source": "serum2", "loadable": true}

    Use the 'id' with load_vst3_preset to load a preset. 'loadable' is false for
    sources Vivid can describe but not yet load directly (browse/recommend only);
    'tags', 'category', and 'description' let you pick a sound by character.

    Args:
        node_id: ID of the Vst3Instrument node (with a plugin already loaded)
        filter: optional case-insensitive substring; keeps only presets whose
            name, category, or any tag matches. Large libraries (e.g. Pigments
            has ~1500 presets) — use this to narrow by character, e.g. "bass",
            "pad", "pluck", "ambient".
    """
    raw = await _post("list_vst3_presets", {"node_id": node_id})
    if not filter:
        return raw
    try:
        data = json.loads(raw)
        presets = data.get("presets", [])
        q = filter.lower()
        def hit(p):
            if q in str(p.get("name", "")).lower(): return True
            if q in str(p.get("category", "")).lower(): return True
            return any(q in str(t).lower() for t in p.get("tags", []))
        data["presets"] = [p for p in presets if hit(p)]
        data["filtered_from"] = len(presets)
        return json.dumps(data)
    except (ValueError, TypeError):
        return raw


@mcp.tool()
async def load_vst3_preset(node_id: str, preset_id: str) -> str:
    """Load a preset into a Vst3Instrument node by its catalog 'id'.

    Applies the preset to the live plugin and persists it into the node's saved
    state, so it survives graph save/reload. Pass an 'id' from list_vst3_presets.

    To audition: load_vst3_preset -> play notes into the node's notes_in ->
    record_audio_to_wav / analyze_audio_spectrum / evaluate_audio_musically,
    then compare candidates and keep the best.

    Args:
        node_id: ID of the Vst3Instrument node
        preset_id: The 'id' field of a preset from list_vst3_presets
    """
    await _post("set_string_param",
                {"node_id": node_id, "param": "_vst3_load_preset", "value": preset_id})
    return json.dumps({"ok": True, "node_id": node_id, "preset_id": preset_id})


@mcp.tool()
async def open_vst3_gui(node_id: str) -> str:
    """Open the native GUI window of the VST3 plugin loaded in a Vst3Instrument node.

    Use this for the *audition* step: open the plugin's own UI so the user can
    browse its preset library and pick a sound by ear. (Loading native presets,
    e.g. Serum's .SerumPreset, is discovery-only — the user picks in the plugin
    UI, then you capture the live state with capture_vst3_preset.)

    Args:
        node_id: ID of the Vst3Instrument node (with a plugin loaded)
    """
    await _post("set_string_param",
                {"node_id": node_id, "param": "_vst3_gui_request", "value": "open"})
    return json.dumps({"ok": True, "node_id": node_id})


@mcp.tool()
async def close_vst3_gui(node_id: str) -> str:
    """Close the native GUI window of the VST3 plugin loaded in a Vst3Instrument node."""
    await _post("set_string_param",
                {"node_id": node_id, "param": "_vst3_gui_request", "value": "close"})
    return json.dumps({"ok": True, "node_id": node_id})


@mcp.tool()
async def capture_vst3_preset(node_id: str, name: str,
                              source_preset_id: str = "", notes: str = "") -> str:
    """Capture the CURRENT live plugin sound as a named, reusable operator-preset.

    This is the bridge for plugins whose native presets can't be loaded directly
    (e.g. Serum): the user loads a factory preset in the plugin's GUI (see
    open_vst3_gui), then you call this to snapshot the live plugin state — which
    Vivid recalls perfectly (recall_preset) and persists in the graph.

    If source_preset_id (an 'id' from list_vst3_presets) is given, the factory
    preset's discovery metadata (category, tags, author, source) is attached so
    the captured library browses by character via list_presets — just like the
    factory catalog. Add free-text notes to record why you kept it / what you changed.

    Workflow: suggest (list_vst3_presets + filter) -> audition (open_vst3_gui, user
    loads/listens) -> capture_vst3_preset -> tweak (set_vst3_param) -> recall_preset/
    update_preset. Reuse the captured sound across the project by name.

    Args:
        node_id: ID of the Vst3Instrument node
        name: Name for the captured preset (e.g. "deep-evolving-bass")
        source_preset_id: Optional factory preset id (from list_vst3_presets) to tag with
        notes: Optional free-text note attached to the captured preset
    """
    metadata: dict = {}
    if source_preset_id:
        raw = await _post("list_vst3_presets", {"node_id": node_id})
        try:
            cat = json.loads(raw).get("presets", [])
            match = next((p for p in cat if p.get("id") == source_preset_id), None)
            if match:
                for k in ("category", "author", "source"):
                    if match.get(k):
                        metadata[k] = match[k]
                if match.get("tags"):
                    metadata["tags"] = match["tags"]
                metadata["source_preset_id"] = source_preset_id
                if match.get("name"):
                    metadata["source_preset_name"] = match["name"]
        except (ValueError, TypeError):
            pass
    if notes:
        metadata["notes"] = notes
    body: dict = {"node_id": node_id, "name": name}
    if metadata:
        body["metadata"] = metadata
    await _post("save_preset", body)
    return json.dumps({"ok": True, "node_id": node_id, "name": name, "metadata": metadata})


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
    Clears all nodes, connections, session state, and undo history."""
    return await _post("new_graph")


@mcp.tool()
async def write_project_lockfile(graph_path: str,
                                 output_path: str | None = None) -> str:
    """Write a project lockfile recording the packages, operators, and
    assets referenced by a graph. The lockfile is produced from the
    on-disk graph (not the currently-loaded runtime state), so it is
    deterministic.

    Args:
        graph_path: Path to the graph file.
        output_path: Optional destination for the lockfile. If omitted,
            writes <graph_dir>/vivid.lock.

    Returns JSON: {"ok": true, "message": "<absolute path to vivid.lock>"}.
    """
    resolved = _resolve_graph_path(graph_path)
    body: dict = {"graph_path": resolved}
    if output_path is not None:
        body["output_path"] = output_path
    return await _post("write_project_lockfile", body)


@mcp.tool()
async def verify_project_lockfile(graph_path: str,
                                  lockfile_path: str) -> str:
    """Verify that the current environment satisfies an existing lockfile.

    Args:
        graph_path: Path to the graph to compare against.
        lockfile_path: Path to the lockfile to verify.

    Returns JSON: {"ok": true, "status": {"overall": ..., "findings": [...]}}.
    overall is one of: "match", "compatible_drift", "mismatch".
    """
    return await _post("verify_project_lockfile", {
        "graph_path": _resolve_graph_path(graph_path),
        "lockfile_path": lockfile_path,
    })


@mcp.tool()
async def get_project_dependency_status(graph_path: str) -> str:
    """Report dependency status for a graph using its sibling vivid.lock.

    Args:
        graph_path: Path to the graph file. Looks for a sibling
            vivid.lock in the same directory.

    Returns JSON: {"ok": true, "status": {"overall": ..., "findings": [...]}}.
    overall is "no_lockfile" when no sibling vivid.lock exists, otherwise
    one of "match", "compatible_drift", "mismatch".
    """
    return await _post("get_project_dependency_status",
                       {"graph_path": _resolve_graph_path(graph_path)})


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
async def set_node_bypassed(node_id: str, bypassed: bool) -> str:
    """Toggle bypass on a node. When bypassed, the operator's process_*() is
    skipped and its first input passes through to its first output each tick
    (same port type). Useful for A/B comparisons without deleting/reconnecting.

    Bypass is only valid for operators whose first input port type matches
    their first output port type. Sources (no inputs) and asymmetric operators
    are not bypass-eligible and the runtime returns an error.

    Args:
        node_id: Target node
        bypassed: True to bypass, False to re-enable
    """
    return await _post("set_node_bypassed", {"node_id": node_id, "bypassed": bypassed})


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
async def get_runtime_health() -> str:
    """Get the runtime's health snapshot: rolled-up severity (ok/warning/error/fatal),
    structured findings, audio engine state, graph compile state, and GPU state.

    Use this for a quick "is the runtime safe to operate?" check before deeper introspection.
    The same snapshot drives the diagnostics-panel pill in the UI, so the MCP and UI views agree.
    Findings carry stable `code` identifiers (e.g. `missing_required_operators`,
    `gpu_device_lost`, `recovered_from_crash`) suitable for switching on without parsing prose."""
    return await _post("get_runtime_health")


@mcp.tool()
async def rescan_operators() -> str:
    """Re-scan the runtime's known operator directories and load any dylibs
    that aren't already in the registry.

    Use this after a manual `cmake --build` (or similar external build) that
    produced a new operator dylib outside the normal scaffold_operator flow.
    The proper scaffold_operator path already auto-registers on build
    completion, so this is an escape hatch for unusual workflows.

    Returns JSON `{ok, newly_registered}` — `newly_registered` is the count
    of operator types added this rescan.
    """
    return await _post("rescan_operators", {})


@mcp.tool()
async def validate_operators() -> str:
    """Report registration mode and descriptor validity for every known operator.

    Returns a summary (total, by_mode counts, invalid count) and a per-operator
    list with: type name, status (loaded/deferred/abi_mismatch), registration_mode
    (legacy/v2/wgsl/builtin/unknown), valid flag, any validation issues, and
    uniform_layout metadata for V2 GPU operators with generated uniforms.

    Use this to audit Phase 3 migration progress: identify which operators are
    still on the legacy VIVID_REGISTER path and spot any descriptor issues.
    """
    return await _post("validate_operators", {})


@mcp.tool()
async def scaffold_operator(name: str, env: str, variant: str = "",
                            destination: str = "project") -> str:
    """Scaffold a starter operator template into the project's local-operators package.

    Default behavior creates a per-project operator beside the saved graph
    (auto-creating `<graph_dir>/operators/` as a linked workspace package the
    first time). Only pass `destination="core"` when adding a broadly-useful
    primitive that should ship with Vivid itself.

    Use this after confirming via list_types that no existing operator (seed
    or installed package) achieves the goal alone or in combination.

    For advanced authoring (custom ports, typed parameters, inspectors), use
    the opdev MCP server tools after the initial scaffold.

    Writes source, patches the destination CMakeLists, triggers build.

    Args:
        name: Operator name in lowercase_with_underscores (e.g. "tone_gen")
        env: One of "control", "audio", "gpu"
        variant: Template variant. Use "child_op" for a ChildOp-based control
            operator with internal LFO + Smooth.
        destination: Where to place the operator. "project" (default) writes
            to the workspace's local-operators package beside the graph,
            auto-scaffolding the package if needed. "core" writes into the
            Vivid seed catalog (only for primitives that should ship with
            Vivid). "package:<name>" targets a specific installed package.
            An absolute path targets that exact root.
    """
    if await _runtime_is_reachable():
        body: dict = {"name": name, "kind": env, "destination": destination}
        if variant:
            body["variant"] = variant
        return await _post("scaffold_operator", body)
    args = ["scaffold-operator", name, "--env", env]
    if variant:
        args.extend(["--variant", variant])
    if destination and destination != "project":
        args.extend(["--destination", destination])
    args.append("--json")
    return await _run_vivid_cli_json(args)


@mcp.tool()
async def queue_state_transition(sm_node: str, state: int, quantize: str = "bar") -> str:
    """Jump a StateMachine to a specific state, quantized to a beat boundary.

    Args:
        sm_node: Node ID of the StateMachine operator
        state: Target state index (0-based)
        quantize: Timing — "instant", "beat", "bar", or "4bar"
    """
    return await _post("queue_state_transition",
                       {"sm_node": sm_node, "state": state, "quantize": quantize})


@mcp.tool()
async def set_quantize_clock(node_id: str) -> str:
    """Designate a Clock node for beat-synced session, clip, and scene launch quantization.

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
    """List the saved operator-presets for a node, with their curation metadata.

    Returns {"ok": true, "presets": [{"name": ..., "metadata": {category, tags,
    author, source, source_preset_id, notes}}], "msg": "name1, name2"}. Presets
    captured via capture_vst3_preset carry the source factory preset's metadata,
    so you can browse a captured plugin library by character (category/tags) and
    recall_preset the right one.

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
async def ensure_state_mapping(sm_node: str) -> str:
    """Register a StateMachine node as a legacy step-sequencer track (advanced).

    Creates an empty state-preset mapping entry for the SM. This is the legacy
    StateMachine-based launcher; new session work should use Tracks/Clips/Scenes.
    Safe to call if the mapping already exists (no-op).

    Args:
        sm_node: ID of the StateMachine node
    """
    return await _post("ensure_state_mapping", {"sm_node": sm_node})


@mcp.tool()
async def add_clip_track(sm_node: str = "", midi_clip_node: str = "",
                         num_states: int = 4, x: float = 0.0, y: float = -200.0) -> str:
    """Create a StateMachine + MidiClip pair as a legacy step-sequencer track (advanced).

    Creates both nodes, connects MidiClip phase → StateMachine beat_phase for
    timing sync, and calls ensure_state_mapping. This is the legacy StateMachine-based
    launcher; new session work should use Tracks/Clips/Scenes instead.

    Args:
        sm_node: ID for the StateMachine node (auto-generated if empty)
        midi_clip_node: ID for the MidiClip node (auto-generated if empty)
        num_states: number of states (clips) for the StateMachine (1-8)
        x: graph-space X position for the StateMachine node
        y: graph-space Y position for the StateMachine node
    """
    import time
    if not sm_node:
        sm_node = f"sm_track_{int(time.time()) % 10000}"
    if not midi_clip_node:
        midi_clip_node = f"midi_clip_{sm_node}"

    await _post("add_node", {"type": "StateMachine", "node_id": sm_node})
    await _post("set_param", {"node_id": sm_node, "param": "states", "value": num_states})
    await _post("add_node", {"type": "MidiClip", "node_id": midi_clip_node})
    await _post("connect", {"from_addr": f"{midi_clip_node}/phase",
                            "to_addr":   f"{sm_node}/beat_phase"})
    await _post("ensure_state_mapping", {"sm_node": sm_node})
    await _post("set_node_layout", {"node_id": sm_node,        "x": x,         "y": y})
    await _post("set_node_layout", {"node_id": midi_clip_node, "x": x + 280.0, "y": y})
    return f"Created clip track: SM={sm_node}, MidiClip={midi_clip_node}"


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
    return await _run_vivid_cli_json(["package-catalog", "--json"])


@mcp.tool()
async def install_package(url: str) -> str:
    """Install an operator package from a git URL or local path.

    Args:
        url: Git URL (e.g. "https://github.com/user/vivid-drums") or local directory path
    """
    if await _runtime_is_reachable():
        return await _post("install_package", {"url": url})
    return await _run_vivid_cli_json(["install", url, "--json"])


@mcp.tool()
async def uninstall_package(name: str) -> str:
    """Uninstall an operator package by name.

    Args:
        name: Package name (e.g. "vivid-drums")
    """
    if await _runtime_is_reachable():
        return await _post("uninstall_package", {"name": name})
    return await _run_vivid_cli_json(["uninstall", name, "--json"])


@mcp.tool()
async def link_package(path: str) -> str:
    """Link a local package directory for development. Creates a symlink instead
    of copying, so edits to the source are picked up on rebuild without reinstalling.

    Args:
        path: Absolute or relative path to the package directory (must contain vivid-package.json)
    """
    if await _runtime_is_reachable():
        return await _post("link_package", {"path": path})
    return await _run_vivid_cli_json(["link", path, "--json"])


@mcp.tool()
async def unlink_package(name: str) -> str:
    """Unlink a development package. Removes the symlink but does not touch the source directory.

    Args:
        name: Package name (e.g. "vivid-glitch")
    """
    if await _runtime_is_reachable():
        return await _post("unlink_package", {"name": name})
    return await _run_vivid_cli_json(["unlink", name, "--json"])


@mcp.tool()
async def rebuild_package(name: str) -> str:
    """Recompile operators for an installed or linked package. Use after editing
    source files in a linked package.

    Args:
        name: Package name (e.g. "vivid-glitch")
    """
    if await _runtime_is_reachable():
        return await _post("rebuild_package", {"name": name})
    return await _run_vivid_cli_json(["rebuild", name, "--json"])


@mcp.tool()
async def list_packages() -> str:
    """List installed operator packages with their operators."""
    return await _run_vivid_cli_json(["list-packages", "--json"])


@mcp.tool()
async def check_package_updates(core_version: str = "0.1.0", include_all_installed: bool = False) -> str:
    """Check installed packages for available updates and vivid_core compatibility.

    Args:
        core_version: Core version string used for compatibility checks (default "0.1.0")
        include_all_installed: If true, include installed packages even when no update is available
    """
    args = ["package-check-updates", "--core-version", core_version]
    if include_all_installed:
        args.append("--all")
    args.append("--json")
    return await _run_vivid_cli_json(args)


@mcp.tool()
async def check_core_updates(force_refresh: bool = False) -> str:
    """Check for Vivid core application updates from the stable appcast.

    Args:
        force_refresh: If true, bypass cached state and fetch immediately
    """
    args = ["check-core-updates"]
    if force_refresh:
        args.append("--force")
    args.append("--json")
    return await _run_vivid_cli_json(args)


@mcp.tool()
async def read_package_docs(name: str) -> str:
    """Read the README documentation for an installed package."""
    return await _run_vivid_cli_json(["read-package-docs", name, "--json"])


@mcp.tool()
async def list_package_examples(name: str) -> str:
    """List example graphs included with an installed package."""
    return await _run_vivid_cli_json(["list-package-examples", name, "--json"])


@mcp.tool()
async def read_package_example(name: str, filename: str) -> str:
    """Read the content of an example graph from an installed package."""
    return await _run_vivid_cli_json(["read-package-example", name, filename, "--json"])


@mcp.tool()
async def operator_docs(name: str, package: str = "") -> str:
    """Get merged operator docs from source comments plus runtime metadata for one operator. The response includes the operator's display_name (human-facing label, auto-derived from the id when authors don't set one) and any keywords/summary the author has supplied. Set package for installed package operators when needed."""
    args = ["operator-docs", name]
    if package:
        args.extend(["--package", package])
    args.append("--json")
    return await _run_vivid_cli_json(args)


@mcp.tool()
async def package_operator_docs(name: str) -> str:
    """Get source-comment-derived operator docs plus runtime metadata for every operator in an installed package."""
    return await _run_vivid_cli_json(["package-operator-docs", name, "--json"])


@mcp.tool()
async def test_package(name: str) -> str:
    """Run tests for an installed package (graph + C++ tests). Returns per-test pass/fail/skip."""
    if await _runtime_is_reachable():
        async with httpx.AsyncClient() as client:
            resp = await client.post(f"{VIVID_URL}/test_package",
                                      json={"name": name}, timeout=90.0)
            return resp.text
    return await _run_vivid_cli_json(["test-package", name, "--json"])


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


# ---------------------------------------------------------------------------
# Music evaluation tools — semantic LALM-backed audio analysis
# Requires the music_eval sidecar service (services/music_eval/) running on
# VIVID_MUSIC_EVAL_URL (default http://127.0.0.1:9877).
# ---------------------------------------------------------------------------

def _meval_evaluate_response(raw: str, extra_payload: dict, include_payload: bool) -> str:
    """Build compact or full response for evaluate_audio_musically / evaluate_audio_file."""
    try:
        doc = json.loads(raw)
    except (ValueError, TypeError):
        return _json_response({"ok": False, "error": {"code": "invalid_json", "message": "service returned non-JSON"}})
    if not doc.get("ok", False):
        return _json_response(doc)
    out: dict = {
        "ok": True,
        "key": doc.get("key"),
        "tempo_bpm": doc.get("tempo_bpm"),
        "summary": doc.get("summary", ""),
        "mode": doc.get("mode", ""),
    }
    if include_payload:
        out["payload"] = {
            "model_response": doc.get("model_response", ""),
            "cot_trace": doc.get("cot_trace", ""),
            "timing_ms": doc.get("timing_ms", 0),
            "backend": doc.get("backend", ""),
            **extra_payload,
        }
    return _json_response(out)


def _meval_compare_response(raw: str, extra_payload: dict, include_payload: bool) -> str:
    """Build compact or full response for compare_audio_to_intent."""
    try:
        doc = json.loads(raw)
    except (ValueError, TypeError):
        return _json_response({"ok": False, "error": {"code": "invalid_json", "message": "service returned non-JSON"}})
    if not doc.get("ok", False):
        return _json_response(doc)
    out: dict = {
        "ok": True,
        "match_score": doc.get("match_score", 0.0),
        "key_deviations": doc.get("key_deviations", []),
        "summary": doc.get("summary", ""),
    }
    if include_payload:
        out["payload"] = {
            "harmony_match": doc.get("harmony_match"),
            "rhythm_match": doc.get("rhythm_match"),
            "timbre_match": doc.get("timbre_match"),
            "structure_match": doc.get("structure_match"),
            "model_response": doc.get("model_response", ""),
            "timing_ms": doc.get("timing_ms", 0),
            "backend": doc.get("backend", ""),
            **extra_payload,
        }
    return _json_response(out)


@mcp.tool()
async def music_eval_status() -> str:
    """Check reachability and state of the music_eval inference service.

    Returns backend name, model, device, ready flag, and queue depth.
    Mirrors runtime_status() — check this before calling other music eval tools.
    """
    managed = (_managed_music_eval_process is not None
               and _managed_music_eval_process.poll() is None)
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.get(f"{MUSIC_EVAL_URL}/v1/status", timeout=3.0)
            doc = resp.json()
            return _json_response({
                "ok": True, "reachable": True,
                "bridge_managed": managed,
                "pid": _managed_music_eval_process.pid if managed else None,
                **doc,
            })
    except httpx.ConnectError:
        return _json_response({"ok": False, "reachable": False, "error": {
            "code": "service_unavailable",
            "message": (
                f"music_eval service not reachable at {MUSIC_EVAL_URL}. "
                "It will start automatically on the next evaluate/compare call."
            ),
        }})
    except Exception as exc:  # noqa: BLE001
        return _json_response({"ok": False, "reachable": False, "error": {
            "code": "service_unavailable", "message": str(exc),
        }})


@mcp.tool()
async def configure_music_eval_backend(backend: str,
                                        model_path: str = "",
                                        api_key: str = "") -> str:
    """Switch the music_eval inference backend.

    Args:
        backend: One of: stub or gemini.
        model_path: Model ID override for gemini (e.g. "gemini-2.5-pro"). Empty = default (gemini-2.5-flash).
        api_key: Google API key for the gemini backend. Falls back to GOOGLE_API_KEY env var.
    """
    body: dict = {"backend": backend}
    if model_path:
        body["model_path"] = model_path
    if api_key:
        body["api_key"] = api_key
    raw = await _post_music_eval("configure", body, timeout=10.0)
    return raw


@mcp.tool()
async def evaluate_audio_musically(window_seconds: float = 30.0,
                                    node_id: str = "",
                                    mode: str = "caption",
                                    include_payload: bool = False) -> str:
    """Capture live audio and return a music-aware semantic analysis via LALM.

    Requires the music_eval service running (check music_eval_status first).
    Inference is seconds-scale — treat this as a reflective tool, not a meter.

    Args:
        window_seconds: Capture duration (1–60). Defaults to 30.
        node_id: Reserved for future isolated capture support. Must be empty for now.
        mode: Analysis depth — caption (key/tempo/summary, fastest), theory
            (harmony/voice-leading/form), reasoning (chain-of-thought, slowest).
        include_payload: Include raw model response, CoT trace, timing, and backend.
    """
    if mode not in ("caption", "theory", "reasoning"):
        return _json_response({"ok": False, "error": {
            "code": "invalid_mode",
            "message": "mode must be caption, theory, or reasoning",
        }})
    if err_env := _validate_live_music_eval_request(node_id, window_seconds):
        return _json_response(err_env)

    if svc_err := await _ensure_music_eval_service():
        return _json_response({"ok": False, "error": {"code": "service_unavailable", "message": svc_err}})

    audio_body, err = await _fetch_audio_for_music_eval(node_id, window_seconds)
    if err:
        return _json_response({"ok": False, "error": err})

    body = {**audio_body, "mode": mode}
    timeout = max(30.0, window_seconds + 60.0)
    raw = await _post_music_eval("evaluate", body, timeout=timeout)
    return _meval_evaluate_response(raw, {"window_seconds": window_seconds, "node_id": node_id}, include_payload)


@mcp.tool()
async def evaluate_audio_file(path: str,
                               start_seconds: float = 0.0,
                               duration_seconds: float = 30.0,
                               mode: str = "caption",
                               include_payload: bool = False) -> str:
    """Evaluate an audio file on disk with music-aware semantic analysis via LALM.

    Useful for rendered exports, reference tracks, or stems exported from audio_out.
    Requires the music_eval service running (check music_eval_status first).

    Args:
        path: Absolute path to an audio file (.wav, .aiff, .mp3, .flac).
        start_seconds: Start offset within the file.
        duration_seconds: How much to analyze (1–1200).
        mode: caption, theory, or reasoning. See evaluate_audio_musically.
        include_payload: Include raw model response, CoT trace, timing, and backend.
    """
    if mode not in ("caption", "theory", "reasoning"):
        return _json_response({"ok": False, "error": {
            "code": "invalid_mode",
            "message": "mode must be caption, theory, or reasoning",
        }})
    if start_seconds < 0:
        return _json_response({"ok": False, "error": {
            "code": "invalid_window",
            "message": "start_seconds must be >= 0",
        }})
    if duration_seconds <= 0:
        return _json_response({"ok": False, "error": {
            "code": "invalid_window",
            "message": "duration_seconds must be > 0",
        }})
    if not pathlib.Path(path).exists():
        return _json_response({"ok": False, "error": {
            "code": "reference_not_found",
            "message": f"file not found: {path}",
        }})

    if svc_err := await _ensure_music_eval_service():
        return _json_response({"ok": False, "error": {"code": "service_unavailable", "message": svc_err}})

    body = {
        "audio_path": path,
        "start_seconds": float(start_seconds),
        "duration_seconds": float(duration_seconds),
        "mode": mode,
    }
    timeout = max(30.0, duration_seconds + 60.0)
    raw = await _post_music_eval("evaluate", body, timeout=timeout)
    return _meval_evaluate_response(
        raw,
        {"path": path, "start_seconds": start_seconds, "duration_seconds": duration_seconds},
        include_payload,
    )


@mcp.tool()
async def compare_audio_to_intent(reference_path: str = "",
                                   intent: str = "",
                                   window_seconds: float = 30.0,
                                   node_id: str = "",
                                   include_payload: bool = False) -> str:
    """Compare current audio against a reference clip, a free-text intent, or both.

    The primary creative-iteration tool: 'does what I'm rendering match what
    I was going for?' Returns a semantic diff with match score and key deviations.
    Requires the music_eval service running (check music_eval_status first).

    Args:
        reference_path: Path to a reference audio file (.wav, .aiff, .mp3, .flac).
            Optional — provide intent, reference_path, or both.
        intent: Free-text description of the intended sound, e.g. 'dark, sparse,
            tension building'. Optional — provide intent, reference_path, or both.
        window_seconds: Capture duration for the current audio (1–60).
        node_id: Reserved for future isolated capture support. Must be empty for now.
        include_payload: Include per-axis scores (harmony, rhythm, timbre, structure),
            raw model response, and timing.
    """
    if not reference_path and not intent:
        return _json_response({"ok": False, "error": {
            "code": "no_audio_source",
            "message": "Provide at least one of reference_path or intent.",
        }})
    if reference_path and not pathlib.Path(reference_path).exists():
        return _json_response({"ok": False, "error": {
            "code": "reference_not_found",
            "message": f"reference file not found: {reference_path}",
        }})
    if err_env := _validate_live_music_eval_request(node_id, window_seconds):
        return _json_response(err_env)

    if svc_err := await _ensure_music_eval_service():
        return _json_response({"ok": False, "error": {"code": "service_unavailable", "message": svc_err}})

    audio_body, err = await _fetch_audio_for_music_eval(node_id, window_seconds)
    if err:
        return _json_response({"ok": False, "error": err})

    body: dict = {"intent": intent, **audio_body}
    if reference_path:
        body["audio_b_path"] = reference_path
    # Rename captured clip keys to the _a_ variants expected by /v1/compare
    if "audio_b64" in body:
        body["audio_a_b64"] = body.pop("audio_b64")
    if "audio_path" in body:
        body["audio_a_path"] = body.pop("audio_path")

    timeout = max(30.0, window_seconds + 60.0)
    raw = await _post_music_eval("compare", body, timeout=timeout)
    return _meval_compare_response(
        raw,
        {"window_seconds": window_seconds, "node_id": node_id,
         "had_reference": bool(reference_path), "had_intent": bool(intent)},
        include_payload,
    )


def _load_package_tools() -> None:
    """Load MCP tools contributed by installed packages via their mcp_tools manifest field."""
    import importlib.util
    import subprocess as _sp
    import sys

    vivid_bin = _resolve_vivid_bin()
    if not vivid_bin.exists():
        return
    try:
        result = _sp.run(
            [str(vivid_bin), "list-packages", "--json"],
            capture_output=True, text=True, timeout=5,
            env=_runtime_subprocess_env(),
        )
        data = json.loads(result.stdout)
    except Exception:
        return

    for pkg in data.get("packages", []):
        pkg_path = pkg.get("path", "")
        pkg_name = pkg.get("name", "")
        if not pkg_path or not pkg_name:
            continue
        try:
            manifest_path = pathlib.Path(pkg_path) / "vivid-package.json"
            manifest = json.loads(manifest_path.read_text())
            mcp_tools_rel = manifest.get("mcp_tools", "")
            if not mcp_tools_rel:
                continue
            tools_path = pathlib.Path(pkg_path) / mcp_tools_rel
            if not tools_path.exists():
                continue
            spec = importlib.util.spec_from_file_location(
                f"_vivid_pkg_{pkg_name}_tools", tools_path
            )
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            if hasattr(mod, "register"):
                mod.register(mcp, vivid_url=VIVID_URL)
        except Exception as e:
            print(f"[vivid_mcp] Warning: failed to load tools from package '{pkg_name}': {e}",
                  file=sys.stderr)


_load_package_tools()


if __name__ == "__main__":
    _start_heartbeat()
    mcp.run()
