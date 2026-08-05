"""Vivid — MCP bridge.

A FastMCP (stdio) server that proxies each tool call to the running app's loopback
control server (HTTP, default http://127.0.0.1:9876). Launch the app first, then
this bridge (or let your MCP client launch it). Set VIVID_URL to override the host.

Vivid is a two-surface AV instrument: a DAW (tracks x scenes of clips, each track
an instrument + FX chain) on the left, a rewireable visuals node-graph on the right,
joined by a mapping bridge (audio characteristics -> visual params, and back).
"""
import os
import sys
import time
import httpx
from fastmcp import FastMCP

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # import sibling theory.py
import theory  # noqa: E402  — pure-Python music-theory helpers (chords/scales/rhythm)

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
def get_perf() -> dict:
    """Whole-frame performance read-out (EMA-smoothed): {frame_ms, fps}. Used by the operator-audit
    harness to time per-operator frame cost via an A/B (with/without the op) delta."""
    return _post("get_perf")


@mcp.tool
def inspect_session_overview(detail: str = "summary") -> dict:
    """Summary-first project/session inspection for agents. Aggregates transport, project,
    tracks, visual graph, mappings count, and warnings in one call. detail='summary' keeps clips
    and mappings compressed; 'normal' includes mappings; 'full' also includes per-track clip
    summaries for every scene."""
    return _post("inspect_session_overview", {"detail": detail})


@mcp.tool
def inspect_track(track: int, detail: str = "normal") -> dict:
    """Inspect one track by index: stable id, kind, gain, active/queued clip, live analysis
    signals, devices, optional per-scene clip summaries, and audio-graph summary when present."""
    return _post("inspect_track", {"track": track, "detail": detail})


@mcp.tool
def inspect_scene(scene: int, detail: str = "normal") -> dict:
    """Inspect one scene/grid row: each track's clip slot, whether it is active/queued,
    note count or audio length, and a concise scene summary."""
    return _post("inspect_scene", {"scene": scene, "detail": detail})


@mcp.tool
def explain_scene(scene: int) -> dict:
    """Narrative explanation of a scene's musical/audio material, active/queued clips, and
    session-level mappings that may react to it."""
    return _post("explain_scene", {"scene": scene})


@mcp.tool
def inspect_signal_flow(scope: str = "session") -> dict:
    """Summary-first signal-flow inspection: visual graph shape, audio analysis sources,
    audio graph summaries, and every audio/control mapping."""
    return _post("inspect_signal_flow", {"scope": scope})


@mcp.tool
def explain_signal_flow(scope: str = "session") -> dict:
    """Narrative explanation of how audio/control signals currently drive visual or audio
    parameters. Use this before editing mappings or explaining why visuals react."""
    return _post("explain_signal_flow", {"scope": scope})


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
def list_quality_checks() -> dict:
    """List the built-in quality checks run_quality_check can run: no_audio_clipping,
    nonblank_visual_output, mappings_resolve, no_quarantined_operators. The proof loop — run a check
    after an edit to verify the result rather than guessing."""
    return _post("list_quality_checks")


@mcp.tool
def run_quality_check(name: str = "all") -> dict:
    """Run a quality check (or 'all') and get pass|warn|fail + concise evidence + a suggested next
    action. Composes audio analysis, visual analysis, mapping resolution, and quarantine state into a
    verdict, so an agent can confirm an edit held up (not clipping, not blank, mappings intact)."""
    return _post("run_quality_check", {"name": name})


@mcp.tool
def list_quarantine() -> dict:
    """ADR-0018: the operators quarantined this launch — repeat crashers (>=3 crashes in 24h),
    disabled by default so they can't brick the session. Each entry has operator, crash_count,
    last_seen. A quarantined op's graph nodes load as 'not registered' (see get_health missing_ops)."""
    return _post("list_quarantine")


@mcp.tool
def unquarantine(op: str) -> dict:
    """ADR-0018: clear an operator's crash history so it is re-enabled on the next launch (undo a
    quarantine). `op` is the operator type name. Returns how many crash records were removed;
    restart the app for the change to take effect."""
    return _post("unquarantine", {"op": op})


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
    summary, keywords (for search), gpu flag, params and ports. A param carries {name,
    type, default, min, max} plus, when the operator declares them, semantic hints —
    semantic_tag (e.g. 'frequency_hz'), semantic_unit ('Hz'), semantic_intent (e.g.
    'brightness'), display_hint, description — so you can choose params by INTENT, not by
    guessing names. Call this to DISCOVER operators before add_node / set_node_param /
    connect_nodes."""
    return _post("list_operators")


@mcp.tool
def list_operator_catalog(domain: str = "all", kind: str = "all", detail: str = "summary") -> dict:
    """Unified operator/plugin catalog. domain=all|visual|audio; kind can be gpu_visual,
    instrument, audio_effect, note_effect, modulator, plugin, or all. Every entry carries its
    domain, kind, and a spawn affordance. detail=summary (default) is a compact listing; detail=full
    adds each op's params/ports/keywords schema (use it, or find_params, when wiring by intent)."""
    return _post("list_operator_catalog", {"domain": domain, "kind": kind, "detail": detail})


@mcp.tool
def find_operators(query: str, domain: str = "all", kind: str = "all") -> dict:
    """Search the unified operator catalog by name, summary, keywords, kind, domain, plugin
    format/class, and parameter semantic metadata."""
    return _post("find_operators", {"query": query, "domain": domain, "kind": kind})


@mcp.tool
def find_params(query: str, scope: str = "all") -> dict:
    """Search live mappable parameters by name/intent across visual nodes and audio targets.
    scope=all|visual|audio. Returns ready-to-use mapping destination strings."""
    return _post("find_params", {"query": query, "scope": scope})


@mcp.tool
def list_shaders() -> dict:
    """The shader library: every .wgsl/.glsl file the app found. A shader FILE *is* an
    operator (ADR-0016) — its JSON header declares its params — so a registered shader also
    appears in list_operators with its full schema and is spawned with add_node like any
    other op. Use THIS tool to see the files themselves: {name, path, tier (user/project/
    bundled), summary, registered} plus, for a file that failed to parse, the error that
    kept it out of the catalog. Also returns the search_path the app scans."""
    return _post("list_shaders")


@mcp.tool
def reload_shaders() -> dict:
    """Re-walk the shader search path for files the app has not seen yet. Edits to shaders it
    already knows about are picked up automatically (a body edit recompiles in place; a header
    edit rebuilds that op's nodes, keeping their param values), so this is only for brand-new
    files you would rather not wait for. Returns {added, count}."""
    return _post("reload_shaders")


@mcp.tool
def fork_shader(op: str, new_name: str) -> dict:
    """Fork a shader into the user's shader folder under a new name and register it LIVE —
    immediately spawnable with add_node, and editable: saving the file hot-reloads it into
    every node using it. This is how you customize a shipped shader without touching it (the
    original stays; the fork is a separate operator). Returns {op, path} — edit that path."""
    return _post("fork_shader", {"op": op, "new_name": new_name})


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
    """The visuals node-graph: op nodes [{id, op, input, x, y, params:[{name, base, value, wired}]}],
    data-source nodes, the active output id, and the generator op. Node ids are stable; build
    mapping dests as "node:<id>.<param_name>"."""
    return _post("get_graph")


@mcp.tool
def layout_graph() -> dict:
    """Auto-arrange the op nodes into a tidy layered left->right layout (rank by depth along
    the input chain; the 'Re-layout' button). Returns {nodes}. Positions show up in get_graph."""
    return _post("layout_graph")


@mcp.tool
def undo() -> dict:
    """Undo the last document edit (ADR-0017). One app-wide history covers the visual graph + the
    mapping bridge (audio-session edits land in a later phase); performance actions (play/launch/arm)
    are not undoable. Returns {did, can_undo, can_redo, undo_label, redo_label}."""
    return _post("undo")


@mcp.tool
def redo() -> dict:
    """Redo the edit most recently undone. Returns {did, can_undo, can_redo, undo_label, redo_label}."""
    return _post("redo")


@mcp.tool
def get_mappings() -> dict:
    """All bridge mappings: [{src, dst, amount, curve, invert, lo, hi}]."""
    return _post("get_mappings")


@mcp.tool
def list_mapping_sources() -> dict:
    """The valid bridge SOURCES you can wire with connect_mapping — every audio characteristic
    for master + each live track (by stable id), as ready-to-use source strings. Returns
    {sources:[{source, kind, label, range, description}]} (e.g. 'master.transient',
    'track_2.low'; all 0..1). Destinations are params from list_operators / list_audio_ops:
    build 'node:<id>.<param>' (visual) or 'param:<track>:<device>:<index>' (audio)."""
    return _post("list_mapping_sources")


@mcp.tool
def list_mapping_destinations(scope: str = "all") -> dict:
    """Discover valid mapping DESTINATIONS instead of inventing destination strings. scope =
    visual|audio|all. Returns visual node params ('node:<id>.<param>'), hosted device params
    ('param:<track>:<device>:<index>'), native audio-op params ('aparam:<track>:<index>:<param>'),
    and audio-graph node params ('gnode:<track>:<node>:<param>')."""
    return _post("list_mapping_destinations", {"scope": scope})


@mcp.tool
def inspect_mappings(detail: str = "summary") -> dict:
    """Inspect the current audio->visual mappings as first-class relationships, with readable
    source/destination labels and shaping values. detail='normal' also includes source and
    destination affordances. (Matches the 'mapping' convention: get_mappings / connect_mapping /
    explain_mapping.)"""
    return _post("inspect_mappings", {"detail": detail})


@mcp.tool
def inspect_bindings(detail: str = "summary") -> dict:
    """Deprecated alias of inspect_mappings (kept for back-compat) — "mapping" is the product-wide
    bridge noun. Prefer inspect_mappings. (Routes to the inspect_bindings handler, same result.)"""
    return _post("inspect_bindings", {"detail": detail})


@mcp.tool
def explain_mapping(src: str = "", dst: str = "") -> dict:
    """Explain one or more existing mappings. Pass src, dst, or both to filter; empty values
    explain all current mappings."""
    return _post("explain_mapping", {"src": src, "dst": dst})


@mcp.tool
def suggest_mappings(intent: str = "", scene: int | None = None,
                     source_scope: str = "all", dest_scope: str = "visual") -> dict:
    """Conservative mapping suggestions from current live sources to valid destinations.
    Today this ranks simple level/transient/band relationships against visual params; later
    phases can use real audio analysis and scene context."""
    payload = {"intent": intent, "source_scope": source_scope, "dest_scope": dest_scope}
    if scene is not None:
        payload["scene"] = scene
    return _post("suggest_mappings", payload)


@mcp.tool
def list_effects() -> dict:
    """Names of the curated FX plugins offered in the UI (for add_effect). For the FULL
    set of installed plugins use list_plugins."""
    return _post("list_effects")


@mcp.tool
def list_plugins() -> dict:
    """Every plugin installed on this machine (VST3 today): [{name, path, format}].
    Use a plugin's `path` as add_track(instrument=path) for an instrument, or
    add_effect(track, name=path) for an effect — the loader validates the type on add.
    (list_instruments / list_effects are the smaller curated menus shown in the UI.)"""
    return _post("list_plugins")


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
def set_node_asset(id: int, asset: str) -> dict:
    """DEPRECATED (no-op since ADR-0016). The legacy host-side asset channel — no operator
    implements it any more; CustomShader now takes its file through a FILE param. Use
    set_node_file_param(id, "file", "<path>.glsl") instead. Kept only so old scripts don't error."""
    return _post("set_node_asset", {"id": id, "asset": asset})


