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
    arp_sig = v.notes(v.track_id(ARP))   # the melody carves the spike crown (per-note, shared by every look)

    # --- Each LOOK is a genuinely different GEOMETRY (so the 4-bar cut is unmistakable from any angle, not
    #     just a recolour): a SPIKY crystal (deformed sphere + a dense arp-driven spike crown), a SMOOTH
    #     churning BLOB (big gently-deformed orb, crown suppressed), and a TORUS RING (a donut faced at the
    #     camera). Composable: timing lives in the Clock, selection in the Switch, each look is a subgraph. ---
    def make_look(cfg):
        cr, cg, cb = cfg["core_rgb"]
        core = v.add_node("Shape3D")            # the persistent form; the Deformer churns it to the bass
        for k, val in dict(shape=cfg["core_shape"], detail=48, r=cr, g=cg, b=cb, roughness=0.4,
                           metallic=0.15, emission=cfg["core_emission"], rot_x=cfg["core_rot_x"],
                           scale_x=cfg["core_scale"], scale_y=cfg["core_scale"], scale_z=cfg["core_scale"]).items():
            v.set_node_param(core, k, float(val))
        defm = v.add_node("Deformer")           # animated noise displacement along the surface normals
        for k, val in dict(mode=0, axis=0, amplitude=cfg["deform_amp"], frequency=cfg["deform_freq"],
                           speed=cfg["deform_speed"]).items():
            v.set_node_param(defm, k, float(val))
        v.connect(defm, core, 0)

        crown_sig = v.add_node("InstancesFromSignal")   # arp-driven radial crown (suppressed to ~0 on non-spiky looks)
        for k, val in dict(size=cfg["size"], radius=cfg["radius"], layout=cfg["layout"], orient=1,
                           elongate=cfg["elongate"], spin=0.0, trail=cfg["trail"], pulse=1.0,
                           palette=cfg["palette"], persist=cfg["persist"]).items():
            v.set_node_param(crown_sig, k, float(val))
        v.connect(crown_sig, arp_sig)
        sr, sg, sb = cfg["spike_rgb"]
        spike = v.add_node("Shape3D")           # base shard (Cone); elongate stretches it radially into a spike
        for k, val in dict(shape=cfg["spike_shape"], detail=6, r=sr, g=sg, b=sb, roughness=0.3,
                           metallic=0.4, emission=1.1).items():
            v.set_node_param(spike, k, float(val))
        crown = v.add_node("Instancer3D")
        v.connect(crown, spike, 0)
        v.connect(crown, crown_sig, 1)

        look = v.add_node("SceneMerge")         # ONE Scene3D per look: core + its crown
        v.connect(look, defm,  0)
        v.connect(look, crown, 1)
        # bridge each look's reactivity: bass churns the core + stabs the crown, highs add detail, transients glow
        v.map("master.low",       defm,     "amplitude", amount=cfg["amp_react"], attack=0.008, release=0.13)
        v.map("master.low",       crown_sig,"elongate",  amount=0.12, attack=0.006, release=0.12)
        v.map("master.high",      defm,     "frequency", amount=0.06, attack=0.03,  release=0.2)
        v.map("master.transient", core,     "emission",  amount=0.3,  attack=0.005, release=0.16)
        return look

    # Three DIFFERENT geometries — a spiky ball, a smooth orb, a ring — so each cut reads at a glance.
    looks = [make_look(c) for c in [
        # SPIKY CRYSTAL — small core, prominent arp-driven spike crown (Cone, orb), icy blue
        dict(core_shape=1, core_scale=0.85, core_rgb=(0.35, 0.55, 0.95), core_emission=0.13, core_rot_x=0.0,
             deform_amp=0.30, deform_freq=3.0, deform_speed=0.8, amp_react=0.16,
             spike_shape=5, palette=3, spike_rgb=(0.60, 0.85, 1.00),
             layout=2, radius=1.00, size=0.42, elongate=8.0, trail=0.60, persist=0.35),
        # SMOOTH BLOB — a big, gently-churning organic orb (crown suppressed), green
        dict(core_shape=1, core_scale=2.60, core_rgb=(0.30, 0.75, 0.55), core_emission=0.45, core_rot_x=0.0,
             deform_amp=0.08, deform_freq=1.4, deform_speed=0.6, amp_react=0.05,
             spike_shape=5, palette=4, spike_rgb=(0.55, 1.00, 0.62),
             layout=2, radius=2.50, size=0.05, elongate=2.0, trail=0.60, persist=0.35),
        # TORUS RING — a donut turned to FACE the camera (rot_x), a clear hole; crown suppressed, fiery orange
        dict(core_shape=2, core_scale=2.60, core_rgb=(0.95, 0.45, 0.20), core_emission=0.30, core_rot_x=1.5708,
             deform_amp=0.08, deform_freq=2.0, deform_speed=0.7, amp_react=0.05,
             spike_shape=5, palette=2, spike_rgb=(1.00, 0.60, 0.25),
             layout=2, radius=1.60, size=0.05, elongate=3.0, trail=0.60, persist=0.35),
    ]]

    # The composable cut: a metronome-locked Clock ticks every 4 bars; Switch3D forwards the look at that
    # step (sequential) — so the whole crystal cuts to a new shape + colour scheme on the bar, in time.
    clock = v.add_node("Clock")
    for k, val in dict(sync=1, unit=1, period=4.0, steps=0, gate_width=0.08).items():   # metronome, 4 bars
        v.set_node_param(clock, k, float(val))
    switch = v.add_node("Switch3D")
    v.set_node_param(switch, "order", 0.0)      # sequential
    v.connect(switch, clock, 0, src_port=0)     # clock `step` (out 0) → Switch `clock` (in port 0)
    for i, look in enumerate(looks):
        v.connect(switch, look, 1 + i)          # → scene_a / scene_b / scene_c

    key = v.add_node("Light3D")
    for k, val in dict(type=0, intensity=2.6, r=1.0, g=0.96, b=0.9,
                       dir_x=-0.4, dir_y=-0.6, dir_z=-0.5).items():
        v.set_node_param(key, k, float(val))
    fill = v.add_node("Light3D")
    for k, val in dict(type=1, intensity=1.5, r=0.55, g=0.7, b=1.0,
                       pos_x=-5.0, pos_y=3.0, pos_z=5.0, radius=26.0).items():
        v.set_node_param(fill, k, float(val))

    merge = v.add_node("SceneMerge")
    v.connect(merge, switch, 0)         # the currently-selected look
    v.connect(merge, key,    1)
    v.connect(merge, fill,   2)

    render = v.add_node("Render3D")
    for k, val in dict(cam_x=0, cam_y=0.5, cam_z=7, target_y=0, fov=48, far=100, near=0.05,
                       bg_r=0.02, bg_g=0.02, bg_b=0.05).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)
    v.connect(out, render, 0)

    v.master_gain(0.6)   # headroom: bass+arp+drums sum clips at 0 dBFS (AV clip needs clean audio)
    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. a crystal that cuts between 3 shape/colour looks every 4 bars (Clock → Switch3D),")
    print("       each churning + erupting note-spikes to the music.")


if __name__ == "__main__":
    build(Vivid())
