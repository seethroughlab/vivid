"""Prism — MIDI notes drive the picture DIRECTLY: pitch becomes colour (110 BPM, A minor).

The one thing audio analysis physically cannot do: know WHICH note is playing. Every other demo
maps a track's audio characteristic (loudness / onset / band energy) to the visuals — a loud low
note and a soft high note both just read as "a transient." Prism instead maps the track's NOTE
STREAM: an ascending A-minor-pentatonic run sweeps a solid mesh **blue → red by pitch**, velocity
drives its size, and each note-on flashes it. A kick's `master.transient` still pulses the grid
alongside — so you can watch the difference: the grid reacts to *loudness*, the mesh to *pitch*.

Note sources (new): `track_<id>.note` (last pitch, 0..1 over MIDI 0..127), `.velocity`, `.gate`
(a note-on flash). Requires Surge XT (free CLAP) for the voices.

Run with the app running:  uv run examples/demos/prism.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset, surge_drum, hits

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "prism")
BPM = 110

# A-minor pentatonic (A C D E G) across three octaves, A2..A5 — 16 notes = one ascending bar of 1/16s.
PENT = [45, 48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 74, 76, 79, 81]


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # --- voices : a bright melodic lead + a kick, both Surge (no drum-plugin dependency) ---
    lead = v.add_graph_track("lead")
    surge_preset(v, lead, "pluck", prefer="Sync", gain=0.7)
    kick = surge_drum(v, "kick", "kick", prefer="", gain=0.95)

    # --- the melody : climb the pentatonic in bar 1, descend in bar 2 (a full pitch sweep up & back),
    #     downbeats accented so velocity visibly varies. 2 bars, 1/16 grid. ---
    notes = []
    for i in range(16):
        notes.append({"p": PENT[i],      "s": i * 0.25,       "d": 0.22, "v": 0.98 if i % 4 == 0 else 0.55})
    for i in range(16):
        notes.append({"p": PENT[15 - i], "s": 4.0 + i * 0.25, "d": 0.22, "v": 0.98 if i % 4 == 0 else 0.55})
    v.set_clip(lead, 0, notes, 8.0)

    # --- kick : four-on-the-floor, so master.transient has a steady loudness pulse to contrast ---
    hits(v, kick, 0, "x...x...x...x...", 8.0, pitch=36, vel=0.95)
    hits(v, kick, 0, "x...x...x...x..." * 2, 8.0, pitch=36, vel=0.95)   # 2 bars

    # --- visuals : a solid Mesh octahedron over a ShapeGrid, added -> Output ---
    out = find(v.graph()["nodes"], "Output")
    sg = v.add_node("ShapeGrid")                         # the grid reacts to LOUDNESS (the contrast)
    for k, val in dict(sides=0.5, cols=0.5, rows=0.5, size=0.46, rotation=0.0,
                       r=0.16, g=0.16, b=0.2, bg_r=0.01, bg_g=0.01, bg_b=0.02).items():
        v.set_node_param(sg, k, val)
    m = v.add_node("Mesh")                               # the solid reacts to the NOTE (pitch->colour)
    for k, val in dict(shape=0.66, wireframe=0.0, size=0.34, spin=0.2, tilt=0.5,
                       r=0.0, g=0.0, b=0.0, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(m, k, val)
    comp = v.add_node("Composite"); v.set_node_param(comp, "mode", 1.0)   # ADD
    v.connect(comp, sg, port=0)
    v.connect(comp, m, port=1)
    v.connect(out, comp)

    # --- the bridge : the NOTE drives the mesh; LOUDNESS drives the grid (the whole point) ---
    lid = v.track_id(lead)
    # pitch -> hue: blue at low notes, red at high notes (a spectrum from one scalar). Amplified because
    # the played range (MIDI 45..81) only spans ~0.35..0.64 of the 0..127 normalization.
    v.map(f"track_{lid}.note", m, "b", amount=1.6, invert=True)          # low pitch -> blue
    v.map(f"track_{lid}.note", m, "r", amount=1.6)                       # high pitch -> red
    v.map(f"track_{lid}.note", m, "g", amount=0.6, curve=0.0)            # a little green in the middle
    v.map(f"track_{lid}.velocity", m, "size", amount=0.7, lo=0.30, hi=0.62)  # harder notes bigger
    v.map(f"track_{lid}.gate", m, "spin", amount=0.6)                    # each note-on kicks the spin
    # the contrast: the grid only knows loudness (it can NEVER know which note)
    v.map("master.transient", sg, "size", amount=0.7, lo=0.42, hi=0.6)

    v.launch_scene(0)
    v.play()

    if save:
        save_geo(v, PROJECT)
    print("built. the mesh colour tracks PITCH (blue low -> red high); the grid tracks LOUDNESS.")


if __name__ == "__main__":
    build(Vivid())
