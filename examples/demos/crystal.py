"""Crystal — a Deformer-driven sphere that grows crystalline spikes to the bass (ADR-0041 composable demo).

A high-detail Shape3D sphere pushed along its normals by the Deformer's animated noise field, so its
surface churns into shifting facets and spikes — a living crystal / sea-urchin. The Deformer's own
`speed` gives continuous motion (it animates off time), and the master-bus bridge drives the reactivity:
the kick grows the spike amplitude and swells the whole form, the highs sharpen the noise frequency
(finer facets), transients flash the emission. Deformer + Shape3D + audio mappings — separate composable
nodes, no monolith.

Run with the app running:  uv run examples/demos/crystal.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset, SURGE_FX, SURGE_FX_TYPE
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "crystal")
CASSETTE = "Cassette Drums"       # free VST3 drum machine (BPB)
BPM = 120


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # --- Audio: a crystalline SONG in three sections (Surge XT + Cassette Drums) so the facets BUILD —
    #     intro (kick + a low square bass grows the spikes), verse (driving bass + backbeat), chorus
    #     (+ a shimmering high arp with Surge FX → the highs sharpen the facets). i-VI-III-VII in C minor. ---
    S_INTRO, S_VERSE, S_CHORUS = v.scenes(["intro", "verse", "chorus"])
    prog = ["Cm", "Ab", "Eb", "Bb"]
    DRUMS = v.add_track(kind="instrument", instrument=CASSETTE)
    v.set_track_gain(DRUMS, 0.85)
    BASS = v.add_graph_track("bass"); surge_preset(v, BASS, "bass", prefer="Square", gain=0.85)
    ARP = v.add_graph_track("arp"); surge_preset(v, ARP, "pluck", prefer="Sync", gain=0.6)
    v.clap_effect(ARP, SURGE_FX)                                    # shimmer/space on the high arp
    v.node_param(ARP, v.audio_node_id(ARP, "effect"), SURGE_FX_TYPE, 0.05)

    def bass_seq(step):   # roots an octave down, every `step` beats — grows the spikes on the lows
        s = []
        for i, c in enumerate(prog):
            root = theory.chord(c, octave=2)[0]
            k = 0
            while k * step < 4.0:
                s.append((root, i * 4.0 + k * step, step * 0.9)); k += 1
        return s

    # intro: kick + closed hats, slow square bass
    v.drums(DRUMS, S_INTRO, {"kick": "x...x...x...x...", "hat": "..x...x...x...x."}, bars=4, vel=0.8)
    v.bassline(BASS, S_INTRO, bass_seq(2.0), length=16.0, vel=0.85)
    # verse: full backbeat + driving 8th bass
    v.drums(DRUMS, S_VERSE, {"kick": "x...x...x...x...", "snare": "....x.......x...", "hat": "x.x.x.x.x.x.x.x."}, bars=4, vel=0.85)
    v.bassline(BASS, S_VERSE, bass_seq(0.5), length=16.0, vel=0.9)
    # chorus: + crisp open-hats + a bright 16th arp over 2 octaves (the facet-sharpening highs)
    v.drums(DRUMS, S_CHORUS, {"kick": "x...x...x...x...", "snare": "....x.......x...", "hat": "x.x.x.x.x.x.x.x."}, bars=4, vel=0.9)
    v.euclid(DRUMS, S_CHORUS, "openhat", pulses=7, steps=16, bars=4, vel=0.5)
    v.bassline(BASS, S_CHORUS, bass_seq(0.5), length=16.0, vel=0.9)
    arp = []
    for i, c in enumerate(prog):
        for n in theory.arpeggiate(theory.chord(c, octave=5), "updown", rate=0.25, octaves=2, length=4.0, vel=0.6):
            arp.append({"p": n["p"], "s": i * 4.0 + n["s"], "d": n["d"] * 0.9, "v": n["v"]})
    v.set_clip(ARP, S_CHORUS, arp, 16.0)

    out = find(v.graph()["nodes"], "Output")

    shape = v.add_node("Shape3D")   # a subdivided sphere for the Deformer to churn
    for k, val in dict(shape=1, detail=56, r=0.4, g=0.68, b=1.0, roughness=0.4, metallic=0.15,
                       emission=0.12, scale_x=2.2, scale_y=2.2, scale_z=2.2).items():
        v.set_node_param(shape, k, float(val))

    defm = v.add_node("Deformer")   # animated noise displacement along the surface normals
    for k, val in dict(mode=0, axis=0, amplitude=0.3, frequency=3.0, speed=0.8).items():
        v.set_node_param(defm, k, float(val))
    v.connect(defm, shape, 0)

    key = v.add_node("Light3D")
    for k, val in dict(type=0, intensity=2.6, r=1.0, g=0.96, b=0.9,
                       dir_x=-0.4, dir_y=-0.6, dir_z=-0.5).items():
        v.set_node_param(key, k, float(val))
    fill = v.add_node("Light3D")
    for k, val in dict(type=1, intensity=1.5, r=0.45, g=0.7, b=1.0,
                       pos_x=-5.0, pos_y=3.0, pos_z=5.0, radius=26.0).items():
        v.set_node_param(fill, k, float(val))

    merge = v.add_node("SceneMerge")
    v.connect(merge, defm, 0)
    v.connect(merge, key,  1)
    v.connect(merge, fill, 2)

    render = v.add_node("Render3D")
    for k, val in dict(cam_x=0, cam_y=0.5, cam_z=7, target_y=0, fov=48, far=100, near=0.05,
                       bg_r=0.02, bg_g=0.02, bg_b=0.05).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)
    v.connect(out, render, 0)

    # --- The bridge: bass grows the spikes + swells the form, highs sharpen the facets, transients flash. ---
    v.map("master.low",       defm,  "amplitude", amount=0.13, attack=0.02, release=0.26)  # +0.65/5: spike growth
    for ax in ("scale_x", "scale_y", "scale_z"):
        v.map("master.low", shape, ax, amount=0.012, attack=0.02, release=0.28)            # gentle whole-form swell
    v.map("master.high",      defm,  "frequency", amount=0.06, attack=0.03, release=0.2)   # +3/50: finer facets
    v.map("master.transient", shape, "emission",  amount=0.3,  attack=0.005, release=0.18) # +1.5/5: glow flash

    v.master_gain(0.6)   # headroom: bass+arp+drums sum clips at 0 dBFS (AV clip needs clean audio)
    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. a crystal that grows spikes + swells on the kick, sharpens facets on the hats.")


if __name__ == "__main__":
    build(Vivid())
