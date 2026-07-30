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

    # The base form each note becomes — a soft, near-metallic sphere (its instance colour comes from the
    # signal's per-element hue, so the base r/g/b is just a neutral fallback).
    shape = v.add_node("Shape3D")
    for k, val in dict(shape=1, detail=28, r=0.7, g=0.85, b=1.0, roughness=0.22, metallic=0.7,
                       emission=0.2, scale_x=1.0, scale_y=1.0, scale_z=1.0).items():
        v.set_node_param(shape, k, float(val))

    lead_sig = v.notes(v.track_id(LEAD))     # arp track → generic signal (active + fired elements)

    # arp → the HERO: a RING where pitch maps to angle, so the 16th-note melody visibly runs around a dial,
    # each note a bright bead that flares on the strike then settles into the standing ring (a medium trail
    # keeps the recent run lit so the ring reads as a shape, not a lone dot). Spectrum palette → a rainbow
    # ring; the arp climbing/falling is unmistakably legible as motion around the circle.
    lead_inst = v.add_node("InstancesFromSignal")
    # layout=wheel → a CAMERA-FACING circle: pitch maps to clock angle so the arp lights beads around a
    # dial (unambiguous, always symmetric + fully framed). pos_lo/hi frame the arp's pitch band (MIDI
    # ~57-86) across the full circle + palette; persist HOLDS each bead so the melody paints the wheel.
    for k, val in dict(size=0.55, radius=3.7, layout=3, spin=0.0, trail=1.4, pulse=1.0,
                       palette=0, persist=0.9, pos_lo=0.48, pos_hi=0.73).items():   # window = the run's MIDI range
        v.set_node_param(lead_inst, k, float(val))
    v.connect(lead_inst, lead_sig)           # signal (port 0) ← Notes

    lead_draw = v.add_node("Instancer3D")
    v.connect(lead_draw, shape, 0)           # scene (base sphere)
    v.connect(lead_draw, lead_inst, 1)       # instances (one per arp note)

    # bass → a single pulsing CORE at the centre (a plain sphere, not instanced): it swells + flashes on
    # every root, anchoring the wheel. A regular geometry node, driven by the bridge (master.low + the bass
    # gate) rather than a signal instancer — so the wheel stays the one instanced draw in the scene.
    core = v.add_node("Shape3D")
    for k, val in dict(shape=1, detail=32, r=0.55, g=0.7, b=1.0, roughness=0.25, metallic=0.6,
                       emission=0.25, scale_x=1.1, scale_y=1.1, scale_z=1.1).items():
        v.set_node_param(core, k, float(val))

    key = v.add_node("Light3D")
    for k, val in dict(type=0, intensity=2.6, r=1.0, g=0.96, b=0.9,
                       dir_x=-0.4, dir_y=-0.7, dir_z=-0.5).items():
        v.set_node_param(key, k, float(val))
    fill = v.add_node("Light3D")
    for k, val in dict(type=1, intensity=1.5, r=0.45, g=0.7, b=1.0,
                       pos_x=-5.0, pos_y=3.0, pos_z=5.0, radius=26.0).items():
        v.set_node_param(fill, k, float(val))

    merge = v.add_node("SceneMerge")
    v.connect(merge, lead_draw, 0)           # the wheel (instanced)
    v.connect(merge, core,      1)           # the pulsing core (plain sphere)
    v.connect(merge, key,       2)
    v.connect(merge, fill,      3)

    render = v.add_node("Render3D")
    # Straight-on and pulled back so the camera-facing wheel (radius 4 + bead) sits well inside the frustum
    # with margin on every side — every instance stays visible; verified against the frame borders after capture.
    for k, val in dict(cam_x=0, cam_y=0, cam_z=15, target_y=0, fov=46, far=120, near=0.05,
                       bg_r=0.02, bg_g=0.02, bg_b=0.05).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)
    v.connect(out, render, 0)

    # --- Reactivity: the arp wheel IS the notes (via the signal edge). The bass core is driven by the
    #     bridge — the low band swells it on the kick, and each bass note flashes its emission. ---
    bid = v.track_id(BASS)
    for ax in ("scale_x", "scale_y", "scale_z"):
        v.map("master.low", core, ax, amount=0.04, attack=0.008, release=0.22)     # kick swells the core (subtle)
    v.map(f"track_{bid}.gate", core, "emission", amount=0.5, attack=0.004, release=0.20)  # flash per bass note

    v.master_gain(0.55)   # extra headroom so the kick + square-bass lows never pile toward 0 dBFS
    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. a constellation of note-spheres: Notes → InstancesFromSignal → Instancer3D ← Shape3D.")


if __name__ == "__main__":
    build(Vivid())
