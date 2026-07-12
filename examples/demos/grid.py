"""Grid — a glitchy / IDM demo (90 BPM, A minor).

Music : a broken Euclidean beat (kick E(5,16), off-beat snare, busy hats), a bitcrushed
        Surge lead, and a dark detuned Surge bass. The native Bitcrush op mangles the lead.
Visual: a bespoke CustomShader digital-glitch field (shaders/grid_glitch.glsl, custom op)
        → Feedback → Blur → Output. Transients burst the RGB tear, the highs shrink the
        cells, the lows shove the blocks.

Run with the app running:  uv run examples/demos/grid.py
"""
import os
from vivid_demo import Vivid, find, save_demo, surge_preset
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER = os.path.join(HERE, "shaders", "grid_glitch.glsl")
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

    # --- visuals : glitch field -> Feedback -> Blur -> Output ---
    g = v.graph()["nodes"]
    fb, blur = find(g, "Feedback"), find(g, "Blur")
    cs = v.add_node("CustomShader")
    xform = v.add_node("Transform")              # NEW primitive: tile/rotate the glitch field
    v.connect(xform, cs)                         # Transform reads the glitch field
    v.connect(fb, xform)                         # Feedback reads the Transform
    v.set_node_asset(cs, SHADER)
    v.set_node_param(xform, "tile", 0.14)        # ~2x2 tiling
    v.set_node_param(fb, "decay", 0.6)           # short, twitchy trails
    v.set_node_param(blur, "radius", 0.04)

    # --- the bridge ---
    v.map("master.transient", cs, "glow", amount=1.0)     # transient bursts the RGB tear
    v.map("master.high",      cs, "density", amount=0.9)  # hats shrink the cells
    v.map("master.low",       cs, "warp", amount=0.7)     # bass shoves the blocks
    v.map("master.mid",       cs, "hue", amount=0.5)
    v.map("master.transient", xform, "tile", amount=0.2)  # beats jolt the tiling
    v.map("master.low",       xform, "rot", amount=0.08)  # bass skews the grid

    v.launch_scene(0)
    v.play()
    if save:
        save_demo(v, PROJECT, SHADER, cs)


if __name__ == "__main__":
    build(Vivid())
