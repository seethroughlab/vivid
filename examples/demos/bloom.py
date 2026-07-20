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

    # --- two voices that WRITE THEMSELVES : a scene-cell note generator per track, no clip ---
    # A scene-cell generator (place_generator) actually EMITS notes when its scene launches -> the
    # track's Surge sounds them. A wandering RandMelody lead + a Euclid bass pulse = a whole
    # arrangement authored by the generators, not by hand. An LFO breathes the lead's filter.
    lead = v.add_graph_track("lead")
    inst = surge_preset(v, lead, "pluck", prefer="Sync", gain=0.6)
    v.place_generator(lead, 0, "RandMelody")
    for k, val in dict(root=57, scale=PENTMIN, octaves=2, rate=3, density=0.5, gate=0.5).items():
        v.set_gen_param(lead, 0, k, val)              # A minor pentatonic, 1/8 notes, wandering
    lfo = v.add_mod(lead, "LFO")                      # a modulator (control signal, no audio)
    for k, val in dict(sync=SYNC, division=2).items():
        v.set_anode_named(lead, lfo, k, val)
    v.connect_mod(lead, lfo, inst, param=SURGE_P["cutoff"], amount=0.5, bipolar=True)

    bass = v.add_graph_track("bass")
    bnode = surge_preset(v, bass, "bass", prefer="", gain=0.7)
    v.place_generator(bass, 0, "Euclid")
    for k, val in dict(note=33, pulses=5, steps=8, rate=3, gate=0.5).items():
        v.set_gen_param(bass, 0, k, val)              # A1 root, E(5,8) eighth-note pulse

    # --- a third self-writing voice : Cassette Drums, a Euclid kick that generates its own pulse ---
    drums = v.add_track(kind="instrument", instrument="Cassette Drums")
    v.place_generator(drums, 0, "Euclid")
    for k, val in dict(note=36, pulses=5, steps=16, rate=4, gate=0.4).items():
        v.set_gen_param(drums, 0, k, val)             # kick (36), E(5,16) sixteenth pulse

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
    # A BLOOM that fires on every generated note: a bright polygon that snaps big + opaque on
    # each onset (transient), then the Feedback decay leaves it as an expanding ring — so you SEE
    # each self-generated note bloom outward. The rapid Euclid/Arp stream makes it pulse visibly.
    burst = v.add_node("Shape")
    for k, val in dict(sides=0.0, x=0.5, y=0.5, size=0.06, rotation=0.0, softness=0.5,
                       r=1.0, g=0.9, b=0.55, a=0.0).items():
        v.set_node_param(burst, k, val)               # amber bloom, invisible until a note hits
    title = v.add_node("VectorText")
    for k, val in dict(size=0.12, x=0.5, y=0.85, r=0.95, g=0.98, b=0.9,
                       bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(title, k, val)
    v.set_node_file(title, "file", os.path.join(HERE, "media", "bloom.txt"))
    compA = v.add_node("Composite")
    v.connect(compA, lines, port=0)
    v.connect(compA, mesh, port=1)
    v.set_node_param(compA, "mode", 1.0)              # ADD
    compBurst = v.add_node("Composite")
    v.connect(compBurst, compA, port=0)
    v.connect(compBurst, burst, port=1)
    v.set_node_param(compBurst, "mode", 1.0)          # ADD the note-bloom in
    fb = v.add_node("Feedback")
    v.set_node_param(fb, "decay", 0.55)               # holds each bloom as an expanding ring
    compB = v.add_node("Composite")
    v.connect(fb, compBurst)
    v.connect(compB, fb, port=0)
    v.connect(compB, title, port=1)
    v.set_node_param(compB, "mode", 1.0)
    v.connect(out, compB)

    # --- the bridge : the self-generated music drives the geometry (each note blooms) ---
    v.map("master.transient", burst, "a",        amount=1.0, lo=0.0,  hi=0.95)  # note onset -> flash in
    v.map("master.transient", burst, "size",     amount=1.0, lo=0.05, hi=0.55)  # -> bloom outward
    v.map("master.transient", mesh,  "size",     amount=0.7, lo=0.28, hi=0.5)
    v.map("master.mid",       lines, "rotation", amount=0.5)
    v.map("master.low",       fb,    "decay",    amount=0.35, lo=0.4, hi=0.72)

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
