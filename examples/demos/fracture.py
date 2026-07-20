"""Fracture — beat-synced glitch pack shredding a REAL audio break (90 BPM, glitch / IDM).

The showcase: an ordinary drum loop, imported as a real audio clip (not a synth), warped to
the project grid, then chewed by the native glitch pack — BeatRepeat rolls, Stutter, and
Reverse — every effect clock-locked to the metronome so the mangling lands ON the beat. A
hard-geometric ShapeGrid gets displaced and feedback-smeared in time with the shredding.

Needs the A1 feature (`import_audio_clip`) — the MCP-native way to get recorded audio into a
sampler cell so the glitch ops have something real to destroy. No plugins: the audio track is a
native sampler and every glitch op is native, so this piece is fully self-contained.

Run with the app running:  uv run examples/demos/fracture.py
(Swap examples/demos/media/break90.wav for a real break to taste.)
"""
import os
from vivid_demo import Vivid, find, save_geo

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "fracture")
BREAK = os.path.join(HERE, "media", "break90.wav")

# Glitch clock choices: Metronome is index 2 for Stutter/BeatRepeat/Reverse (Free/External/Metronome).
# division choices: 0=1/1 1=1/2 2=1/4 3=1/8 4=1/16 5=1/32 …
METRO = 2


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(90)

    # --- audio : a REAL break, imported into a native sampler track, warped to the grid ---
    drums = v.add_track(kind="audio")                 # a native sampler track (no plugin)
    beats = v.import_audio(drums, 0, BREAK, src_bpm=90)   # A1: decode the wav into scene 0
    print(f"  imported break: {beats:.1f} beats")
    v.auto_warp(drums, 0, sensitivity=0.5)            # detect transients -> warp markers
    v.warp(drums, 0, mode="beats")                    # lock the loop to the project tempo

    # --- glitch pack : a beat-synced chain of native ops appended after the sampler ---
    v.add_glitch(drums, "BeatRepeat", clock=METRO, chance=0.55, division=4, count=4, decay=0.1)
    v.add_glitch(drums, "Stutter",    clock=METRO, chance=0.4,  division=4, count=6)
    v.add_glitch(drums, "Reverse",    clock=METRO, chance=0.3,  division=3)   # 1/8 reversals

    # --- visuals : hard vertex geometry (a hex ShapeGrid), displaced + feedback-smeared ---
    # After new_project the graph is just an Output node; build a real-geometry chain into it.
    out = find(v.graph()["nodes"], "Output")
    sg = v.add_node("ShapeGrid")
    for k, val in dict(sides=0.6, cols=0.5, rows=0.5, size=0.5, rotation=0.0,
                       r=0.1, g=0.95, b=0.85, bg_r=0.02, bg_g=0.03, bg_b=0.05).items():
        v.set_node_param(sg, k, val)                  # teal hexes on near-black
    disp = v.add_node("Displace")                     # amount, mode — the "fracture"
    v.set_node_param(disp, "amount", 0.15)
    v.set_node_param(disp, "mode", 1.0)
    fb = v.add_node("Feedback")                       # decay — trails on the glitch hits
    v.set_node_param(fb, "decay", 0.55)
    v.connect(disp, sg)                               # ShapeGrid -> Displace
    v.connect(fb, disp)                               # -> Feedback
    v.connect(out, fb)                                # -> Output

    # --- the bridge : the shredded audio drives the picture (only smooth params) ---
    v.map("master.transient", sg,   "size",   amount=0.8, lo=0.42, hi=0.62)  # hits punch the grid
    v.map("master.high",      disp, "amount", amount=1.0, lo=0.08, hi=0.4)   # hats fracture it
    v.map("master.low",       fb,   "decay",  amount=0.5, lo=0.4,  hi=0.78)  # kick lengthens trails

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
