"""Storm — a GPU curl-noise particle storm that churns and bursts to the beat (ADR-0041 composable demo).

Particles3D advects ~60k GPU particles through a curl-noise field — a flowing, ember-warm cloud that
lives on its own (noise_speed), driven by the master-bus bridge: transients BURST new particles
(emission_rate), the kick churns the field harder (curl_strength) and swells the particle size, the highs
speed up the turbulence. Particles3D + audio mappings — one op, all reactivity from the composable bridge.

Run with the app running:  uv run examples/demos/storm.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "storm")
CASSETTE = "Cassette Drums"       # free VST3 drum machine (BPB)
BPM = 128


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # --- Audio: a driving, transient-rich SONG in three sections (Surge XT + Cassette Drums) so the
    #     storm BUILDS — intro (kick + low bass, the field drifts), verse (snare backbeat churns it),
    #     chorus (+ busy 16th hats & an energetic arp → bursts on transients, highs stir turbulence).
    #     i-VI-III-VII in E minor. ---
    S_INTRO, S_VERSE, S_CHORUS = v.scenes(["intro", "verse", "chorus"])
    prog = ["Em", "C", "G", "D"]
    DRUMS = v.add_track(kind="instrument", instrument=CASSETTE)
    v.set_track_gain(DRUMS, 0.9)
    BASS = v.add_graph_track("bass"); surge_preset(v, BASS, "bass", prefer="Square", gain=0.85)
    LEAD = v.add_graph_track("lead"); surge_preset(v, LEAD, "pluck", prefer="Sync", gain=0.6)

    def bass_seq(step):   # roots an octave down, every `step` beats
        s = []
        for i, c in enumerate(prog):
            root = theory.chord(c, octave=2)[0]
            k = 0
            while k * step < 4.0:
                s.append((root, i * 4.0 + k * step, step * 0.9)); k += 1
        return s

    # intro: kick + closed hats, low bass on quarters — the field drifts
    v.drums(DRUMS, S_INTRO, {"kick": "x...x...x...x...", "hat": "..x...x...x...x."}, bars=4, vel=0.85)
    v.bassline(BASS, S_INTRO, bass_seq(1.0), length=16.0, vel=0.85)
    # verse: snare backbeat + driving 8th bass — churns the turbulence
    v.drums(DRUMS, S_VERSE, {"kick": "x...x...x...x...", "snare": "....x.......x...", "hat": "x.x.x.x.x.x.x.x."}, bars=4, vel=0.9)
    v.bassline(BASS, S_VERSE, bass_seq(0.5), length=16.0, vel=0.9)
    # chorus: busy 16th hats + claps + an energetic arp — bursts + high-freq turbulence
    v.drums(DRUMS, S_CHORUS, {"kick": "x...x...x...x...", "snare": "....x.......x...",
                              "clap": "..x..x....x..x..", "hat": "xxxxxxxxxxxxxxxx"}, bars=4, vel=0.9)
    v.bassline(BASS, S_CHORUS, bass_seq(0.5), length=16.0, vel=0.9)
    arp = []
    for i, c in enumerate(prog):
        for n in theory.arpeggiate(theory.chord(c, octave=4), "up", rate=0.25, octaves=2, length=4.0, vel=0.65):
            arp.append({"p": n["p"], "s": i * 4.0 + n["s"], "d": n["d"] * 0.85, "v": n["v"]})
    v.set_clip(LEAD, S_CHORUS, arp, 16.0)

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

    v.master_gain(0.6)   # headroom: bass+arp+drums sum clips at 0 dBFS (AV clip needs clean audio)
    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. a curl-noise ember storm — bursts on transients, churns + swells on the kick.")


if __name__ == "__main__":
    build(Vivid())
