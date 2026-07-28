"""Spectrum — a 3D equaliser: cube bars whose heights ARE the live frequency spectrum (ADR-0041).

The first demo of the master-spectrum bus. A new **AudioSpectrum3D** op reads the live audio spectrum
(log bands, low→high) and emits one instance per band; Instancer3D draws a cube per band, so the row of
bars is literally the music's frequency content — the kick lights the low bars, the arp dances across the
mids, the hats sparkle the highs. Per-bar attack/release smoothing (in the op) makes each bar snap up on
a hit then fall smoothly. Reactivity that reads as PER-BAND, not a single global pump.

Run with the app running:  uv run examples/demos/spectrum.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset, surge_drum, hits

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "spectrum")
BPM = 124


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # --- Audio: full-spectrum material so every band has something to show — kick (lows), sub bass,
    #     off-beat hats (highs), and a bright arp sweeping the mids/highs. ---
    kick = surge_drum(v, "kick", "kick",  prefer="909", gain=1.0)
    hat  = surge_drum(v, "hat",  "hat",   prefer="closed", gain=0.8)
    clap = surge_drum(v, "clap", "clap",  prefer="", gain=0.85)   # broadband → fills the mids/highs
    bass = v.add_graph_track("bass"); surge_preset(v, bass, "bass", prefer="sub", gain=0.7)
    lead = v.add_graph_track("lead"); surge_preset(v, lead, "pluck", prefer="bright", gain=0.7)

    hits(v, kick, 0, "x...x...x...x...", 4.0, vel=1.0)
    hits(v, hat,  0, "..x.x.x...x.x.x.", 4.0, pitch=42, vel=0.7)   # busy 16ths → constant highs
    hits(v, clap, 0, "....x.......x...", 4.0, pitch=39, vel=0.9)   # backbeat on 2 & 4
    v.bassline(bass, 0, [(33, 0.0, 1.5), (33, 2.0, 1.5), (40, 3.0, 1.0)], 4.0, vel=0.95)
    # A 1/16 arp climbing an A-minor shape — lots of moving mid/high content for the bars to track.
    arp_pitches = [69, 72, 76, 79, 81, 79, 76, 72, 69, 72, 76, 79, 81, 84, 81, 76]
    v.set_clip(lead, 0, [{"p": p, "s": i * 0.25, "d": 0.22, "v": 0.75}
                         for i, p in enumerate(arp_pitches)], 4.0)

    # --- Visual: AudioSpectrum3D → Instancer3D ← Shape3D(cube); SceneMerge(+lights) → Render3D. ---
    out = find(v.graph()["nodes"], "Output")

    shape = v.add_node("Shape3D")     # a unit cube; the spectrum op sizes each bar via instance scale
    for k, val in dict(shape=0, detail=1, r=1.0, g=1.0, b=1.0, metallic=0.0, roughness=0.55,
                       emission=0.25, scale_x=1.0, scale_y=1.0, scale_z=1.0).items():
        v.set_node_param(shape, k, float(val))

    spec = v.add_node("AudioSpectrum3D")
    for k, val in dict(bars=48, layout=0, width=26.0, height=6.0, gain=3.0, tilt=1.8, thickness=0.5,
                       floor=0.3, attack=0.02, release=0.16, palette=0).items():
        v.set_node_param(spec, k, float(val))

    inst = v.add_node("Instancer3D")
    v.connect(inst, shape, 0)     # scene (base cube)
    v.connect(inst, spec, 1)      # instances (per-band transforms)

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
    for k, val in dict(cam_x=9, cam_y=7, cam_z=36, target_x=0, target_y=3, target_z=0,
                       fov=42, far=200, near=0.1).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)
    v.connect(out, render, 0)

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. the bars ARE the spectrum — kick lights the lows, arp dances the mids, hats sparkle the highs.")


if __name__ == "__main__":
    build(Vivid())
