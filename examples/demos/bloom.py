"""Bloom — music that writes itself, as a full generative SONG (124 BPM, A minor).

The showcase: the melodic parts are NOT authored clips — they come from note-as-signal generators
(ADR-0015): a wandering RandMelody lead, a wandering RandMelody bass, and a Euclid kick are graph
NODES emitting notes, with an LFO breathing the lead's filter. An authored pad gives the harmony to
wander over. The generators are re-placed per SECTION with different density/pulses, so the piece
evolves through intro / verse / chorus / bridge / outro instead of looping:

  section   kick(pulses) bass lead pad   what it is
  INTRO       2           .    .   pad   pad blooms alone
  VERSE       4          rand  .   pad   bass wanders in
  CHORUS      6          rand rand pad   full — lead + bass wander together
  BRIDGE      8           .   rand pad   busy kick + lead, bass drops out
  OUTRO       1           .    .   pad   resolve

A note-bloom flashes on every generated LEAD note (the signature flourish), each instrument marks
the geometry differently, and the picture follows notes it can't predict. Hard geometry: Lines + a
wireframe Mesh + the note-bloom + a title. No plasma.

Requires Surge XT (free CLAP) + BPB Cassette Drums (free VST3).
Run with the app running:  uv run examples/demos/bloom.py   (append 'perform' to play the whole song)
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset, SURGE_P

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "bloom")
CASSETTE = "Cassette Drums"
BPM = 124
# native generator choice indices: RandMelody scale 2=PentMin; rate 3=1/8, 4=1/16; LFO sync 1=Sync
PENTMIN, SYNC = 2, 1


def build(v: Vivid, save: bool = True, perform: bool = False):
    v.reset()
    v.bpm(BPM)
    S_INTRO, S_VERSE, S_CHORUS, S_BRIDGE, S_OUTRO = v.scenes(
        ["intro", "verse", "chorus", "bridge", "outro"])
    v.launch_quantize(4)   # switch scenes on 4-bar phrase boundaries, not mid-clip

    # ---------------- roster ----------------
    drums = v.add_track(kind="instrument", instrument=CASSETTE)
    bass  = v.add_graph_track("bass");   bnode = surge_preset(v, bass, "bass",  prefer="Square", gain=0.7)
    lead  = v.add_graph_track("lead");   lnode = surge_preset(v, lead, "pluck", prefer="Sync",   gain=0.6)
    pad   = v.add_graph_track("pad");            surge_preset(v, pad,  "pad",   prefer="",        gain=0.5)

    # ---------------- generative parts, re-placed per section (this is the "evolving") ----------------
    def euclid_kick(scene, pulses):
        v.place_generator(drums, scene, "Euclid")
        for k, val in dict(note=36, pulses=pulses, steps=16, rate=4, gate=0.4).items():
            v.set_gen_param(drums, scene, k, val)

    def rand(track, scene, root, octaves, density, rate=3):
        v.place_generator(track, scene, "RandMelody")
        for k, val in dict(root=root, scale=PENTMIN, octaves=octaves, rate=rate, density=density, gate=0.5).items():
            v.set_gen_param(track, scene, k, val)

    euclid_kick(S_INTRO, 2)
    euclid_kick(S_VERSE, 4);  rand(bass, S_VERSE, 45, 1, 0.5)                       # bass wanders in (A2, moving)
    euclid_kick(S_CHORUS, 6); rand(bass, S_CHORUS, 45, 1, 0.6); rand(lead, S_CHORUS, 69, 2, 0.6)
    euclid_kick(S_BRIDGE, 8); rand(lead, S_BRIDGE, 69, 2, 0.7, rate=4)             # busy lead, no bass
    euclid_kick(S_OUTRO, 1)

    # ---------------- pad / chords (authored harmony under the wandering; every section) ----------------
    v.progression(pad, S_INTRO,  ["i", "VI"],               beats_per_chord=4.0, key="A", scale="minor", octave=4, vel=0.5,  dur_frac=0.98)
    v.progression(pad, S_VERSE,  ["i", "VI", "III", "VII"], beats_per_chord=4.0, key="A", scale="minor", octave=4, vel=0.55, dur_frac=0.98)
    v.progression(pad, S_CHORUS, ["i", "VI", "III", "VII"], beats_per_chord=4.0, key="A", scale="minor", octave=4, vel=0.6,  dur_frac=0.98)
    v.progression(pad, S_BRIDGE, ["iv", "VII", "III", "VI"], beats_per_chord=4.0, key="A", scale="minor", octave=4, vel=0.55, dur_frac=0.98)
    v.progression(pad, S_OUTRO,  ["i"],                     beats_per_chord=8.0, key="A", scale="minor", octave=4, vel=0.5,  dur_frac=0.99)

    # ================= visuals : geometry that follows the self-generated notes =================
    out = find(v.graph()["nodes"], "Output")
    lines = v.add_node("Lines")                            # <- PAD rotates these
    for k, val in dict(mode=0.0, count=0.5, sides=0.0, size=0.4, rotation=0.0,
                       r=0.2, g=0.95, b=0.7, bg_r=0.02, bg_g=0.03, bg_b=0.05).items():
        v.set_node_param(lines, k, val)
    mesh = v.add_node("Mesh")                              # <- KICK pulses this
    for k, val in dict(shape=0.33, wireframe=1.0, size=0.36, spin=0.3, tilt=0.5,
                       r=1.0, g=0.8, b=0.3, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(mesh, k, val)
    burst = v.add_node("Shape")                            # <- the LEAD blooms this (the signature)
    for k, val in dict(sides=0.0, x=0.5, y=0.5, size=0.06, rotation=0.0, softness=0.5,
                       r=1.0, g=0.9, b=0.55, a=0.0).items():
        v.set_node_param(burst, k, val)
    title = v.add_node("VectorText")
    for k, val in dict(size=0.12, x=0.5, y=0.85, r=0.95, g=0.98, b=0.9,
                       bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(title, k, val)
    v.set_node_file(title, "file", os.path.join(HERE, "media", "bloom.txt"))
    compA = v.add_node("Composite"); v.set_node_param(compA, "mode", 1.0)
    v.connect(compA, lines, port=0); v.connect(compA, mesh, port=1)
    compBurst = v.add_node("Composite"); v.set_node_param(compBurst, "mode", 1.0)
    v.connect(compBurst, compA, port=0); v.connect(compBurst, burst, port=1)
    fb = v.add_node("Feedback"); v.set_node_param(fb, "decay", 0.55)   # <- BASS holds each bloom as a ring
    v.connect(fb, compBurst)
    compB = v.add_node("Composite"); v.set_node_param(compB, "mode", 1.0)
    v.connect(compB, fb, port=0); v.connect(compB, title, port=1)
    v.connect(out, compB)

    # ---- the bridge : each instrument marks the picture differently (per-track sources) ----
    v.track_viz(lead,  "transient", burst, "a",        amount=1.0, lo=0.0,  hi=0.95)   # lead note -> bloom flash
    v.track_viz(lead,  "transient", burst, "size",     amount=1.0, lo=0.05, hi=0.55)   # lead note -> bloom outward
    v.track_viz(drums, "low",       mesh,  "size",     amount=0.7, lo=0.28, hi=0.5)    # kick -> mesh pulse
    v.track_viz(bass,  "low",       fb,    "decay",    amount=0.4, lo=0.4,  hi=0.72)   # bass -> ring persistence
    v.track_viz(pad,   "level",     lines, "rotation", amount=0.6, lo=0.0,  hi=1.0)    # pad -> line rotation

    if save:
        save_geo(v, PROJECT)

    order = [S_INTRO, S_VERSE, S_CHORUS, S_VERSE, S_BRIDGE, S_OUTRO]
    if perform:
        v.perform(order, [4, 8, 8, 8, 8, 4], bpm=BPM)
    else:
        v.play()
        v.launch_scene(S_CHORUS)
        print("built. sections = scenes; v.perform(%s, [4,8,8,8,8,4], bpm=%d) plays the whole song." % (order, BPM))


if __name__ == "__main__":
    import sys
    build(Vivid(), perform="perform" in sys.argv)