@mcp.tool
def set_node_param(node_id: int, name: str, value: float) -> dict:
    """Set a visual node's base param (0..1). Names: Plasma = warp/hue/density/glow;
    Feedback = decay; Blur = radius. Final value = clamp(base + any mapped modulation)."""
    return _post("set_node_param", {"node_id": node_id, "name": name, "value": value})


@mcp.tool
def set_node_file_param(node_id: int, name: str, value: str) -> dict:
    """Set a visual node's FILE/TEXT param (a file path string). Used by the file-backed ops:
    Image (name="file" -> a PNG/JPG), CustomShader (name="file" -> a .glsl), and Text/VectorText
    (name="file" -> a .txt whose contents are the rendered string). The op reloads on change and
    degrades to a no-op / fallback if the file is missing or fails. Persisted with save_project."""
    return _post("set_node_file_param", {"node_id": node_id, "name": name, "value": value})


@mcp.tool
def save_node_preset(node_id: int, name: str) -> dict:
    """Save a visual node's current params as a named preset (ADR-0021). A node preset is a param
    name->value snapshot (distinct from the plugin list_presets/load_preset flow, which loads opaque
    per-instrument binary state). Stored per op type under the user data dir; recall with
    load_node_preset. Persisted by param NAME, so a later shader-header edit that adds/removes a param
    doesn't scramble it."""
    return _post("save_node_preset", {"node_id": node_id, "name": name})


@mcp.tool
def list_node_presets(node_id: int = -1, op_type: str = "") -> dict:
    """List the presets for a node's op type — pass either a node_id or an explicit op_type. Returns
    each preset's name and whether it's a factory (shipped, read-only) or user preset."""
    body = {}
    if node_id >= 0: body["node_id"] = node_id
    if op_type: body["op_type"] = op_type
    return _post("list_node_presets", body)


@mcp.tool
def load_node_preset(node_id: int, name: str) -> dict:
    """Apply a named preset to a visual node. Sets every param the preset names that the node still
    has (params the node no longer has are skipped). Returns the count applied."""
    return _post("load_node_preset", {"node_id": node_id, "name": name})


@mcp.tool
def add_data_node(source: str) -> dict:
    """Place an audio-source node in the graph (cosmetic; mappings work without it). source =
    'master.<kind>' or 'track_<n>.<kind>', kind in level|transient|low|mid|high."""
    return _post("add_data_node", {"source": source})


# ---------------- annotations + labels (ADR-0033 P5) ----------------
@mcp.tool
def set_node_name(node_id: int, name: str) -> dict:
    """Rename a visual graph node — a user label shown on its card instead of the op type. An empty
    name resets it to the op type. Persists with the session; reported by get_graph. Undoable."""
    return _post("set_node_name", {"node_id": node_id, "name": name})


@mcp.tool
def add_annotation(x: float = 560.0, y: float = 488.0, text: str = "") -> dict:
    """Add a sticky note to the visual graph canvas at world position (x,y) — free-floating
    explainability text (not a graph node; no wiring). Returns its id. Persists; undoable.
    Use it to leave intent in the session for a human or another agent."""
    return _post("add_annotation", {"x": x, "y": y, "text": text})


@mcp.tool
def set_annotation_text(id: int, text: str) -> dict:
    """Set a sticky note's text (id from add_annotation / get_graph annotations). Undoable."""
    return _post("set_annotation_text", {"id": id, "text": text})


@mcp.tool
def move_annotation(id: int, x: float, y: float) -> dict:
    """Move a sticky note to world position (x,y). Undoable."""
    return _post("move_annotation", {"id": id, "x": x, "y": y})


@mcp.tool
def remove_annotation(id: int) -> dict:
    """Delete a sticky note by id. Undoable."""
    return _post("remove_annotation", {"id": id})


@mcp.tool
def duplicate_nodes(ids: list[int], dx: float = 24.0, dy: float = 24.0) -> dict:
    """Duplicate visual graph nodes (by id) at a small offset. Each copy gets a FRESH id with the
    original's params, file params, curated pins, and asset. Edges strictly between the copied nodes
    are recreated; edges to nodes outside the set are dropped. Incoming audio->param mappings are
    replicated onto the copies (a duplicated reactive node keeps reacting). Returns {ids:[new ids]}.
    Undoable. (Visual graph only for now; audio-node duplication is a follow-up.)"""
    return _post("duplicate_nodes", {"ids": ids, "dx": dx, "dy": dy})


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
def map_audio_to_visual_param(source: str = "track", characteristic: str = "",
                              node_id: int = -1, param: str = "",
                              track: int | None = None, track_id: int | None = None,
                              track_name: str = "", amount: float = 1.0,
                              curve: float = 0.0, invert: bool = False,
                              lo: float = 0.0, hi: float = 1.0) -> dict:
    """First-class bridge helper: map an audio characteristic to a visual param without hand-building
    raw source/destination strings. source='track' uses one of track, track_id, or track_name plus a
    characteristic (level|transient|low|mid|high|note|velocity|gate). source='master' uses
    characteristic level|transient|low|mid|high. node_id and param identify the visual destination.
    Returns the canonical src/dst strings plus readable source/destination info."""
    payload = {"source": source, "characteristic": characteristic, "node_id": node_id,
               "param": param, "amount": amount, "curve": curve, "invert": invert,
               "lo": lo, "hi": hi}
    if track is not None:
        payload["track"] = track
    if track_id is not None:
        payload["track_id"] = track_id
    if track_name:
        payload["track_name"] = track_name
    return _post("map_audio_to_visual_param", payload)


@mcp.tool
def disconnect_mapping(dst: str) -> dict:
    """Remove the mapping driving this destination."""
    return _post("disconnect_mapping", {"dst": dst})


@mcp.tool
def connect_mapping_by_intent(source_intent: str, dest_intent: str, amount: float = 1.0,
                              curve: float = 0.0, invert: bool = False) -> dict:
    """Wire a mapping from intent words on both sides. source_intent picks an audio characteristic
    ('kick'/'punch'/'onset' -> master.transient; 'bass'/'sub' -> master.low; 'bright'/'hat' ->
    master.high; 'mid'/'vocal' -> master.mid; else master.level). dest_intent matches a visual/audio
    param by keyword. Conservative best-match; use list_mapping_destinations + connect_mapping for
    exact control. Returns the resolved src/dst and their labels."""
    return _post("connect_mapping_by_intent", {"source_intent": source_intent, "dest_intent": dest_intent,
                                               "amount": amount, "curve": curve, "invert": invert})


@mcp.tool
def set_param_by_intent(intent: str, value: float, target: str = "") -> dict:
    """Set a param by intent across visual + audio. intent = a param name/keyword; optional target
    narrows by domain/op/device (e.g. 'visual', an op name, or a device name). Resolves one param
    (exact name preferred), clamps value to its range, and routes the set to the right setter. Use
    find_params to inspect candidates first."""
    payload = {"intent": intent, "value": value}
    if target:
        payload["target"] = target
    return _post("set_param_by_intent", payload)


# ---------------- audio authoring ----------------
@mcp.tool
def set_bpm(bpm: float) -> dict:
    """Set the master tempo (BPM)."""
    return _post("set_bpm", {"bpm": bpm})


@mcp.tool
def play() -> dict:
    """Start the transport (playback runs; clips advance). status.playing reflects it."""
    return _post("set_playing", {"playing": True})


@mcp.tool
def stop() -> dict:
    """Pause the transport (the clock freezes; clips stop advancing). Pair with reset_transport
    for a full stop back to the top."""
    return _post("set_playing", {"playing": False})


@mcp.tool
def toggle_play() -> dict:
    """Toggle play/stop; returns the new `playing` state."""
    return _post("toggle_play")


@mcp.tool
def reset_transport() -> dict:
    """Return the transport to the top (bar 1 / beat 0)."""
    return _post("reset_transport")


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
def set_master_gain(gain: float) -> dict:
    """Set the master node's gain (the session's single sink; 1.0 = unity).

    The master node sums every track's output; its gain + meters are reported under
    the 'master' key of list_tracks."""
    return _post("set_master_gain", {"gain": gain})


@mcp.tool
def set_launch_quantize(bars: int) -> dict:
    """Set the scene-launch quantization, in bars.

    A queued scene switch (launch_scene / launch_clip) takes effect at the next N-bar
    boundary instead of switching mid-clip. bars=1 is the default (next bar); 4 is a
    common phrase length ("let the current phrase finish before switching"). Persisted
    with the project; reported as 'launch_quantum_bars' in get_session. bars must be >= 1."""
    return _post("set_launch_quantize", {"bars": bars})


@mcp.tool
def set_track_mute(track: int, mute: bool = True) -> dict:
    """Mute/unmute a track in the master mix. The track's own meter stays pre-mute.

    Reported as 'mute' per track in list_tracks."""
    return _post("set_track_mute", {"track": track, "mute": mute})


@mcp.tool
def set_track_solo(track: int, solo: bool = True) -> dict:
    """Solo/unsolo a track: while any track is soloed, only soloed (and non-muted) tracks
    are heard. Reported as 'solo' per track in list_tracks."""
    return _post("set_track_solo", {"track": track, "solo": solo})


@mcp.tool
def audio_set_warp(track: int, scene: int, enabled: bool = True, mode: str = "complex") -> dict:
    """Enable/disable warping on an audio clip. mode: 'complex' (pitch-preserving), 'beats'
    (transient-aware, tight for drums), or 'repitch' (tape-style, pitch follows tempo)."""
    return _post("audio_set_warp", {"track": track, "scene": scene, "enabled": enabled, "mode": mode})


@mcp.tool
def audio_set_pitch(track: int, scene: int, semitones: float) -> dict:
    """Transpose an audio clip by `semitones` (pitch-preserved; applies in complex/beats warp)."""
    return _post("audio_set_pitch", {"track": track, "scene": scene, "semitones": semitones})


@mcp.tool
def audio_set_gain(track: int, scene: int, gain: float) -> dict:
    """Set an audio clip's gain (0..4)."""
    return _post("audio_set_gain", {"track": track, "scene": scene, "gain": gain})


@mcp.tool
def audio_set_reverse(track: int, scene: int, on: bool = True) -> dict:
    """Play an audio clip backwards."""
    return _post("audio_set_reverse", {"track": track, "scene": scene, "on": on})


@mcp.tool
def audio_auto_warp(track: int, scene: int, sensitivity: float = 0.5) -> dict:
    """Auto-warp an audio clip: detect its transients, place warp markers at the beats, and
    enable Complex warp so it locks to the project tempo. Returns the marker count."""
    return _post("audio_auto_warp", {"track": track, "scene": scene, "sensitivity": sensitivity})


