"""Mirror Ball — a tarnished chrome sphere orbiting inside a real HDR environment (ADR-0060 Phase 2 IBL).

A near-mirror SDF sphere (metallic, very low roughness) reflects a loaded equirectangular HDRI
(royal_esplanade) through image-based lighting. The environment is BOTH what the ball reflects AND the
skybox behind it, so it clearly reads as a chrome ball standing in the hall. A faint tarnish — subtle
roughness smudges + fine scratches — gives the surface its own character, so the slow orbit reads as a
rotating physical object (a perfectly featureless mirror would look static), and the slight gloss keeps
the reflection clean. There are NO direct lights: everything on the ball is reflected environment.

Run with the app running:  uv run examples/demos/mirrorball.py
"""
import os
from vivid_demo import Vivid, find, save_geo

HERE = os.path.dirname(os.path.abspath(__file__))
MEDIA = os.path.join(HERE, "media", "mirrorball")
PROJECT = os.path.join(HERE, "projects", "mirrorball")


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(120)

    # ENVIRONMENT: the IBL source. royal_esplanade is loaded as an equirect .hdr and baked into
    # irradiance + prefiltered cubemaps; Render3D also draws it as the skybox behind the ball.
    env = v.add_node("Environment")
    v.set_node_param(env, "intensity", 0.4)      # keep the bright hall from blowing the background out
    v.call("set_node_file_param", node_id=env, name="file",
           value=os.path.join(MEDIA, "royal_esplanade_1k.hdr"))

    # The chrome ball: a PERFECT mirror — metallic 1, roughness 0 (samples the sharp base environment
    # cube directly, so the hall reflects crisply with no prefilter blur), light neutral albedo. The
    # orbit camera below sweeps the reflection across it, which is what reads as rotation.
    ball = v.add_node("SDF3D")
    for k, val in dict(shape=0, size_x=2.0, size_y=2.0, size_z=2.0,
                       r=0.97, g=0.98, b=1.0, roughness=0.0, metallic=1.0,
                       max_steps=96, shadow=0.0).items():
        v.set_node_param(ball, k, float(val))

    merge = v.add_node("SceneMerge")
    v.connect(merge, ball, 0)
    v.connect(merge, env,  1)

    render = v.add_node("Render3D")
    for k, val in dict(orbit=1, orbit_radius=4.5, orbit_height=0.3, fov=45, far=200).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)

    # A transport-locked Clock ramps orbit_phase 0→1 once every 12 bars → a slow, continuous orbit that
    # sweeps the reflection across the ball and reveals the tarnish, so it obviously reads as a rotating
    # reflective object in a real space.
    clock = v.add_node("Clock")
    for k, val in dict(sync=1, unit=1, period=12.0).items():
        v.set_node_param(clock, k, float(val))
    v.call("connect_control_to_param", node_id=render, param="orbit_phase",
           src_node_id=clock, signal="phase")

    out = find(v.graph()["nodes"], "Output")
    v.connect(out, render, 0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("mirror ball: tarnished chrome SDF sphere reflecting royal_esplanade via IBL")


if __name__ == "__main__":
    build(Vivid())
