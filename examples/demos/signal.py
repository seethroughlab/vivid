"""Signal — external video footage driving a full electronic SONG (100 BPM, A minor).

The showcase: pixels from OUTSIDE the synth engine — a real video clip — pulled into the graph and
treated like any other source, reacting to a complete arrangement. Five sections laid out as
launchable SCENES (intro / verse / chorus / bridge / outro); the song FORM is the launch order
intro→verse→chorus→verse→bridge→outro (`v.perform(...)` plays it all). Instruments enter and leave:

  section   drums bass chords lead arp  sfx      what it is
  INTRO      texture .   pad    .    .   riser    footage alone, building
  VERSE      beat  bass  pad    .    .    .       the groove locks in
  CHORUS     full  bass  pad   lead  .    .       full arrangement
  BRIDGE     kick   .    pad   lead arp   .       breakdown + arp flourish
  OUTRO      tail   .    pad    .    .   downlift  resolve

Deliberately technical, not psychedelic: the footage is grid-displaced and feedback-smeared, with
overlay geometry so each instrument marks the picture differently (kick shakes the footage, bass
swells the trails, pad pulses the frame, lead spins a mesh, arp spins rings).

Requires Surge XT (free CLAP) + BPB Cassette Drums (free VST3). Media root = examples/demos/media.
Run with the app running:  uv run examples/demos/signal.py   (append 'perform' to play the whole song)
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "signal")
MEDIA = os.path.join(HERE, "media")
CASSETTE = "Cassette Drums"
BPM = 100
PENTMIN = 2                                            # RandMelody scale index (pentatonic minor)
A2, C3, D3, E3, G3, A3 = 45, 48, 50, 52, 55, 57


def build(v: Vivid, save: bool = True, perform: bool = False):
    v.reset()
    v.bpm(BPM)
    S_INTRO, S_VERSE, S_CHORUS, S_BRIDGE, S_OUTRO = v.scenes(
        ["intro", "verse", "chorus", "bridge", "outro"])
    v.launch_quantize(4)   # switch scenes on 4-bar phrase boundaries, not mid-clip

    # ---------------- roster ----------------
    drums  = v.add_track(kind="instrument", instrument=CASSETTE)
    bass   = v.add_graph_track("bass");   surge_preset(v, bass,   "acid",  prefer="Bassline", gain=0.85)
    chords = v.add_graph_track("chords"); surge_preset(v, chords, "pad",   prefer="",         gain=0.5)
    lead   = v.add_graph_track("lead");   surge_preset(v, lead,   "lead",  prefer="Sync",     gain=0.55)
    arp    = v.add_graph_track("arp");    surge_preset(v, arp,    "pluck", prefer="",         gain=0.45)
    sfx    = v.add_graph_track("sfx");    surge_preset(v, sfx,    "sweep", prefer="",         gain=0.4)

    # ---------------- drums per section (100 BPM broken groove) ----------------
    v.drums(drums, S_INTRO,  {"hat": "..x...x...x...x."}, bars=2, vel=0.6)
    v.drums(drums, S_VERSE,  {"kick": "x.......x.......", "snare": "....x.......x...",
                              "hat":  "..x.x...x.x.x..."}, bars=2, vel=0.9)
    v.drums(drums, S_CHORUS, {"kick": "x.....x.x.......", "snare": "....x.......x...",
                              "hat":  "x.x.x.x.x.x.x.x.", "clap": "....x.......x..."}, bars=2, vel=0.95)
    v.drums(drums, S_BRIDGE, {"kick": "x.......x.......", "clap":  "....x.......x..."}, bars=2, vel=0.85)
    v.drums(drums, S_OUTRO,  {"kick": "x...............", "clap":  "............x..."}, bars=2, vel=0.75)

    # ---------------- bass (moving acid riff; verse + chorus) ----------------
    bpat = [A2, C3, E3, A3, G3, E3, D3, C3]
    v.bassline(bass, S_VERSE,  [(bpat[i % 8], i * 0.5,  0.4) for i in range(16)], length=8.0, vel=0.85)
    v.bassline(bass, S_CHORUS, [(bpat[i % 8], i * 0.5,  0.4) for i in range(16)], length=8.0, vel=0.9)

    # ---------------- chords (pad; every section) ----------------
    v.progression(chords, S_INTRO,  ["i", "VI"],               beats_per_chord=4.0, key="A", scale="minor", octave=3, vel=0.5,  dur_frac=0.95)
    v.progression(chords, S_VERSE,  ["i", "VI", "III", "VII"], beats_per_chord=2.0, key="A", scale="minor", octave=3, vel=0.55, dur_frac=0.95)
    v.progression(chords, S_CHORUS, ["i", "VI", "III", "VII"], beats_per_chord=2.0, key="A", scale="minor", octave=3, vel=0.6,  dur_frac=0.95)
    v.progression(chords, S_BRIDGE, ["iv", "VII", "III", "VI"], beats_per_chord=2.0, key="A", scale="minor", octave=3, vel=0.55, dur_frac=0.95)
    v.progression(chords, S_OUTRO,  ["i"],                     beats_per_chord=8.0, key="A", scale="minor", octave=3, vel=0.5,  dur_frac=0.98)

    # ---------------- lead (chorus hook + bridge variation) ----------------
    v.bassline(lead, S_CHORUS, [(76, 0.0, 0.75), (72, 1.0, 0.5), (74, 1.5, 0.5), (72, 2.0, 1.0),
                                (69, 3.5, 0.5), (72, 4.0, 0.75), (76, 5.0, 0.5), (79, 5.5, 0.5),
                                (76, 6.0, 1.0), (72, 7.5, 0.5)], length=8.0, vel=0.7)
    v.bassline(lead, S_BRIDGE, [(72, 0, 1.0), (74, 1, 0.5), (76, 1.5, 0.5), (77, 2, 1.0), (76, 3, 0.5),
                                (74, 3.5, 0.5), (72, 4, 1.0), (69, 5, 1.0), (71, 6, 0.5), (72, 6.5, 1.5)],
               length=8.0, vel=0.68)

    # ---------------- arp : GENERATIVE bleeps (RandMelody note-as-signal) + sfx (riser / downlifter) ----
    # The "signal" writes itself — a wandering high 1/16 line the generator improvises, chorus + bridge.
    for sc in (S_CHORUS, S_BRIDGE):
        v.place_generator(arp, sc, "RandMelody")
        for k, val in dict(root=72, scale=PENTMIN, octaves=2, rate=4, density=0.45, gate=0.35).items():
            v.set_gen_param(arp, sc, k, val)
    v.bassline(sfx, S_INTRO, [(48 + i, i * 0.5, 0.4) for i in range(16)], length=8.0, vel=0.5)
    v.bassline(sfx, S_OUTRO, [(57 - 2 * i, i * 0.5, 0.4) for i in range(8)], length=8.0, vel=0.5)

    # ================= visuals : the video, plus overlay geometry per instrument =================
    out = find(v.graph()["nodes"], "Output")
    v.set_media_root(MEDIA)
    vid  = v.video(0)                                     # external pixels
    disp = v.add_node("Displace")                         # <- the KICK shakes the footage
    v.set_node_param(disp, "amount", 0.06); v.set_node_param(disp, "mode", 0.0)
    v.connect(disp, vid)
    fb = v.add_node("Feedback"); v.set_node_param(fb, "decay", 0.26)   # <- the BASS swells the trails
    v.connect(fb, disp)
    mesh = v.add_node("Mesh")                             # <- the LEAD spins this
    for k, val in dict(wireframe=1.0, shape=0.5, size=0.35, r=0.2, g=1.0, b=0.9, spin=0.0).items():
        v.set_node_param(mesh, k, val)
    comp1 = v.add_node("Composite"); v.set_node_param(comp1, "mode", 1.0)
    v.connect(comp1, fb, port=0); v.connect(comp1, mesh, port=1)
    lines = v.add_node("Lines")                           # <- the ARP spins these rings
    for k, val in dict(mode=0.66, count=0.4, size=0.5, rotation=0.0, r=1.0, g=0.4, b=0.2).items():
        v.set_node_param(lines, k, val)
    comp2 = v.add_node("Composite"); v.set_node_param(comp2, "mode", 1.0)
    v.connect(comp2, comp1, port=0); v.connect(comp2, lines, port=1)
    warp = v.add_node("Transform"); v.set_node_param(warp, "rot", 0.5)   # <- the PAD pulses the frame
    v.connect(warp, comp2)
    v.connect(out, warp)

    # ---- the bridge : ONE instrument -> ONE visual effect (per-track, identifiable) ----
    v.track_viz(drums,  "transient", disp,  "amount",   amount=1.0, lo=0.04, hi=0.24)   # kick shakes footage
    v.track_viz(bass,   "low",       fb,    "decay",    amount=0.6, lo=0.22, hi=0.6)    # bass -> trails
    v.track_viz(chords, "level",     warp,  "rot",      amount=1.0, lo=0.5,  hi=0.53)   # pad -> frame pulse
    v.track_viz(lead,   "high",      mesh,  "spin",     amount=1.0, lo=0.0,  hi=1.0)    # lead -> mesh spin
    v.track_viz(arp,    "high",      lines, "rotation", amount=1.0, lo=0.0,  hi=1.0)    # arp -> rings spin
    v.track_viz(sfx,    "level",     mesh,  "size",     amount=1.0, lo=0.3,  hi=0.6)    # sfx -> mesh size

    if save:
        save_geo(v, PROJECT)

    order = [S_INTRO, S_VERSE, S_CHORUS, S_VERSE, S_BRIDGE, S_OUTRO]
    if perform:
        v.perform(order, [4, 8, 8, 8, 8, 4], bpm=BPM)
    else:
        v.play()                # transport must run BEFORE launch — launch is bar-quantized
        v.launch_scene(S_CHORUS)
        print("built. sections = scenes; v.perform(%s, [4,8,8,8,8,4], bpm=%d) plays the whole song." % (order, BPM))


if __name__ == "__main__":
    import sys
    build(Vivid(), perform="perform" in sys.argv)
