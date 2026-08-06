"""Grid — a NONOTAK-style monochrome geometric light structure over a 3-section breakbeat.

Inspired by NONOTAK / public.visuals: precise geometric light, receding grids, rings, feedback trails,
tight rhythmic strobing — hypnotic + architectural, not busy or spastic.

AUDIO (a breakbeat song in three sections, so it BUILDS — launch/perform intro → main → peak):
  - DRUMS: the amen break (audio clip), warped to tempo, in every section.
  - BASS:  a smooth Surge SUB (not the old harsh FM patch), roots of a C-minor progression.
  - MELODY: a Surge pluck that arps the chords — sparse in the intro, fuller at the peak.

VISUALS: three geometric "movements" cut every 4 bars via a metronome-locked Clock → Switch3D —
  A  a GRID LATTICE (a receding cubic field of light-cells)
  B  a RING of vertical light-PILLARS (a colonnade)
  C  a SPECTRUM WALL (per-band bars)
White/cyan emissive geometry on pure black + ONE warm note-bloom that fires only on the melody notes.

Anti-spastic by construction (the old grid mapped raw master.transient → size, no smoothing):
  - the BIG reactivity is in the instance set (AudioSpectrum → bars) + the Clock cuts, not loud mappings;
  - the KICK (master.low, smoothed) brightens + breathes the structure; the DOWNBEAT accents the bar;
  - MELODY note-ons bloom beads (the signal edge's pulse), never a continuous jitter;
  - every direct mapping is a small base+mod×range nudge with a fast attack + slow release.

Run with the app running:  uv run examples/demos/grid.py
"""
import os
import random
from vivid_demo import Vivid, find, save_geo, surge_preset
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "grid")
AMEN = os.path.join(PROJECT, "media", "amen-break-frenzy-jazzy_174bpm_G#_major.wav")
BPM = 174   # the amen break's native tempo — play it 1:1 (no time-stretch)

WHITE = (0.82, 0.92, 1.00)
CYAN  = (0.35, 0.85, 1.00)
AMBER = (1.00, 0.62, 0.18)

