"""Blob — a CONSTELLATION of note-forms: one soft 3D sphere per note, the music rendered as geometry.

Reconceived (ADR-0041). A Notes adapter turns each track into a generic reactive signal; the new
InstancesFromSignal op draws one form per ACTIVE element — pitch → position around an orb, velocity →
size, pitch → hue — and spawn-pops each note then fades it, so every note is a legible, discrete event.
Two streams give the piece structure: the bass is a few big chrome spheres orbiting slow (the pumping
low core), the arp is a fast inner swarm of small bright spheres streaming by pitch (the shimmer). The
kick (master.low) breathes the whole base form under it all.

The instancer never knows it's "notes" — it just consumes a signal (an instancer that CAN accept notes),
so the same op drives off any onset/Beat source. The 3D chain is fully composable, node by node:

    Notes(bass) ─(signal)─> InstancesFromSignal ─┐
                                                 ├─> Instancer3D ─┐
    Shape3D (a soft sphere) ─────────────────────┘               ├─> SceneMerge ─> Render3D
    Notes(arp) ──(signal)─> InstancesFromSignal ─> Instancer3D ──┘        ^  ^
                                                                     Light3D  Light3D

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
    #     constellation BUILDS — intro (kick + a slow sub bass: a few big spheres drift), verse (driving
    #     8th bass + a quiet 16th arp enters: the swarm fills in), chorus (+ open-hats & a fuller two-octave
    #     arp: the whole cluster blooms & spins). i-VI-III-VII in F minor. ---
    S_INTRO, S_VERSE, S_CHORUS = v.scenes(["intro", "verse", "chorus"])
    prog = ["Fm", "Db", "Ab", "Eb"]
    DRUMS = v.add_track(kind="instrument", instrument=CASSETTE)
    v.set_track_gain(DRUMS, 0.9)
    BASS = v.add_graph_track("bass"); surge_preset(v, BASS, "bass", prefer="Square", gain=0.78)
    LEAD = v.add_graph_track("lead"); surge_preset(v, LEAD, "pluck", prefer="Sync", gain=0.5)

    def bass_seq(step):   # roots an octave down, every `step` beats
        s = []
        for i, c in enumerate(prog):
            root = theory.chord(c, octave=2)[0]
            k = 0
            while k * step < 4.0:
                s.append((root, i * 4.0 + k * step, step * 0.9)); k += 1
        return s

    def arp_notes(count, vel):   # a 16th run UP-and-DOWN the F-minor scale — MANY distinct pitches, so the
        rate = 0.25              # wheel lights a bead per scale tone all the way around (not just a triad).
        tones = theory.scale_notes("F", "minor", octave=4, count=count)   # `count` ascending scale pitches
        seq = tones + tones[-2:0:-1]                                      # …then back down (no end repeats)
        ns, t, i = [], 0.0, 0
        while t < 16.0 - 1e-6:
            ns.append({"p": seq[i % len(seq)], "s": t, "d": rate * 0.9, "v": vel}); t += rate; i += 1
        return ns

    # intro: kick + sparse hat, slow sub bass (half-notes) — a few big spheres drift, no swarm yet
    v.drums(DRUMS, S_INTRO, {"kick": "x...x...x...x...", "hat": "..x...x...x...x."}, bars=4, vel=0.85)
    v.bassline(BASS, S_INTRO, bass_seq(2.0), length=16.0, vel=0.85)
    # verse: full four-on-floor + clap + driving 8th bass, and a quiet one-octave arp enters
    v.drums(DRUMS, S_VERSE, {"kick": "x...x...x...x...", "clap": "....x.......x...", "hat": "..x...x...x...x."}, bars=4, vel=0.9)
    v.bassline(BASS, S_VERSE, bass_seq(0.5), length=16.0, vel=0.95)
    v.set_clip(LEAD, S_VERSE, arp_notes(count=8, vel=0.6), 16.0)     # one octave → ~8 beads fill part of the wheel
    # chorus: + open-hats + a fuller two-octave arp — the swarm blooms & spins
    v.drums(DRUMS, S_CHORUS, {"kick": "x...x...x...x...", "clap": "....x.......x...", "hat": "x.x.x.x.x.x.x.x."}, bars=4, vel=0.92)
    v.euclid(DRUMS, S_CHORUS, "openhat", pulses=3, steps=16, bars=4, vel=0.45)
    v.bassline(BASS, S_CHORUS, bass_seq(0.5), length=16.0, vel=0.95)
    v.set_clip(LEAD, S_CHORUS, arp_notes(count=15, vel=0.7), 16.0)   # two octaves → ~15 beads bloom the full wheel

    out = find(v.graph()["nodes"], "Output")

    lead_sig = v.notes(v.track_id(LEAD))     # arp track → generic signal (active + fired elements)

    # arp → the HERO: pitch runs the melody around a wheel. But instead of drawing each note as a SEPARATE
    # instanced sphere, the note points feed an SDF3D METABALL FIELD (instances_only) — so neighbouring and
    # trailing notes genuinely MERGE, gooey necks forming between them as the arp clusters, then pulling
    # apart. This is the real gloopy merge instances can't do: one raymarched field, not N rigid meshes.
    lead_inst = v.add_node("InstancesFromSignal")
    # layout=wheel → pitch maps to clock angle; persist HOLDS each note so the recent run stays lit → the
    # metaball field reads as a gooey, connected arc rather than a lone blob. size sets the sphere radius.
    for k, val in dict(size=1.0, radius=3.0, layout=3, spin=0.0, trail=1.2,
                       palette=0, persist=0.85, pos_lo=0.48, pos_hi=0.73).items():
        v.set_node_param(lead_inst, k, float(val))
    v.connect(lead_inst, lead_sig)           # signal (port 0) ← Notes

    # The note METABALL FIELD. Identity transform so the note points (world-space, around the wheel) land
    # directly in its local space; instances_only hides any base shape. A warm amber that glows through
    # Bloom, contrasting the cool central blobs. instance_smooth is the merge radius between notes.
    lead_draw = v.add_node("SDF3D")
    for k, val in dict(instances_only=1, instance_smooth=0.7, max_steps=64,
                       r=1.0, g=0.72, b=0.3, roughness=0.35, metallic=0.2, emission=0.35, shadow=0.6).items():
        v.set_node_param(lead_draw, k, float(val))
    v.connect(lead_draw, lead_inst, 0)       # instances input (port 0) ← the note points

    # The hero at the centre is now MERGING BLOBS — a raymarched SDF metaball: two spheres joined by a
    # SMOOTH UNION, so they gloop into one gooey mass with a soft neck (like a lava lamp), not a rigid
    # sphere. It isn't time-animated, which is the point: the MERGE itself is driven by the audio (below) —
    # the lower lobe rides UP into the upper one on the bass, so the two blobs slam together on the low end;
    # the whole mass pops on the beat and flashes on each bass note. Deep-blue, mostly-dielectric so the
    # lights shade its curvature instead of clipping it white.
    blob = v.add_node("SDF3D")
    for k, val in dict(shape=0, size_x=1.4, size_y=1.4, size_z=1.4,                         # lobe A (a sphere)
                       operation=1, shape_b=1, size_bx=1.05, size_by=1.05, size_bz=1.05,    # lobe B, SMOOTH UNION
                       pos_bx=0.0, pos_by=-2.4, pos_bz=0.0, smooth_k=0.55, max_steps=96,     # B well below A → clearly TWO at rest
                       r=0.34, g=0.36, b=0.42, roughness=0.4, metallic=0.15, emission=0.05,  # DARK near-neutral → coloured light reads as colour, not a white clip
                       shadow=0.7,                                                    # soft self-shadow: the two lobes shadow each other
                       scale=1.2).items():
        v.set_node_param(blob, k, float(val))

    # A SECOND, smaller companion metaball, up and to the side, reacting to the MIDS (the main mass is
    # bass-driven) — so two gooey masses breathe to different parts of the music. Teal, to echo the beads.
    blob2 = v.add_node("SDF3D")
    for k, val in dict(shape=0, size_x=0.9, size_y=0.9, size_z=0.9,
                       operation=1, shape_b=1, size_bx=0.7, size_by=0.7, size_bz=0.7,
                       pos_bx=0.0, pos_by=-1.5, pos_bz=0.0, smooth_k=0.45, max_steps=80,
                       pos_x=3.6, pos_y=2.4, pos_z=-0.5,
                       r=0.3, g=0.34, b=0.42, roughness=0.4, metallic=0.15, emission=0.06,  # dark near-neutral → coloured-light shaded
                       shadow=0.7,
                       scale=0.8).items():
        v.set_node_param(blob2, k, float(val))

    # A saturated COLOURED lighting rig (ADR-0051: SDF metaballs now take the scene's real Light3D nodes,
    # so these actually shade the blobs). Complementary key/fill from opposite sides paint the gooey
    # surfaces two colours — a magenta-lit side melting into a cyan-lit side — with a violet BACK/RIM light
    # edging the silhouettes and a deep-blue AMBIENT so the shadow side reads colour, not black.
    # Key + fill are OPPOSED (one lights the left face, the other the right) so each side of a blob takes a
    # single colour — magenta melting to cyan across the gooey surface — instead of both summing to white on
    # the camera-facing front. Modest intensities + a dark albedo (below) keep the colour from clipping.
    key = v.add_node("Light3D")          # KEY — magenta, shining rightward → lights the LEFT/top faces
    for k, val in dict(type=0, intensity=2.6, r=1.0, g=0.3, b=0.6,
                       dir_x=0.6, dir_y=-0.35, dir_z=-0.25).items():
        v.set_node_param(key, k, float(val))
    fill = v.add_node("Light3D")         # FILL — cyan, shining leftward → lights the RIGHT/bottom faces
    for k, val in dict(type=0, intensity=2.2, r=0.2, g=0.8, b=1.0,
                       dir_x=-0.6, dir_y=0.28, dir_z=-0.3).items():
        v.set_node_param(fill, k, float(val))
    rim = v.add_node("Light3D")          # RIM — violet, from BEHIND (toward camera) → edges only, weak
    for k, val in dict(type=0, intensity=1.3, r=0.75, g=0.45, b=1.0,
                       dir_x=0.0, dir_y=-0.2, dir_z=0.96).items():
        v.set_node_param(rim, k, float(val))
    amb = v.add_node("Light3D")          # AMBIENT — deep blue, so the unlit side is colour, not black
    for k, val in dict(type=3, intensity=0.35, r=0.14, g=0.17, b=0.38).items():
        v.set_node_param(amb, k, float(val))

    # A PARTICLE NEBULA that INTERACTS with the melody and is DRIVEN by the audio (not a free-running field):
    #   - the arp note-instances are wired to the `attractors` port (it takes an InstanceArray3D — the same
    #     thing the wheel draws), so attract_strength makes the swarm CHASE each note-bead as it lights
    #     around the wheel: the particles and the melody become one system.
    #   - baseline curl is LOW (a calm drift at rest) so the audio-driven surges below actually read; drag
    #     is high so a beat shockwave settles back instead of flinging everything to the bounds.
    # Calmer at REST (lower count/curl/lifetime) so the audio-driven surges read as caused, not busy: the
    # field barely drifts until a beat shockwaves it out and the melody pulls it in.
    neb = v.add_node("Particles3D")
    for k, val in dict(count=2200, emission_rate=850, lifetime=3.2, emit_radius=4.5, speed=0.5,
                       gravity=0.0, spread=360, drag=0.75, attract_strength=9.0, curl_strength=0.6,
                       noise_scale=0.7, noise_speed=0.3, size=0.055, elongation=3.0, bounds=44,
                       r=0.5, g=0.72, b=1.0, a=0.8, emission=1.0, unlit=1).items():
        v.set_node_param(neb, k, float(val))
    v.connect(neb, lead_inst, 1)              # attractors ← the arp note-instances: the swarm CHASES the melody

    # SceneMerge is 4-in, and there are 8 elements (4 geometry/particles + 4 lights) → a small merge tree:
    # geometry into one, the four lights into another, then combine.
    merge = v.add_node("SceneMerge")         # geometry + particles
    v.connect(merge, lead_draw, 0)           # the note metaball field (the melody)
    v.connect(merge, blob,      1)           # the central churning metaball
    v.connect(merge, blob2,     2)           # the companion metaball
    v.connect(merge, neb,       3)           # the particle nebula
    merge_lights = v.add_node("SceneMerge")  # the four coloured lights
    v.connect(merge_lights, key,  0)
    v.connect(merge_lights, fill, 1)
    v.connect(merge_lights, rim,  2)
    v.connect(merge_lights, amb,  3)
    merge2 = v.add_node("SceneMerge")        # combine geometry + lights
    v.connect(merge2, merge,        0)
    v.connect(merge2, merge_lights, 1)

    render = v.add_node("Render3D")
    # ORBIT camera for PARALLAX: the eye circles the scene (radius/height) instead of sitting static, so the
    # blobs, wheel and nebula slide past each other in depth. orbit_phase is driven by a Clock below — one
    # smooth revolution per 4-bar section (0≡1 on the circle, so the loop is seamless, no snap).
    for k, val in dict(orbit=1, orbit_radius=17.0, orbit_height=3.0, target_y=0, fov=48, far=140, near=0.05,
                       bg_r=0.02, bg_g=0.02, bg_b=0.05).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge2, 0)

    # A transport-locked Clock: its `phase` lane ramps 0→1 once every 4 bars. Wired into the camera's
    # orbit_phase as a Phase-B control edge (a NODE output driving a param — the same typed edge the audio
    # sources use), so the orbit is tempo-locked and smooth.
    clock = v.add_node("Clock")
    for k, val in dict(sync=1, unit=1, period=4.0).items():   # metronome-synced, 4 BARS per revolution
        v.set_node_param(clock, k, float(val))
    v.call("connect_control_to_param", node_id=render, param="orbit_phase",
           src_node_id=clock, signal="phase")

    # BLOOM (post) — the emissive core flashes, the bright beads, and the glowing motes bleed into a soft
    # halo. This is most of the "atmosphere" upgrade: matte 3D shapes → a luminous, alive scene.
    bloom = v.add_node("Bloom")
    for k, val in dict(threshold=0.62, intensity=0.9, radius=2.4).items():
        v.set_node_param(bloom, k, float(val))
    v.connect(bloom, render, 0)
    v.connect(out, bloom, 0)

    # --- Reactivity: the arp wheel IS the notes (via the signal edge). The bass core is driven by the
    #     bridge — the low band swells it on the kick, and each bass note flashes its emission. These
    #     mappings now surface as explicit nodes on the visual canvas (ADR-0053): a Master node and a
    #     Track node whose value outputs (low, gate, …) wire into the core's scale/emission params. ---
    bid = v.track_id(BASS)
    # Four legible reactivity axes, each a visible control edge (ADR-0053) pointing at ONE audio cause the
    # eye can name — layered so the piece reads as choreographed, not just "near the music".
    #   1) beat PUNCH — transport.beat_pulse pops the whole metaball's scale on every beat (clock-locked →
    #      reads as hitting WITH the music). SDF3D scale is uniform, so it pulses (never stretches).
    v.map("transport.beat_pulse", blob, "scale", amount=0.04, attack=0.005, release=0.15)
    #   2) bass FLASH — each bass note briefly lights the metaball's emission: a STRIKE on a shaded mass
    #      (base emission is low), not a constant glow.
    v.map(f"track_{bid}.gate", blob, "emission", amount=0.16, attack=0.004, release=0.18)  # subtle now the coloured lights carry it
    #   3) energy GLOW — sustained low energy swells the background's blue (the room breathing) WITHOUT
    #      over-lighting the centre mass (driving the lights blew it out to white).
    v.map("master.low", render, "bg_b", amount=0.08, attack=0.03, release=0.28)
    #   4) high SHIMMER — highs (hats + arp detail) brighten the note metaball field (→ more Bloom glow).
    v.map("master.high", lead_draw, "emission", amount=0.25, attack=0.01, release=0.12)
    #   5) blob MERGE + ORBIT — the bass rides lobe B UP into A (pos_by) while the mids swing it sideways
    #      (pos_bx), so B ORBITS A and slams in from a moving angle: the two blobs go two → one → two, from
    #      a different side each time. Bigger travel than before so it clearly reads as merging, not one mass.
    v.map("master.low", blob, "pos_by", amount=0.11, attack=0.02, release=0.24)
    v.map("master.mid", blob, "pos_bx", amount=0.09, attack=0.03, release=0.26)   # sideways orbit of lobe B
    #   6) gloopy NECK — the beat swells smooth_k, so the connecting neck fattens/pinches on each kick.
    v.map("transport.beat_pulse", blob, "smooth_k", amount=0.32, attack=0.006, release=0.2)
    #   7) COLOUR by energy — the bass heats the mass from deep blue toward warm magenta as it merges.
    v.map("master.low", blob, "r", amount=0.22, attack=0.03, release=0.3)   # gentle warm shift; the lights do the colouring
    #   8) COMPANION metaball — the second (teal) mass pulses + merges on the MIDS, breathing to a different
    #      band than the bass-driven hero, so the two masses feel like separate living things.
    v.map("master.mid", blob2, "scale",  amount=0.05, attack=0.01, release=0.18)
    v.map("master.mid", blob2, "pos_by", amount=0.09, attack=0.02, release=0.24)
    v.map("master.high", blob2, "emission", amount=0.5, attack=0.01, release=0.14)
    #   6) nebula = the curl noise made REACTIVE + INTERACTIVE:
    #      • beat SHOCKWAVE — transport.beat_pulse blasts the motes OUTWARD from the blob on each beat
    #        (punctual → unmistakably caused by the kick); drag + attract then pull them back = a breathing
    #        swarm locked to the blob's pulse.
    v.map("transport.beat_pulse", neb, "repel_strength", amount=0.7, attack=0.005, release=0.22)
    #      • bass CHURN — the low end drives the curl swirl itself, so the whole field boils with the blob
    #        (monotonic-large, sharing the bass with the blob so they move together).
    v.map("master.low", neb, "curl_strength", amount=0.22, attack=0.02, release=0.26)
    #      • high SPARKLE — highs brighten the motes (→ more Bloom bleed).
    v.map("master.high", neb, "emission", amount=0.4, attack=0.01, release=0.15)

    v.master_gain(0.55)   # extra headroom so the kick + square-bass lows never pile toward 0 dBFS
    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. a constellation of note-spheres: Notes → InstancesFromSignal → Instancer3D ← Shape3D.")


if __name__ == "__main__":
    build(Vivid())
