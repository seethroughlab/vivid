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
swells the trails, pad pulses the frame, lead MELTS a 3D statue's surface with animated noise).

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

    # ================= visuals : BROADCAST/CRT — footage + a 3D statue through a custom CRT op ========
    # Style: the external footage over a phosphor-green data grid, with a real 3D model (glTF — the
    # **Model** op) turning inside it, ghosted by the bass, then the whole thing pushed through the
    # custom **CRT** op (barrel curve, scanlines, RGB shadow-mask, chromatic aberration, vignette,
    # rolling hum bar). Deliberately technical/desaturated — a broadcast on a tube, statue and all.
    out = find(v.graph()["nodes"], "Output")
    v.set_media_root(MEDIA)
    vid  = v.video(0)                                     # external pixels
    lines = v.add_node("Lines")                           # a faint phosphor-green data grid under the footage
    for k, val in dict(mode=0.0, count=0.45, size=0.7, rotation=0.0, r=0.15, g=0.9, b=0.4,
                       bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(lines, k, val)
    comp = v.add_node("Composite"); v.set_node_param(comp, "mode", 1.0)   # video + grid
    v.connect(comp, vid, port=0); v.connect(comp, lines, port=1)
    disp = v.add_node("Displace")                         # <- the KICK shakes the footage
    v.set_node_param(disp, "amount", 0.05); v.set_node_param(disp, "mode", 0.0)
    v.connect(disp, comp)
    fb = v.add_node("Feedback"); v.set_node_param(fb, "decay", 0.22)      # <- the BASS ghosts it
    v.connect(fb, disp)
    # a real 3D model (glTF) run through the GEOMETRY PIPELINE: MeshLoad -> MeshDisplace -> MeshRender.
    # An animated NoiseField churns the statue's SURFACE as real per-vertex displacement on the GPU
    # (the lead drives how far it melts). Composited OVER the ghosted footage (AFTER the feedback, so
    # it stays legible), then the whole frame goes through the tube.
    mesh = v.add_node("MeshLoad")                        # glTF -> a mesh VALUE on the graph
    v.set_node_file(mesh, "file", os.path.join(MEDIA, "frank", "scene.gltf"))
    nfield = v.add_node("NoiseField")                    # animated FBm displacement map (core primitive)
    for k, val in dict(scale=0.30, speed=0.28, octaves=0.5, noise_type=0.0).items():
        v.set_node_param(nfield, k, val)
    melt = v.add_node("MeshDisplace")                    # <- the LEAD melts the surface (real geometry)
    for k, val in dict(amount=0.12, scale=0.22, mode=0.0).items():   # base melt; smooth object-space field
        v.set_node_param(melt, k, val)
    v.connect(melt, mesh,   port=0)                      # mesh in
    v.connect(melt, nfield, port=1)                      # displace map in
    model = v.add_node("MeshRender")                     # displaced mesh -> textured/lit image
    # tilt fixed at 0.5 => UPRIGHT (0.5 maps to no lean); a slow upright turntable spin, never a
    # tumble. The mesh carries its glTF baseColor (weathered stone) all the way through the pipeline,
    # so tint stays near-white to show the true material — it sits right under the ADD composite.
    for k, val in dict(size=0.62, spin=0.05, tilt=0.5, light=0.5,
                       r=0.85, g=1.0, b=0.9).items():
        v.set_node_param(model, k, val)
    v.connect(model, melt, port=0)                       # the displaced mesh feeds the renderer
    compF = v.add_node("Composite"); v.set_node_param(compF, "mode", 1.0)   # ADD the statue over the footage
    v.connect(compF, fb, port=0); v.connect(compF, model, port=1)
    crt = v.add_node("CRT")                               # <- the custom signature: the whole tube
    for k, val in dict(scan=0.55, mask=0.5, aberration=0.25, vignette=0.45, roll=0.0, curve=0.22).items():
        v.set_node_param(crt, k, val)
    v.connect(crt, compF)
    v.connect(out, crt)

    # ---- the bridge : ONE instrument -> ONE visual effect (per-track, identifiable) ----
    v.track_viz(drums,  "transient", disp, "amount",     amount=1.0, lo=0.03, hi=0.22)   # kick shakes footage
    v.track_viz(bass,   "low",       fb,   "decay",      amount=0.6, lo=0.18, hi=0.6)    # bass -> ghost trails
    v.track_viz(lead,   "high",      crt,  "aberration", amount=1.0, lo=0.15, hi=0.8)    # lead -> chromatic fringing
    v.track_viz(chords, "level",     crt,  "vignette",   amount=1.0, lo=0.3,  hi=0.7)    # pad  -> breathing vignette
    v.track_viz(arp,    "high",      crt,  "scan",       amount=1.0, lo=0.35, hi=0.9)    # arp  -> scanline pulse
    v.track_viz(sfx,    "level",     crt,  "roll",       amount=1.0, lo=0.0,  hi=0.7)    # riser-> hum-bar roll
    # ---- the 3D model reacts too: the LEAD melts its surface (3D noise displacement), the bass
    #      swells it, the kick nods it. It barely spins (a slow drift) instead of whirling. ----
    v.track_viz(lead,   "high",      melt,  "amount",    amount=1.0, lo=0.05, hi=0.4)    # lead -> surface melts (real geometry)
    v.track_viz(bass,   "low",       model, "size",      amount=0.6, lo=0.42, hi=0.68)   # bass -> statue swells
    v.track_viz(drums,  "low",       melt,  "scale",     amount=0.5, lo=0.15, hi=0.5)    # kick -> percussive surface ripple (upright-safe; tilt is NOT modulated)

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
