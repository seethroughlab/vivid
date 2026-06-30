"""Vivid PoC — MCP bridge.

A FastMCP (stdio) server that proxies each tool call to the running app's loopback
control server (HTTP, default http://127.0.0.1:9876). Launch the app first, then
this bridge (or let your MCP client launch it). Set VIVID_URL to override the host.

The PoC is a two-surface AV instrument: a DAW (tracks x scenes of clips, each track
an instrument + FX chain) on the left, a rewireable visuals node-graph on the right,
joined by a mapping bridge (audio characteristics -> visual params, and back).
"""
import os
import httpx
from fastmcp import FastMCP

VIVID_URL = os.environ.get("VIVID_URL", "http://127.0.0.1:9876")
mcp = FastMCP("vivid")


def _post(method: str, payload: dict | None = None) -> dict:
    try:
        return httpx.post(f"{VIVID_URL}/{method}", json=payload or {}, timeout=15.0).json()
    except Exception as e:  # noqa: BLE001
        return {"ok": False, "error": f"control server unreachable at {VIVID_URL}: {e}"}


# ---------------- introspection ----------------
@mcp.tool
def status() -> dict:
    """Liveness + a quick summary: track/scene counts, transport bpm/beats, op-node count."""
    return _post("status")


@mcp.tool
def get_version() -> dict:
    """Version + compatibility surface: app_version, operator_abi (a loaded .dylib operator
    must match), session_schema (a saved session is gated against this), and build_type.
    Read this to check whether an operator package or saved session is compatible."""
    return _post("get_version")


@mcp.tool
def get_health() -> dict:
    """Rolled-up engine health snapshot: a top-level severity (ok|warning|error) plus
    gpu (device ok + uncaptured error count + last_error), graph (op_nodes/op_types and
    missing_ops — chain nodes whose operator vanished), packages.loaded, and control.running.
    Poll this to check the running app is healthy before/after driving it."""
    return _post("get_health")


@mcp.tool
def get_session() -> dict:
    """The full session as JSON (window, every track's gain/clips/FX/state, the visuals
    chain + node base params, all mappings, view). The authoritative snapshot to read state."""
    return _post("get_session")


@mcp.tool
def list_tracks() -> dict:
    """Per-track summary: index, stable id, name, gain, is_audio, active/queued clip, live
    level/transient/3-band energy, and the device chain (instrument + FX with their device
    indices). To map a track characteristic to a visual, use its **id** in the source string:
    "track_<id>.<kind>" (kind = level|transient|low|mid|high) — the id survives reorders/deletes,
    the index does not."""
    return _post("list_tracks")


@mcp.tool
def list_params(track: int, device: int = 0, filter: str = "", limit: int = 64) -> dict:
    """List a device's parameters (device 0 = instrument, 1+ = FX index+1). Plugins can expose
    thousands of params — always pass a name `filter` (case-insensitive substring) and a `limit`.
    Returns {index, id, name, value(normalized 0..1)}; use the index with set_param."""
    return _post("list_params", {"track": track, "device": device, "filter": filter, "limit": limit})


@mcp.tool
def list_operators() -> dict:
    """The full catalog of visual operators that can be spawned — built-in AND loaded
    .dylib packages, uniformly. Each entry has: name (pass to add_node), display_name,
    summary, keywords (for search), gpu flag, params [{name, type, default, min, max,
    description}] and ports [{name, dir}]. Call this to DISCOVER what operators exist and
    how to wire/parameterize them before add_node / set_node_param / connect_nodes."""
    return _post("list_operators")


@mcp.tool
def install_operator_package(path: str) -> dict:
    """Install an operator package from a directory (a vivid-package.json + operator .cpp
    sources). Compiles each operator to a loadable .dylib on this machine and registers it
    LIVE — the new operator is immediately spawnable via add_node, no app restart. Returns
    per-operator {name, compiled, registered, op|error}. Call list_operators afterward to
    see the new op's schema. Installed ops also persist + reload on the next launch."""
    return _post("install_operator_package", {"path": path})


@mcp.tool
def get_graph() -> dict:
    """The visuals node-graph: op nodes [{id, op, input, params:[{name, base, value, wired}]}],
    data-source nodes, the active output id, and the generator op. Node ids are stable; build
    mapping dests as "node:<id>.<param_name>"."""
    return _post("get_graph")


@mcp.tool
def get_mappings() -> dict:
    """All bridge mappings: [{src, dst, amount, curve, invert, lo, hi}]."""
    return _post("get_mappings")