@mcp.tool
def capture_audio(source: str = "master", duration_beats: float | None = None,
                  duration_seconds: float | None = None, start: str = "now",
                  track: int | None = None) -> dict:
    """Snapshot recent live audio from the bounded runtime capture buffer. source='master'
    captures the mix; pass track=<index> or source='track:<index>' for a post-gain track tap."""
    payload = {"source": source, "start": start}
    if track is not None:
        payload["track"] = track
    if duration_beats is not None:
        payload["duration_beats"] = duration_beats
    if duration_seconds is not None:
        payload["duration_seconds"] = duration_seconds
    return _post("capture_audio", payload)


@mcp.tool
def analyze_audio(source: str = "master", track: int | None = None, scene: int | None = None,
                  windows: int = 16) -> dict:
    """Analyze audio. With track+scene, analyzes that audio clip's PCM. Without them, returns
    measurements over recent live master or track audio from the bounded runtime capture buffer."""
    payload = {"source": source, "windows": windows}
    if track is not None:
        payload["track"] = track
    if scene is not None:
        payload["scene"] = scene
    return _post("analyze_audio", payload)


@mcp.tool
def analyze_audio_clip(track: int, scene: int, windows: int = 16) -> dict:
    """Analyze an audio clip's decoded PCM off the audio thread. Returns RMS, peak, clipping,
    crest factor, silence ratio, 3-band proxy, spectral/flux proxies, transient density,
    strongest onsets, tempo estimate, and energy windows."""
    return _post("analyze_audio_clip", {"track": track, "scene": scene, "windows": windows})


@mcp.tool
def analyze_audio_file(path: str, windows: int = 16) -> dict:
    """Decode and analyze an audio file using the same Phase 4 measurements as
    analyze_audio_clip."""
    return _post("analyze_audio_file", {"path": path, "windows": windows})


@mcp.tool
def detect_onsets(track: int | None = None, scene: int | None = None,
                  path: str = "", sensitivity: float = 0.5) -> dict:
    """Detect onset/transient times for an audio clip or file. Pass path for file analysis,
    pass track+scene for an audio clip, or omit both to use recent master audio."""
    payload = {"sensitivity": sensitivity}
    if path:
        payload["path"] = path
    if track is not None:
        payload["track"] = track
    if scene is not None:
        payload["scene"] = scene
    return _post("detect_onsets", payload)


@mcp.tool
def summarize_mix(source: str = "master", duration_beats: float | None = None,
                  duration_seconds: float | None = None, track: int | None = None) -> dict:
    """Summarize recent live audio from the bounded runtime capture buffer. Defaults to the
    master mix; pass track=<index> or source='track:<index>' for a per-track tap."""
    payload = {"source": source}
    if track is not None:
        payload["track"] = track
    if duration_beats is not None:
        payload["duration_beats"] = duration_beats
    if duration_seconds is not None:
        payload["duration_seconds"] = duration_seconds
    return _post("summarize_mix", payload)


@mcp.tool
def analyze_spectrum(bands: str = "octave", source: str = "master",
                     track: int | None = None, scene: int | None = None,
                     path: str = "", duration_seconds: float | None = None) -> dict:
    """Per-band energy spectrum via a bandpass filterbank (real band energy, no FFT).
    bands: 'octave' (10 bands 31Hz..16kHz) | 'mel' (24 mel-spaced) | 'linear' (16 equal to Nyquist).
    Source: track+scene for a clip, path for a file, else recent live master/track audio. Returns
    bands[{center_hz, rms, db}] + spectral_centroid_hz + the loudest band."""
    payload = {"bands": bands, "source": source}
    if track is not None:
        payload["track"] = track
    if scene is not None:
        payload["scene"] = scene
    if path:
        payload["path"] = path
    if duration_seconds is not None:
        payload["duration_seconds"] = duration_seconds
    return _post("analyze_spectrum", payload)


@mcp.tool
def compare_audio(a: dict, b: dict, windows: int = 16) -> dict:
    """Before/after comparison of two audio sources. Each of a, b is a source spec:
    {"track": t, "scene": s} for a clip, {"path": "..."} for a file, or
    {"source": "master", "duration_seconds": n} for recent live audio. Returns each analysis, the
    deltas (loudness_db, brightness, transient density, clipping, low/mid/high bands), and a plain
    'B vs A' verdict — the core edit→perceive→compare tool."""
    return _post("compare_audio", {"a": a, "b": b, "windows": windows})


# ---------------- visual perception (ADR-0024 Phase 6) ----------------
@mcp.tool
def capture_frame(path: str = "") -> dict:
    """Capture the ACTIVE visual output to a PNG you can view, and report whether it is blank.
    path optional (else saved under the user data dir's captures/). Returns {captured, width, height,
    path, is_blank, brightness}. captured=false with a reason means nothing feeds the Output node
    (an empty canvas). The eyes: use it to confirm a visual edit produced something on screen."""
    payload: dict = {}
    if path:
        payload["path"] = path
    return _post("capture_frame", payload)


# ---------------- video export (realtime AV) ----------------
@mcp.tool
def export_video(path: str, seconds: float, fps: float = 60.0) -> dict:
    """Record the live visual output + master audio to an AV-synced video for `seconds`, then auto-stop.
    Returns immediately with {status:"recording", path, seconds, width, height}; poll video_export_status
    until recording:false, then the file is finalized. `path` must be absolute and end in .mp4 (default,
    web-friendly) or .mov. Something must be PLAYING for the capture to have motion/audio. The one-call
    way to grab a showcase clip: export_video → poll → done."""
    return _post("export_video", {"path": path, "seconds": seconds, "fps": fps})


@mcp.tool
def start_video_export(path: str, fps: float = 60.0) -> dict:
    """Begin a MANUAL AV video export (records until stop_video_export). Returns {status:"recording",
    path, width, height}. `path` absolute, .mp4 or .mov. Use export_video instead when you know the
    duration up front; use this pair to bracket an arbitrary live session."""
    return _post("start_video_export", {"path": path, "fps": fps})


@mcp.tool
def stop_video_export() -> dict:
    """Finalize the current manual video export. Returns {path, frames, duration_sec}. Errors if no
    export is running."""
    return _post("stop_video_export", {})


@mcp.tool
def video_export_status() -> dict:
    """Poll the current/last video export. Returns {recording, path, frames, elapsed_sec, width, height}.
    After export_video, poll this until recording:false to know the file is written."""
    return _post("video_export_status", {})


@mcp.tool
def analyze_frame(path: str = "") -> dict:
    """Structured perception of the active visual output (or a saved image via path). Returns
    {is_blank, blank_reason, brightness, contrast, activity, dominant_colors, color_spread, hash}.
    Verify 'is it blank?' / how bright / how busy the output is without eyeballing a file."""
    payload: dict = {}
    if path:
        payload["path"] = path
    return _post("analyze_frame", payload)


@mcp.tool
def compare_frames(a: dict, b: dict) -> dict:
    """Before/after visual comparison. Each of a, b is a frame spec: {"path": "before.png"} for a saved
    image, or {} to capture the current output. Returns each analysis, deltas (hash_hamming distance
    0..64, brightness/contrast/activity), and a plain 'B vs A' verdict — did the look actually change?"""
    return _post("compare_frames", {"a": a, "b": b})


@mcp.tool
def compare_variations(a: dict, b: dict) -> dict:
    """Compare two variations across audio AND visual at once. Each of a, b is
    {"audio": <source spec>, "frame": <frame spec>} — a dimension is compared only when both sides
    supply it. Composes compare_audio + compare_frames into one before/after verdict spanning both."""
    return _post("compare_variations", {"a": a, "b": b})


@mcp.tool
def explain_tradeoffs(a: dict, b: dict, criteria: list[str] | None = None) -> dict:
    """Same inputs as compare_variations, but articulate the notable differences as measured tradeoffs
    (louder but clips more; brighter but busier; ...). Each tradeoff has an aspect, delta, and a
    good/bad note. Optional criteria (aspect keywords: 'loudness','brightness','clipping','transients',
    'change','activity') narrows what to emphasize."""
    payload: dict = {"a": a, "b": b}
    if criteria:
        payload["criteria"] = criteria
    return _post("explain_tradeoffs", payload)


@mcp.tool
def analyze_visual_motion(duration_seconds: float = 2.0) -> dict:
    """Measure motion/change in the visual output over a short window. Each call samples the LIVE output;
    poll it a few times across your window to accumulate samples (motion = the inter-sample change).
    Returns motion_score (0..1), inter_frame_change, is_moving, samples, span_seconds. The first call
    just seeds the window — call again to get a reading."""
    return _post("analyze_visual_motion", {"duration_seconds": duration_seconds})


@mcp.tool
def summarize_visual_output(duration_seconds: float = 2.0) -> dict:
    """Rolled-up view of the live visual output in one call: the current frame's perception (brightness,
    contrast, activity, dominant colors, blank state) plus recent motion. A quick 'what's on screen, and
    is it moving?' check."""
    return _post("summarize_visual_output", {"duration_seconds": duration_seconds})


@mcp.tool
def get_audio_graph(track: int) -> dict:
    """Read a track's audio signal graph — the authoritative topology the RT engine runs
    (nodes + edges), distinct from the linear device list (list_audio_ops). Returns
    {graph_ok, nodes:[{id, kind, type}], edges:[{from, to}], output_id}. kind is
    'instrument' | 'effect' | 'output'; ids are stable across rebuilds; edges are node-id
    pairs. graph_ok is false for VST3 / inline tracks (empty graph)."""
    return _post("get_audio_graph", {"track": track})


# ---------------- native audio operators ----------------
# The audio peer of the visual operator surface: native instruments + effects on a track,
# discovered via list_audio_operators, built with these tools. Distinct from VST3 devices.
@mcp.tool
def list_audio_operators() -> dict:
    """The catalog of native audio operators available to build a track's audio chain, split
    into {instruments:[...], effects:[...]} — the audio peer of list_operators (which is
    visual). Each entry carries the full schema (name, display_name, summary, kind, and
    params with semantic hints — semantic_tag/unit/intent/display_hint — so you can pick
    params by intent). Instruments are sources (no audio input); effects process audio
    in->out. Pass a name to set_track_audio_instrument (instruments) or add_audio_effect."""
    return _post("list_audio_operators")


@mcp.tool
def list_audio_ops(track: int) -> dict:
    """List the native audio operators currently ON a track: its instrument (if any) + the
    ordered effect chain, each with its params. This is the built chain; use
    list_audio_operators for the catalog of what can be added."""
    return _post("list_audio_ops", {"track": track})


@mcp.tool
def set_track_audio_instrument(track: int, op: str) -> dict:
    """Set a track's native instrument (a source operator name from list_audio_operators).
    Pass "" to clear it."""
    return _post("set_track_audio_instrument", {"track": track, "op": op})


@mcp.tool
def set_track_clap_instrument(track: int, path: str) -> dict:
    """Assign a CLAP plugin (a `.clap` bundle path from list_plugins) as a track's instrument.
    Loading is ASYNC — a slow plugin ctor never blocks the server — so this returns immediately
    with {loading: true} (or clears synchronously when path=""). Poll plugin_load_status until
    pending==0 before using the instrument (list_presets / params). Free/open CLAP synths like
    Surge XT are the reliable audible instruments here."""
    return _post("set_track_clap_instrument", {"track": track, "path": path})


