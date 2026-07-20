"""Bloom — music that writes itself, from note-as-signal generators (124 BPM, A minor).

The showcase: NO clip is authored. The notes come from the audio graph itself — a Euclid pulse
and a random-melody generator are graph NODES emitting notes, run through an Arp note-effect, with
an LFO breathing the filter. Notes are a signal here (ADR-0015): generators -> note effect ->
instrument, wired with note edges, all evolving on their own. The geometry follows the notes it
can't predict.

Voice: Surge XT (free CLAP) — the note graph itself is native and self-contained; only the timbre
is a plugin, like the surge-lead example. Hard geometry: Lines + a wireframe Mesh + a BLOOM
call-sign. No plasma.

Run with the app running (Surge XT installed):  uv run examples/demos/bloom.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset, SURGE_P

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "bloom")

# Note-generator param values (native ops read raw indices/ints):
#  Euclid    rate choices: 0=1/1 1=1/2 2=1/4 3=1/8 4=1/16 5=1/8T
#  RandMelody scale choices: 0=Major 1=Minor 2=PentMin 3=Dorian ; rate as above
#  Arp       rate: 0=1/4 1=1/8 2=1/8T 3=1/16 4=1/32 ; mode: 0=Up 1=Down 2=UpDown 3=Random
#  LFO       sync: 0=Free 1=Sync ; division: 0=1/1 1=1/2 2=1/4 3=1/8 4=1/16
PENTMIN, UPDOWN, SYNC = 2, 2, 1


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(124)

    # --- voice : Surge on a bare graph track (the note graph below drives it) ---
    lead = v.add_graph_track("bloom")                 # empty note-driven track (no instrument yet)
    inst = surge_preset(v, lead, "pluck", prefer="Sync", gain=0.7)  # a Surge pluck as the voice

    # --- the generative NOTE GRAPH (ADR-0015: notes are a signal in the graph) ---
    euclid = v.add_generator(lead, "Euclid")          # a rhythmic pulse source
    for k, val in dict(steps=16, pulses=6, note=45, rate=4, gate=0.4).items():
        v.set_anode_named(lead, euclid, k, val)       # A2 (45) sixteenth pulses, E(6,16)
    rand = v.add_generator(lead, "RandMelody")        # an evolving melody source
    for k, val in dict(root=57, scale=PENTMIN, octaves=2, rate=4, density=0.55, gate=0.5).items():
        v.set_anode_named(lead, rand, k, val)         # A minor pentatonic, wandering
    arp = v.add_note_fx(lead, "Arp")                  # arpeggiate the melody (note in -> note out)
    for k, val in dict(rate=3, mode=UPDOWN, octaves=2, gate=0.5).items():
        v.set_anode_named(lead, arp, k, val)
    lfo = v.add_mod(lead, "LFO")                      # a modulator (control signal, no audio)
    for k, val in dict(sync=SYNC, division=2).items():
        v.set_anode_named(lead, lfo, k, val)

    # wire the note graph: Euclid -> instrument, RandMelody -> Arp -> instrument (note edges);
    # LFO -> the instrument's filter cutoff (a control edge).
    v.connect_audio_nodes(lead, euclid, inst, kind="note")
    v.connect_audio_nodes(lead, rand, arp, kind="note")
    v.connect_audio_nodes(lead, arp, inst, kind="note")
    v.connect_mod(lead, lfo, inst, param=SURGE_P["cutoff"], amount=0.5, bipolar=True)

    # --- visuals : real geometry that follows the self-generated notes. No plasma. ---
    out = find(v.graph()["nodes"], "Output")
    lines = v.add_node("Lines")
    for k, val in dict(mode=0.0, count=0.5, sides=0.0, size=0.4, rotation=0.0,
                       r=0.2, g=0.95, b=0.7, bg_r=0.02, bg_g=0.03, bg_b=0.05).items():
        v.set_node_param(lines, k, val)               # a teal line field
    mesh = v.add_node("Mesh")
    for k, val in dict(shape=0.33, wireframe=1.0, size=0.36, spin=0.3, tilt=0.5,
                       r=1.0, g=0.8, b=0.3, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(mesh, k, val)                # amber wireframe solid
    title = v.add_node("VectorText")
    for k, val in dict(size=0.12, x=0.5, y=0.85, r=0.95, g=0.98, b=0.9,
                       bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(title, k, val)
    v.set_node_file(title, "file", os.path.join(HERE, "media", "bloom.txt"))
    compA = v.add_node("Composite")
    v.connect(compA, lines, port=0)
    v.connect(compA, mesh, port=1)
    v.set_node_param(compA, "mode", 1.0)              # ADD
    fb = v.add_node("Feedback")
    v.set_node_param(fb, "decay", 0.4)
    compB = v.add_node("Composite")
    v.connect(fb, compA)
    v.connect(compB, fb, port=0)
    v.connect(compB, title, port=1)
    v.set_node_param(compB, "mode", 1.0)
    v.connect(out, compB)

    # --- the bridge : the self-generated music drives the geometry ---
    v.map("master.transient", mesh,  "size",     amount=0.7, lo=0.28, hi=0.5)
    v.map("master.mid",       lines, "rotation", amount=0.5)
    v.map("master.low",       fb,    "decay",    amount=0.4, lo=0.35, hi=0.7)

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