@mcp.tool
def list_effects() -> dict:
    """Names of FX plugins that can be added to a track (for add_effect)."""
    return _post("list_effects")


# ---------------- visuals construction ----------------
@mcp.tool
def add_node(op: str) -> dict:
    """Add a visual op node by operator type name. The built-ins are Plasma | Video |
    Feedback | Blur | Output | Tint (Tint is a WGSL example op); the full live list is
    status()['op_types']. Unknown types return bad_arg listing the valid ones. Returns
    the node's stable id. Wire it with connect_nodes; feed the viewer via Output."""
    return _post("add_node", {"op": op})


@mcp.tool
def remove_node(id: int) -> dict:
    """Remove the visual op node with this id (at least one Output is always kept)."""
    return _post("remove_node", {"id": id})


@mcp.tool
def connect_nodes(node_id: int, input_id: int) -> dict:
    """Wire input_id's output texture into node_id's input (input_id = -1 to disconnect).
    Effects (Feedback/Blur) and Output take an input; generators (Plasma/Video) ignore it."""
    return _post("connect_nodes", {"node_id": node_id, "input_id": input_id})


@mcp.tool
def set_generator(op: str) -> dict:
    """Set the generator op (Plasma or Video) for the first generator node."""
    return _post("set_generator", {"op": op})


@mcp.tool
def set_active_output(id: int) -> dict:
    """Make the Output node with this id drive the on-screen viewer."""
    return _post("set_active_output", {"id": id})


@mcp.tool
def set_node_param(node_id: int, name: str, value: float) -> dict:
    """Set a visual node's base param (0..1). Names: Plasma = warp/hue/density/glow;
    Feedback = decay; Blur = radius. Final value = clamp(base + any mapped modulation)."""
    return _post("set_node_param", {"node_id": node_id, "name": name, "value": value})


@mcp.tool
def add_data_node(source: str) -> dict:
    """Place an audio-source node in the graph (cosmetic; mappings work without it). source =
    'master.<kind>' or 'track_<n>.<kind>', kind in level|transient|low|mid|high."""
    return _post("add_data_node", {"source": source})


# ---------------- mapping (the bridge) ----------------
@mcp.tool
def connect_mapping(src: str, dst: str, amount: float = 1.0, curve: float = 0.0,
                    invert: bool = False, lo: float = 0.0, hi: float = 1.0) -> dict:
    """Wire a source to a destination (replaces any existing wire into dst).
    src: 'master.transient' | 'track_2.low' | 'viz.warp' (a visual's value, for the return path).
    dst: 'node:<id>.<param>' (visual param) | 'param:<track>:<device>:<index>' (audio param).
    Shaping: amount (gain), curve (-1 ease-out .. +1 ease-in), invert (polarity), [lo,hi] range."""
    return _post("connect_mapping", {"src": src, "dst": dst, "amount": amount,
                                      "curve": curve, "invert": invert, "lo": lo, "hi": hi})


@mcp.tool
def disconnect_mapping(dst: str) -> dict:
    """Remove the mapping driving this destination."""
    return _post("disconnect_mapping", {"dst": dst})


# ---------------- audio authoring ----------------
@mcp.tool
def set_bpm(bpm: float) -> dict:
    """Set the master tempo (BPM)."""
    return _post("set_bpm", {"bpm": bpm})


@mcp.tool
def launch_clip(track: int, scene: int) -> dict:
    """Launch a track's clip (applied at the next bar)."""
    return _post("launch_clip", {"track": track, "scene": scene})


@mcp.tool
def launch_scene(scene: int) -> dict:
    """Launch a whole scene (its clip on every track) at the next bar."""
    return _post("launch_scene", {"scene": scene})


@mcp.tool
def set_track_gain(track: int, gain: float) -> dict:
    """Set a track's mixer gain (0..1)."""
    return _post("set_track_gain", {"track": track, "gain": gain})


@mcp.tool
def set_param(track: int, device: int, param: int, value: float) -> dict:
    """Set a device param by its index (from list_params). device 0 = instrument, 1+ = FX. value 0..1."""
    return _post("set_param", {"track": track, "device": device, "param": param, "value": value})


@mcp.tool
def set_clip(track: int, scene: int, notes: list[dict], length: float = 4.0) -> dict:
    """Replace a MIDI clip's notes. notes = [{p:pitch, s:startBeat, d:durBeats, v:velocity0..1}, ...];
    length = loop length in beats. (MIDI tracks only — not the audio/sampler track.)"""
    return _post("set_clip", {"track": track, "scene": scene, "notes": notes, "length": length})


