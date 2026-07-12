"""Neon — a synthwave demo (100 BPM, A minor).

Music : a driving 16th arpeggio (Pigments), an octave root bass on 8ths (Serum), and a
        gated-feel backbeat (kick/snare/hats) over an i-VI-III-VII loop (Am-F-C-G).
Visual: a bespoke CustomShader retro grid + neon sun (shaders/neon_grid.glsl, custom op)
        → Feedback → Blur → Output. Bass rolls the grid, snare/kick flash it, the highs
        sharpen the scanlines, the mids shift the neon hue.

Run with the app running:  uv run examples/demos/neon.py
"""
import os
from vivid_demo import Vivid, find, save_demo, surge_voice, SURGE_FX, SURGE_FX_TYPE
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER = os.path.join(HERE, "shaders", "neon_grid.glsl")
PROJECT = os.path.join(HERE, "projects", "neon")

ARP, BASS, DRUMS = 0, 1, 2


def build(v: Vivid, save: bool = True):
    v.new_project()
    v.bpm(100)
    prog = ["Am", "F", "C", "G"]      # i-VI-III-VII, 1 bar each

    # --- instruments : Surge XT — a bright detuned arp (with a shimmer FX) and a dark bass;
    # drums stay on EZdrummer. ---
    an = surge_voice(v, ARP, cutoff=0.72, res=0.12, uni_voices=0.35, uni_detune=0.3, gain=0.75)
    v.clap_effect(ARP, SURGE_FX)                  # synthwave shimmer/space on the arp
    v.node_param(ARP, v.audio_node_id(ARP, "effect"), SURGE_FX_TYPE, 0.05)
    surge_voice(v, BASS, cutoff=0.4, res=0.25, gain=1.0)

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

    # --- visuals : retro grid -> Feedback -> Blur -> Output ---
    cs = v.swap_generator("CustomShader")
    v.set_node_asset(cs, SHADER)
    nodes = v.graph()["nodes"]
    fb, blur = find(nodes, "Feedback"), find(nodes, "Blur")
    v.set_node_param(fb, "decay", 0.7)           # neon glow trails
    v.set_node_param(blur, "radius", 0.08)

    # --- the bridge ---
    v.map("master.low",       cs, "warp", amount=0.7)     # bass rolls the horizon
    v.map("master.transient", cs, "glow", amount=1.0)     # snare/kick flash
    v.map("master.high",      cs, "density", amount=0.9)  # hats sharpen scanlines
    v.map("master.mid",       cs, "hue", amount=0.5)      # neon hue drift
    v.map("master.transient", fb, "decay", amount=0.3, lo=0.55, hi=0.85)

    v.launch_scene(0)
    v.play()
    if save:
        save_demo(v, PROJECT, SHADER, cs)


if __name__ == "__main__":
    build(Vivid())
