"""Drift — an ambient / cinematic demo (70 BPM, C major, no drums).

Music : a lush I-vi-IV-V pad of maj7/min7 chords in slow open voicings, a whole-note
        root bass an octave down, and a sparse high arpeggio drifting over the top.
Visual: SWISS / TYPOGRAPHIC — minimal REAL geometry. A "DRIFT" title as filled vector
        type (VectorText: real FreeType glyph outlines → triangles) added over a slow
        radial Gradient → Output. Loudness breathes the title, the mids drift the field.

Run with the app running:  uv run examples/demos/drift.py
"""
import os
from vivid_demo import Vivid, find, save_demo, surge_preset, SURGE_FX, SURGE_FX_TYPE
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
TEXT = os.path.join(HERE, "text", "drift.txt")   # the vector-type title string
PROJECT = os.path.join(HERE, "projects", "drift")

ARP, PAD, EXTRA = 0, 1, 2         # Pigments (arp), Serum 2 (pad), EZdrummer (unused here)


def build(v: Vivid, save: bool = True):
    v.new_project()
    v.bpm(70)

    prog = ["Cmaj7", "Am7", "Fmaj7", "G"]

    # --- pad : Surge XT as a soft, wide pad (slow attack + unison), HELD maj7 chords (Surge
    # sustains — no pluck workaround needed) + Surge XT Effects reverb for real ambient space. ---
    surge_preset(v, PAD, "pad", prefer="Verb", gain=0.9)   # lush reverbed pad patch
    bed = []
    for i, c in enumerate(prog):
        for p in theory.chord(c, octave=3, voicing="open"):
            bed.append({"p": p, "s": i * 8.0, "d": 8.0, "v": 0.55})   # whole-chord swells
    v.set_clip(PAD, 0, bed, 32.0)
    v.clap_effect(PAD, SURGE_FX)                  # attach Surge XT Effects (reverb) after the pad
    rvb = v.audio_node_id(PAD, "effect")
    v.node_param(PAD, rvb, SURGE_FX_TYPE, 0.05)   # FX Type ~ reverb/delay family (a lush tail; tweak to taste)

    # --- sparkle : Surge, a slow high sparse arpeggio drifting over the top ---
    surge_preset(v, ARP, "keys", prefer="Bell", gain=0.55)   # bell keys sparkle
    top = []
    for i, c in enumerate(prog):
        ps = theory.chord(c, octave=5)
        seq = theory.arpeggiate(ps, "updown", rate=2.0, octaves=1, length=8.0, vel=0.5)
        for n in seq:
            top.append({"p": n["p"], "s": i * 8.0 + n["s"], "d": n["d"], "v": n["v"]})
    v.set_clip(ARP, 0, top, 32.0)
    v.clear_clip(EXTRA, 0)   # no drums for the ambient bed

    # --- visuals : a "DRIFT" vector-type title over a slow radial Gradient, added -> Output ---
    out = find(v.graph()["nodes"], "Output")
    grad = v.add_node("Gradient")                 # the calm cinematic field
    for k, val in dict(mode=1.0, ar=0.55, ag=0.62, ab=0.78, br=0.04, bg=0.05, bb=0.1, scale=0.4).items():
        v.set_node_param(grad, k, val)            # radial: soft blue-white center -> deep cool edges
    title = v.add_node("VectorText")              # REAL filled vector type (glyph outlines -> triangles)
    for k, val in dict(size=0.16, x=0.5, y=0.5, r=0.97, g=0.98, b=1.0, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(title, k, val)           # white title, black bg (ADD keys it over the gradient)
    v.set_node_asset(title, TEXT)                 # absolute path for live preview (save_demo makes it portable)
    comp = v.add_node("Composite")
    v.connect(comp, grad, port=0)                 # A = the gradient field
    v.connect(comp, title, port=1)                # B = the title
    v.set_node_param(comp, "mode", 1.0)          # ADD (white type glows over the field)
    v.connect(out, comp)                          # Output <- Composite

    # --- the bridge : slow, gentle reactivity (smooth params only) ---
    v.map("master.level", title, "size", amount=0.06, lo=0.16, hi=0.2)   # the title breathes with loudness
    v.map("master.mid",   grad,  "scale", amount=0.2, lo=0.4, hi=0.6)    # the field drifts with the mids

    v.launch_scene(0)
    v.play()
    if save:
        save_demo(v, PROJECT, TEXT, title)


if __name__ == "__main__":
    build(Vivid())
