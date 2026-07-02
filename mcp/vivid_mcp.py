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
