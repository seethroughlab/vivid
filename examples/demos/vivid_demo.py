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

    def bpm(self, bpm: float):
        return self.call("set_bpm", bpm=bpm)

    def play(self):
        return self.call("set_playing", playing=True)

    def stop(self):
        return self.call("set_playing", playing=False)

    def launch_scene(self, scene: int):
        return self.call("launch_scene", scene=scene)

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
    def clap_instrument(self, track: int, path: str):
        return self.call("set_track_clap_instrument", track=track, path=path)

    def clap_effect(self, track: int, path: str) -> int:
        return self.call("add_track_clap_effect", track=track, path=path)["index"]

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

    def connect(self, node_id: int, input_id: int):
        return self.call("connect_nodes", node_id=node_id, input_id=input_id)

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


def find(nodes, op_type):
    """First node id of a given op type in a get_graph() `nodes` list."""
    for n in nodes:
        if n.get("op") == op_type:
            return n["id"]
    return None


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
