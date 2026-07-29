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
from vivid_demo import Vivid, find, save_geo

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "spectrum")
BREAK = os.path.join(HERE, "media", "break90.wav")
BPM = 90


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # --- Audio: one sampler track with a broadband drum break — loads instantly (no CLAP), and its
    #     kick/snare/hats give the spectrum content across low/mid/high. (Swap in your own loop, or a
    #     Surge kit, for a richer song.) ---
    drums = v.add_track(kind="audio")
    v.import_audio(drums, 0, BREAK, src_bpm=90.0)
    try:
        v.warp(drums, 0, mode="beats")
    except Exception:
        pass

    NBARS = 44
    SPREAD = 34.0   # bar row half-width (bars stay distinct: thin bars, ~1.5-unit pitch)

    # --- Visual: the composable lane chain. ---
    out = find(v.graph()["nodes"], "Output")

    # A tall, thin base cube; instance scale_y (from the spectrum) stretches it into a bar.
    shape = v.add_node("Shape3D")
    for k, val in dict(shape=0, detail=1, r=0.25, g=0.8, b=1.0, metallic=0.0, roughness=0.55,
                       emission=0.4, scale_x=0.22, scale_y=11.0, scale_z=0.22).items():
        v.set_node_param(shape, k, float(val))

    spec = v.add_node("AudioSpectrum")          # → 0..1 magnitude per band (the reactive lane)
    for k, val in dict(bands=NBARS, gain=0.42, tilt=1.4, normalize=0.1, attack=0.02, release=0.16).items():
        v.set_node_param(spec, k, float(val))

    ramp = v.add_node("LaneRamp")               # → bar X positions (the layout lane)
    for k, val in dict(count=NBARS, lo=-SPREAD, hi=SPREAD, mode=0).items():
        v.set_node_param(ramp, k, float(val))

    lanes = v.add_node("InstancesFromLanes")
    v.connect(lanes, ramp, 0)                   # pos_x  ← LaneRamp
    v.connect(lanes, spec, 4)                   # scale_y ← AudioSpectrum

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
    for k, val in dict(cam_x=7, cam_y=8, cam_z=50, target_x=0, target_y=0, target_z=0,
                       fov=42, far=200, near=0.1).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)
    v.connect(out, render, 0)

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. composable chain: AudioSpectrum + LaneRamp → InstancesFromLanes → Instancer3D ← Shape3D.")


if __name__ == "__main__":
    build(Vivid())
