"""Pulse — a driving techno / acid demo (128 BPM, A minor).

Music : 4-on-floor kick + clap + Euclidean hats, a rolling 16th acid bass (with a
        Drive stage for grit), and off-beat minor stabs.
Visual: PER-NOTE reactivity, not band energy. Every instrument drives its own layer off
        the ACTUAL notes: the lead's chord notes become spinning wireframe octahedra
        (Solids), the bass riff becomes crisp hexagons sweeping by pitch (Instancer), and
        each drum hit fires a particle burst (Emitter) — beat-synced, composited over
        black, no haze. You see WHICH notes play, not just how loud the mix is.

Run with the app running:  uv run examples/demos/pulse.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "pulse")

CASSETTE = "Cassette Drums"      # free VST3 drum machine (BPB)
A1, A2, A3 = 33, 45, 57          # A across octaves


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(128)

    # --- roster : self-contained. Author our OWN tracks (don't assume the default session's
    # tracks) so the project regenerates from any session — a free drum machine + real Surge XT
    # factory patches (generic preset flow, pick by name). ---
    DRUMS = v.add_track(kind="instrument", instrument=CASSETTE)     # free VST3 drums
    BASS = v.add_graph_track("bass"); surge_preset(v, BASS, "acid", prefer="Bassline 1", gain=1.0)
    LEAD = v.add_graph_track("lead"); surge_preset(v, LEAD, "pluck", prefer="Sync", gain=0.7)

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

    # --- visuals : the PER-NOTE reactivity palette — real geometry driven by the ACTUAL notes,
    # beat-synced, NO Feedback/Blur haze. Each instrument owns a layer: a Notes node adapts its MIDI
    # into a generic signal that a draw op renders (the ops never refer to "notes"). You SEE which
    # notes play, not just how loud the mix is. ---
    out = find(v.graph()["nodes"], "Output")
    d_sig = v.notes(v.track_id(DRUMS))
    b_sig = v.notes(v.track_id(BASS))
    l_sig = v.notes(v.track_id(LEAD))

    # LEAD stabs → big spinning wireframe octahedra, one per chord note (the hard-geometric hero)
    solids = v.solids(l_sig, shape=2, size=0.95, spread=0.62, spin=0.55, trail=0.5, wireframe=1.0)
    # BASS riff → crisp hexagons sweeping by pitch (data-viz), re-popping on each hit
    bass = v.instancer(b_sig, shape=2, sides=6, size=0.34, spread=0.9, trail=0.14, pulse=1.0)
    # DRUMS → a particle burst on every kick / clap / hat
    sparks = v.emitter(d_sig, count=0.45, speed=0.7, gravity=0.4, life=0.45, size=0.4, spread=0.95)

    # beat-synced throb: the bass hexagons pump gently on each beat (transport source, not band energy)
    v.beat_sync("beat_pulse", bass, "size", amount=0.22, lo=0.22, hi=0.4)

    # composite the three geometry layers, ADD over black (each op clears black/transparent → ADD keys them together)
    def add_layer(a, b):
        c = v.add_node("Composite"); v.set_node_param(c, "mode", 1.0)
        v.connect(c, a, port=0); v.connect(c, b, port=1); return c
    stack = add_layer(bass, solids)
    stack = add_layer(stack, sparks)
    v.connect(out, stack)                            # Output <- the composited stack

    v.master_gain(0.6)   # headroom: the summed mix was clipping at 0 dBFS (AV clip needs clean audio)
    v.launch_scene(0)
    v.play()

    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
