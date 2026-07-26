"""Constellation — MIDI notes become glowing geometry: chords bloom, arps trail (100 BPM, A minor).

The polyphonic note→visual payoff. A composable **Notes** node (a track's live notes as a value) DRIVES
a generic **Instancer** node through a graph edge; the Instancer draws ONE glowing instance per live
note — pitch → x-position + colour (blue low → red high), velocity → size + brightness — so a chord
blooms into a coloured cluster and an arpeggio sweeps a trail (each note fades out on release). Unlike
the audio-analysis bridge, this reads the actual note stream: the picture knows WHICH notes.

The Notes node pulls the track's held notes from the engine's active-notes bus by track STABLE id (its
`track` param), so it follows the track across reorder/delete — just point it at the track. Requires
Surge XT for the voice.

Run with the app running:  uv run examples/demos/constellation.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "constellation")
BPM = 100


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # A lush pad (chords sustain → bloom) + an arp overlay (fast single notes → trail), both on ONE
    # Surge track so the instancer shows a cluster that breathes with an arpeggio sweeping through it.
    lead = v.add_graph_track("lead")            # track index 0; its STABLE id feeds the Notes node below
    surge_preset(v, lead, "pad", prefer="", gain=0.55)

    # A minor progression, held nearly full-length so each chord blooms as a colour-by-pitch cluster,
    # plus a 1/8 arpeggio woven on top (the moving, trailing voices). One clip, 8 beats.
    from vivid_demo import Vivid as _V  # noqa
    import theory  # the demo helpers' theory module is already on sys.path via vivid_demo
    notes = []
    prog = [("i", 0.0), ("VI", 2.0), ("III", 4.0), ("VII", 6.0)]
    for sym, start in prog:
        for p in theory.roman(sym, "A", "minor", 4):
            notes.append({"p": p, "s": start, "d": 1.9, "v": 0.85})     # sustained chord → bloom
    arp_pitches = [57, 60, 64, 67, 69, 67, 64, 60]                       # A-minor arp, 1/8s
    for i in range(16):
        notes.append({"p": arp_pitches[i % 8] + 12, "s": i * 0.5, "d": 0.4, "v": 0.7})  # trailing sweep
    v.set_clip(lead, 0, notes, 8.0)

    # Visual (composable): a Notes source node (track 0's live notes) DRIVES a generic Instancer node
    # through a graph edge → Feedback (soft trails) → Output. The Notes node is first-class + visible;
    # the Instancer just consumes whatever note-set it's fed.
    out = find(v.graph()["nodes"], "Output")
    notes = v.add_node("Notes"); v.set_node_param(notes, "track", float(v.track_id(lead)))   # the lead's STABLE id
    inst = v.add_node("Instancer")
    for k, val in dict(size=0.6, spread=0.85, trail=0.4).items():
        v.set_node_param(inst, k, val)
    v.connect(inst, notes)          # Notes → Instancer (the driving edge)
    fb = v.add_node("Feedback"); v.set_node_param(fb, "decay", 0.3)   # light trails; keep dots distinct
    v.connect(fb, inst)
    v.connect(out, fb)

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. chords bloom into pitch-coloured clusters; the arp sweeps a trail across them.")


if __name__ == "__main__":
    build(Vivid())