@mcp.tool
def add_effect(track: int, name: str) -> dict:
    """Append an FX plugin (by name from list_effects) to a track's chain."""
    return _post("add_effect", {"track": track, "name": name})


@mcp.tool
def remove_effect(track: int, effect: int) -> dict:
    """Remove the FX at this index (0-based) from a track's chain."""
    return _post("remove_effect", {"track": track, "effect": effect})


@mcp.tool
def list_instruments() -> dict:
    """The instrument catalog you can pass to add_track (a label like "Pigments"; a .vst3
    path also works). Call before add_track to see what instruments are available."""
    return _post("list_instruments")


@mcp.tool
def add_track(instrument: str = "", kind: str = "instrument") -> dict:
    """Create a track. kind="instrument" (default) needs `instrument` (a list_instruments
    label or a .vst3 path); kind="audio" makes a sampler track (no instrument). Returns the
    new track index — write clips on it with set_clip(track=...)."""
    payload: dict = {"kind": kind}
    if instrument:
        payload["instrument"] = instrument
    return _post("add_track", payload)


@mcp.tool
def remove_track(track: int) -> dict:
    """Delete a track by index. Tracks below it shift down by one INDEX, but each track keeps
    its stable `id` (see list_tracks): audio->visual mappings reference the id, so only the
    deleted track's mappings are dropped (see mappings_dropped) — every other wire still
    follows its own track."""
    return _post("remove_track", {"track": track})


# ---------------- session author / persist ----------------
@mcp.tool
def save_session(path: str) -> dict:
    """Save the full session to a JSON file on the app's machine."""
    return _post("save_session", {"path": path})


@mcp.tool
def load_session(path: str = "", session: dict | None = None) -> dict:
    """Load a session — either from a file `path`, or inline by passing a `session` JSON object
    (the shape returned by get_session). Restores state onto the existing tracks by index."""
    payload: dict = {}
    if session is not None:
        payload["session"] = session
    if path:
        payload["path"] = path
    return _post("load_session", payload)


@mcp.tool
def get_authoring_guide() -> dict:
    """How to compose an audiovisual scene with these tools (recipe + gotchas)."""
    return {
        "overview": "Vivid PoC = a DAW (tracks x scenes of clips; each track has an instrument + FX) "
                    "wired to a rewireable visuals node-graph via a mapping bridge.",
        "recipe": [
            "1. READ: status, list_tracks, get_graph, get_mappings to see current state.",
            "2. TEMPO: set_bpm(bpm).",
            "3. NOTES: set_clip(track, scene, notes=[{p,s,d,v}], length) on a MIDI track, then launch_clip.",
            "4. SOUND: list_params(track, 0, filter='cutoff') then set_param(track, 0, index, value).",
            "5. VISUALS: the default chain is Plasma->Feedback->Blur->Output. add_node / connect_nodes "
            "to extend it; set_node_param for base look; set_active_output to pick the viewer source.",
            "6. BRIDGE: connect_mapping(src, dst) — e.g. src='master.transient', dst='node:<id>.warp'. "
            "Visual node ids + param names come from get_graph; dst for audio params is 'param:T:D:index'.",
            "7. RETURN PATH: src can be 'viz.<name>' to drive an audio param from a visual value.",
            "8. VERIFY: get_graph / get_mappings to confirm; read list_tracks for live level/bands.",
            "9. PERSIST: save_session(path) / load_session(path or inline session).",
        ],
        "errors": "Every reply has an 'ok' bool. Failures are {ok:false, code, error}: "
                  "branch on the stable `code` (bad_json, unknown_method, no_session, no_graph, "
                  "no_vgraph, no_transport, bad_arg, out_of_range, not_found, io_error, internal, "
                  "timeout), not the human `error` text. An out-of-range track/scene/device index "
                  "now returns out_of_range instead of silently succeeding.",
        "gotchas": {
            "params_are_huge": "Plugins expose thousands of params; always filter+limit list_params.",
            "param_index": "set_param takes the *index* from list_params (mapped to the VST id internally).",
            "mapping_dest": "visual = 'node:<id>.<param>'; audio = 'param:<track>:<device>:<index>'.",
            "modulation": "A wired visual param = clamp(base + modulation); set the base with set_node_param.",
            "audio_track": "set_clip only applies to MIDI tracks (is_audio=false in list_tracks).",
        },
    }


if __name__ == "__main__":
    mcp.run()