@mcp.tool
def add_track_clap_effect(track: int, path: str) -> dict:
    """Append a CLAP plugin (a `.clap` bundle path from list_plugins) as a track effect. Loads
    ASYNC like set_track_clap_instrument — returns {loading: true}; poll plugin_load_status until
    pending==0. (e.g. Surge XT Effects for reverb/delay.)"""
    return _post("add_track_clap_effect", {"track": track, "path": path})


@mcp.tool
def plugin_load_status() -> dict:
    """Poll the async CLAP loader: {pending: <in-flight loads>, error: "<last failure or ''>"}.
    After set_track_clap_instrument / add_track_clap_effect (or load_project restoring CLAP
    plugins), wait until pending==0 before driving the plugin; a non-empty error reports a
    failed load."""
    return _post("plugin_load_status")


@mcp.tool
def add_audio_effect(track: int, op: str) -> dict:
    """Append a native audio effect (an effect operator name from list_audio_operators) to a
    track's chain. Returns its effect index."""
    return _post("add_audio_effect", {"track": track, "op": op})


@mcp.tool
def remove_audio_effect(track: int, index: int) -> dict:
    """Remove the native audio effect at `index` from a track's chain."""
    return _post("remove_audio_effect", {"track": track, "index": index})


@mcp.tool
def set_audio_op_param(track: int, index: int, param: int, value: float) -> dict:
    """Set a native audio operator's param by index (from list_audio_ops). index -1 = the
    track's instrument; 0+ = the effect at that chain position."""
    return _post("set_audio_op_param", {"track": track, "index": index, "param": param, "value": value})


@mcp.tool
def set_audio_op_param_by_name(track: int, index: int, name: str, value: float) -> dict:
    """Set a native audio operator param by exact case-insensitive name. index -1 = instrument;
    0+ = native effects. Use list_audio_ops or find_params first to inspect names/ranges."""
    return _post("set_audio_op_param_by_name", {"track": track, "index": index, "name": name, "value": value})


@mcp.tool
def slice_to_midi(track: int, scene: int, slice_mode: int = 3) -> dict:
    """Slice an audio clip into a new MIDI track driven by a native Sampler loaded with the
    clip's slices (ascending pitches from C1 trigger successive slices). slice_mode: 1 =
    transients, 3 = 16-step grid. Returns the new track index."""
    return _post("slice_to_midi", {"track": track, "scene": scene, "mode": slice_mode})


# ---------------- audio node graph (authoritative topology) ----------------
# get_audio_graph reports a track's node graph (nodes with stable ids + kinds, from->to edges,
# output_id). These edits make the GRAPH the source of truth (vs the linear device chain), so
# parallel chains / racks become expressible. Node ids come from get_audio_graph.
@mcp.tool
def audio_graph_add_op(track: int, op: str) -> dict:
    """Add a native effect (name from list_audio_operators) as a new node in a track's audio
    graph, inserted just before the Output so it lands at the end of the signal path. Returns
    its new node id. This flips the track to graph-authoritative editing."""
    return _post("audio_graph_add_op", {"track": track, "op": op})


@mcp.tool
def audio_graph_add_source(track: int, op: str) -> dict:
    """Add a native instrument (name from list_audio_operators) as a new *source* node in a
    track's audio graph, wired straight to the Output in parallel with any existing source.
    Two sources with disjoint key ranges (see audio_graph_set_node_key_range) = a key-split
    (e.g. a bass synth below C3, a lead above). Returns its new node id; flips the track to
    graph-authoritative editing."""
    return _post("audio_graph_add_source", {"track": track, "op": op})


@mcp.tool
def load_sampler(track: int, node_id: int, path: str, base_note: int = 60) -> dict:
    """Load an audio file (WAV/AIFF/MP3/FLAC/OGG) into an existing Sampler node so it plays that
    sample pitched across the keyboard — the note `base_note` plays it at original pitch, others
    transpose it (linear interpolation, amplitude ADSR, polyphony). `node_id` must be a Sampler
    node (add one with audio_graph_add_source(track, op="Sampler"); node ids from get_audio_graph).
    This is how you set a Sampler's sample — audio nodes carry no file param. The load is live-safe
    (no graph rebuild, params preserved). Returns {frames} loaded. For a drum-rack (one slice per
    note) instead, use slice_to_midi on an audio clip."""
    return _post("audio_graph_load_sampler",
                 {"track": track, "node_id": node_id, "path": path, "base_note": base_note})


@mcp.tool
def audio_graph_set_node_key_range(track: int, node: int, lo: int = 0, hi: int = 127) -> dict:
    """Set the MIDI key range [lo,hi] (0..127) a source node voices (node id from
    get_audio_graph). The audio thread then hands that source only its in-range notes, so two
    sources with disjoint ranges split the keyboard. Full range 0..127 = no filtering."""
    return _post("audio_graph_set_node_key_range", {"track": track, "node": node, "lo": lo, "hi": hi})


@mcp.tool
def audio_graph_remove_node(track: int, node: int) -> dict:
    """Remove an effect node from a track's audio graph by node id (from get_audio_graph). Its
    predecessors reconnect to its successors so signal keeps flowing. Instrument and Output
    nodes are not removable."""
    return _post("audio_graph_remove_node", {"track": track, "node": node})


@mcp.tool
def duplicate_audio_nodes(track: int, ids: list[int], dx: float = 24.0, dy: float = 24.0) -> dict:
    """Duplicate audio nodes within a track's graph (ids from get_audio_graph) at a small offset. Each
    copy gets a FRESH id with the original's params, pinned params, key range, plugin patch (VST3 sync,
    CLAP restored via the async loader), and sampler sample/slices. Edges strictly between the copied
    nodes are recreated; edges to outside or engine-managed nodes (Output/MIDI In/Selector/clip/gen)
    are dropped. Returns {ids:[new node ids]}. Undoable. (Audio graph; visual graph = duplicate_nodes.)"""
    return _post("duplicate_audio_nodes", {"track": track, "ids": ids, "dx": dx, "dy": dy})


@mcp.tool
def set_node_bypass(track: int, ids: list[int], bypass: bool = True) -> dict:
    """Bypass audio nodes (ids from get_audio_graph), routing signal AROUND each while keeping the graph
    shape intact. An effect passes its input through untouched; a source/instrument/generator gates to
    silence (no audio, no notes); a modulator emits no control (driven params fall back to their base).
    Pass bypass=False to restore. Persisted + undoable. Returns {count:<nodes changed>}."""
    return _post("set_node_bypass", {"track": track, "ids": ids, "bypass": bypass})


@mcp.tool
def audio_graph_connect(track: int, from_node: int, to_node: int, kind: str = "audio") -> dict:
    """Add an edge between two audio-graph nodes (ids from get_audio_graph). `kind` is the SIGNAL
    the wire carries: "audio" (multiple edges into a node sum, stereo) or "note" (ADR-0015: notes
    merge). A note edge is how notes reach an instrument once you route them explicitly —
    e.g. midi_in -> Arp -> instrument. Rejected if it duplicates an edge of that kind, is a
    self-loop, or creates a cycle."""
    return _post("audio_graph_connect", {"track": track, "from": from_node, "to": to_node, "kind": kind})


@mcp.tool
def graph_connect(from_gnid: int, to_gnid: int, kind: str = "audio") -> dict:
    """Connect two nodes by session-global id (gnid from get_audio_graph). ONE call whether the
    endpoints are on the same track (intra-track edge) or different tracks (a cross-track edge) —
    the session-global way to wire the audio graph. `kind`: "audio" or "note"."""
    return _post("graph_connect", {"from": from_gnid, "to": to_gnid, "kind": kind})


@mcp.tool
def graph_disconnect(from_gnid: int, to_gnid: int, kind: str = "audio") -> dict:
    """Remove an edge between two nodes by session-global id (gnid). Intra- or cross-track."""
    return _post("graph_disconnect", {"from": from_gnid, "to": to_gnid, "kind": kind})


@mcp.tool
def graph_set_node_param(gnid: int, name: str, value: float) -> dict:
    """Set an audio-graph node's parameter by session-global id (gnid) + param name (names from
    get_audio_graph's per-node params). Sets the BASE value (survives modulation)."""
    return _post("graph_set_node_param", {"gnid": gnid, "name": name, "value": value})


@mcp.tool
def graph_connect_control(from_gnid: int, to_gnid: int, param: int, amount: float = 1.0,
                          curve: float = 0.0, invert: bool = False, bipolar: bool = False) -> dict:
    """Wire a MODULATOR node (e.g. an LFO) to one PARAM of a target node, by session-global id (gnid).
    ONE call whether the two nodes are on the same track or different tracks (cross-track modulation).
    `param` is the target's param index (get_audio_graph). amount is a fraction of the param's range;
    bipolar straddles the base, unipolar runs up from it."""
    return _post("graph_connect_control", {"from": from_gnid, "to": to_gnid, "param": param,
                                           "amount": amount, "curve": curve, "invert": invert, "bipolar": bipolar})


@mcp.tool
def graph_disconnect_control(from_gnid: int, to_gnid: int, param: int) -> dict:
    """Remove a modulation edge (gnid -> gnid, target param). Intra- or cross-track."""
    return _post("graph_disconnect_control", {"from": from_gnid, "to": to_gnid, "param": param})


@mcp.tool
def graph_set_control_shape(from_gnid: int, to_gnid: int, param: int, amount: float = 1.0,
                            curve: float = 0.0, invert: bool = False, bipolar: bool = False) -> dict:
    """Re-shape an existing modulation edge (amount/curve/invert/bipolar) by gnid, in place."""
    return _post("graph_set_control_shape", {"from": from_gnid, "to": to_gnid, "param": param,
                                             "amount": amount, "curve": curve, "invert": invert, "bipolar": bipolar})


@mcp.tool
def graph_set_node_key_range(gnid: int, lo: int = 0, hi: int = 127) -> dict:
    """Set a source node's MIDI key range by gnid (for a key-split: two sources with disjoint ranges)."""
    return _post("graph_set_node_key_range", {"gnid": gnid, "lo": lo, "hi": hi})


@mcp.tool
def graph_remove_node(gnid: int) -> dict:
    """Remove an audio-graph node by session-global id (gnid). Effects/added nodes only (not the
    instrument or output)."""
    return _post("graph_remove_node", {"gnid": gnid})


@mcp.tool
def audio_graph_add_midi_in(track: int) -> dict:
    """Add the track's NOTE STREAM as a node (ADR-0015): its clips, the musical-typing keyboard,
    live MIDI, and note_on/note_off all flow out of it. Wire it with a NOTE edge (audio_graph_connect
    kind="note") into an instrument, or into a note effect first. Returns its new node id.

    Without any note edges an instrument still receives the track's notes implicitly, exactly as
    before — the MidiIn node is how you route them somewhere else."""
    return _post("audio_graph_add_midi_in", {"track": track})


@mcp.tool
def audio_graph_add_note_op(track: int, op: str) -> dict:
    """Add a NOTE EFFECT node (ADR-0015) — e.g. "Arp": notes in, notes out, no sound of its own.
    Wire it with NOTE edges: midi_in -> note_op -> instrument. The Arp holds whatever keys are
    down and re-issues them as a rhythmic sequence (params: rate, mode, octaves, gate), so one
    held note becomes an arpeggio. Returns its new node id."""
    return _post("audio_graph_add_note_op", {"track": track, "op": op})


