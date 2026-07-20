"""Mirror — a closed audio<->visual feedback loop (112 BPM).

The showcase: the bridge runs BOTH ways at once. The kick drives the visual feedback amount
(audio -> visual, the usual direction), and that same feedback value is wired straight BACK into
a filter on the drums (visual -> audio, the return leg almost nothing else demonstrates). So the
picture's state audibly shapes the sound that is shaping the picture — the loop closes on screen.

Plugin-free: a native sampler plays a bundled break, a native SVFilter sits on it, and the return
leg `viz.feedback -> filter cutoff` needs no plugin. Hard geometry only — a wireframe Mesh and a
ShapeGrid, feedback-smeared, with a MIRROR call-sign. No plasma.

Return-leg note: the `viz.feedback` source is the Feedback node's *current* decay value, so we
DRIVE that value from the kick (forward map) — a static visual param would send back a constant.

Run with the app running:  uv run examples/demos/mirror.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "mirror")
CASSETTE = "Cassette Drums"
# SVFilter param indices: 0=type, 1=cutoff, 2=resonance.
CUTOFF, RES = 1, 2


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(112)

    # --- drums : Cassette Drums, a steady 112-BPM pulse ---
    drums = v.add_track(kind="instrument", instrument=CASSETTE)
    v.drums(drums, 0, {
        "kick": "x...x...x...x...",
        "clap": "....x.......x...",
        "hat":  "..x...x...x...x.",
    }, bars=2, vel=0.9)

    # --- bass : Surge XT sub bass on the root ---
    basst = v.add_graph_track("bass")
    surge_preset(v, basst, "bass", prefer="", gain=0.8)
    v.bassline(basst, 0, [(33, i * 1.0, 0.9) for i in range(8)], length=8.0, vel=0.9)

    # --- pad : Surge XT pad through a native SVFilter — the filter the PICTURE drives back ---
    pad = v.add_graph_track("pad")
    surge_preset(v, pad, "pad", prefer="", gain=0.6)
    v.progression(pad, 0, ["i", "VI", "III", "VII"], key="A", scale="minor",
                  beats_per_chord=4.0, octave=4, vel=0.6, dur_frac=0.95)
    svf = v.add_audio_fx(pad, "SVFilter")             # native filter on the pad (no extra plugin)
    v.set_audio_node_param(pad, svf, 1, 900.0)        # base cutoff (Hz); the return leg sweeps it

    # --- visuals : wireframe Mesh + ShapeGrid -> Feedback -> Blur, with a call-sign ---
    out = find(v.graph()["nodes"], "Output")
    mesh = v.add_node("Mesh")
    for k, val in dict(shape=1.0, wireframe=1.0, size=0.4, spin=0.25, tilt=0.5,
                       r=1.0, g=0.35, b=0.95, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(mesh, k, val)                # bright magenta wireframe solid on black
    sg = v.add_node("ShapeGrid")
    for k, val in dict(sides=0.6, cols=0.45, rows=0.45, size=0.5, rotation=0.0,
                       r=0.2, g=0.9, b=1.0, bg_r=0.01, bg_g=0.02, bg_b=0.04).items():
        v.set_node_param(sg, k, val)                  # bright cyan hex grid
    compA = v.add_node("Composite")
    v.connect(compA, sg, port=0)
    v.connect(compA, mesh, port=1)
    v.set_node_param(compA, "mode", 1.0)             # ADD
    fb = v.add_node("Feedback")
    v.set_node_param(fb, "decay", 0.42)               # driven by the kick -> also the return source
    blur = v.add_node("Blur")
    v.set_node_param(blur, "radius", 0.15)
    v.connect(fb, compA)
    v.connect(blur, fb)
    title = v.add_node("VectorText")
    for k, val in dict(size=0.12, x=0.5, y=0.12, r=0.95, g=0.95, b=1.0,
                       bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(title, k, val)
    v.set_node_file(title, "file", os.path.join(HERE, "media", "mirror.txt"))
    compB = v.add_node("Composite")
    v.connect(compB, blur, port=0)
    v.connect(compB, title, port=1)
    v.set_node_param(compB, "mode", 1.0)
    v.connect(out, compB)

    # --- forward leg (audio -> visual) : drive the feedback + blur so the return sources MOVE ---
    v.map("master.transient", mesh, "size",   amount=0.7, lo=0.28, hi=0.5)
    v.map("master.low",       fb,   "decay",  amount=0.6, lo=0.4,  hi=0.82)   # kick -> feedback (=viz.feedback)
    v.map("master.mid",       blur, "radius", amount=0.5, lo=0.2,  hi=0.6)    # mids -> blur   (=viz.blur)

    # --- RETURN leg (visual -> audio) : the picture's state sweeps the filter on the PAD ---
    v.map_to_audio("viz.feedback", pad, svf, CUTOFF, amount=1.0, lo=400.0, hi=6000.0)  # feedback -> cutoff
    v.map_to_audio("viz.blur",     pad, svf, RES,    amount=1.0, lo=0.05,  hi=0.55)     # blur -> resonance

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