# Shape3D `shape`: Cube0 Sphere1 Torus2 Plane3 …    InstanceGrid `layout`: Grid0 Circle1(XZ) Line2 Grid3D3
CUBE, SPHERE, TORUS, PLANE = 0, 1, 2, 3
PROG = ["Cm", "Ab", "Eb", "Bb"]   # i–VI–III–VII in C minor (dark, hypnotic)


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(BPM)

    # ================= AUDIO — a 3-section breakbeat =================
    S_INTRO, S_MAIN, S_PEAK = v.scenes(["intro", "main", "peak"])

    MELODY = v.add_graph_track("melody")                        # track for the amber note-bloom
    surge_preset(v, MELODY, "pluck", prefer="Nice Pluck", gain=0.6)
    BASS = v.add_graph_track("bass")
    surge_preset(v, BASS, "bass", prefer="Rubber", gain=0.85)   # smooth round bass (not the harsh FM)       # a SMOOTH sub (replaces the harsh FM bass)
    # DRUMS: the amen break, SLICED and re-sequenced into a chopped breakbeat. slice_to_midi turns the
    # audio clip into a Sampler of 16 slices + a trigger MIDI clip; we set the Sampler GATED so it respects
    # each note's length (a slice STOPS when its note ends — no runaway one-shot overlap), then re-author the
    # MIDI to jump around the slices. The raw source clip is muted; only the chop is heard.
    SRC = v.add_track(kind="audio")
    v.import_audio(SRC, S_INTRO, AMEN, src_bpm=174.0)
    v.warp(SRC, S_INTRO, mode="repitch")
    DRUMS = v.call("slice_to_midi", track=SRC, scene=S_INTRO, mode=3)["track"]   # → a Sampler (16-grid slices)
    v.set_track_gain(SRC, 0.0)
    v.set_track_gain(DRUMS, 0.95)
    v.call("set_audio_op_param_by_name", track=DRUMS, index=-1, name="gate", value=1.0)   # respect note-off
    _sl = sorted({int(x["p"]) for x in v.call("get_clip", track=DRUMS, scene=S_INTRO).get("notes", [])})
    SLBASE, SLN = (_sl[0] if _sl else 36), (len(_sl) if _sl else 16)

    def chop(seed, dens, step=0.5):
        """A re-sequenced breakbeat: the kick slice anchors beats 1 & 3, the rest jumps to random slices on
        the grid. GATED, so each note plays its slice for its (step-length) hold then stops — clean jumps."""
        rng = random.Random(seed); notes = []
        for bar in range(4):
            i = 0
            while i * step < 4.0:
                pos = i * step
                if pos in (0.0, 2.0):
                    p = SLBASE
                elif rng.random() < dens:
                    p = SLBASE + rng.randrange(SLN)
                else:
                    i += 1; continue
                notes.append({"p": p, "s": round(bar * 4.0 + pos, 3), "d": round(step * 0.95, 3),
                              "v": round(rng.uniform(0.75, 0.98), 2)})
                i += 1
        return notes

    def bass_seq(step):
        s = []
        for i, c in enumerate(PROG):
            root = theory.chord(c, octave=2)[0]
            k = 0
            while k * step < 4.0:
                s.append((root, i * 4.0 + k * step, step * 0.9)); k += 1
        return s

    def melody(rate, octaves, pattern="up", octave=5):
        notes = []
        for i, c in enumerate(PROG):
            for n in theory.arpeggiate(theory.chord(c, octave=octave), pattern, rate, octaves, 4.0, 0.7):
                notes.append({"p": n["p"], "s": i * 4.0 + n["s"], "d": n["d"] * 0.9, "v": n["v"]})
        return notes

    # intro: sparse chop + bass roots; main: rolling chop + driving bass + melody; peak: busy 16th chop + more.
    v.set_clip(DRUMS, S_INTRO, chop(1, 0.35, step=0.5),  16.0)
    v.set_clip(DRUMS, S_MAIN,  chop(2, 0.55, step=0.5),  16.0)
    v.set_clip(DRUMS, S_PEAK,  chop(3, 0.70, step=0.25), 16.0)   # 16th-note chops (busier)
    v.bassline(BASS, S_INTRO, bass_seq(4.0), length=16.0, vel=0.85)
    v.bassline(BASS, S_MAIN,  bass_seq(1.0), length=16.0, vel=0.9)
    v.bassline(BASS, S_PEAK,  bass_seq(1.0), length=16.0, vel=0.9)
    v.set_clip(MELODY, S_MAIN, melody(1.0,  1, "up",     octave=5), 16.0)
    v.set_clip(MELODY, S_PEAK, melody(0.5,  2, "updown", octave=5), 16.0)

    mel_id = v.track_id(MELODY)   # stable id for the note-bloom signal + the gate mapping

    # ================= VISUALS — three geometric movements =================
    out = find(v.graph()["nodes"], "Output")

    def emissive(shape, rgb, emission=0.85, **extra):
        n = v.add_node("Shape3D")
        p = dict(shape=shape, detail=8, r=rgb[0], g=rgb[1], b=rgb[2], emission=emission, unlit=1,
                 roughness=0.5, metallic=0.0)
        p.update(extra)
        for k, val in p.items():
            v.set_node_param(n, k, float(val))
        return n

    # LOOK A: a volumetric GRID LATTICE — a 6^3 cubic field of glowing cells you look into.
    cell = emissive(CUBE, WHITE, emission=0.85, scale_x=0.42, scale_y=0.42, scale_z=0.42)
    fieldA = v.add_node("InstanceGrid")
    for k, val in dict(count=216, layout=3, spacing=3.4, palette=0).items():   # 6^3 Grid3D
        v.set_node_param(fieldA, k, float(val))
    instA = v.add_node("Instancer3D"); v.connect(instA, cell, 0); v.connect(instA, fieldA, 1)
    lookA = v.add_node("SceneMerge"); v.connect(lookA, instA, 0)

    # LOOK B: a ring of vertical light-PILLARS (a colonnade the raised camera reads as an ellipse of light).
    pillar = emissive(CUBE, WHITE, emission=0.75, scale_x=0.16, scale_y=7.0, scale_z=0.16)
    ringB = v.add_node("InstanceGrid")
    for k, val in dict(count=44, layout=1, spacing=9.0, palette=0).items():   # Circle (XZ floor)
        v.set_node_param(ringB, k, float(val))
    instB = v.add_node("Instancer3D"); v.connect(instB, pillar, 0); v.connect(instB, ringB, 1)
    lookB = v.add_node("SceneMerge"); v.connect(lookB, instB, 0)

    # LOOK C: a spectrum wall — 40 per-band bars along x (AudioSpectrum drives scale_y per bar).
    spec = v.add_node("AudioSpectrum")
    for k, val in dict(bands=40, gain=7.0, tilt=0.9, normalize=1.0, attack=0.02, release=0.16).items():
        v.set_node_param(spec, k, float(val))
    ramp = v.add_node("LaneRamp")
    for k, val in dict(count=40, lo=-20.0, hi=20.0, mode=0).items():
        v.set_node_param(ramp, k, float(val))
    lanes = v.add_node("InstancesFromLanes")
    v.connect(lanes, ramp, 0)     # LaneRamp values → pos_x
    v.connect(lanes, spec, 4)     # AudioSpectrum → scale_y (each band's height)
    bar = emissive(CUBE, WHITE, emission=0.9, scale_x=0.42, scale_y=1.2, scale_z=0.42)
    instC = v.add_node("Instancer3D"); v.connect(instC, bar, 0); v.connect(instC, lanes, 1)
    lookC = v.add_node("SceneMerge"); v.connect(lookC, instC, 0)

    # The note-bloom (over EVERY look): melody note-ons bloom warm beads (amber, via the Fire palette's warm zone).
    mel = v.notes(mel_id)
    bloom_sig = v.add_node("InstancesFromSignal")
    for k, val in dict(size=0.42, radius=9.0, layout=3, orient=0, spin=0.1, trail=0.22, pulse=0.85,
                       palette=2, persist=0.08, pos_lo=0.08, pos_hi=0.22).items():
        v.set_node_param(bloom_sig, k, float(val))
    v.connect(bloom_sig, mel)
    bead = emissive(SPHERE, AMBER, emission=1.3, detail=12)
    bloom = v.add_node("Instancer3D"); v.connect(bloom, bead, 0); v.connect(bloom, bloom_sig, 1)

    # Assembly: metronome Clock (4 bars) → Switch3D cuts A/B/C; + note bloom + a soft key light.
    clock = v.add_node("Clock")
    for k, val in dict(sync=1, unit=1, period=4.0, steps=0, gate_width=0.08).items():
        v.set_node_param(clock, k, float(val))
    switch = v.add_node("Switch3D"); v.set_node_param(switch, "order", 0.0)   # sequential
    v.connect(switch, clock, 0, src_port=0)
    for i, look in enumerate([lookA, lookB, lookC]):
        v.connect(switch, look, 1 + i)

    key = v.add_node("Light3D")
    for k, val in dict(type=0, intensity=1.0, r=0.7, g=0.85, b=1.0, dir_x=-0.3, dir_y=-0.6, dir_z=-0.5).items():
        v.set_node_param(key, k, float(val))

    merge = v.add_node("SceneMerge")
    v.connect(merge, switch, 0); v.connect(merge, bloom, 1); v.connect(merge, key, 2)

    render = v.add_node("Render3D")
    for k, val in dict(cam_x=0.0, cam_y=3.2, cam_z=16.0, target_x=0.0, target_y=1.0, fov=54,
                       near=0.1, far=400, bg_r=0.0, bg_g=0.0, bg_b=0.0, shadow_enabled=0).items():
        v.set_node_param(render, k, float(val))
    v.connect(render, merge, 0)

    # Post: Feedback light-trails → Blur → screen back = a controlled glow (long-exposure look).
    fb = v.add_node("Feedback"); v.set_node_param(fb, "decay", 0.42); v.connect(fb, render, 0)
    blur = v.add_node("Blur"); v.set_node_param(blur, "radius", 0.35); v.connect(blur, fb, 0)
    glow = v.add_node("Composite")
    v.set_node_param(glow, "mode", 3.0); v.set_node_param(glow, "opacity", 0.28)
    v.connect(glow, fb, 0); v.connect(glow, blur, 1); v.connect(out, glow, 0)

    # Reactivity — smooth + quantized (base + mod×range; fast attack, slow release).
    for shp in (cell, pillar, bar):
        v.map("master.low", shp, "emission", amount=0.22, attack=0.01, release=0.14)   # kick brightens
    v.map("master.low", fieldA, "spacing", amount=0.05, attack=0.03, release=0.25)     # grid breathes
    v.map("master.low", render, "cam_z",   amount=-0.02, attack=0.05, release=0.30)    # dolly-in punch
    v.map("transport.downbeat", cell, "emission", amount=0.35, attack=0.004, release=0.22)  # bar accent
    v.map("master.high", pillar, "emission", amount=0.14, attack=0.02, release=0.18)   # highs shimmer
    v.map(f"track_{mel_id}.gate", bead, "emission", amount=0.4, attack=0.004, release=0.18)  # note flash

    v.master_gain(0.6)
    v.launch_scene(S_MAIN)
    v.play()
    if save:
        save_geo(v, PROJECT)
    print("built. grid: a 3-section breakbeat (intro/main/peak) under 3 monochrome geometric movements")
    print("       (grid lattice / pillar ring / spectrum wall) that cut every 4 bars.")


if __name__ == "__main__":
    build(Vivid())