@mcp.tool
def audio_graph_add_mod_op(track: int, op: str) -> dict:
    """Add a MODULATOR node (ADR-0022) — e.g. "LFO": no sound, it emits a 0..1 control signal. Wire
    its output to any node param with audio_graph_connect_control to animate that param over time
    (LFO params: waveform, sync, rate, division). Returns its new node id."""
    return _post("audio_graph_add_mod_op", {"track": track, "op": op})


@mcp.tool
def audio_graph_connect_control(track: int, from_node: int, to_node: int, param: int,
                                amount: float = 1.0, curve: float = 0.0,
                                invert: bool = False, bipolar: bool = False) -> dict:
    """Wire a modulator (from_node) to ONE param (by index) of to_node — modulation (ADR-0022). The
    param keeps its user-set BASE value; the modulator adds an offset on top, so disconnecting
    restores the knob. `amount` is the depth as a fraction of the param's declared range. `bipolar`
    straddles the base (0.5 -> base, the shape for pitch/pan), unipolar runs up from it. `curve`
    (-1..+1) shapes the response; `invert` flips the source. get_audio_graph reports each param's
    base/value/wired, and control edges as kind:"control" with their param + shape."""
    return _post("audio_graph_connect_control", {"track": track, "from": from_node, "to": to_node,
                                                 "param": param, "amount": amount, "curve": curve,
                                                 "invert": invert, "bipolar": bipolar})


@mcp.tool
def audio_graph_disconnect_control(track: int, from_node: int, to_node: int, param: int) -> dict:
    """Remove a modulation edge (ADR-0022): the param returns to its base value. Same (from,to,param)
    triple used to create it with audio_graph_connect_control."""
    return _post("audio_graph_disconnect_control", {"track": track, "from": from_node,
                                                    "to": to_node, "param": param})


@mcp.tool
def session_connect_control(src_track: int, src_node: int, dst_track: int, dst_node: int, param: int,
                            amount: float = 1.0, curve: float = 0.0,
                            invert: bool = False, bipolar: bool = False) -> dict:
    """CROSS-TRACK modulation (ADR-0022 P2a): a modulator (src_node) on src_track drives ONE param
    (by index) of dst_node on ANOTHER track (dst_track). Same live base+shape model as the in-track
    audio_graph_connect_control (the dst param keeps its base; the modulator adds an offset on top).
    The source may be on an instrument track or a dedicated modulator-only track. Tracks are indices;
    nodes are stable graph node ids (from get_audio_graph)."""
    return _post("session_connect_control", {"src_track": src_track, "src_node": src_node,
                                             "dst_track": dst_track, "dst_node": dst_node, "param": param,
                                             "amount": amount, "curve": curve,
                                             "invert": invert, "bipolar": bipolar})


@mcp.tool
def session_disconnect_control(src_track: int, src_node: int, dst_track: int, dst_node: int, param: int) -> dict:
    """Remove a cross-track modulation edge (ADR-0022 P2a): the dst param returns to its base. Same
    (src_track, src_node, dst_track, dst_node, param) used to create it with session_connect_control."""
    return _post("session_disconnect_control", {"src_track": src_track, "src_node": src_node,
                                                "dst_track": dst_track, "dst_node": dst_node, "param": param})


@mcp.tool
def session_connect_audio(src_track: int, src_node: int, dst_track: int, dst_node: int) -> dict:
    """CROSS-TRACK AUDIO (ADR-0022 P2b.4): sum the output of src_node on src_track into dst_node on
    ANOTHER track (dst_track). Unlike modulation this routes a full audio signal — the source's
    rendered output is added to the destination node's summed input. The destination must be an
    effect/output node (not a source/instrument). Rejected on a same-track edge (use the in-track
    audio_graph_connect), a duplicate, or a cross-track cycle. Tracks are indices; nodes are stable
    graph node ids (from get_audio_graph)."""
    return _post("session_connect_audio", {"src_track": src_track, "src_node": src_node,
                                           "dst_track": dst_track, "dst_node": dst_node})


@mcp.tool
def session_disconnect_audio(src_track: int, src_node: int, dst_track: int, dst_node: int) -> dict:
    """Remove a cross-track audio edge (ADR-0022 P2b.4). Same (src_track, src_node, dst_track, dst_node)
    used to create it with session_connect_audio."""
    return _post("session_disconnect_audio", {"src_track": src_track, "src_node": src_node,
                                              "dst_track": dst_track, "dst_node": dst_node})


@mcp.tool
def session_connect_note(src_track: int, src_node: int, dst_track: int, dst_node: int) -> dict:
    """CROSS-TRACK NOTES (ADR-0022 P2b.5): route the note output of src_node on src_track into a
    note-consuming node (dst_node) on ANOTHER track (dst_track). The source must EMIT notes (a MidiIn
    node, a note effect like Arp, or a note-generating plugin); the destination must CONSUME notes (an
    instrument or note effect). The source's notes are MERGED into the destination's note input (in
    addition to the destination track's own notes). For now the source should sit on a track that
    renders (has an instrument) so its note node runs. Rejected on a same-track edge (use the in-track
    audio_graph_connect kind:"note"), a duplicate, or a cross-track cycle. Tracks are indices; nodes are
    stable graph node ids (from get_audio_graph)."""
    return _post("session_connect_note", {"src_track": src_track, "src_node": src_node,
                                          "dst_track": dst_track, "dst_node": dst_node})


@mcp.tool
def session_disconnect_note(src_track: int, src_node: int, dst_track: int, dst_node: int) -> dict:
    """Remove a cross-track note edge (ADR-0022 P2b.5). Same (src_track, src_node, dst_track, dst_node)
    used to create it with session_connect_note."""
    return _post("session_disconnect_note", {"src_track": src_track, "src_node": src_node,
                                             "dst_track": dst_track, "dst_node": dst_node})


@mcp.tool
def session_set_control_shape(src_track: int, src_node: int, dst_track: int, dst_node: int, param: int,
                              amount: float = 1.0, curve: float = 0.0,
                              invert: bool = False, bipolar: bool = False) -> dict:
    """Re-shape an existing cross-track modulation edge (ADR-0022 P2a) without rewiring — same
    amount/curve/invert/bipolar controls as session_connect_control. Cross-track edges are reported
    under the 'xcontrol' key of get_audio_graph."""
    return _post("session_set_control_shape", {"src_track": src_track, "src_node": src_node,
                                              "dst_track": dst_track, "dst_node": dst_node, "param": param,
                                              "amount": amount, "curve": curve,
                                              "invert": invert, "bipolar": bipolar})


@mcp.tool
def audio_graph_set_control_shape(track: int, from_node: int, to_node: int, param: int,
                                  amount: float = 1.0, curve: float = 0.0,
                                  invert: bool = False, bipolar: bool = False) -> dict:
    """Re-shape an EXISTING modulation edge (ADR-0022) without rewiring — tune its depth/response.
    `amount` is the depth as a fraction of the param's declared range; `bipolar` straddles the base
    (0.5 -> base), unipolar runs up from it; `curve` (-1..+1) shapes the response; `invert` flips the
    source. Identifies the edge by the (from, to, param) triple. Errors if no such edge exists."""
    return _post("audio_graph_set_control_shape", {"track": track, "from": from_node, "to": to_node,
                                                   "param": param, "amount": amount, "curve": curve,
                                                   "invert": invert, "bipolar": bipolar})


@mcp.tool
def audio_graph_add_plugin(track: int, path: str, source: bool = False, uid: str = "") -> dict:
    """Add an installed VST3/CLAP plugin (path from list_plugins) as a NODE in a track's audio
    graph. source=True adds it as an instrument (fans in to the Output, in parallel with any other
    source — two with disjoint key ranges = a key-split); source=False splices it in as an effect
    at the end of the signal path. Returns its node id immediately: a CLAP loads asynchronously, so
    `ready` may be 0 until its handle binds (poll get_audio_graph / plugin_load_status)."""
    return _post("audio_graph_add_plugin", {"track": track, "path": path,
                                            "source": 1 if source else 0, "uid": uid})


@mcp.tool
def audio_graph_disconnect(track: int, from_node: int, to_node: int) -> dict:
    """Remove the edge from_node -> to_node from a track's audio graph (ids from
    get_audio_graph). No-op if the edge is absent."""
    return _post("audio_graph_disconnect", {"track": track, "from": from_node, "to": to_node})


@mcp.tool
def audio_graph_set_node_param(track: int, node: int, param: int, value: float) -> dict:
    """Set a param (by index) on an audio-graph node addressed by node id (from
    get_audio_graph). Unlike set_audio_op_param (which uses linear chain index), this works for
    any node in a non-linear/rewired graph."""
    return _post("audio_graph_set_node_param", {"track": track, "node": node, "param": param, "value": value})


@mcp.tool
def audio_graph_set_node_param_by_name(track: int, node: int, name: str, value: float) -> dict:
    """Set an audio-graph node param by exact case-insensitive name. Use get_audio_graph or
    find_params to inspect node ids, param names, and ranges."""
    return _post("audio_graph_set_node_param_by_name", {"track": track, "node": node, "name": name, "value": value})


# ---------------- live input / recording ----------------
@mcp.tool
def arm_track(track: int) -> dict:
    """Arm a track for live MIDI input + recording (monitors notes through its instrument).
    Pass -1 to disarm. Returns the armed track index."""
    return _post("arm_track", {"track": track})


@mcp.tool
def note_on(pitch: int, vel: float = 0.8) -> dict:
    """Send a live note-on (pitch 0..127) to the currently armed track's instrument."""
    return _post("note_on", {"pitch": pitch, "vel": vel})


@mcp.tool
def note_off(pitch: int) -> dict:
    """Send a live note-off (pitch 0..127) to the armed track's instrument."""
    return _post("note_off", {"pitch": pitch})


@mcp.tool
def record(on: bool = True, count_in: float = 0.0) -> dict:
    """Start/stop recording live input into the armed track's playing clip. count_in = bars of
    metronome count-in before capture. Returns the recording state."""
    return _post("record", {"on": on, "count_in": count_in})


@mcp.tool
def metronome(on: bool = True) -> dict:
    """Enable/disable the metronome click. Returns its state."""
    return _post("metronome", {"on": on})


@mcp.tool
def set_clip_loop(track: int, scene: int, loop_start: float = 0.0, loop_end: float = 0.0) -> dict:
    """Set a clip's loop region in beats (loop_start..loop_end). 0/0 clears the loop."""
    return _post("set_clip_loop", {"track": track, "scene": scene, "loop_start": loop_start, "loop_end": loop_end})


@mcp.tool
def set_param(track: int, device: int, param: int, value: float) -> dict:
    """Set a device param by its index (from list_params). device 0 = instrument, 1+ = FX. value 0..1."""
    return _post("set_param", {"track": track, "device": device, "param": param, "value": value})


@mcp.tool
def set_clip(track: int, scene: int, notes: list[dict], length: float = 4.0) -> dict:
    """Replace a MIDI clip's notes. notes = [{p:pitch, s:startBeat, d:durBeats, v:velocity0..1}, ...];
    `p` accepts a MIDI int OR a name ("C4", "F#3", "Bb5"). length = loop length in beats.
    (MIDI tracks only — not the audio/sampler track.) Full-replace — use add_notes to append."""
    return _post("set_clip", {"track": track, "scene": scene, "notes": theory.norm_notes(notes), "length": length})


