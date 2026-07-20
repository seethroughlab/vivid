"""Fracture — the glitch pack shredding a real drum machine (90 BPM, glitch / IDM).

A full arrangement, authored over MCP: a Cassette Drums kit (the free VST3 — 606/808/909/MFB)
plays a broken beat that the native glitch pack chews — BeatRepeat rolls, Stutter, Reverse —
every effect clock-locked to the metronome so the mangling lands ON the beat. Under it, two
Surge XT voices: an acid bassline and off-beat minor stabs. A chromatic-split hex grid tears and
jolts with the shredding, so you SEE the glitch, not just hear it.

Requires Surge XT (free CLAP) + BPB Cassette Drums (free VST3), both in your plugin folders.

Run with the app running:  uv run examples/demos/fracture.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "fracture")
CASSETTE = "Cassette Drums"
A2, A3 = 45, 57                                        # A across octaves

# Glitch clock choices: Metronome is index 2 for Stutter/BeatRepeat/Reverse (Free/External/Metronome).
# division choices: 0=1/1 1=1/2 2=1/4 3=1/8 4=1/16 5=1/32 …
METRO = 2


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(90)

    # --- drums : Cassette Drums (a real kit) playing a broken IDM beat, GM-mapped ---
    drums = v.add_track(kind="instrument", instrument=CASSETTE)
    v.drums(drums, 0, {
        "kick": "x..x..x...x..x..",
        "snare": "....x.......x...",
        "hat":  "x.x.x.x.x.x.x.x.",
        "clap": "..........x.....",
    }, bars=2, vel=0.95)

    # --- glitch pack : a beat-synced chain of native ops appended after the drum plugin ---
    v.add_glitch(drums, "BeatRepeat", clock=METRO, chance=0.5, division=4, count=4, decay=0.1)
    v.add_glitch(drums, "Stutter",    clock=METRO, chance=0.35, division=4, count=6)
    v.add_glitch(drums, "Reverse",    clock=METRO, chance=0.25, division=3)   # 1/8 reversals

    # --- bass : Surge XT acid bassline, rolling 16ths in A minor pentatonic ---
    bass = v.add_graph_track("bass")
    surge_preset(v, bass, "acid", prefer="Bassline", gain=0.9)
    riff = [A2, A2, None, A2, A3, None, A2, None, A2, 48, None, A2, 52, None, A2, A3]
    seq = [(p, round(bar * 4.0 + i * 0.25, 6), 0.22) for bar in range(2)
           for i, p in enumerate(riff) if p is not None]
    v.bassline(bass, 0, seq, length=8.0, vel=0.95)

    # --- stabs : Surge XT sync pluck, off-beat minor stabs (Am) ---
    stab = v.add_graph_track("stab")
    surge_preset(v, stab, "pluck", prefer="Sync", gain=0.5)
    stabs = [{"p": p, "s": bar * 4.0 + off, "d": 0.18, "v": 0.7}
             for bar in range(2) for off in (1.5, 2.5, 3.5) for p in (57, 60, 64)]
    v.set_clip(stab, 0, stabs, 8.0)

    # --- visuals : a CHROMATIC-SPLIT hex grid that visibly TEARS on the glitch hits ---
    # Two offset ShapeGrids (hot red + cold cyan) ADD into an RGB-split look; the transient
    # rips them apart through Displace and jolts the whole frame through Transform — so you SEE
    # the audio being shredded, not just hear it. After new_project the graph is just Output.
    out = find(v.graph()["nodes"], "Output")
    sgR = v.add_node("ShapeGrid")                     # the red channel
    for k, val in dict(sides=0.6, cols=0.5, rows=0.5, size=0.52, rotation=0.0,
                       r=1.0, g=0.15, b=0.1, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(sgR, k, val)
    sgC = v.add_node("ShapeGrid")                     # the cyan channel (same grid, will be offset)
    for k, val in dict(sides=0.6, cols=0.5, rows=0.5, size=0.52, rotation=0.0,
                       r=0.1, g=0.9, b=1.0, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(sgC, k, val)
    split = v.add_node("Transform")                   # offsets the cyan grid -> chromatic aberration
    v.set_node_param(split, "tx", 0.01)
    v.connect(split, sgC)
    comp = v.add_node("Composite")
    v.connect(comp, sgR, port=0)
    v.connect(comp, split, port=1)
    v.set_node_param(comp, "mode", 1.0)               # ADD -> red + cyan = the split
    jolt = v.add_node("Transform")                    # whole-frame glitch jump on the beat
    v.connect(jolt, comp)
    disp = v.add_node("Displace")                     # the tear
    v.set_node_param(disp, "amount", 0.04)
    v.set_node_param(disp, "mode", 1.0)
    v.connect(disp, jolt)
    fb = v.add_node("Feedback")                       # trails on the hits
    v.set_node_param(fb, "decay", 0.45)
    v.connect(fb, disp)
    v.connect(out, fb)

    # --- the bridge : the shredded audio TEARS the picture (visible glitch, not just reactive) ---
    v.map("master.transient", disp,  "amount", amount=1.0, lo=0.03, hi=0.7)   # every hit rips the frame
    v.map("master.transient", jolt,  "tx",     amount=1.0, lo=0.5,  hi=0.62)  # + jolts it sideways
    v.map("master.high",      split, "tx",     amount=1.0, lo=0.0,  hi=0.05)  # hats widen the RGB split
    v.map("master.low",       jolt,  "rot",    amount=0.5, lo=0.5,  hi=0.56)  # kick skews it
    v.map("master.low",       fb,    "decay",  amount=0.5, lo=0.35, hi=0.72)  # kick lengthens trails

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
