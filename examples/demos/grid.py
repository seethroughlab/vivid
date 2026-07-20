"""Grid — a glitchy / IDM demo (90 BPM, A minor).

Music : a broken Euclidean beat (kick E(5,16), off-beat snare, busy hats), a bitcrushed
        Surge lead, and a dark detuned Surge bass. The native Bitcrush op mangles the lead.
Visual: TECHNICAL / WIREFRAME — REAL geometry, not a field. A teal Lines grid (real
        LineList) with a white wireframe icosahedron Mesh spinning over it, added → Output
        (sharp, no blur). Transients jolt the solid's scale, the highs jitter the grid,
        the lows spin the icosahedron.

Run with the app running:  uv run examples/demos/grid.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "grid")

LEAD, BASS, DRUMS = 0, 1, 2


def build(v: Vivid, save: bool = True):
    v.new_project()
    v.bpm(90)

    # --- instruments : a bitcrushed Surge lead + a dark Surge bass; EZdrummer for the beat ---
    surge_preset(v, LEAD, "lead", prefer="Digi", gain=0.8)     # digital/detuned lead
    crush = v.add_audio_fx(LEAD, "Bitcrush")     # native glitch op after the Surge lead
    v.set_audio_node_param(LEAD, crush, 0, 6.0)  # ~6-bit
    surge_preset(v, BASS, "FM Bass", prefer="FM Bass 3", gain=1.0)   # gritty FM bass

    # --- broken beat : Euclidean kick, off-beat snare, busy hats (2 bars) ---
    v.euclid(DRUMS, 0, "kick", pulses=5, steps=16, bars=2, vel=0.95, append=False)
    v.euclid(DRUMS, 0, "snare", pulses=2, steps=8, bars=2, rotation=1, vel=0.8)
    v.euclid(DRUMS, 0, "hat", pulses=11, steps=16, bars=2, vel=0.45)

    # --- lead : a syncopated A-minor-pentatonic riff, chopped ---
    scale = theory.scale_notes("A", "pentatonic_minor", octave=4)
    pat = [0, 2, 1, 4, 3, 2, 4, 1]           # scale-degree indices
    lead = []
    for bar in range(2):
        for i, d in enumerate(pat):
            if (i * 7) % 3:                   # skip some steps for a broken feel
                lead.append({"p": scale[d % len(scale)], "s": bar * 4.0 + i * 0.5, "d": 0.4, "v": 0.8})
    v.set_clip(LEAD, 0, lead, 8.0)

    # --- bass : root drone on the down-beats, detuned ---
    seq = [(theory.scale_notes("A", "pentatonic_minor", octave=2)[0], b * 1.0, 0.9) for b in range(8)]
    v.bassline(BASS, 0, seq, length=8.0, vel=0.95)

    # --- visuals : REAL geometry (a Lines grid + a wireframe Mesh icosahedron), added -> Output ---
    out = find(v.graph()["nodes"], "Output")
    lines = v.add_node("Lines")                  # real LineList: a technical grid
    for k, val in dict(mode=0.0, count=0.5, sides=0.6, size=0.85, rotation=0.0,
                       r=0.2, g=0.9, b=0.7, bg_r=0.02, bg_g=0.03, bg_b=0.05).items():
        v.set_node_param(lines, k, val)          # teal grid on near-black
    mesh = v.add_node("Mesh")                    # a spinning wireframe icosahedron
    for k, val in dict(shape=1.0, wireframe=1.0, size=0.42, spin=0.4, tilt=0.55,
                       r=0.75, g=1.0, b=0.85, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(mesh, k, val)           # white wireframe, black bg (ADD keys it over the grid)
    comp = v.add_node("Composite")
    v.connect(comp, lines, port=0)               # A = the grid
    v.connect(comp, mesh, port=1)                # B = the icosahedron
    v.set_node_param(comp, "mode", 1.0)         # ADD (sharp, no blur/haze — a data-viz look)
    v.connect(out, comp)                         # Output <- Composite

    # --- the bridge (smooth params only; grid/mesh structure stays baked) ---
    v.map("master.transient", mesh,  "size", amount=1.0, lo=0.3, hi=0.62)   # beats jolt the solid
    v.map("master.high",      lines, "rotation", amount=0.15)               # hats jitter the grid
    v.map("master.transient", lines, "size", amount=0.5, lo=0.78, hi=0.92)  # + the grid snaps
    v.map("master.low",       mesh,  "spin", amount=0.7, lo=0.25, hi=0.8)   # bass spins the icosahedron

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