@mcp.tool
def get_clip(track: int, scene: int) -> dict:
    """Read a MIDI clip back: {notes:[{p,s,d,v}], length}. The read half that editing/transform
    tools use (set_clip is full-replace, so they read-modify-write)."""
    return _post("get_clip", {"track": track, "scene": scene})


@mcp.tool
def add_notes(track: int, scene: int, notes: list[dict]) -> dict:
    """APPEND notes to a clip without clearing it (unlike set_clip). `p` accepts names or ints.
    Reads the current clip, appends, writes it back."""
    cur = _post("get_clip", {"track": track, "scene": scene})
    if not cur.get("ok"):
        return cur
    merged = cur.get("notes", []) + theory.norm_notes(notes)
    return _post("set_clip", {"track": track, "scene": scene, "notes": merged, "length": cur.get("length", 4.0)})


@mcp.tool
def clear_clip(track: int, scene: int) -> dict:
    """Empty a clip (remove all its notes; keep its loop length)."""
    cur = _post("get_clip", {"track": track, "scene": scene})
    length = cur.get("length", 4.0) if cur.get("ok") else 4.0
    return _post("set_clip", {"track": track, "scene": scene, "notes": [], "length": length})


@mcp.tool
def list_pool() -> dict:
    """List the clip pool — loose clips stashed outside the track×scene grid:
    {pool:[{index, name, length, kind}]} where kind is "midi" or "audio". Stash from the
    grid or place back (see pool_stash / pool_place). MIDI pool clips are saved with the
    project; audio pool clips are runtime-only."""
    return _post("list_pool")


@mcp.tool
def pool_stash(track: int, scene: int, name: str = "") -> dict:
    """Move a grid clip into the clip pool: the source cell is cleared (the clip leaves the
    session). Works for MIDI (instrument) and audio (sampler) tracks. Returns {index, kind}.
    name defaults to "<track> <A/B/C>"."""
    return _post("pool_stash", {"track": track, "scene": scene, "name": name})


@mcp.tool
def pool_place(index: int, track: int, scene: int) -> dict:
    """Place a pooled clip (by pool index) into a grid cell, overwriting it. Types must
    match: an audio clip onto an audio track, a MIDI clip onto an instrument track. The
    pool item stays; use pool_remove to discard it."""
    return _post("pool_place", {"index": index, "track": track, "scene": scene})


@mcp.tool
def pool_remove(index: int) -> dict:
    """Remove a clip from the pool by index (later indices shift down by one)."""
    return _post("pool_remove", {"index": index})


@mcp.tool
def import_audio_clip(track: int, scene: int, path: str, src_bpm: float = 0.0) -> dict:
    """Import an audio file (.wav/.aif/.flac/.mp3) into a sampler track's scene clip, decoding
    and resampling to the device rate. `track` must be an audio (sampler) track — make one with
    add_track(kind="audio"). `src_bpm` (0 = unknown) seeds warp/BPM estimation; follow with
    audio_auto_warp / audio_set_warp(mode="beats") to lock the loop to the project tempo.
    Returns {track, scene, length} (length in beats). This is the way to get real recorded audio
    into the grid so the glitch pack / warp can process it."""
    return _post("import_audio_clip", {"track": track, "scene": scene, "path": path, "src_bpm": src_bpm})


@mcp.tool
def add_chord(track: int, scene: int, symbol: str, beat: float = 0.0, dur: float = 4.0,
              vel: float = 0.8, octave: int = 4, inversion: int = 0, voicing: str = "close") -> dict:
    """Append a chord to a clip by SYMBOL — e.g. "Cmaj7", "Am", "G7", "F#m7b5", "Dsus4", "C/G"
    (slash bass). voicing = close|open|drop2; inversion = 0,1,2,…; octave sets the register.
    Root supports #/b. Appends (read-modify-write), so build a clip chord-by-chord."""
    try:
        pitches = theory.chord(symbol, octave=octave, inversion=inversion, voicing=voicing)
    except ValueError as e:
        return {"ok": False, "code": "bad_arg", "error": str(e)}
    notes = [{"p": p, "s": beat, "d": dur, "v": vel} for p in pitches]
    cur = _post("get_clip", {"track": track, "scene": scene})
    if not cur.get("ok"):
        return cur
    merged = cur.get("notes", []) + notes
    length = max(cur.get("length", 4.0), beat + dur)
    return _post("set_clip", {"track": track, "scene": scene, "notes": merged, "length": length})


@mcp.tool
def set_progression(track: int, scene: int, chords: list[str], beats_per_chord: float = 4.0,
                    octave: int = 4, voicing: str = "close", vel: float = 0.8,
                    key: str = "", scale: str = "major") -> dict:
    """REPLACE a clip with a chord progression. If `key` is given, `chords` are ROMAN NUMERALS
    diatonic to key/scale (["ii","V","I"] or ["i","iv","V","i"]); otherwise they're absolute
    chord SYMBOLS (["Dm7","G7","Cmaj7"]). Each chord spans beats_per_chord; the loop length is
    len(chords)*beats_per_chord."""
    notes = []
    try:
        for i, sym in enumerate(chords):
            start = i * beats_per_chord
            pitches = (theory.roman(sym, key, scale, octave) if key
                       else theory.chord(sym, octave=octave, voicing=voicing))
            for p in pitches:
                notes.append({"p": p, "s": start, "d": beats_per_chord, "v": vel})
    except ValueError as e:
        return {"ok": False, "code": "bad_arg", "error": str(e)}
    length = max(1.0, len(chords) * beats_per_chord)
    return _post("set_clip", {"track": track, "scene": scene, "notes": notes, "length": length})


# ---------------- key context + transforms ----------------
_key_ctx = {"root": "C", "scale": "major"}


def _rmw(track: int, scene: int, fn) -> dict:
    """Read a clip, transform its notes via fn(notes, length) -> notes, write it back."""
    cur = _post("get_clip", {"track": track, "scene": scene})
    if not cur.get("ok"):
        return cur
    length = cur.get("length", 4.0)
    return _post("set_clip", {"track": track, "scene": scene,
                              "notes": fn(cur.get("notes", []), length), "length": length})


@mcp.tool
def set_key(root: str, scale: str = "major") -> dict:
    """Set the session key/scale context (root e.g. "C"/"F#", scale e.g. major|minor|dorian|
    pentatonic_minor|blues|…). Tools with an optional key/scale (quantize_to_scale, harmonize,
    get_scale) default to this. Bridge-side + EPHEMERAL in v1 (resets on bridge restart; not
    saved with the session)."""
    if scale.lower() not in theory.SCALES:
        return {"ok": False, "code": "bad_arg", "error": f"unknown scale '{scale}'"}
    _key_ctx.update(root=root, scale=scale.lower())
    return {"ok": True, **_key_ctx}


@mcp.tool
def get_key() -> dict:
    """The current key/scale context + its scale note names."""
    return {"ok": True, **_key_ctx,
            "notes": [theory.note_name(m) for m in theory.scale_notes(_key_ctx["root"], _key_ctx["scale"])]}


@mcp.tool
def get_scale(root: str = "", scale: str = "") -> dict:
    """The MIDI notes + names of a scale (defaults to the key context), e.g. get_scale("D","dorian")."""
    r, sc = root or _key_ctx["root"], (scale or _key_ctx["scale"]).lower()
    if sc not in theory.SCALES:
        return {"ok": False, "code": "bad_arg", "error": f"unknown scale '{sc}'"}
    midi = theory.scale_notes(r, sc)
    return {"ok": True, "root": r, "scale": sc, "midi": midi, "names": [theory.note_name(m) for m in midi]}


@mcp.tool
def transpose(track: int, scene: int, semitones: int) -> dict:
    """Transpose every note in a clip by ±semitones."""
    return _rmw(track, scene, lambda notes, L: theory.transpose(notes, semitones))


@mcp.tool
def quantize_to_scale(track: int, scene: int, root: str = "", scale: str = "") -> dict:
    """Snap a clip's off-key notes into the scale (defaults to the key context)."""
    r, sc = root or _key_ctx["root"], scale or _key_ctx["scale"]
    return _rmw(track, scene, lambda notes, L: theory.quantize_to_scale(notes, r, sc))


@mcp.tool
def harmonize(track: int, scene: int, degree: int = 2, root: str = "", scale: str = "") -> dict:
    """Add a diatonic harmony voice `degree` scale-steps from each note (2 = a third above,
    4 = a fifth; negative = below). Defaults to the key context. Keeps the originals."""
    r, sc = root or _key_ctx["root"], scale or _key_ctx["scale"]
    return _rmw(track, scene, lambda notes, L: theory.harmonize(notes, degree, r, sc))


@mcp.tool
def invert_clip(track: int, scene: int, axis: int = -1) -> dict:
    """Melodically invert a clip (mirror pitches). axis = the MIDI pitch to mirror around
    (-1 = the clip's first note)."""
    a = None if axis < 0 else axis
    return _rmw(track, scene, lambda notes, L: theory.invert(notes, a))


@mcp.tool
def retrograde_clip(track: int, scene: int) -> dict:
    """Reverse a clip in time (retrograde)."""
    return _rmw(track, scene, lambda notes, L: theory.retrograde(notes, L))


@mcp.tool
def arpeggiate(track: int, scene: int, chord: str = "", pattern: str = "up", rate: float = 0.25,
               octaves: int = 1, length: float = 4.0, vel: float = 0.8) -> dict:
    """REPLACE a clip with an arpeggio. `chord` = a symbol (e.g. "Am7"); if empty, arpeggiates
    the pitches already in the clip. pattern = up|down|updown|downup; rate = beats per step."""
    if chord:
        try:
            pitches = theory.chord(chord)
        except ValueError as e:
            return {"ok": False, "code": "bad_arg", "error": str(e)}
    else:
        cur = _post("get_clip", {"track": track, "scene": scene})
        pitches = [n["p"] for n in cur.get("notes", [])] if cur.get("ok") else []
    notes = theory.arpeggiate(pitches, pattern, rate, octaves, length, vel)
    return _post("set_clip", {"track": track, "scene": scene, "notes": notes, "length": length})


# ---------------- rhythm ----------------
@mcp.tool
def set_drum_pattern(track: int, scene: int, patterns: dict, bar_beats: float = 4.0,
                     bars: int = 1, vel: float = 0.8, dur: float = 0.1) -> dict:
    """REPLACE a clip with a drum pattern. `patterns` maps a drum name -> a step-string, e.g.
    {"kick":"x..x..x.", "snare":"....x...", "hat":"xxxxxxxx"}. Drum names: kick/snare/rim/clap/
    hat/openhat/tom_lo/tom_mid/tom_hi/crash/ride (or a raw MIDI int). Step chars: x = hit,
    digit 1-9 = velocity, '.'/'-'/' ' = rest. Each pattern spreads across bar_beats; `bars`
    tiles it. (Use on a drum/MIDI track.)"""
    notes = []
    try:
        for name, steps in patterns.items():
            note = theory.drum_note(name)
            for b in range(max(1, bars)):
                notes += theory.drum_steps(steps, note, bar_beats, vel, dur, start=b * bar_beats)
    except ValueError as e:
        return {"ok": False, "code": "bad_arg", "error": str(e)}
    return _post("set_clip", {"track": track, "scene": scene, "notes": notes,
                              "length": bar_beats * max(1, bars)})


