"""Storm — a GPU curl-noise particle storm that churns and bursts to the beat (ADR-0041 composable demo).

Particles3D advects ~60k GPU particles through a curl-noise field — a flowing, ember-warm cloud that
lives on its own (noise_speed), driven by the master-bus bridge: transients BURST new particles
(emission_rate), the kick churns the field harder (curl_strength) and swells the particle size, the highs
speed up the turbulence. Particles3D + audio mappings — one op, all reactivity from the composable bridge.

Run with the app running:  uv run examples/demos/storm.py
"""
import os
from vivid_demo import Vivid, find, save_geo

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "storm")
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

    parts = v.add_node("Particles3D")   # emissive billboards through a curl-noise field
    for k, val in dict(count=60000, emission_rate=4000, lifetime=2.6, speed=1.0, gravity=0.0,
                       curl_strength=1.8, noise_scale=0.4, noise_speed=0.45, size=0.05, spread=90.0,
                       bounds=18.0, shape=0, r=1.0, g=0.55, b=0.22, a=1.0, emission=1.9, unlit=1).items():
        v.set_node_param(parts, k, float(val))

    render = v.add_node("Render3D")
    for k, val in dict(cam_x=0, cam_y=0, cam_z=16, target_y=0, fov=52, far=120, near=0.05,
                       bg_r=0.02, bg_g=0.015, bg_b=0.03).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, parts, 0)
    v.connect(out, render, 0)

    # --- The bridge: transients BURST particles, the kick churns + swells, highs stir the turbulence. ---
    v.map("master.transient", parts, "emission_rate", amount=0.65, attack=0.004, release=0.2)  # +6500/10000
    v.map("master.low",       parts, "curl_strength", amount=0.16, attack=0.02, release=0.3)   # +3.2/20: churn
    v.map("master.low",       parts, "size",          amount=0.035, attack=0.02, release=0.26) # +0.07/2: swell
    v.map("master.high",      parts, "noise_speed",   amount=0.22, attack=0.02, release=0.18)  # +1.1/5: turbulence

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. a curl-noise ember storm — bursts on transients, churns + swells on the kick.")


if __name__ == "__main__":
    build(Vivid())
