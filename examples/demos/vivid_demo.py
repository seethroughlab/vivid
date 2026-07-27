"""Shared MCP-authoring helpers for the Vivid demo projects.

Everything here talks to the running app's loopback control server (127.0.0.1:9876)
— the same backend the MCP bridge (mcp/vivid_mcp.py) drives. Authoring a whole
song + reactive visual graph through this thin client IS the point: it shows how
MCP-friendly Vivid is. The music helpers reuse mcp/theory.py (the same theory the
MCP tools use), so a demo reads like a score.

Run a demo builder with the app running, e.g.:  uv run examples/demos/pulse.py
"""
from __future__ import annotations
import json
import os
import shutil
import sys
import time
import urllib.request

# Reuse the bridge's music theory (chords / scales / drums / euclidean).
_MCP = os.path.join(os.path.dirname(__file__), "..", "..", "mcp")
sys.path.insert(0, os.path.abspath(_MCP))
import theory  # noqa: E402

PORT = int(os.environ.get("VIVID_PORT", "9876"))
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


class Vivid:
    """A tiny control-server client. Every method maps to one MCP tool."""

    def __init__(self, port: int = PORT):
        self.base = f"http://127.0.0.1:{port}"

    def call(self, method: str, **payload) -> dict:
        data = json.dumps(payload).encode()
        req = urllib.request.Request(f"{self.base}/{method}", data=data,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=30) as r:
            res = json.loads(r.read())
        if not res.get("ok", False):
            raise RuntimeError(f"{method} failed: {res.get('code')} {res.get('error')}")
        return res

    # --- transport / session ---
    def new_project(self):
        return self.call("new_project")

    def reset(self):
        """A truly clean slate: new_project clears clips/visuals/mappings but LEAVES tracks in
        place (clean-start sessions launch at 0 tracks, so re-running a builder would otherwise
        pile up tracks). Remove them all so the builder authors from nothing."""
        self.call("new_project")
        for _ in range(64):
            n = self.call("list_tracks").get("tracks", [])
            if not n:
                break
            self.call("remove_track", track=0)
        return self

    def bpm(self, bpm: float):
        return self.call("set_bpm", bpm=bpm)

    def launch_quantize(self, bars: int):
        """Scene-launch quantization in bars: a queued scene switch waits until the next N-bar
        boundary (1 = next bar; 4 = let a 4-bar phrase finish before switching). Persisted."""
        return self.call("set_launch_quantize", bars=bars)

    def play(self):
        return self.call("set_playing", playing=True)

    def stop(self):
        return self.call("set_playing", playing=False)

    def launch_scene(self, scene: int):
        return self.call("launch_scene", scene=scene)

    def scenes(self, names: list[str]) -> list[int]:
        """Lay out song SECTIONS as named scenes (intro/verse/chorus/bridge/outro). Call right after
        reset() — a fresh session has 1 scene, so this appends the rest and names them all. The song
        FORM is the order you later launch/perform them; instruments 'leave' a section simply by
        having no clip in that scene. Returns [0..N-1]. (kMaxScenes = 8.)"""
        cur = self.call("status").get("scenes", 1)   # new_project keeps the count; add only the deficit
        for _ in range(max(0, len(names) - cur)):
            self.call("add_scene")
        for i, nm in enumerate(names):
            self.call("set_scene_name", scene=i, name=nm)
        return list(range(len(names)))

    def perform(self, order: list[int], bars_each: list[int], bpm: float, bar_beats: float = 4.0):
        """Audition the whole song: step launch_scene through the section `order`, holding each for
        `bars_each` bars. Launch is bar-quantized in the engine, so switches land on the bar. This is
        the only way to HEAR the full form (there's no native scene auto-advance); the populated scene
        grid + saved project remain the durable artifact. Drives entirely over the control server."""
        spb = 60.0 / max(1e-6, bpm)
        self.play()
        for scene, bars in zip(order, bars_each):
            self.launch_scene(scene)
            time.sleep(bars * bar_beats * spb)

    def save_project(self, path: str):
        return self.call("save_project", path=path)

    def load_project(self, path: str):
        return self.call("load_project", path=path)

    def clear_vst_fx(self, track: int):
        """Strip a track's leftover VST3 effect chain (new_project keeps the per-track audio
        graph, so the default session's effects linger). Leaves the instrument in place."""
        for _ in range(12):
            try:
                self.call("remove_effect", track=track, index=0)
            except RuntimeError:
                break

    # --- CLAP plugins (Surge XT + Surge XT Effects — free, audible, patch-rich) ---
    # Loading is async in the app (a slow plugin ctor won't wedge the control server); these
    # helpers fire the request then block CLIENT-SIDE until it's applied, so callers keep their
    # simple synchronous flow while the app stays responsive throughout.
    def wait_for_plugins(self, timeout: float = 240.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            st = self.call("plugin_load_status")
            if st.get("pending", 0) == 0:
                if st.get("error"):
                    raise RuntimeError(f"CLAP load failed: {st['error']}")
                return
            time.sleep(0.3)
        raise RuntimeError("CLAP load timed out (plugin still instantiating)")

    def clap_instrument(self, track: int, path: str):
        r = self.call("set_track_clap_instrument", track=track, path=path)
        if r.get("loading"):
            self.wait_for_plugins()
        return r

    def clap_effect(self, track: int, path: str) -> int:
        self.call("add_track_clap_effect", track=track, path=path)
        self.wait_for_plugins()
        return self.audio_node_id(track, "effect") or 0   # first effect node (index no longer sync)

    def audio_node_id(self, track: int, kind: str) -> int | None:
        """The graph node id of the first node of a kind ('instrument'/'effect'/'output')."""
        for n in self.call("get_audio_graph", track=track).get("nodes", []):
            if n["kind"] == kind:
                return n["id"]
        return None

    def node_param(self, track: int, node: int, index: int, value: float):
        """Set a graph node's param (0..1 for CLAP/VST3; native uses the op's own range)."""
        return self.call("audio_graph_set_node_param", track=track, node=node, param=index, value=value)

    # --- audio graph (per-track native FX) ---
    def add_audio_fx(self, track: int, op: str) -> int:
        return self.call("audio_graph_add_op", track=track, op=op)["node"]

    def set_audio_node_param(self, track: int, node: int, param: int, value: float):
        return self.call("audio_graph_set_node_param", track=track, node=node, param=param, value=value)

    def set_track_gain(self, track: int, gain: float):
        return self.call("set_track_gain", track=track, gain=gain)

    # --- clip authoring (notes are {p,s,d,v}: pitch, start-beat, dur-beats, velocity) ---
    def set_clip(self, track: int, scene: int, notes: list[dict], length: float):
        return self.call("set_clip", track=track, scene=scene, notes=notes, length=length)

    def clear_clip(self, track: int, scene: int):
        return self.call("set_clip", track=track, scene=scene, notes=[], length=4.0)

    def progression(self, track, scene, chords, beats_per_chord=4.0, key="", scale="major",
                    octave=4, vel=0.8, voicing="close", dur_frac=1.0):
        """Chord progression. `chords` are roman numerals if `key` is set, else symbols.
        dur_frac < 1 shortens each chord into a stab."""
        notes = []
        for i, sym in enumerate(chords):
            start = i * beats_per_chord
            pitches = (theory.roman(sym, key, scale, octave) if key
                       else theory.chord(sym, octave=octave, voicing=voicing))
            for p in pitches:
                notes.append({"p": p, "s": start, "d": beats_per_chord * dur_frac, "v": vel})
        self.set_clip(track, scene, notes, max(1.0, len(chords) * beats_per_chord))
        return notes

    def drums(self, track, scene, patterns: dict, bar_beats=4.0, bars=1, vel=0.85, dur=0.1):
        notes = []
        for name, steps in patterns.items():
            note = theory.drum_note(name)
            for b in range(max(1, bars)):
                notes += theory.drum_steps(steps, note, bar_beats, vel, dur, start=b * bar_beats)
        self.set_clip(track, scene, notes, bar_beats * max(1, bars))
        return notes

    def euclid(self, track, scene, drum, pulses, steps, bar_beats=4.0, bars=1,
               rotation=0, vel=0.7, dur=0.1, append=True):
        note = theory.drum_note(drum)
        pat = theory.euclidean(pulses, steps, rotation)
        step = bar_beats / steps if steps else bar_beats
        add = [{"p": note, "s": round(b * bar_beats + i * step, 6), "d": dur, "v": vel}
               for b in range(max(1, bars)) for i, on in enumerate(pat) if on]
        cur = self.call("get_clip", track=track, scene=scene) if append else {"notes": [], "length": 0}
        notes = cur.get("notes", []) + add
        self.set_clip(track, scene, notes, max(cur.get("length", 0.0), bar_beats * max(1, bars)))
        return add

    def bassline(self, track, scene, seq: list[tuple], length: float, vel=0.9):
        """A step bassline: seq of (pitch_or_None, start_beat, dur_beats). None = rest (skipped)."""
        notes = [{"p": p, "s": s, "d": d, "v": vel} for (p, s, d) in seq if p is not None]
        self.set_clip(track, scene, notes, length)
        return notes

    def arp(self, track, scene, chord_symbol, pattern="up", rate=0.25, octaves=1,
            length=4.0, vel=0.8, root_octave=4):
        pitches = theory.chord(chord_symbol, octave=root_octave)
        notes = theory.arpeggiate(pitches, pattern, rate, octaves, length, vel)
        self.set_clip(track, scene, notes, length)
        return notes

    # --- visual graph ---
    def graph(self) -> dict:
        return self.call("get_graph")

    def add_node(self, op: str) -> int:
        return self.call("add_node", op=op)["id"]

    def connect(self, node_id: int, input_id: int, port: int = 0):
        return self.call("connect_nodes", node_id=node_id, input_id=input_id, port=port)

    # --- instrument presets (generic browse/load) ---
    def list_presets(self, track: int, filter: str = "") -> dict:
        return self.call("list_presets", track=track, filter=filter)

    def load_preset(self, track: int, id: str):
        return self.call("load_preset", track=track, id=id)

    def set_generator(self, op: str):
        return self.call("set_generator", op=op)

    def set_active_output(self, node_id: int):
        return self.call("set_active_output", id=node_id)

    def set_node_asset(self, node_id: int, asset: str):
        return self.call("set_node_asset", id=node_id, asset=asset)

    def set_node_param(self, node_id: int, name: str, value: float):
        return self.call("set_node_param", node_id=node_id, name=name, value=value)

    def swap_generator(self, op: str) -> int:
        """Replace the active generator: add a node of `op` and rewire the first Feedback's
        input to it (the default Plasma is left idle). Returns the new node id."""
        nodes = self.graph()["nodes"]
        fb = find(nodes, "Feedback")
        new = self.add_node(op)
        if fb is not None:
            self.connect(fb, new)
        return new

    # --- the bridge (audio characteristic -> visual param) ---
    def map(self, src: str, node_id: int, param: str, amount=1.0, curve=0.0, lo=0.0, hi=1.0, invert=False):
        return self.call("connect_mapping", src=src, dst=f"node:{node_id}.{param}",
                         amount=amount, curve=curve, lo=lo, hi=hi, invert=invert)

    def track_id(self, track: int) -> int:
        """The STABLE id of a track (by index) — the one used in per-track mapping sources. It is NOT
        the index (e.g. index 0 may be id 19)."""
        for t in self.call("list_tracks").get("tracks", []):
            if t.get("index") == track:
                return t["id"]
        raise RuntimeError(f"track {track} not found")

    def track_viz(self, track: int, band: str, node_id: int, param: str, **kw):
        """Route ONE instrument to ONE visual param, so each instrument has a visibly separate effect.
        `band` is that track's characteristic — audio analysis: 'low' | 'high' | 'transient' | 'level';
        or NOTE-derived (drives visuals by WHICH note, not just loudness): 'note' (last pitch, 0..1 over
        MIDI 0..127) | 'velocity' | 'gate' (a note-on flash). Uses the track's stable id: source =
        'track_<id>.<band>'. Same knobs as map() (amount/curve/lo/hi/invert)."""
        return self.call("map_audio_to_visual_param", source="track", track=track,
                         characteristic=band, node_id=node_id, param=param, **kw)

    def master_viz(self, band: str, node_id: int, param: str, **kw):
        """Route a master-bus characteristic to one visual param through the first-class MCP helper."""
        return self.call("map_audio_to_visual_param", source="master", characteristic=band,
                         node_id=node_id, param=param, **kw)

    # --- the RETURN leg of the bridge (visual -> audio): a viz.* source drives an audio param ---
    # This is what makes the loop bidirectional. Sources: "viz.warp" / "viz.glow" / "viz.feedback"
    # (read from the current value of the visual param owning that shader-uniform, so map that param
    # from audio too, or it won't move). `node` is an audio-graph node id, `param` its param index.
    def map_to_audio(self, viz_src: str, track: int, node: int, param: int, amount=1.0,
                     curve=0.0, lo=0.0, hi=1.0, invert=False):
        return self.call("connect_mapping", src=viz_src, dst=f"gnode:{track}:{node}:{param}",
                         amount=amount, curve=curve, lo=lo, hi=hi, invert=invert)

    # --- audio tracks + REAL-audio clips (A1: decode a file straight into a sampler cell) ---
    def add_track(self, kind: str = "instrument", instrument: str = "") -> int:
        return self.call("add_track", kind=kind, instrument=instrument)["track"]

    def add_graph_track(self, name: str = "") -> int:
        """A bare instrument (note-driven) track with an empty audio graph — no instrument label
        required (native instruments don't resolve through add_track). Set its voice afterwards
        with clap_instrument / surge_preset, and author its note graph with the add_* helpers."""
        return self.call("add_graph_track", name=name)["track"]

    def import_audio(self, track: int, scene: int, path: str, src_bpm: float = 0.0) -> float:
        """Import an audio file (.wav/.aif/.flac/.mp3) into a sampler track's scene clip (decoded
        and resampled). Returns the loop length in beats. Follow with auto_warp/warp to lock it to
        the project tempo. `track` must be a sampler track (add_track(kind='audio'))."""
        return self.call("import_audio_clip", track=track, scene=scene, path=path,
                         src_bpm=src_bpm).get("length", 0.0)

    def auto_warp(self, track: int, scene: int, sensitivity: float = 0.5):
        return self.call("audio_auto_warp", track=track, scene=scene, sensitivity=sensitivity)

    def warp(self, track: int, scene: int, mode: str = "beats", enabled: bool = True):
        """Lock an audio clip to the project tempo. mode: 'beats' (grid), 'complex', or 'repitch'."""
        return self.call("audio_set_warp", track=track, scene=scene, enabled=enabled, mode=mode)

    # --- native audio-graph authoring (note-as-signal generators, modulators, glitch) ---
    def add_generator(self, track: int, op: str) -> int:
        """A native note/audio SOURCE as a graph node (Euclid / Chord / RandMelody / …)."""
        return self.call("audio_graph_add_source", track=track, op=op)["node"]

    def add_note_fx(self, track: int, op: str) -> int:
        """A native NOTE EFFECT node — notes in, notes out (e.g. 'Arp')."""
        return self.call("audio_graph_add_note_op", track=track, op=op)["node"]

    def add_mod(self, track: int, op: str) -> int:
        """A native MODULATOR node — emits a control signal, no audio (e.g. 'LFO')."""
        return self.call("audio_graph_add_mod_op", track=track, op=op)["node"]

    def add_midi_in(self, track: int) -> int:
        return self.call("audio_graph_add_midi_in", track=track)["node"]

    def place_generator(self, track: int, scene: int, gtype: str):
        """Place a note GENERATOR in a scene cell (Euclid / Chord / RandMelody). Unlike a graph
        source node, a scene-cell generator actually EMITS notes when its scene is launched — they
        run through the track's note path into the instrument. This is how 'no clip authored'
        generative music sounds."""
        return self.call("place_generator", track=track, scene=scene, type=gtype)

    def set_gen_param(self, track: int, scene: int, name: str, value: float):
        """Set a placed generator's param by name (e.g. Euclid 'pulses', RandMelody 'density')."""
        return self.call("set_generator_param", track=track, scene=scene, name=name, value=value)

    def connect_audio_nodes(self, track: int, frm: int, to: int, kind: str = "audio"):
        """Wire two nodes on ONE track. kind='note' carries notes, 'audio' sums signal."""
        return self.call("audio_graph_connect", track=track, to=to, kind=kind, **{"from": frm})

    def connect_mod(self, track: int, frm: int, to: int, param: int, amount: float = 1.0,
                    bipolar: bool = False, curve: float = 0.0):
        """Wire a modulator node -> one param (by index) of another node. amount is a fraction of
        the target param's range; bipolar straddles the base value (an LFO for pitch/pan)."""
        return self.call("audio_graph_connect_control", track=track, to=to, param=param,
                         amount=amount, bipolar=bipolar, curve=curve, **{"from": frm})

    def set_anode_named(self, track: int, node: int, name: str, value: float):
        """Set a native audio-graph node's param BY NAME (e.g. Euclid 'pulses', LFO 'division')."""
        return self.call("audio_graph_set_node_param_by_name", track=track, node=node,
                         name=name, value=value)

    def add_glitch(self, track: int, op: str, **params) -> int:
        """Insert a native glitch op as a graph node and set its params by name. Ops:
        Stutter / BeatRepeat / Reverse / TapeStop / Scratch / Stretch / FreqShift. To beat-sync,
        pass the op's clock choice index (e.g. clock=2 = Metronome for Stutter/BeatRepeat/Reverse,
        clock=1 for TapeStop/Scratch). Returns the node id."""
        node = self.call("audio_graph_add_op", track=track, op=op)["node"]
        for name, val in params.items():
            self.set_anode_named(track, node, name, float(val))
        return node

    # --- external-pixel sources (video clips / webcam / stills) ---
    def set_media_root(self, path: str):
        """Set the project media root (the base relative Video/Image paths resolve against)."""
        return self.call("set_media_root", path=path)

    def video(self, path: str) -> int:
        """Add a self-decoding Video node playing the movie at `path` (mp4/mov, incl. HAP-encoded
        .mov). Absolute path; make it portable on save. Each Video node owns its own decoder."""
        nid = self.add_node("Video")
        self.call("set_node_file_param", node_id=nid, name="file", value=path)
        return nid

    def webcam(self, device: int = 0, resolution: int = 1, fps: int = 2) -> int:
        """Add a live Webcam source node. resolution: 0=480p,1=720p,2=1080p; fps: 0=15..3=60."""
        nid = self.add_node("Webcam")
        for k, val in dict(active=1.0, device=float(device),
                           resolution=float(resolution), fps=float(fps)).items():
            self.set_node_param(nid, k, val)
        return nid

    def image(self, path: str) -> int:
        """Add an Image node showing the still at `path` (absolute; make it portable on save)."""
        nid = self.add_node("Image")
        self.call("set_node_file_param", node_id=nid, name="file", value=path)
        return nid

    def set_node_file(self, node_id: int, name: str, value: str):
        return self.call("set_node_file_param", node_id=node_id, name=name, value=value)


def save_demo(v: Vivid, project_dir: str, shader_src: str, cs_id: int):
    """Copy a CustomShader source into the project folder, point the node at it by a
    RELATIVE name (portable), save the project, then restore the absolute path so the
    live preview keeps rendering."""
    os.makedirs(project_dir, exist_ok=True)
    name = os.path.basename(shader_src)
    shutil.copy(shader_src, os.path.join(project_dir, name))
    v.set_node_asset(cs_id, name)
    v.save_project(project_dir)
    v.set_node_asset(cs_id, shader_src)
    print(f"saved -> {project_dir}")


def save_geo(v: Vivid, project_dir: str):
    """Save a geometry-only demo (real-geometry ops need no external data asset)."""
    os.makedirs(project_dir, exist_ok=True)
    v.save_project(project_dir)
    print(f"saved -> {project_dir}")


def find(nodes, op_type):
    """First node id of a given op type in a get_graph() `nodes` list."""
    for n in nodes:
        if n.get("op") == op_type:
            return n["id"]
    return None


# --- Control-server preflight / capture helpers -----------------------------------------------
# Shared by the demo builders, the tutorial build.py scripts, and the showcase QA harness so
# there is one copy of the control-server etiquette (preflight, tolerant calls, warm-up capture).

def require_control_server(v: "Vivid", context: str = "") -> None:
    """Raise SystemExit with a launch hint if the app's control server is unreachable. `context`
    names the walkthrough for the error message (e.g. "the live-shader-edit walkthrough")."""
    try:
        v.call("status")
    except Exception as exc:  # noqa: BLE001
        where = f" before {context}" if context else ""
        raise SystemExit(
            f"Vivid must be running{where}.\n"
            f"Could not reach the control server at {v.base}: {exc}\n"
            "Launch Vivid (VIVID_DISCARD_RECOVERY=1 for a disposable run) and confirm the control "
            "server is listening; set VIVID_PORT if you changed it."
        ) from exc


def call_optional(v: "Vivid", method: str, **payload) -> dict | None:
    """Call a control-server method, warning instead of failing. Used for proof/QA hooks so a
    warming buffer or an absent optional feature degrades to a warning, not a hard error."""
    try:
        return v.call(method, **payload)
    except RuntimeError as exc:  # noqa: BLE001
        print(f"[warn] {method} skipped: {exc}")
        return None


def warm_capture(v: "Vivid", path: str, tries: int = 10, delay: float = 0.5) -> dict | None:
    """Capture, retrying until the frame is non-blank. A freshly-registered dylib operator's GPU
    pipeline lazily initializes on its first draw; in a headless/unfocused session the frame loop is
    throttled, so the node can take several seconds of wall-clock to warm up (in a normal focused
    session it warms in one frame). Returns early once non-blank, else the last capture. The number
    of attempts made is stamped onto the returned dict as `warm_attempts` (1 = warmed immediately)."""
    r = None
    for attempt in range(1, tries + 1):
        r = call_optional(v, "capture_frame", path=path)
        if isinstance(r, dict):
            r["warm_attempts"] = attempt
            if r.get("captured") and not r.get("is_blank", True):
                return r
        time.sleep(delay)
    return r


# --- Surge XT (a free/open CLAP synth — always audible, unlike the licensed VST3s) ---
SURGE = "/Library/Audio/Plug-Ins/CLAP/Surge XT.clap"
SURGE_FX = "/Library/Audio/Plug-Ins/CLAP/Surge XT Effects.clap"
# Scene-A param indices (all normalized 0..1). Enough to voice a bass / pad / lead from the Init saw.
SURGE_P = dict(cutoff=319, res=320, atk=329, dec=331, sus=333, rel=334,
               osc1_type=256, uni_voices=265, uni_detune=264)
SURGE_FX_TYPE = 12   # 0..1 selects the effect (low end ≈ delay/reverb family)


def surge_voice(v: "Vivid", track: int, cutoff=0.6, res=0.1, uni_voices=None, uni_detune=None,
                atk=None, rel=None, gain=1.0):
    """Assign Surge XT to a track and lightly shape the (loud) Init saw into a role. Only the
    passed params are set — the amp envelope is left at Surge's full-sustain default unless atk/rel
    are given, so voices stay audible. Returns the Surge instrument node id."""
    v.clear_vst_fx(track)          # drop the default session's leftover VST3 fx (clean Surge -> Output)
    v.clap_instrument(track, SURGE)
    v.set_track_gain(track, gain)
    n = v.audio_node_id(track, "instrument")
    p = SURGE_P
    opt = dict(cutoff=cutoff, res=res, uni_voices=uni_voices, uni_detune=uni_detune, atk=atk, rel=rel)
    for key, val in opt.items():
        if val is not None:
            v.node_param(track, n, p[key], val)
    return n


def surge_drum(v: "Vivid", name: str, patch_filter: str, prefer: str = "", gain: float = 0.9) -> int:
    """A drum VOICE on its own Surge track: a fresh graph track loaded with a Surge percussion
    patch (Surge XT ships 60 kicks / 44 snares / 46 hats / claps / toms — all pickable by name via
    the generic preset flow). Trigger it with `hits()`. Leaning on the one free CLAP plugin for the
    whole kit keeps the demos reproducible — no sample packs, no per-plugin code."""
    t = v.add_graph_track(name)
    surge_preset(v, t, patch_filter, prefer=prefer, gain=gain)
    return t


def hits(v: "Vivid", track: int, scene: int, steps: str, length: float,
         pitch: int = 60, vel: float = 0.9, dur: float = 0.11) -> list[dict]:
    """Write a one-note rhythm (a step string like 'x...x...x...x...') into a drum voice's clip —
    each 'x' is a hit, '.'/'-'/' ' is a rest. `length` beats span the whole string."""
    n = max(1, len(steps)); step = length / n
    notes = [{"p": pitch, "s": round(i * step, 6), "d": dur, "v": vel}
             for i, ch in enumerate(steps) if ch not in ".- "]
    v.set_clip(track, scene, notes, length)
    return notes


def surge_preset(v: "Vivid", track: int, name_filter: str, prefer: str = "", gain: float = 1.0) -> int:
    """Assign Surge XT to a track and load a factory PATCH whose name matches `name_filter` (the
    generic preset flow — pick the timbre by name, not by tweaking knobs). `prefer` biases the
    choice to the first name containing it. Returns the Surge instrument node id."""
    v.clear_vst_fx(track)
    v.clap_instrument(track, SURGE)
    v.set_track_gain(track, gain)
    presets = v.list_presets(track, name_filter).get("presets", [])
    if presets:
        pick = None
        if prefer:
            pick = next((p for p in presets if prefer.lower() in p["name"].lower()), None)
        pick = pick or presets[0]
        v.load_preset(track, pick["id"])
        print(f"  t{track}: '{pick['name']}'  ({name_filter!r})")
    else:
        print(f"  t{track}: no preset matched {name_filter!r} — using Surge init")
    return v.audio_node_id(track, "instrument")
