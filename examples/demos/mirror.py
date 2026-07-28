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

The geometry is per-NOTE (each track's notes draw their own wireframe forms); only the bridge-state
params ride band energy, because THAT is the loop: kick -> feedback trails (which sweep the pad
cutoff), pad level -> blur (which sweeps the resonance), arp -> the mirror's rotation. Hard geometry
only — per-note wireframe octahedra (bass), cubes (pad), triangles (lead), rings (arp), folded into
mirror symmetry, lightly feedback-smeared, with a title.

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

    # ================= visuals : SYMMETRIC per-note geometry through a custom Fold op ==============
    # Style: PER-NOTE hard wireframe geometry (Solids + polygon Instancers — the picture reads WHICH
    # notes play, not band energy) folded through the custom **Fold** op (crisp axis-aligned mirror
    # symmetry — architectural, not a busy kaleido). Feedback+Blur stay — not as haze but as the
    # BRIDGE MECHANISM: kick->feedback and pad->blur move viz.feedback / viz.blur, which the return
    # leg wires straight back into the pad's filter (cutoff / resonance). The picture shapes the sound.
    out = find(v.graph()["nodes"], "Output")
    bass_sig = v.notes(v.track_id(bass))
    pad_sig  = v.notes(v.track_id(pad))
    lead_sig = v.notes(v.track_id(lead))
    arp_sig  = v.notes(v.track_id(arp))

    octa  = v.solids(bass_sig, shape=2, size=0.42, spread=0.5,  spin=0.28, wireframe=1.0)  # bass  → wireframe octahedra
    cubes = v.solids(pad_sig,  shape=0, size=0.3,  spread=0.72, spin=0.14, wireframe=1.0)  # pad   → wireframe cube cluster
    tris  = v.instancer(lead_sig, shape=2, sides=3, size=0.19, spread=0.7,  pulse=0.85)    # lead  → triangles
    arpI  = v.instancer(arp_sig,  shape=1,          size=0.16, spread=0.9, trail=0.4, pulse=0.7)  # arp → rings
    v.beat_sync("beat_pulse", octa, "size", amount=0.14, lo=0.34, hi=0.5)  # on-beat throb of the octahedra

    # stack the per-note layers (ADD), then fold into mirror symmetry
    c1 = v.add_node("Composite"); v.set_node_param(c1, "mode", 1.0)
    v.connect(c1, octa, port=0); v.connect(c1, cubes, port=1)
    c2 = v.add_node("Composite"); v.set_node_param(c2, "mode", 1.0)
    v.connect(c2, c1, port=0); v.connect(c2, tris, port=1)
    c3 = v.add_node("Composite"); v.set_node_param(c3, "mode", 1.0)
    v.connect(c3, c2, port=0); v.connect(c3, arpI, port=1)
    fold = v.add_node("Fold")                              # <- the custom signature: mirror symmetry
    for k, val in dict(axes=1.0, angle=0.0, cx=0.5, cy=0.5, zoom=0.5).items():   # axes=1 -> quad (2-axis)
        v.set_node_param(fold, k, val)
    v.connect(fold, c3)
    fb = v.add_node("Feedback"); v.set_node_param(fb, "decay", 0.32)   # subtle trails <- KICK (=viz.feedback)
    v.connect(fb, fold)
    blur = v.add_node("Blur"); v.set_node_param(blur, "radius", 0.08)  # gentle <- PAD level (=viz.blur)
    v.connect(blur, fb)
    title = v.add_node("VectorText")
    for k, val in dict(size=0.12, x=0.5, y=0.12, r=0.95, g=0.95, b=1.0,
                       bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(title, k, val)
    v.set_node_file(title, "file", os.path.join(HERE, "media", "mirror.txt"))
    compB = v.add_node("Composite"); v.set_node_param(compB, "mode", 1.0)
    v.connect(compB, blur, port=0); v.connect(compB, title, port=1)
    v.connect(out, compB)

    # ---- forward leg : the kick->feedback and pad->blur maps drive BOTH the visible trails/blur AND
    #      the return-leg sources (viz.feedback / viz.blur). The geometry itself is per-note (above),
    #      so only the bridge-state params ride band energy — that IS the bridge, not a screensaver. ----
    v.track_viz(drums, "low",   fb,   "decay",  amount=0.6, lo=0.24, hi=0.55)   # kick -> trails (=viz.feedback)
    v.track_viz(pad,   "level", blur, "radius", amount=0.6, lo=0.05, hi=0.28)   # pad  -> blur   (=viz.blur)
    v.track_viz(arp,   "high",  fold, "angle",  amount=1.0, lo=0.0,  hi=1.0)    # arp  -> the mirror rotates
    v.track_viz(sfx,   "level", fold, "zoom",   amount=1.0, lo=0.35, hi=0.75)   # riser-> the mirror zooms

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
