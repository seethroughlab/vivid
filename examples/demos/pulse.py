"""Pulse — a driving techno / acid demo (128 BPM, A minor).

Music : 4-on-floor kick + clap + Euclidean hats, a rolling 16th acid bass (with a
        Drive stage for grit), and off-beat minor stabs.
Visual: HARD-GEOMETRIC — REAL vertex geometry, not a shader field. A ShapeGrid of
        hexagons (a real vertex buffer) with a solid 3D Mesh octahedron punching
        through the center, added together → Output. The kick transient punches the
        octahedron + the grid, the bass rotates the grid, the mids spin the solid.

Run with the app running:  uv run examples/demos/pulse.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "pulse")

# track map of the default session
LEAD, BASS, DRUMS = 0, 1, 2      # Pigments, Serum 2, EZdrummer 3
A1, A2, A3 = 33, 45, 57          # A across octaves


def build(v: Vivid, save: bool = True):
    v.new_project()
    v.bpm(128)

    # --- instruments : real Surge XT factory patches (generic preset flow — pick by name). ---
    surge_preset(v, BASS, "acid", prefer="Bassline 1", gain=1.0)   # acid bass
    surge_preset(v, LEAD, "pluck", prefer="Sync", gain=0.7)        # bright stab

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

    # --- visuals : real geometry (ShapeGrid hexagons + a solid Mesh octahedron), added -> Output ---
    # After new_project the chain is Plasma->Feedback->Blur->Output and the Output node is the
    # display. We build real-geometry generators and route them straight to Output (no field/haze).
    out = find(v.graph()["nodes"], "Output")
    sg = v.add_node("ShapeGrid")                     # a real vertex-buffer grid of hexagons
    for k, val in dict(sides=0.6, cols=0.45, rows=0.3, size=0.5, rotation=0.0,
                       r=0.95, g=0.25, b=0.2, bg_r=0.03, bg_g=0.02, bg_b=0.04).items():
        v.set_node_param(sg, k, val)                 # bold red hexagons on near-black
    m = v.add_node("Mesh")                           # a solid 3D octahedron punching the center
    for k, val in dict(shape=0.66, wireframe=0.0, size=0.32, spin=0.3, tilt=0.5,
                       r=1.0, g=0.85, b=0.3, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(m, k, val)                  # amber solid, black bg so ADD keys it over the grid
    comp = v.add_node("Composite")
    v.connect(comp, sg, port=0)                      # A = the hex grid
    v.connect(comp, m, port=1)                       # B = the octahedron
    v.set_node_param(comp, "mode", 1.0)             # ADD (black bg of each layer drops out)
    v.connect(out, comp)                             # Output <- Composite

    # --- the bridge : audio -> visual params (only smooth params; structure stays baked) ---
    v.map("master.transient", m,  "size", amount=1.0, lo=0.28, hi=0.6)   # kick punches the solid
    v.map("master.transient", sg, "size", amount=0.8, lo=0.42, hi=0.62)  # + the grid breathes
    v.map("master.low",       sg, "rotation", amount=0.5)                # bass rotates the grid
    v.map("master.mid",       m,  "spin", amount=0.6, lo=0.2, hi=0.7)    # mids spin the octahedron

    v.launch_scene(0)
    v.play()

    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
