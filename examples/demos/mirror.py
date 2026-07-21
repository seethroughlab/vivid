"""Mirror — a closed audio<->visual feedback loop, as a full SONG (112 BPM, A minor).

The showcase: the bridge runs BOTH ways. Instruments drive the picture (audio -> visual), and the
picture's feedback/blur state is wired straight BACK into a filter on the pad (visual -> audio, the
return leg almost nothing else demonstrates) — the picture audibly shapes the sound that is shaping
the picture. That loop is the signature flourish, on top of a five-section arrangement:

  section   drums bass chords lead arp  sfx      what it is
  INTRO      hats   .   pad    .    .   riser    filtered pad blooms alone
  VERSE      beat  bass pad    .    .    .       the pulse locks in
  CHORUS     full  bass pad   lead  .    .       full, the loop breathes
  BRIDGE     kick   .   pad   lead arp   .       breakdown + arp flourish
  OUTRO      tail   .   pad    .    .   downlift  resolve

Each instrument marks the picture differently (kick -> feedback trails, which also sweep the pad
cutoff; pad level -> blur, which sweeps the resonance; bass -> mesh size; lead -> mesh spin; arp ->
grid rotation). Hard geometry only — a wireframe Mesh + a ShapeGrid, feedback-smeared, with a title.

Requires Surge XT (free CLAP) + BPB Cassette Drums (free VST3).
Run with the app running:  uv run examples/demos/mirror.py   (append 'perform' to play the whole song)
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "mirror")
CASSETTE = "Cassette Drums"
CUTOFF, RES = 1, 2                                     # SVFilter param indices (0=type,1=cutoff,2=res)
BPM = 112
A2, C3, D3, E3, G3, A3 = 45, 48, 50, 52, 55, 57


def build(v: Vivid, save: bool = True, perform: bool = False):
    v.reset()
    v.bpm(BPM)
    S_INTRO, S_VERSE, S_CHORUS, S_BRIDGE, S_OUTRO = v.scenes(
        ["intro", "verse", "chorus", "bridge", "outro"])
    v.launch_quantize(4)   # switch scenes on 4-bar phrase boundaries, not mid-clip

    # ---------------- roster ----------------
    drums = v.add_track(kind="instrument", instrument=CASSETTE)
    bass  = v.add_graph_track("bass");   surge_preset(v, bass,  "bass",  prefer="Square", gain=0.8)
    pad   = v.add_graph_track("pad");    surge_preset(v, pad,   "pad",   prefer="",       gain=0.6)
    lead  = v.add_graph_track("lead");   surge_preset(v, lead,  "lead",  prefer="Sync",   gain=0.55)
    arp   = v.add_graph_track("arp");    surge_preset(v, arp,   "pluck", prefer="",       gain=0.45)
    sfx   = v.add_graph_track("sfx");    surge_preset(v, sfx,   "sweep", prefer="",       gain=0.4)
    # the native filter the PICTURE drives back (the return leg's target) — on the pad, no extra plugin
    svf = v.add_audio_fx(pad, "SVFilter")
    v.set_audio_node_param(pad, svf, CUTOFF, 900.0)

    # ---------------- drums per section ----------------
    v.drums(drums, S_INTRO,  {"hat": "..x...x...x...x."}, bars=2, vel=0.6)
    v.drums(drums, S_VERSE,  {"kick": "x...x...x...x...", "clap": "....x.......x...",
                              "hat":  "..x...x...x...x."}, bars=2, vel=0.9)
    v.drums(drums, S_CHORUS, {"kick": "x...x...x.x.x...", "clap": "....x.......x...",
                              "hat":  "x.x.x.x.x.x.x.x."}, bars=2, vel=0.95)
    v.drums(drums, S_BRIDGE, {"kick": "x.......x.......", "clap": "....x.......x..."}, bars=2, vel=0.85)
    v.drums(drums, S_OUTRO,  {"kick": "x...............", "clap": "............x..."}, bars=2, vel=0.75)

    # ---------------- bass (moving; verse + chorus) ----------------
    bpat = [A2, A2, E3, G3, A2, C3, D3, E3]
    v.bassline(bass, S_VERSE,  [(bpat[i % 8], i * 0.5, 0.4) for i in range(16)], length=8.0, vel=0.9)
    v.bassline(bass, S_CHORUS, [(bpat[i % 8], i * 0.5, 0.4) for i in range(16)], length=8.0, vel=0.92)

    # ---------------- pad / chords (through the SVFilter; every section) ----------------
    v.progression(pad, S_INTRO,  ["i", "VI"],               beats_per_chord=4.0, key="A", scale="minor", octave=4, vel=0.55, dur_frac=0.98)
    v.progression(pad, S_VERSE,  ["i", "VI", "III", "VII"], beats_per_chord=4.0, key="A", scale="minor", octave=4, vel=0.6,  dur_frac=0.98)
    v.progression(pad, S_CHORUS, ["i", "VI", "III", "VII"], beats_per_chord=4.0, key="A", scale="minor", octave=4, vel=0.62, dur_frac=0.98)
    v.progression(pad, S_BRIDGE, ["iv", "VII", "III", "VI"], beats_per_chord=4.0, key="A", scale="minor", octave=4, vel=0.6,  dur_frac=0.98)
    v.progression(pad, S_OUTRO,  ["i"],                     beats_per_chord=8.0, key="A", scale="minor", octave=4, vel=0.55, dur_frac=0.99)

    # ---------------- lead (chorus + bridge) ----------------
    v.bassline(lead, S_CHORUS, [(76, 0, 0.5), (74, 0.5, 0.5), (72, 1, 1.0), (69, 2.5, 0.5), (72, 3, 1.0),
                                (76, 4, 0.5), (79, 4.5, 0.5), (81, 5, 1.0), (79, 6.5, 0.5), (76, 7, 1.0)],
               length=8.0, vel=0.7)
    v.bassline(lead, S_BRIDGE, [(72, 0, 1.0), (74, 1, 0.5), (76, 1.5, 0.5), (77, 2, 1.0), (76, 3, 0.5),
                                (74, 3.5, 0.5), (72, 4, 1.0), (69, 5, 1.0), (71, 6, 0.5), (72, 6.5, 1.5)],
               length=8.0, vel=0.68)

    # ---------------- arp : a GENERATIVE Euclid pulse (note-as-signal, not a clip) + sfx ----
    # A self-writing rhythmic blip the feedback loop chews — E(7,16) sixteenths, chorus + bridge.
    for sc in (S_CHORUS, S_BRIDGE):
        v.place_generator(arp, sc, "Euclid")
        for k, val in dict(note=69, pulses=7, steps=16, rate=4, gate=0.35).items():
            v.set_gen_param(arp, sc, k, val)
    v.bassline(sfx, S_INTRO, [(48 + i, i * 0.5, 0.4) for i in range(16)], length=8.0, vel=0.5)
    v.bassline(sfx, S_OUTRO, [(57 - 2 * i, i * 0.5, 0.4) for i in range(8)], length=8.0, vel=0.5)

    # ================= visuals : wireframe Mesh + ShapeGrid -> Feedback -> Blur + title =================
    out = find(v.graph()["nodes"], "Output")
    mesh = v.add_node("Mesh")                              # <- BASS size, LEAD spin
    for k, val in dict(shape=1.0, wireframe=1.0, size=0.4, spin=0.25, tilt=0.5,
                       r=1.0, g=0.35, b=0.95, bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(mesh, k, val)
    sg = v.add_node("ShapeGrid")                           # <- ARP rotation, SFX size
    for k, val in dict(sides=0.6, cols=0.45, rows=0.45, size=0.5, rotation=0.0,
                       r=0.2, g=0.9, b=1.0, bg_r=0.01, bg_g=0.02, bg_b=0.04).items():
        v.set_node_param(sg, k, val)
    compA = v.add_node("Composite"); v.set_node_param(compA, "mode", 1.0)
    v.connect(compA, sg, port=0); v.connect(compA, mesh, port=1)
    fb = v.add_node("Feedback"); v.set_node_param(fb, "decay", 0.42)   # <- KICK (also the return source)
    v.connect(fb, compA)
    blur = v.add_node("Blur"); v.set_node_param(blur, "radius", 0.15)  # <- PAD level (also return source)
    v.connect(blur, fb)
    title = v.add_node("VectorText")
    for k, val in dict(size=0.12, x=0.5, y=0.12, r=0.95, g=0.95, b=1.0,
                       bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(title, k, val)
    v.set_node_file(title, "file", os.path.join(HERE, "media", "mirror.txt"))
    compB = v.add_node("Composite"); v.set_node_param(compB, "mode", 1.0)
    v.connect(compB, blur, port=0); v.connect(compB, title, port=1)
    v.connect(out, compB)

    # ---- forward leg : ONE instrument -> ONE visual effect (per-track). The kick->feedback and
    #      pad->blur maps ALSO move the return-leg sources (viz.feedback / viz.blur). ----
    v.track_viz(drums, "low",   fb,   "decay",    amount=0.7, lo=0.4,  hi=0.82)   # kick -> trails (=viz.feedback)
    v.track_viz(pad,   "level", blur, "radius",   amount=0.6, lo=0.2,  hi=0.6)    # pad  -> blur   (=viz.blur)
    v.track_viz(bass,  "low",   mesh, "size",     amount=0.7, lo=0.28, hi=0.5)    # bass -> mesh size
    v.track_viz(lead,  "high",  mesh, "spin",     amount=1.0, lo=0.1,  hi=0.7)    # lead -> mesh spin
    v.track_viz(arp,   "high",  sg,   "rotation", amount=1.0, lo=0.0,  hi=1.0)    # arp  -> grid rotation
    v.track_viz(sfx,   "level", sg,   "size",     amount=1.0, lo=0.5,  hi=0.7)    # sfx  -> grid size

    # ---- RETURN leg (visual -> audio) : the picture's state sweeps the filter on the PAD ----
    v.map_to_audio("viz.feedback", pad, svf, CUTOFF, amount=1.0, lo=400.0, hi=6000.0)
    v.map_to_audio("viz.blur",     pad, svf, RES,    amount=1.0, lo=0.05,  hi=0.55)

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