@mcp.tool
def euclidean_fill(track: int, scene: int, drum: str, pulses: int, steps: int,
                   bar_beats: float = 4.0, rotation: int = 0, vel: float = 0.7, dur: float = 0.1) -> dict:
    """APPEND a Euclidean rhythm on one drum: `pulses` hits spread over `steps` across bar_beats.
    E.g. euclidean_fill(t,s,"hat",3,8) = the tresillo x..x..x. on hi-hat. `drum` = a name or MIDI int."""
    try:
        note = theory.drum_note(drum)
    except ValueError as e:
        return {"ok": False, "code": "bad_arg", "error": str(e)}
    pat = theory.euclidean(pulses, steps, rotation)
    step = bar_beats / steps if steps else bar_beats
    add = [{"p": note, "s": round(i * step, 6), "d": dur, "v": vel} for i, on in enumerate(pat) if on]
    cur = _post("get_clip", {"track": track, "scene": scene})
    if not cur.get("ok"):
        return cur
    merged = cur.get("notes", []) + add
    return _post("set_clip", {"track": track, "scene": scene, "notes": merged,
                              "length": max(cur.get("length", 4.0), bar_beats)})


@mcp.tool
def humanize(track: int, scene: int, timing: float = 0.02, velocity: float = 0.1, seed: int = 0) -> dict:
    """Nudge a clip's note starts + velocities by small (seeded, reproducible) random amounts for feel."""
    return _rmw(track, scene, lambda notes, L: theory.humanize(notes, timing, velocity, seed))


@mcp.tool
def quantize_rhythm(track: int, scene: int, grid: float = 0.25) -> dict:
    """Snap a clip's note starts to a beat grid (0.25 = 16ths, 0.5 = 8ths, 1/3 = triplets)."""
    return _rmw(track, scene, lambda notes, L: theory.quantize_rhythm(notes, grid))


# ---------------- analysis ----------------
@mcp.tool
def analyze_clip(track: int, scene: int, bar_beats: float = 4.0) -> dict:
    """Read a clip and infer its music theory (HEURISTIC): detected {key:{root,scale,confidence}}
    (Krumhansl–Schmuckler), a best-fit chord per bar, pitch range, and note count. Short or
    ambiguous clips are unreliable. Great for making visuals respond to harmony."""
    cur = _post("get_clip", {"track": track, "scene": scene})
    if not cur.get("ok"):
        return cur
    notes, length = cur.get("notes", []), cur.get("length", 4.0)
    if not notes:
        return {"ok": True, "empty": True, "note_count": 0, "length": length}
    ps = [n["p"] for n in notes]
    return {"ok": True, "key": theory.detect_key(notes),
            "chords": theory.chords_per_bar(notes, length, bar_beats),
            "range": [theory.note_name(min(ps)), theory.note_name(max(ps))],
            "note_count": len(notes), "length": length}


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
def list_presets(track: int, filter: str = "") -> dict:
    """Browse the presets/patches of a track's instrument — GENERIC (no per-plugin code).
    Returns [{name, id, loadable, category?, tags?}]. `filter` narrows by a case-insensitive name
    substring; USE IT — a synth can ship thousands of patches (Serum ~626, Pigments ~1651, Surge
    ~5k). Pick by matching name/category/tags to the SOUND you want and pass its `id` to
    load_preset. `loadable:false` means browse-only (see below) — don't call load_preset on those.

    Sonic guidance (the point of this flow): you know the musical intent; you choose the patch.
    State the target in plain terms, then filter/scan for a matching name:
      pad / warm / strings / choir   -> lush sustained beds     (avoid: lead, bass, pluck)
      bass / sub / 808 / reese       -> low end
      lead / solo / saw / pluck      -> melodic top
      keys / ep / piano / bell       -> tonal comping
      arp / seq / motion             -> rhythmic
    If unsure, show the user a shortlist with why each fits and let them choose.
    (Sources: CLAP preset-discovery factory; VST3 `.vstpreset` files; VST3 program lists (IUnitInfo
    factory programs, loadable); and native-format adapters — Arturia Pigments presets are browsable
    AND loadable, Xfer Serum `.SerumPreset` are browsable with rich metadata but `loadable:false` —
    Serum's format can't be host-loaded, so recommend one by its metadata and have the user load it
    in Serum's own UI, then save_project captures it.)"""
    return _post("list_presets", {"track": track, "filter": filter})


@mcp.tool
def load_preset(track: int, id: str) -> dict:
    """Load a preset onto a track's instrument by the `id` from list_presets (a `.vstpreset`/patch
    file path, or an internal CLAP key). Audibly changes the timbre and is saved with the project
    (save_project captures the plugin state). Use after list_presets to give a part its voice."""
    return _post("load_preset", {"track": track, "id": id})


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
def add_scene() -> dict:
    """Append a new scene (grid ROW) to the session — an empty clip slot on every track.
    Returns the new scene index. Fails once the session reaches the scene cap (8). Undoable."""
    return _post("add_scene", {})


@mcp.tool
def set_scene_name(scene: int, name: str) -> dict:
    """Rename a scene (the label shown on its launch button; default "A","B",…). An empty name
    resets it to the default. Names persist with the session and are reported by inspect_scene.
    Undoable."""
    return _post("set_scene_name", {"scene": scene, "name": name})


@mcp.tool
def list_generators() -> dict:
    """List the note-GENERATOR ops that can be placed in a scene cell (Euclid, Chord, RandMelody).
    A generator is an algorithmic note source — it plays that scene like a clip does, but from an
    algorithm instead of recorded notes."""
    return _post("list_generators", {})


@mcp.tool
def place_generator(track: int, scene: int, type: str) -> dict:
    """Put a note generator (from list_generators) into a scene cell on an instrument track. That
    cell now voices the generator when its scene is active, in place of its clip. Replaces any
    generator already in the cell. Undoable. (Derived tracks; set params via set_audio_op_param on
    the node once placed.)"""
    return _post("place_generator", {"track": track, "scene": scene, "type": type})


@mcp.tool
def remove_generator(track: int, scene: int) -> dict:
    """Revert a scene cell back to a clip, removing its generator. Undoable."""
    return _post("remove_generator", {"track": track, "scene": scene})


@mcp.tool
def set_generator_param(track: int, scene: int, name: str, value: float) -> dict:
    """Set a parameter on a scene cell's generator (e.g. Euclid steps/pulses/rotate/note/rate/gate,
    Chord root/quality, RandMelody scale/density/seed). Param names + current values are listed by
    inspect_scene. Takes effect live. Undoable (a rapid run coalesces into one edit)."""
    return _post("set_generator_param", {"track": track, "scene": scene, "name": name, "value": value})


@mcp.tool
def add_graph_track(instrument: str = "", name: str = "") -> dict:
    """Create a bare NATIVE-instrument track that hosts a native audio node graph (get_audio_graph
    / audio_graph_* edits). Unlike add_track, this makes no VST3/plugin handle, so the track is
    graph-capable. Pass `instrument` (a native audio-instrument op like "SineSynth") to set the
    instrument in the same call. Returns the new track index."""
    payload: dict = {}
    if instrument:
        payload["instrument"] = instrument
    if name:
        payload["name"] = name
    return _post("add_graph_track", payload)


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
def new_project() -> dict:
    """Start a fresh project: empty every clip, reset the visuals to the default chain, drop
    all mappings, and clear the current-project path. Keeps the loaded instruments/tracks."""
    return _post("new_project")


@mcp.tool
def get_project_status() -> dict:
    """Current project workflow state: explicit project path, recent project paths,
    media_root, missing_media diagnostics, and discovered video count."""
    return _post("get_project_status")


@mcp.tool
def save_project(path: str = "") -> dict:
    """Save the project/session JSON. Pass path for Save As; omit it after a project path exists."""
    payload = {"path": path} if path else {}
    return _post("save_project", payload)


@mcp.tool
def load_project(path: str) -> dict:
    """Load a project/session JSON from path and make it the current project."""
    return _post("load_project", {"path": path})


@mcp.tool
def set_media_root(path: str) -> dict:
    """Set the project media root (the base a Video/Image node's relative path resolves against).
    Missing roots are reported, not silent. Video is per-node now: add a Video node and set its
    `file` param (via set_node_file_param) to the movie path — mp4/mov, including HAP-encoded .mov."""
    return _post("set_media_root", {"path": path})


@mcp.tool
def check_tutorial_prereqs(tutorial: str = "mcp_native_first_project") -> dict:
    """Run a named tutorial readiness checklist before a builder mutates anything. Returns
    ready, checks, missing, and next_actions so an MCP client can explain setup gaps. The first
    supported checklist is mcp_native_first_project, which verifies Surge XT and project-shader
    onboarding state."""
    return _post("check_tutorial_prereqs", {"tutorial": tutorial})


@mcp.tool
def scaffold_project_shader_operator(name: str, filename: str = "", source: str = "",
                                     overwrite: bool = False) -> dict:
    """Write and live-register a project-local shader operator in the current saved folder project.
    The shader appears by its metadata/operator name in list_operators and can be spawned like any
    other visual node. If source is omitted, Vivid writes a small WGSL starter."""
    payload = {"name": name, "overwrite": overwrite}
    if filename:
        payload["filename"] = filename
    if source:
        payload["source"] = source
    return _post("scaffold_project_shader_operator", payload)


# ---- ADR-0024 Phase 7: project workflow (inspect / resolve / reload project assets) ----

@mcp.tool
def validate_project() -> dict:
    """Structural health of the loaded project: whether it is saved, its session file exists on disk,
    whether it carries a project-local operator package / shaders dir, and any missing media. Returns
    `valid` plus a leveled `issues` list."""
    return _post("validate_project")


@mcp.tool
def list_project_assets() -> dict:
    """Enumerate the current project folder's co-located assets: session file, operator package
    (manifest + declared operator sources), shaders, and loose media. Empty for an unsaved or
    single-file project."""
    return _post("list_project_assets")


@mcp.tool
def resolve_asset(asset: str) -> dict:
    """Resolve a project-relative asset reference (e.g. "shaders/foo.glsl" or a bare filename) to an
    absolute path, reporting whether it exists and its kind. Rejects paths that escape the project dir."""
    return _post("resolve_asset", {"asset": asset})


@mcp.tool
def reload_project_files() -> dict:
    """Pick up on-disk edits to the project's authored assets without reverting the live session:
    re-scan the shaders dir and recompile the project package, registering any newly-authored
    operator. Needs a saved folder project. (Hot-swapping an existing compiled op is
    reload_operator_package.)"""
    return _post("reload_project_files")


@mcp.tool
def diff_project() -> dict:
    """Structural delta between the live (in-memory) session and its last saved state on disk:
    per-section counts on each side plus a `differs` flag. Excludes opaque plugin-state so the diff
    reflects authored structure, not serialized plugin bytes."""
    return _post("diff_project")


# ---- ADR-0024 Phase 7: operator-package authoring (scaffold / validate / build / reload) ----

