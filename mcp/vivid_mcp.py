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
    summary, keywords (for search), gpu flag, params and ports. A param carries {name,
    type, default, min, max} plus, when the operator declares them, semantic hints —
    semantic_tag (e.g. 'frequency_hz'), semantic_unit ('Hz'), semantic_intent (e.g.
    'brightness'), display_hint, description — so you can choose params by INTENT, not by
    guessing names. Call this to DISCOVER operators before add_node / set_node_param /
    connect_nodes."""
    return _post("list_operators")


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
    """Point a node at a data asset. For a CustomShader node, `asset` is a project-relative
    .glsl filename (resolved against the loaded project folder); the node renders that shader.
    Pass "" to clear. The shader must follow the GLSL contract (v_uv/o_color + the
    u_res/u_time/u_warp/u_hue/u_density/u_glow uniform block — see the authoring guide). A
    missing file or compile error degrades to a no-op node (never crashes). Save it with the
    project (save_project) so a folder holds project.json + the .glsl together."""
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
def slice_to_midi(track: int, scene: int, slice_mode: int = 3) -> dict:
    """Slice an audio clip into a new MIDI track driven by a native Sampler loaded with the
    clip's slices (ascending pitches from C1 trigger successive slices). slice_mode: 1 =
    transients, 3 = 16-step grid. Returns the new track index."""
    return _post("slice_to_midi", {"track": track, "scene": scene, "slice_mode": slice_mode})


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
def audio_graph_connect(track: int, from_node: int, to_node: int, kind: str = "audio") -> dict:
    """Add an edge between two audio-graph nodes (ids from get_audio_graph). `kind` is the SIGNAL
    the wire carries: "audio" (multiple edges into a node sum, stereo) or "note" (ADR-0015: notes
    merge). A note edge is how notes reach an instrument once you route them explicitly —
    e.g. midi_in -> Arp -> instrument. Rejected if it duplicates an edge of that kind, is a
    self-loop, or creates a cycle."""
    return _post("audio_graph_connect", {"track": track, "from": from_node, "to": to_node, "kind": kind})


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
    """Set the project media root used for video discovery. Missing roots are reported, not silent."""
    return _post("set_media_root", {"path": path})


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
            "Custom look? add_node('CustomShader') + set_node_asset(id,'<file>.glsl') renders a project "
            "folder .glsl (contract in 'custom_assets' below); project-local C++ ops load on load_project.",
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
            "custom_shader": "add_node('CustomShader') + set_node_asset(id,'look.glsl') renders a .glsl in the "
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


if __name__ == "__main__":
    mcp.run()
