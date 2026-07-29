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
from vivid_demo import Vivid, find, save_geo

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "crystal")
BREAK = os.path.join(HERE, "media", "break90.wav")
BPM = 90


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    drums = v.add_track(kind="audio")
    v.import_audio(drums, 0, BREAK, src_bpm=90.0)
    try:
        v.warp(drums, 0, mode="beats")
    except Exception:
        pass

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

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. a crystal that grows spikes + swells on the kick, sharpens facets on the hats.")


if __name__ == "__main__":
    build(Vivid())
