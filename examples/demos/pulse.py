"""Pulse — a driving techno / acid demo (128 BPM, A minor).

Music : 4-on-floor kick + clap + Euclidean hats, a rolling 16th acid bass (with a
        Drive stage for grit), and off-beat minor stabs.
Visual: a bespoke CustomShader "acid tunnel" (custom op, shaders/pulse_tunnel.glsl)
        → Feedback → Blur → Output (built-in FX). The kick transient punches the
        glow, the bass drives the warp, loudness swells the ring density.

Run with the app running:  uv run examples/demos/pulse.py
"""
import os
from vivid_demo import Vivid, find, save_demo, surge_voice
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER = os.path.join(HERE, "shaders", "pulse_tunnel.glsl")
PROJECT = os.path.join(HERE, "projects", "pulse")

# track map of the default session
LEAD, BASS, DRUMS = 0, 1, 2      # Pigments, Serum 2, EZdrummer 3
A1, A2, A3 = 33, 45, 57          # A across octaves


def build(v: Vivid, save: bool = True):
    v.new_project()
    v.bpm(128)

    # --- instruments : Surge XT (free CLAP synth — audible) shaped into a punchy acid bass and
    # a bright stab lead; drums stay on EZdrummer. ---
    surge_voice(v, BASS, cutoff=0.45, res=0.30, gain=1.0)   # dark, punchy acid bass
    surge_voice(v, LEAD, cutoff=0.78, res=0.18, gain=0.8)   # bright stab

    # --- drums (2 bars) : four-on-the-floor + clap on 2 & 4 + driving hats ---
    v.drums(DRUMS, 0, {
        "kick": "x...x...x...x...",
        "clap": "....x.......x...",
        "hat":  "..x...x...x...x.",
    }, bars=2, vel=0.9)
    v.euclid(DRUMS, 0, "openhat", pulses=5, steps=16, bars=2, vel=0.5)

    # --- acid bass (2 bars of 16ths, A minor pentatonic) : root pulse + octave pops ---
    st = 0.25
    riff = [A2, A2, None, A2, A3, None, A2, None, A2, 48, None, A2, 52, None, A2, A3]  # 1 bar, 16 steps
    seq = []
    for bar in range(2):
        base = bar * 4.0
        for i, p in enumerate(riff):
            seq.append((p, base + i * st, st * 0.9))
    v.bassline(BASS, 0, seq, length=8.0, vel=0.95)

    # --- lead : off-beat minor stabs, i-VI-III-VII over the 2 bars (Am F C G) ---
    stabs = []
    for bar, sym in enumerate(["Am", "F"]):          # 2 bars
        for p in theory.chord(sym, octave=4):
            for off in (1.5, 2.5, 3.5):              # the "and"s
                stabs.append({"p": p, "s": bar * 4.0 + off, "d": 0.2, "v": 0.7})
    v.set_clip(LEAD, 0, stabs, 8.0)

    # --- visuals : CustomShader acid tunnel -> Feedback -> Blur -> Output ---
    # After new_project the chain is Plasma->Feedback->Blur->Output; swap the generator
    # by adding a CustomShader and rewiring Feedback's input to it (Plasma is left idle).
    g = v.graph()["nodes"]
    fb, blur = find(g, "Feedback"), find(g, "Blur")
    cs = v.add_node("CustomShader")
    v.connect(fb, cs)                                # Feedback now reads the custom shader
    v.set_node_asset(cs, SHADER)                     # absolute path for live preview
    v.set_node_param(fb, "decay", 0.82)             # long trails
    v.set_node_param(blur, "radius", 0.15)

    # --- the bridge : audio -> visual params ---
    v.map("master.transient", cs, "glow", amount=1.0)     # kick flashes the core
    v.map("master.low",       cs, "warp", amount=0.9)     # bass bends the tunnel
    v.map("master.level",     cs, "density", amount=0.8)  # loudness swells the rings
    v.map("master.mid",       cs, "hue", amount=0.6)      # mids rotate the colour
    v.map("master.level",     fb, "decay", amount=0.4, lo=0.6, hi=0.95)

    v.launch_scene(0)
    v.play()

    if save:
        save_demo(v, PROJECT, SHADER, cs)


if __name__ == "__main__":
    build(Vivid())
