"""Fracture — a full glitch/IDM SONG, authored over MCP (90 BPM, A minor).

Not a loop: a five-section arrangement laid out as launchable SCENES — intro / verse / chorus /
bridge / outro — that you step through (the song FORM is the launch order intro→verse→chorus→verse
→bridge→outro; `v.perform(...)` auditions the whole thing). Instruments come and go by section:

  section   drums bass chords lead stabs arp  sfx      what it is
  INTRO      hats   .    pad    .     .    .   riser    sparse, building
  VERSE      full  acid  pad    .     .    .    .       the groove
  CHORUS     busy  acid  pad   lead  stabs .    .       full energy + glitch
  BRIDGE     kick   .    pad   lead   .   arp   .       breakdown + arp flourish
  OUTRO      tail   .    pad    .     .    .   downlift  resolve

Flourishes: the native GLITCH PACK chews the drums (BeatRepeat / Stutter / Reverse, clock-locked),
an ARP sparkles the bridge, and SFX riser/downlifter mark the section changes.

Each INSTRUMENT drives its OWN identifiable visual effect through the bridge (per-track sources), so
you can SEE the arrangement: the kick punches the whole frame, the bass swells the feedback trails,
the pad opens the chromatic split, the lead spins the mesh, the stabs tear the picture, the arp spins
the rings. Because a part only plays in its sections, its visual only fires then — the picture writes
itself from the music.

Requires Surge XT (free CLAP) + BPB Cassette Drums (free VST3), both in your plugin folders.
Run with the app running:  uv run examples/demos/fracture.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "fracture")
CASSETTE = "Cassette Drums"
METRO = 2                                              # glitch-op clock index = Metronome (beat-sync)
PENTMIN = 2                                            # RandMelody scale index (pentatonic minor)
BPM = 90
# A-minor pitches used across the parts.
A2, C3, D3, E3, G3 = 45, 48, 50, 52, 55
A3, C4, E4 = 57, 60, 64                                # stab triad (Am)


def build(v: Vivid, save: bool = True, perform: bool = False):
    v.reset()
    v.bpm(BPM)
    S_INTRO, S_VERSE, S_CHORUS, S_BRIDGE, S_OUTRO = v.scenes(
        ["intro", "verse", "chorus", "bridge", "outro"])
    v.launch_quantize(4)   # switch scenes on 4-bar phrase boundaries, not mid-clip

    # ---------------- roster (voices assigned once; parts authored per scene) ----------------
    drums  = v.add_track(kind="instrument", instrument=CASSETTE)
    bass   = v.add_graph_track("bass");   surge_preset(v, bass,   "acid",  prefer="Bassline", gain=0.9)
    chords = v.add_graph_track("chords"); surge_preset(v, chords, "pad",   prefer="",         gain=0.5)
    lead   = v.add_graph_track("lead");   surge_preset(v, lead,   "lead",  prefer="Sync",     gain=0.6)
    stabs  = v.add_graph_track("stabs");  surge_preset(v, stabs,  "pluck", prefer="Sync",     gain=0.5)
    arp    = v.add_graph_track("arp");    surge_preset(v, arp,    "pluck", prefer="",         gain=0.45)
    sfx    = v.add_graph_track("sfx");    surge_preset(v, sfx,    "sweep", prefer="",         gain=0.4)

    # the glitch pack — native ops on the drum track, clock-locked; active whenever the drums play
    v.add_glitch(drums, "BeatRepeat", clock=METRO, chance=0.5,  division=4, count=4, decay=0.1)
    v.add_glitch(drums, "Stutter",    clock=METRO, chance=0.35, division=4, count=6)
    v.add_glitch(drums, "Reverse",    clock=METRO, chance=0.25, division=3)

    # ---------------- drums per section ----------------
    v.drums(drums, S_INTRO,  {"hat": "..x...x...x...x."}, bars=2, vel=0.7)
    v.drums(drums, S_VERSE,  {"kick": "x..x..x...x..x..", "snare": "....x.......x...",
                              "hat":  "x.x.x.x.x.x.x.x.", "clap":  "..........x....."}, bars=2, vel=0.95)
    v.drums(drums, S_CHORUS, {"kick": "x..x..x.x.x..x..", "snare": "....x.......x...",
                              "hat":  "xxxxxxxxxxxxxxxx", "clap":  "....x...x...x..x"}, bars=2, vel=0.98)
    v.drums(drums, S_BRIDGE, {"kick": "x.......x.......", "clap":  "....x.......x..."}, bars=2, vel=0.9)
    v.drums(drums, S_OUTRO,  {"kick": "x...............", "clap":  "............x..."}, bars=2, vel=0.8)

    # ---------------- bass (acid; verse + chorus only) ----------------
    bpat = [A2, A2, E3, A2, G3, A2, D3, C3]
    v.bassline(bass, S_VERSE,  [(bpat[i % 8], i * 0.5,  0.4) for i in range(16)], length=8.0, vel=0.95)
    v.bassline(bass, S_CHORUS, [(bpat[i % 8], i * 0.25, 0.2) for i in range(32)], length=8.0, vel=0.95)

    # ---------------- chords (pad; every section) ----------------
    v.progression(chords, S_INTRO,  ["i", "VI"],               beats_per_chord=4.0, key="A", scale="minor", octave=3, vel=0.5,  dur_frac=0.95)
    v.progression(chords, S_VERSE,  ["i", "VI", "III", "VII"], beats_per_chord=2.0, key="A", scale="minor", octave=3, vel=0.55, dur_frac=0.95)
    v.progression(chords, S_CHORUS, ["i", "VI", "III", "VII"], beats_per_chord=2.0, key="A", scale="minor", octave=3, vel=0.6,  dur_frac=0.95)
    v.progression(chords, S_BRIDGE, ["iv", "VII", "III", "VI"], beats_per_chord=2.0, key="A", scale="minor", octave=3, vel=0.55, dur_frac=0.95)
    v.progression(chords, S_OUTRO,  ["i"],                     beats_per_chord=8.0, key="A", scale="minor", octave=3, vel=0.5,  dur_frac=0.98)

    # ---------------- lead (chorus hook + bridge variation) ----------------
    v.bassline(lead, S_CHORUS, [(76, 0, 0.5), (74, 0.5, 0.5), (72, 1, 1.0), (69, 2.5, 0.5), (72, 3, 1.0),
                                (76, 4, 0.5), (79, 4.5, 0.5), (81, 5, 1.0), (79, 6.5, 0.5), (76, 7, 1.0)],
               length=8.0, vel=0.72)
    v.bassline(lead, S_BRIDGE, [(72, 0, 1.0), (74, 1, 0.5), (76, 1.5, 0.5), (77, 2, 1.0), (76, 3, 0.5),
                                (74, 3.5, 0.5), (72, 4, 1.0), (69, 5, 1.0), (71, 6, 0.5), (72, 6.5, 1.5)],
               length=8.0, vel=0.7)

    # ---------------- stabs (chorus only — off-beat Am triad) ----------------
    v.set_clip(stabs, S_CHORUS, [{"p": p, "s": bar * 4.0 + off, "d": 0.18, "v": 0.7}
                                 for bar in range(2) for off in (1.5, 2.5, 3.5) for p in (A3, C4, E4)],
               8.0)

    # ---------------- arp (a GENERATIVE sparkle — RandMelody note-as-signal, not an authored clip) ----
    # The arp voice writes itself: a wandering 1/16 pentatonic line the generator improvises each pass.
    for sc in (S_CHORUS, S_BRIDGE):
        v.place_generator(arp, sc, "RandMelody")
        for k, val in dict(root=69, scale=PENTMIN, octaves=2, rate=4, density=0.55, gate=0.4).items():
            v.set_gen_param(arp, sc, k, val)

    # ---------------- sfx (riser into the drop, downlifter at the end) ----------------
    v.bassline(sfx, S_INTRO, [(48 + i, i * 0.5, 0.4) for i in range(16)], length=8.0, vel=0.5)   # rising run
    v.bassline(sfx, S_OUTRO, [(57 - 2 * i, i * 0.5, 0.4) for i in range(8)], length=8.0, vel=0.5)  # downlifter

    # ================= visuals : layered geometry, one target per instrument =================
    out = find(v.graph()["nodes"], "Output")
    sgR = v.add_node("ShapeGrid")                         # red hex channel
    for k, val in dict(sides=0.6, cols=0.5, rows=0.5, size=0.52, rotation=0.0,
                       r=1.0, g=0.15, b=0.1, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(sgR, k, val)
    sgC = v.add_node("ShapeGrid")                         # cyan hex channel (offset -> chromatic split)
    for k, val in dict(sides=0.6, cols=0.5, rows=0.5, size=0.52, rotation=0.0,
                       r=0.1, g=0.9, b=1.0, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(sgC, k, val)
    split = v.add_node("Transform"); v.set_node_param(split, "tx", 0.01)   # <- chords open the split
    v.connect(split, sgC)
    comp1 = v.add_node("Composite"); v.set_node_param(comp1, "mode", 1.0)   # ADD
    v.connect(comp1, sgR, port=0); v.connect(comp1, split, port=1)

    mesh = v.add_node("Mesh")                             # <- the LEAD spins this
    for k, val in dict(wireframe=1.0, shape=0.5, size=0.4, r=0.85, g=0.4, b=1.0, spin=0.0).items():
        v.set_node_param(mesh, k, val)
    comp2 = v.add_node("Composite"); v.set_node_param(comp2, "mode", 1.0)
    v.connect(comp2, comp1, port=0); v.connect(comp2, mesh, port=1)

    lines = v.add_node("Lines")                           # <- the ARP spins these rings
    for k, val in dict(mode=0.66, count=0.5, size=0.6, rotation=0.0, r=0.2, g=1.0, b=0.8).items():
        v.set_node_param(lines, k, val)
    comp3 = v.add_node("Composite"); v.set_node_param(comp3, "mode", 1.0)
    v.connect(comp3, comp2, port=0); v.connect(comp3, lines, port=1)

    jolt = v.add_node("Transform"); v.set_node_param(jolt, "tx", 0.5)       # <- the KICK punches the frame
    v.connect(jolt, comp3)
    disp = v.add_node("Displace")                          # <- the STABS tear it
    v.set_node_param(disp, "amount", 0.04); v.set_node_param(disp, "mode", 1.0)
    v.connect(disp, jolt)
    fb = v.add_node("Feedback"); v.set_node_param(fb, "decay", 0.4)         # <- the BASS swells the trails
    v.connect(fb, disp)
    v.connect(out, fb)

    # ---- the bridge : ONE instrument -> ONE visual effect (per-track sources, identifiable) ----
    v.track_viz(drums,  "transient", jolt,  "tx",       amount=1.0, lo=0.5,  hi=0.62)   # kick punch
    v.track_viz(bass,   "low",       fb,    "decay",    amount=0.6, lo=0.3,  hi=0.72)   # bass -> trails
    v.track_viz(chords, "level",     split, "tx",       amount=1.0, lo=0.0,  hi=0.06)   # pad -> chromatic split
    v.track_viz(lead,   "high",      mesh,  "spin",     amount=1.0, lo=0.0,  hi=1.0)    # lead -> mesh spin
    v.track_viz(stabs,  "transient", disp,  "amount",   amount=1.0, lo=0.03, hi=0.5)    # stabs -> tears
    v.track_viz(arp,    "high",      lines, "rotation", amount=1.0, lo=0.0,  hi=1.0)    # arp -> rings spin
    v.track_viz(sfx,    "level",     lines, "size",     amount=1.0, lo=0.5,  hi=0.9)    # riser -> rings swell

    if save:
        save_geo(v, PROJECT)

    # form = the launch order. perform() steps it (bar-quantized); else land on the chorus so a plain
    # run immediately shows the full arrangement.
    order = [S_INTRO, S_VERSE, S_CHORUS, S_VERSE, S_BRIDGE, S_OUTRO]
    if perform:
        v.perform(order, [4, 8, 8, 8, 8, 4], bpm=BPM)
    else:
        v.play()                # transport must run BEFORE launch — launch is bar-quantized
        v.launch_scene(S_CHORUS)
        print("built. sections = scenes intro/verse/chorus/bridge/outro; "
              "v.perform(%s, [4,8,8,8,8,4], bpm=%d) plays the whole song." % (order, BPM))


if __name__ == "__main__":
    import sys
    build(Vivid(), perform="perform" in sys.argv)
