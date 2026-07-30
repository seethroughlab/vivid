"""Blob — a raymarched SDF3D metaball that swells and melds to the bass (ADR-0041 composable demo).

Two spheres smooth-unioned into one organic blob (SDF3D, GPU raymarch). The master-bus analysis bridge
drives it: the kick swells its size and pulls the second lobe out, tightens the smooth-union blend so the
two spheres meld and separate, and flashes its emission; the highs add a metallic shimmer. Every wire is
an audio→visual mapping with a fast-attack / slow-release envelope (the bridge smoothing), so the blob
lurches on the hit then glides back — a breathing, liquid-metal feel, no monolithic op.

Run with the app running:  uv run examples/demos/blob.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "blob")
CASSETTE = "Cassette Drums"       # free VST3 drum machine (BPB)
BPM = 124


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # --- Audio: a deep, bass-forward house SONG in three sections (Surge XT + Cassette Drums) so the
    #     blob BUILDS — intro (kick + a slow sub bass), verse (driving 8th bass swells it), chorus
    #     (+ open-hats & offbeat stabs adding the metallic-shimmer highs). i-VI-III-VII in F minor. ---
    S_INTRO, S_VERSE, S_CHORUS = v.scenes(["intro", "verse", "chorus"])
    prog = ["Fm", "Db", "Ab", "Eb"]
    DRUMS = v.add_track(kind="instrument", instrument=CASSETTE)
    v.set_track_gain(DRUMS, 0.9)
    BASS = v.add_graph_track("bass"); surge_preset(v, BASS, "bass", prefer="Square", gain=0.95)
    STAB = v.add_graph_track("stab"); surge_preset(v, STAB, "pluck", prefer="Sync", gain=0.5)

    def bass_seq(step):   # roots an octave down, every `step` beats
        s = []
        for i, c in enumerate(prog):
            root = theory.chord(c, octave=2)[0]
            k = 0
            while k * step < 4.0:
                s.append((root, i * 4.0 + k * step, step * 0.9)); k += 1
        return s

    # intro: kick + sparse hat, slow sub bass (half-notes) — the blob breathes
    v.drums(DRUMS, S_INTRO, {"kick": "x...x...x...x...", "hat": "..x...x...x...x."}, bars=4, vel=0.85)
    v.bassline(BASS, S_INTRO, bass_seq(2.0), length=16.0, vel=0.85)
    # verse: full four-on-floor + clap + driving 8th bass — bigger swell
    v.drums(DRUMS, S_VERSE, {"kick": "x...x...x...x...", "clap": "....x.......x...", "hat": "..x...x...x...x."}, bars=4, vel=0.9)
    v.bassline(BASS, S_VERSE, bass_seq(0.5), length=16.0, vel=0.95)
    # chorus: + open-hats + offbeat chord stabs (the shimmer highs)
    v.drums(DRUMS, S_CHORUS, {"kick": "x...x...x...x...", "clap": "....x.......x...", "hat": "x.x.x.x.x.x.x.x."}, bars=4, vel=0.92)
    v.euclid(DRUMS, S_CHORUS, "openhat", pulses=3, steps=16, bars=4, vel=0.45)
    v.bassline(BASS, S_CHORUS, bass_seq(0.5), length=16.0, vel=0.95)
    stabs = [{"p": p, "s": i * 4.0 + 2.5, "d": 0.3, "v": 0.55}
             for i, c in enumerate(prog) for p in theory.chord(c, octave=4)]
    v.set_clip(STAB, S_CHORUS, stabs, 16.0)

    out = find(v.graph()["nodes"], "Output")

    # A metaball: sphere A smooth-unioned with sphere B (offset on +x).
    sdf = v.add_node("SDF3D")
    for k, val in dict(shape=0, size_x=2.2, size_y=2.2, size_z=2.2, operation=1, shape_b=1,
                       size_bx=1.4, size_by=1.4, size_bz=1.4, pos_bx=1.6, smooth_k=0.7,
                       r=0.45, g=0.72, b=1.0, roughness=0.32, metallic=0.2, emission=0.12).items():
        v.set_node_param(sdf, k, float(val))

    key = v.add_node("Light3D")
    for k, val in dict(type=0, intensity=2.6, r=1.0, g=0.96, b=0.9,
                       dir_x=-0.4, dir_y=-0.7, dir_z=-0.5).items():
        v.set_node_param(key, k, float(val))
    fill = v.add_node("Light3D")
    for k, val in dict(type=1, intensity=1.5, r=0.45, g=0.7, b=1.0,
                       pos_x=-5.0, pos_y=3.0, pos_z=5.0, radius=26.0).items():
        v.set_node_param(fill, k, float(val))

    merge = v.add_node("SceneMerge")
    v.connect(merge, sdf,  0)
    v.connect(merge, key,  1)
    v.connect(merge, fill, 2)

    render = v.add_node("Render3D")
    for k, val in dict(cam_x=0, cam_y=1, cam_z=9, target_y=0, fov=45, far=100, near=0.05,
                       bg_r=0.02, bg_g=0.02, bg_b=0.05).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)
    v.connect(out, render, 0)

    # --- The bridge: bass swells + melds the blob (amount = excursion / param_range), fast attack /
    #     slow release so it lurches then glides. ---
    for ax in ("size_x", "size_y", "size_z"):
        v.map("master.low", sdf, ax, amount=0.11, attack=0.02, release=0.28)   # +1.1 over range 10: swell
    v.map("master.low",       sdf, "pos_bx",   amount=0.09, attack=0.03, release=0.3)   # +1.8/20: lobe stretch
    v.map("master.low",       sdf, "smooth_k", amount=0.3,  attack=0.03, release=0.3)   # +0.6/2: meld/separate
    v.map("master.low",       sdf, "rot_y",    amount=0.03, attack=0.05, release=0.5)   # slow twist on the bass
    v.map("master.transient", sdf, "emission", amount=0.3,  attack=0.005, release=0.18) # +1.5/5: glow flash
    v.map("master.high",      sdf, "metallic", amount=0.5,  attack=0.02, release=0.16)  # +0.5/1: hi-hat shimmer

    v.master_gain(0.6)   # headroom: bass+drums+stabs sum clips at 0 dBFS (AV clip needs clean audio)
    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. an SDF metaball that swells + melds to the kick, shimmers on the hats.")


if __name__ == "__main__":
    build(Vivid())
