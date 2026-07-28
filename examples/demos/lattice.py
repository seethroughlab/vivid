"""Lattice — a 3D instanced lattice that PUMPS to the kick, wrapped in a curl-noise particle storm.

The first fully 3D-native showcase (ADR-0041): a Grid3D of spheres (Shape3D → Instancer3D ← InstanceGrid)
breathes with the bass while a GPU curl-noise Particles3D field storms around it and a coloured Light3D
rakes across. Everything is driven by the master-bus analysis bridge, tuned so the reactivity is OBVIOUS:
  master.low       → the instances' scale (the whole lattice swells on every kick)
  master.low       → the grid spacing (the field expands + contracts, breathing)
  master.transient → particle emission + size (bursts of particles on each hit)
  master.high      → particle speed + the light's intensity (hats shimmer the storm)
  master.mid       → the instances' emission (they glow on the mids)

Run with the app running:  uv run examples/demos/lattice.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset, surge_drum, hits

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "lattice")
BPM = 124


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # --- Audio: a driving four-on-the-floor kick + a sub bass + off-beat hats. Strong, clean
    #     low/transient/high so the bridge has obvious material to react to. ---
    kick = surge_drum(v, "kick", "kick", prefer="909", gain=1.0)
    hat  = surge_drum(v, "hat",  "hat",  prefer="closed", gain=0.5)
    bass = v.add_graph_track("bass"); surge_preset(v, bass, "bass", prefer="sub", gain=0.7)

    hits(v, kick, 0, "x...x...x...x...", 4.0, vel=1.0)          # four-on-the-floor
    hits(v, hat,  0, "..x...x...x...x.", 4.0, pitch=42, vel=0.7)  # off-beat hats
    v.bassline(bass, 0, [(33, 0.0, 1.5), (33, 2.0, 1.5), (40, 3.0, 1.0)], 4.0, vel=0.95)

    # --- Visual: Shape3D(sphere) → Instancer3D ← InstanceGrid(Grid3D); Particles3D fountain; two
    #     Light3Ds (key + accent); SceneMerge → Render3D → Output.
    #
    #     Bridge amounts follow reference_visual_mapping_amount_vs_range: the mapped value is
    #     base + mod×(param_max−param_min), so amount = desired_excursion / param_range and the node's
    #     BASE param is its resting value. (Putting absolute values in lo/hi blows big-range params —
    #     e.g. scale [0.01,50] — up until the lattice engulfs the camera and renders blank.) ---
    out = find(v.graph()["nodes"], "Output")

    shape = v.add_node("Shape3D")   # a glowing cell: base emission so it's always lit, pumps brighter
    for k, val in dict(shape=1, detail=3, r=0.3, g=0.6, b=1.0, metallic=0.0, roughness=0.6,
                       emission=0.25, scale_x=0.7, scale_y=0.7, scale_z=0.7).items():
        v.set_node_param(shape, k, float(val))

    grid = v.add_node("InstanceGrid")
    for k, val in dict(count=125, layout=3, spacing=2.4, palette=2).items():  # 5^3 Grid3D, Cool palette
        v.set_node_param(grid, k, float(val))

    inst = v.add_node("Instancer3D")
    v.connect(inst, shape, 0)      # scene input
    v.connect(inst, grid, 1)       # instances input

    parts = v.add_node("Particles3D")   # a curl-noise fountain sparkle at the core
    for k, val in dict(count=5000, emission_rate=400, lifetime=1.2, speed=1.0, gravity=0.0,
                       curl_strength=1.2, noise_scale=0.4, noise_speed=0.6, size=0.04, spread=10.0,
                       r=1.0, g=0.7, b=0.25, emission=0.9, bounds=14.0).items():
        v.set_node_param(parts, k, float(val))

    key = v.add_node("Light3D")     # directional key so the lattice always reads in 3D
    for k, val in dict(type=0, intensity=3.5, r=1.0, g=0.95, b=0.85,
                       dir_x=-0.4, dir_y=-0.8, dir_z=-0.45).items():
        v.set_node_param(key, k, float(val))
    accent = v.add_node("Light3D")  # a point accent that shimmers on the hats
    for k, val in dict(type=1, intensity=1.5, r=0.6, g=0.8, b=1.0, radius=30.0,
                       pos_x=6.0, pos_y=8.0, pos_z=6.0).items():
        v.set_node_param(accent, k, float(val))

    merge = v.add_node("SceneMerge")
    v.connect(merge, inst,   0)
    v.connect(merge, parts,  1)
    v.connect(merge, accent, 2)
    v.connect(merge, key,    3)

    render = v.add_node("Render3D")
    for k, val in dict(cam_x=9, cam_y=6, cam_z=15, target_y=0, fov=55, far=200, near=0.1).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)
    v.connect(out, render, 0)

    # --- The bridge: amount = excursion / param_range (ranges in the op decls). ---
    for ax in ("scale_x", "scale_y", "scale_z"):
        v.map("master.low", shape, ax, amount=0.016)          # 0.7→~1.5 over scale range 50: the PUMP
    v.map("master.low",       shape,  "emission",      amount=0.32)   # +1.6 over range 5: glow harder
    v.map("master.low",       render, "cam_z",         amount=-0.03)  # −3 over range 100: dolly-in punch
    v.map("master.transient", parts,  "emission_rate", amount=0.7)    # +7000: particle bursts on hits
    v.map("master.transient", parts,  "size",          amount=0.04)   # +0.08 over range 2
    v.map("master.high",      parts,  "speed",         amount=0.12)   # +2.4 over range 20: hats shimmer
    v.map("master.high",      accent, "intensity",     amount=0.28)   # +2.8 over range 10

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. lattice pumps + glows on the kick (scale+emission+dolly); particles burst on "
          "transients; the accent light shimmers on the hats.")


if __name__ == "__main__":
    build(Vivid())
