"""Storm — a GPU curl-noise particle storm that churns and bursts to the beat (ADR-0041 composable demo).

Particles3D fills a VOLUME with GPU embers (volumetric emission, so they never converge into one solid
colour) churned by a curl-noise field, and the arp drives glowing orbs arranged around the PERIMETER (a
camera-facing wheel). Each orb REPELS the particles near it — carving clean voids at the edges while the
field flows free in the middle — and the master-bus bridge pumps the orb voids on the kick, puffs the field
on transients, and whips the curl on the highs. Particles3D (with `emit_radius`/`repel_strength`) +
InstancesFromSignal orbs — the interaction happens where the notes are.

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

    def arp(octaves, vel):   # rising 16th arp — each note is an ATTRACTOR the storm swarms toward
        ns = []
        for i, c in enumerate(prog):
            for n in theory.arpeggiate(theory.chord(c, octave=4), "updown", rate=0.25, octaves=octaves, length=4.0, vel=vel):
                ns.append({"p": n["p"], "s": i * 4.0 + n["s"], "d": n["d"] * 0.85, "v": n["v"]})
        return ns
    v.set_clip(LEAD, S_VERSE,  arp(octaves=1, vel=0.6), 16.0)   # verse: a one-octave arp — the storm starts reaching
    v.set_clip(LEAD, S_CHORUS, arp(octaves=2, vel=0.68), 16.0)  # chorus: two octaves — a full swarm of note-points

    out = find(v.graph()["nodes"], "Output")

    # An ember field that FILLS the volume (volumetric emission — no single-point convergence into one solid
    # colour), with the arp driving glowing orbs arranged around the PERIMETER (a camera-facing wheel). Each
    # orb REPELS the particles near it (Particles3D `repel_strength` + the orb's radius), so the field is
    # shoved into clean voids at the edges while it flows free in the middle — interaction where the notes are.
    lead_sig = v.notes(v.track_id(LEAD))
    orbs = v.add_node("InstancesFromSignal")   # arp notes → orbs around a camera-facing ring near the edges
    for k, val in dict(size=2.2, radius=6.0, layout=3, spin=0.0, trail=0.7, pulse=1.0,
                       palette=3, persist=0.6, pos_lo=0.46, pos_hi=0.72).items():   # wheel layout, Fire palette
        v.set_node_param(orbs, k, float(val))
    v.connect(orbs, lead_sig)                  # signal (port 0) ← Notes

    ball = v.add_node("Shape3D")               # the visible orb (smaller than its void → a clear hole around it)
    for k, val in dict(shape=1, detail=32, r=0.55, g=0.8, b=1.0, roughness=0.4, metallic=0.3,
                       emission=0.9, scale_x=1.0, scale_y=1.0, scale_z=1.0).items():
        v.set_node_param(ball, k, float(val))
    balls = v.add_node("Instancer3D")
    v.connect(balls, ball, 0)
    v.connect(balls, orbs, 1)

    # The ember field: VOLUMETRIC emission fills a sphere (emit_radius) so it never converges to one bright
    # solid-colour point; color_jitter varies per-particle brightness. Repelled by the perimeter orbs.
    parts = v.add_node("Particles3D")
    for k, val in dict(count=4500, emission_rate=2100, lifetime=3.6, emit_radius=8.5, speed=0.9,
                       gravity=0.0, drag=0.2, curl_strength=4.8, noise_scale=0.4, noise_speed=0.5,
                       size=0.13, bounds=12.0, shape=0, elongation=7.0, r=1.0, g=0.5, b=0.16, a=0.45, emission=0.8,
                       unlit=1, color_jitter=0.5, repel_strength=54.0).items():
        v.set_node_param(parts, k, float(val))
    v.connect(parts, orbs, 1)                  # repeller points (port 1) ← the SAME perimeter orbs

    key = v.add_node("Light3D")                # key from upper-left → a bright side + a shadowed side (3D form)
    for k, val in dict(type=0, intensity=1.9, r=1.0, g=0.92, b=0.82, dir_x=-0.5, dir_y=-0.7, dir_z=-0.5).items():
        v.set_node_param(key, k, float(val))
    fill = v.add_node("Light3D")               # cool fill from the other side so the dark side isn't black
    for k, val in dict(type=0, intensity=0.7, r=0.45, g=0.6, b=1.0, dir_x=0.6, dir_y=0.2, dir_z=0.4).items():
        v.set_node_param(fill, k, float(val))

    merge = v.add_node("SceneMerge")
    v.connect(merge, parts, 0)                 # the volumetric ember field
    v.connect(merge, balls, 1)                 # the pulsing perimeter orbs
    v.connect(merge, key,   2)
    v.connect(merge, fill,  3)

    render = v.add_node("Render3D")
    for k, val in dict(cam_x=0, cam_y=0, cam_z=13.5, target_y=0, fov=56, far=120, near=0.05,
                       bg_r=0.02, bg_g=0.012, bg_b=0.025).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)
    v.connect(out, render, 0)

    # --- Reactivity: each arp note lights an orb around the perimeter (signal edge); the kick PUMPS the orb
    #     size so the perimeter voids breathe; transients puff the field; highs whip the curl. ---
    v.map("master.low",       orbs,  "size",         amount=0.22, attack=0.005, release=0.2)   # perimeter voids pump on the kick
    v.map("master.low",       parts, "emission",     amount=0.28, attack=0.005, release=0.2)   # field flashes on the kick
    v.map("master.transient", parts, "emission_rate", amount=0.4, attack=0.003, release=0.14)  # puff on snare/clap/hat
    v.map("master.high",      parts, "noise_speed",  amount=0.12, attack=0.02,  release=0.16)  # hats whip the curl

    v.master_gain(0.6)   # headroom: bass+arp+drums sum clips at 0 dBFS (AV clip needs clean audio)
    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. a curl-noise ember storm — bursts on transients, churns + swells on the kick.")


if __name__ == "__main__":
    build(Vivid())