@mcp.tool
def scaffold_operator_package(name: str, kind: str = "gpu_visual", path: str = "") -> dict:
    """Write a fresh single-operator package (a known-good starter source + manifest) and validate the
    output. `name` becomes the operator type (must be a C++ identifier); `kind` defaults to gpu_visual
    (the only templated domain today); `path` defaults to <user_data>/scaffold/<name>. Then edit the
    source and build/reload_operator_package."""
    payload = {"name": name, "kind": kind}
    if path:
        payload["path"] = path
    return _post("scaffold_operator_package", payload)


@mcp.tool
def validate_operator_package(path: str) -> dict:
    """Parse a package's vivid-package.json and confirm each declared operator source exists on disk.
    Read-only pre-flight — no compile, no live mutation. Returns `valid` + per-operator source status."""
    return _post("validate_operator_package", {"path": path})


@mcp.tool
def build_operator_package(path: str) -> dict:
    """Compile every operator in the package to a .dylib in a temp build dir WITHOUT registering
    anything live — a "does it build?" check that never touches the running catalog. Returns per-op
    success + compiler errors. (Use install_operator_package to compile + register live.)"""
    return _post("build_operator_package", {"path": path})


@mcp.tool
def reload_operator_package(path: str = "") -> dict:
    """Recompile the package and register any operator NOT already live (a newly-authored op appears
    in list_operators immediately). Already-registered ops are reported as live — edit their source to
    hot-reload (ADR-0020 watcher); this never unregisters a live type. `path` defaults to the open
    folder project's co-located package."""
    payload = {"path": path} if path else {}
    return _post("reload_operator_package", payload)


@mcp.tool
def clone_operator(op: str, new_name: str) -> dict:
    """Fork a compiled operator into a fresh editable copy under `new_name`, compiled + registered
    live (spawn/edit it immediately). Mirrors fork_shader for compiled ops. Works for a built-in with
    a clone template (e.g. Plasma) or any operator whose source is on disk + watched (a prior clone /
    installed / project C++ op). A shipped built-in dylib with no editable source can't be cloned;
    shaders use fork_shader."""
    return _post("clone_operator", {"op": op, "new_name": new_name})


@mcp.tool
def get_authoring_guide() -> dict:
    """How to compose an audiovisual scene with these tools (recipe + gotchas)."""
    return {
        "overview": "Vivid = a DAW (tracks x scenes of clips; each track has an instrument + FX) "
                    "wired to a rewireable visuals node-graph via a mapping bridge.",
        "recipe": [
            "1. READ: status, list_tracks, get_graph, get_mappings to see current state.",
            "2. TEMPO: set_bpm(bpm).",
            "3. NOTES: prefer the music-theory tools (see 'music_theory' below) — set_progression / "
            "add_chord / set_drum_pattern / arpeggiate — over raw set_clip. `p` accepts names ('C4') "
            "or MIDI ints. Then launch_clip.",
            "4. SOUND: list_params(track, 0, filter='cutoff') then set_param(track, 0, index, value).",
            "5. VISUALS: the default chain is Plasma->Feedback->Blur->Output. add_node / connect_nodes "
            "to extend it; set_node_param for base look; set_active_output to pick the viewer source. "
            "Custom look? add_node('CustomShader') + set_node_file_param(id,'file','<file>.glsl') renders a "
            "project folder .glsl (contract in 'custom_assets' below); project-local C++ ops load on load_project.",
            "6. BRIDGE: connect_mapping(src, dst) — e.g. src='master.transient', dst='node:<id>.warp'. "
            "Visual node ids + param names come from get_graph; dst for audio params is 'param:T:D:index'.",
            "7. RETURN PATH: src can be 'viz.<name>' to drive an audio param from a visual value.",
            "8. VERIFY: get_graph / get_mappings to confirm; read list_tracks for live level/bands.",
            "9. PROJECT: get_project_status; use save_project(path) / load_project(path), "
            "and set_media_root(path) for video assets.",
        ],
        "errors": "Every reply has an 'ok' bool. Failures are {ok:false, code, error}: "
                  "branch on the stable `code` (bad_json, unknown_method, no_session, no_graph, "
                  "no_vgraph, no_transport, bad_arg, out_of_range, not_found, io_error, internal, "
                  "timeout), not the human `error` text. An out-of-range track/scene/device index "
                  "now returns out_of_range instead of silently succeeding.",
        "music_theory": {
            "notes": "Anywhere a pitch is taken, `p` accepts a MIDI int OR a name: 'C4','F#3','Bb5' "
                     "(C4 = middle C = 60). get_clip reads a clip back; add_notes appends; clear_clip empties.",
            "harmony": "add_chord(t,s,'Cmaj7') / 'Am' / 'G7' / 'F#m7b5' / 'C/G' (slash bass), voicing="
                       "close|open|drop2, inversion=0,1,2. set_progression(t,s,['Dm7','G7','Cmaj7']) — or "
                       "ROMAN numerals when key is given: set_progression(t,s,['ii','V','I'], key='F').",
            "scale_key": "set_key('C','minor') sets a context quantize_to_scale / harmonize / get_scale "
                         "default to. quantize_to_scale(t,s) snaps off-key notes; get_scale('D','dorian').",
            "transforms": "transpose(t,s,±semitones), arpeggiate(t,s,'Am7',pattern=up|down|updown), "
                          "harmonize(t,s,degree=2) (a diatonic third), invert_clip, retrograde_clip.",
            "rhythm": "set_drum_pattern(t,s,{'kick':'x..x..x.','snare':'....x...','hat':'xxxxxxxx'}) — "
                      "names kick/snare/hat/openhat/clap/tom_lo/…, chars x=hit, 1-9=velocity, .=rest. "
                      "euclidean_fill(t,s,'hat',3,8)=tresillo. humanize(t,s), quantize_rhythm(t,s,grid).",
            "analysis": "analyze_clip(t,s) -> detected key + chord-per-bar + range (heuristic) — good for "
                        "driving visuals from harmony.",
        },
        "custom_assets": {
            "project_folder": "save_project(dir) (a path with no .json extension) makes a FOLDER project: "
                              "<dir>/project.json plus co-located assets. load_project(dir) restores it.",
            "custom_shader": "add_node('CustomShader') + set_node_file_param(id,'file','look.glsl') renders a .glsl in the "
                             "project folder. The fragment must declare: `#version 450`, "
                             "`layout(location=0) in vec2 v_uv; layout(location=0) out vec4 o_color;` and "
                             "`layout(set=0,binding=0) uniform U { vec2 u_res; float u_time; float u_warp; "
                             "float u_hue; float u_density; float u_glow; };`. The 4 node params map to "
                             "u_warp/u_hue/u_density/u_glow (wire them from audio). Bad file/compile = a no-op "
                             "node (never crashes). Write the .glsl into the folder, then save_project.",
            "custom_operator": "Drop a vivid-package.json + a C++ operator source in the project folder; "
                               "load_project compiles it into the folder and registers it by name BEFORE the "
                               "graph loads, so a node of that type resolves. (See install_operator_package.)",
        },
        "gotchas": {
            "params_are_huge": "Plugins expose thousands of params; always filter+limit list_params.",
            "param_index": "set_param takes the *index* from list_params (mapped to the VST id internally).",
            "mapping_dest": "visual = 'node:<id>.<param>'; audio = 'param:<track>:<device>:<index>'.",
            "modulation": "A wired visual param = clamp(base + modulation); set the base with set_node_param.",
            "audio_track": "note tools apply to MIDI tracks only (is_audio=false in list_tracks).",
            "full_replace": "set_clip / set_progression / arpeggiate / set_drum_pattern REPLACE the clip; "
                            "add_notes / add_chord / euclidean_fill APPEND.",
            "key_context": "set_key is bridge-side + ephemeral (resets if the bridge restarts; not saved).",
        },
    }


# ---- ADR-0026: in-app Gemini music evaluation (semantic, intent-aware) ----

def _meval_poll(job_id: int, timeout_s: float) -> dict:
    """Poll music_eval_result until the async Gemini job finishes (or times out)."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        r = _post("music_eval_result", {"job_id": job_id})
        if r.get("ok") is False or r.get("status") in ("done", "error"):
            return r
        time.sleep(1.0)
    return {"ok": False, "status": "timeout", "error": "music eval timed out"}


@mcp.tool
def configure_music_eval_backend(backend: str = "gemini", api_key: str = "", model: str = "") -> dict:
    """Configure in-app music evaluation (ADR-0026). backend must be 'gemini'. Pass your Google Gemini
    api_key (stored in the macOS Keychain, never on disk) and optionally a model (default
    gemini-2.5-flash). Verify with music_eval_status that has_key is true before trusting any verdict:
    with no key the eval tools fail closed (no fake results)."""
    return _post("configure_music_eval_backend", {"backend": backend, "api_key": api_key, "model": model})


@mcp.tool
def music_eval_status() -> dict:
    """Music-eval backend state: {backend: 'gemini'|'unconfigured', ready, has_key, model}. When
    has_key is false the evaluators fail closed."""
    return _post("music_eval_status")


@mcp.tool
def music_eval_result(job_id: int) -> dict:
    """Poll a music-eval job by id: {status: 'pending'|'done'|'error', ...}. The high-level
    evaluate_audio_musically / compare_audio_to_intent tools poll this for you."""
    return _post("music_eval_result", {"job_id": job_id})


@mcp.tool
def evaluate_audio_musically(window_seconds: float = 20.0, mode: str = "caption",
                             include_payload: bool = False) -> dict:
    """Capture the live master output and have Gemini analyze it (ADR-0026): key, tempo,
    instrumentation, mood, structure. mode = caption | theory | reasoning. Requires a configured
    Gemini key (configure_music_eval_backend) — fails closed otherwise. Blocks ~5-15s while Gemini
    runs. Returns {ok, key, tempo_bpm, instrumentation, mood, summary}."""
    started = _post("evaluate_audio_musically", {"window_seconds": window_seconds, "mode": mode})
    if not started.get("ok") or "job_id" not in started:
        return started  # fail-closed / capture error passes straight through
    r = _meval_poll(started["job_id"], max(30.0, window_seconds + 90.0))
    if include_payload or not r.get("ok", True):
        return r
    return {"ok": True, "mode": r.get("mode", mode), "key": r.get("key"), "tempo_bpm": r.get("tempo_bpm"),
            "instrumentation": r.get("instrumentation"), "mood": r.get("mood"), "summary": r.get("summary")}


@mcp.tool
def compare_audio_to_intent(intent: str = "", reference_path: str = "", window_seconds: float = 20.0,
                            include_payload: bool = False) -> dict:
    """Capture the live master output and have Gemini score it against a free-text intent (and/or a
    reference clip) — the ADR-0026 quality gate. Returns match_score (0-1) plus harmony/rhythm/timbre/
    structure sub-scores (include_payload) and ranked key_deviations. Be honest: a part described as
    melodic that is actually a held/repeated note scores low. Requires a configured Gemini key; fails
    closed otherwise. Blocks ~5-15s."""
    started = _post("compare_audio_to_intent",
                    {"intent": intent, "reference_path": reference_path, "window_seconds": window_seconds})
    if not started.get("ok") or "job_id" not in started:
        return started
    r = _meval_poll(started["job_id"], max(30.0, window_seconds + 90.0))
    if include_payload or not r.get("ok", True):
        return r
    return {"ok": True, "match_score": r.get("match_score"),
            "key_deviations": r.get("key_deviations", []), "summary": r.get("summary")}


if __name__ == "__main__":
    mcp.run()
