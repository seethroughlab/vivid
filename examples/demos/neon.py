"""Neon — a synthwave demo (100 BPM, A minor).

Music : a driving 16th arpeggio (Pigments), an octave root bass on 8ths (Serum), and a
        gated-feel backbeat (kick/snare/hats) over an i-VI-III-VII loop (Am-F-C-G).
Visual: RETRO-VECTOR — REAL geometry with a Tron glow. Cyan concentric Lines rings (real
        LineList) + a magenta wireframe Mesh octahedron spinning in the center, added and
        run through Feedback→Blur for the neon bloom → Output. Bass rolls the rings, kick/
        snare flash the solid, the highs push the rings, the mids spin the octahedron.

Run with the app running:  uv run examples/demos/neon.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset, SURGE_FX, SURGE_FX_TYPE
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "neon")

ARP, BASS, DRUMS = 0, 1, 2


def build(v: Vivid, save: bool = True):
    v.new_project()
    v.bpm(100)
    prog = ["Am", "F", "C", "G"]      # i-VI-III-VII, 1 bar each

    # --- instruments : Surge XT — a bright detuned arp (with a shimmer FX) and a dark bass;
    # drums stay on EZdrummer. ---
    an = surge_preset(v, ARP, "pluck", prefer="Sync", gain=0.75)   # bright synthwave pluck arp
    v.clap_effect(ARP, SURGE_FX)                  # synthwave shimmer/space on the arp
    v.node_param(ARP, v.audio_node_id(ARP, "effect"), SURGE_FX_TYPE, 0.05)
    surge_preset(v, BASS, "bass", prefer="Square", gain=1.0)       # driving square bass

    # --- arp : classic driving 16th synthwave arp, one chord per bar ---
    notes = []
    for i, c in enumerate(prog):
        ps = theory.chord(c, octave=4)
        seq = theory.arpeggiate(ps, "up", rate=0.25, octaves=2, length=4.0, vel=0.75)
        for n in seq:
            notes.append({"p": n["p"], "s": i * 4.0 + n["s"], "d": n["d"] * 0.9, "v": n["v"]})
    v.set_clip(ARP, 0, notes, 16.0)

    # --- bass : root on straight 8ths, an octave down (drives the pulse) ---
    seq = []
    for i, c in enumerate(prog):
        root = theory.chord(c, octave=2)[0]
        for k in range(8):            # 8 eighth-notes per bar
            seq.append((root, i * 4.0 + k * 0.5, 0.45))
    v.bassline(BASS, 0, seq, length=16.0, vel=0.9)

    # --- drums : four-on-floor kick, snare backbeat, gated 16th hats (4 bars) ---
    v.drums(DRUMS, 0, {
        "kick":  "x...x...x...x...",
        "snare": "....x.......x...",
        "hat":   "x.x.x.x.x.x.x.x.",
    }, bars=4, vel=0.85)

    # --- visuals : REAL geometry (cyan Lines rings + a magenta wireframe Mesh), added, then
    # run through the existing Feedback->Blur->Output for a Tron/neon bloom. ---
    nodes = v.graph()["nodes"]
    fb, blur = find(nodes, "Feedback"), find(nodes, "Blur")
    rings = v.add_node("Lines")                   # real LineList: concentric n-gon rings (the grid)
    for k, val in dict(mode=0.8, count=0.35, sides=0.75, size=0.8, rotation=0.0,
                       r=0.2, g=0.85, b=1.0, bg_r=0.02, bg_g=0.01, bg_b=0.06).items():
        v.set_node_param(rings, k, val)           # cyan rings on near-black
    sun = v.add_node("Mesh")                      # a magenta wireframe octahedron — the vector "sun"
    for k, val in dict(shape=0.66, wireframe=1.0, size=0.5, spin=0.25, tilt=0.5,
                       r=1.0, g=0.2, b=0.75, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(sun, k, val)
    comp = v.add_node("Composite")
    v.connect(comp, rings, port=0)                # A = the rings
    v.connect(comp, sun, port=1)                  # B = the octahedron
    v.set_node_param(comp, "mode", 1.0)          # ADD
    v.connect(fb, comp)                           # Feedback reads the composite (glow) -> Blur -> Output
    v.set_node_param(fb, "decay", 0.55)           # a modest neon bloom (not a haze)
    v.set_node_param(blur, "radius", 0.05)

    # --- the bridge (smooth params only) ---
    v.map("master.low",       rings, "rotation", amount=0.3)                # bass rolls the rings
    v.map("master.transient", sun,   "size", amount=0.5, lo=0.45, hi=0.7)   # kick/snare flash the solid
    v.map("master.high",      rings, "size", amount=0.25, lo=0.7, hi=0.95)  # hats push the rings
    v.map("master.mid",       sun,   "spin", amount=0.6, lo=0.15, hi=0.6)   # mids spin the octahedron

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
