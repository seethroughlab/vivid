"""Spectrum — a 3D equaliser built from a COMPOSABLE lane chain (ADR-0041).

No monolithic op: the audio→visual pipeline is separate, visible nodes wired together via FLOAT-MANY
value lanes (the new visual-graph lane transport):

    AudioSpectrum ─(spectrum lane)─┐
                                   ├─> InstancesFromLanes ─> Instancer3D ─> SceneMerge ─> Render3D
    LaneRamp ─────(pos_x lane)─────┘                            ^
                                                             Shape3D (a cube)

AudioSpectrum reads the live master spectrum and emits one 0..1 magnitude per band; LaneRamp emits the
bar X positions; InstancesFromLanes packs those lanes into per-instance transforms (pos_x + scale_y);
Instancer3D draws the base cube once per band. Each stage is a node you can see, retune, and recombine.

Run with the app running:  uv run examples/demos/spectrum.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "spectrum")
CASSETTE = "Cassette Drums"       # free VST3 drum machine (BPB)
BPM = 122


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # --- Audio: a real electronic SONG in three sections (Surge XT + Cassette Drums), so the equaliser
    #     BUILDS — intro (kick + hats), verse (+ square bass on the lows), chorus (+ a bright 16th arp
    #     filling the mids/highs). i-VI-III-VII in A minor. The capture performs intro→verse→chorus. ---
    S_INTRO, S_VERSE, S_CHORUS = v.scenes(["intro", "verse", "chorus"])
    prog = ["Am", "F", "C", "G"]                                   # 1 bar each, 4-bar sections
    DRUMS = v.add_track(kind="instrument", instrument=CASSETTE)     # free VST3 drums
    v.set_track_gain(DRUMS, 0.85)                                  # leave master headroom (kick is hottest)
    BASS = v.add_graph_track("bass"); surge_preset(v, BASS, "bass", prefer="Square", gain=0.8)
    LEAD = v.add_graph_track("lead"); surge_preset(v, LEAD, "pluck", prefer="Sync", gain=0.7)

    def bass_seq(step):   # roots an octave down, every `step` beats — anchors the low bands
        s = []
        for i, c in enumerate(prog):
            root = theory.chord(c, octave=2)[0]
            k = 0
            while k * step < 4.0:
                s.append((root, i * 4.0 + k * step, step * 0.9)); k += 1
        return s

    def arp_notes():      # driving 16th arpeggio up over 2 octaves — spreads energy across bands
        ns = []
        for i, c in enumerate(prog):
            for n in theory.arpeggiate(theory.chord(c, octave=4), "up", rate=0.25, octaves=2, length=4.0, vel=0.72):
                ns.append({"p": n["p"], "s": i * 4.0 + n["s"], "d": n["d"] * 0.9, "v": n["v"]})
        return ns

    # intro: kick + closed hats, sparse root bass on half-notes
    v.drums(DRUMS, S_INTRO, {"kick": "x...x...x...x...", "hat": "..x...x...x...x."}, bars=4, vel=0.8)
    v.bassline(BASS, S_INTRO, bass_seq(2.0), length=16.0, vel=0.8)
    # verse: full backbeat + driving 8th bass
    v.drums(DRUMS, S_VERSE, {"kick": "x...x...x...x...", "clap": "....x.......x...", "hat": "x.x.x.x.x.x.x.x."}, bars=4, vel=0.85)
    v.bassline(BASS, S_VERSE, bass_seq(0.5), length=16.0, vel=0.9)
    # chorus: full drums + open-hats + bass + the bright arp (peak energy across the whole spectrum)
    v.drums(DRUMS, S_CHORUS, {"kick": "x...x...x...x...", "clap": "....x.......x...", "hat": "x.x.x.x.x.x.x.x."}, bars=4, vel=0.9)
    v.euclid(DRUMS, S_CHORUS, "openhat", pulses=5, steps=16, bars=4, vel=0.5)
    v.bassline(BASS, S_CHORUS, bass_seq(0.5), length=16.0, vel=0.95)
    v.set_clip(LEAD, S_CHORUS, arp_notes(), 16.0)

    NBARS = 44
    SPREAD = 34.0   # bar row half-width (bars stay distinct: thin bars, ~1.5-unit pitch)

    # --- Visual: the composable lane chain. ---
    out = find(v.graph()["nodes"], "Output")

    # A tall, thin base cube; instance scale_y (from the spectrum) stretches it into a bar.
    shape = v.add_node("Shape3D")
    # A thick, TALL base bar (scale_y sets the max height) — the instance scale_y (from the spectrum)
    # stretches it, and since the cube is centred at y=0 it grows symmetrically up+down (a mirrored EQ).
    for k, val in dict(shape=0, detail=1, r=0.25, g=0.8, b=1.0, metallic=0.0, roughness=0.55,
                       emission=0.4, scale_x=0.5, scale_y=24.0, scale_z=0.5).items():
        v.set_node_param(shape, k, float(val))

    spec = v.add_node("AudioSpectrum")          # → 0..1 magnitude per band (the reactive lane)
    # Full per-band AGC (normalize=1.0) lifts the quiet MID bands so the wall reads FULL across the
    # whole width instead of pinching to a bowtie (this song is bass- + treble-heavy); a slow release
    # keeps the amplified quiet bands from jittering.
    for k, val in dict(bands=NBARS, gain=1.5, tilt=0.4, normalize=1.0, attack=0.02, release=0.22).items():
        v.set_node_param(spec, k, float(val))

    ramp = v.add_node("LaneRamp")               # → bar X positions (the layout lane)
    for k, val in dict(count=NBARS, lo=-SPREAD, hi=SPREAD, mode=0).items():
        v.set_node_param(ramp, k, float(val))

    pal = v.add_node("LanePalette")             # → per-band r/g/b gradient (3 lanes from ONE node)
    for k, val in dict(count=NBARS, palette=0, offset=0.0, spread=1.0).items():
        v.set_node_param(pal, k, float(val))

    lanes = v.add_node("InstancesFromLanes")
    v.connect(lanes, ramp, 0)                   # pos_x   ← LaneRamp
    v.connect(lanes, spec, 4)                   # scale_y ← AudioSpectrum
    v.connect(lanes, pal, 7, src_port=0)        # color_r ← LanePalette out 0
    v.connect(lanes, pal, 8, src_port=1)        # color_g ← LanePalette out 1
    v.connect(lanes, pal, 9, src_port=2)        # color_b ← LanePalette out 2

    inst = v.add_node("Instancer3D")
    v.connect(inst, shape, 0)                   # scene (base cube)
    v.connect(inst, lanes, 1)                   # instances (per-band transforms)

    key = v.add_node("Light3D")
    for k, val in dict(type=0, intensity=2.6, r=1.0, g=0.97, b=0.9,
                       dir_x=-0.3, dir_y=-0.8, dir_z=-0.5).items():
        v.set_node_param(key, k, float(val))
    fill = v.add_node("Light3D")
    for k, val in dict(type=0, intensity=1.1, r=0.5, g=0.65, b=1.0,
                       dir_x=0.6, dir_y=-0.2, dir_z=0.4).items():
        v.set_node_param(fill, k, float(val))

    merge = v.add_node("SceneMerge")
    v.connect(merge, inst, 0)
    v.connect(merge, key,  1)
    v.connect(merge, fill, 2)

    render = v.add_node("Render3D")
    # Head-on + centred (cam_y=target_y=0) so the mirrored bars fill the frame vertically; pulled in to
    # cam_z=46 so the ±SPREAD row fills most of the width without clipping the edge bands.
    for k, val in dict(cam_x=0, cam_y=0, cam_z=46, target_x=0, target_y=0, target_z=0,
                       fov=42, far=200, near=0.1).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)
    v.connect(out, render, 0)

    v.master_gain(0.6)   # headroom: arp+bass+drums sum clips at 0 dBFS (AV clip needs clean audio)
    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. composable chain: AudioSpectrum + LaneRamp → InstancesFromLanes → Instancer3D ← Shape3D.")


if __name__ == "__main__":
    build(Vivid())
