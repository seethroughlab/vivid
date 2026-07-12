"""Drift — an ambient / cinematic demo (70 BPM, C major, no drums).

Music : a lush I-vi-IV-V pad of maj7/min7 chords in slow open voicings, a whole-note
        root bass an octave down, and a sparse high arpeggio drifting over the top.
Visual: a bespoke CustomShader flowing nebula (shaders/drift_flow.glsl, custom op)
        → Feedback (long trails) → Tint (built-in) → Output. Loudness lifts the bloom,
        the mids drift the colour, the lows gently bend the flow.

Run with the app running:  uv run examples/demos/drift.py
"""
import os
from vivid_demo import Vivid, find, save_demo, surge_voice, SURGE_FX, SURGE_FX_TYPE
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER = os.path.join(HERE, "shaders", "drift_flow.glsl")
PROJECT = os.path.join(HERE, "projects", "drift")

ARP, PAD, EXTRA = 0, 1, 2         # Pigments (arp), Serum 2 (pad), EZdrummer (unused here)


def build(v: Vivid, save: bool = True):
    v.new_project()
    v.bpm(70)

    prog = ["Cmaj7", "Am7", "Fmaj7", "G"]

    # --- pad : Surge XT as a soft, wide pad (slow attack + unison), HELD maj7 chords (Surge
    # sustains — no pluck workaround needed) + Surge XT Effects reverb for real ambient space. ---
    surge_voice(v, PAD, cutoff=0.55, res=0.08, uni_voices=0.5, uni_detune=0.35, atk=0.16, rel=0.45, gain=0.9)
    bed = []
    for i, c in enumerate(prog):
        for p in theory.chord(c, octave=3, voicing="open"):
            bed.append({"p": p, "s": i * 8.0, "d": 8.0, "v": 0.55})   # whole-chord swells
    v.set_clip(PAD, 0, bed, 32.0)
    v.clap_effect(PAD, SURGE_FX)                  # attach Surge XT Effects (reverb) after the pad
    rvb = v.audio_node_id(PAD, "effect")
    v.node_param(PAD, rvb, SURGE_FX_TYPE, 0.05)   # FX Type ~ reverb/delay family (a lush tail; tweak to taste)

    # --- sparkle : Surge, a slow high sparse arpeggio drifting over the top ---
    surge_voice(v, ARP, cutoff=0.8, res=0.1, atk=0.05, rel=0.5, gain=0.6)
    top = []
    for i, c in enumerate(prog):
        ps = theory.chord(c, octave=5)
        seq = theory.arpeggiate(ps, "updown", rate=2.0, octaves=1, length=8.0, vel=0.5)
        for n in seq:
            top.append({"p": n["p"], "s": i * 8.0 + n["s"], "d": n["d"], "v": n["v"]})
    v.set_clip(ARP, 0, top, 32.0)
    v.clear_clip(EXTRA, 0)   # no drums for the ambient bed

    # --- visuals : flowing nebula -> Feedback (long) -> Tint -> Output ---
    cs = v.swap_generator("CustomShader")
    v.set_node_asset(cs, SHADER)
    nodes = v.graph()["nodes"]
    fb, blur = find(nodes, "Feedback"), find(nodes, "Blur")
    out = find(nodes, "Output")
    tint = v.add_node("Tint")                     # built-in WGSL op, inserted before Output
    v.connect(tint, blur)
    v.connect(out, tint)
    v.set_node_param(fb, "decay", 0.94)          # very long, dreamy trails
    v.set_node_param(blur, "radius", 0.3)
    v.set_node_param(tint, "hue", 0.55)

    # --- the bridge : slow, gentle reactivity ---
    v.map("master.level", cs, "glow", amount=0.9)
    v.map("master.level", cs, "density", amount=0.7)
    v.map("master.mid",   cs, "hue", amount=0.5)
    v.map("master.low",   cs, "warp", amount=0.5)
    v.map("master.mid",   tint, "hue", amount=0.4)

    v.launch_scene(0)
    v.play()
    if save:
        save_demo(v, PROJECT, SHADER, cs)


if __name__ == "__main__":
    build(Vivid())
